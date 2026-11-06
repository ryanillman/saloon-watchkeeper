#include "emergency.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "platform.h"

static void *lock;
static em_state_t st;

static void ensure_lock(void) {
  if (!lock) lock = platform_lock_create();
}
#define LOCK() (ensure_lock(), platform_lock(lock))
#define UNLOCK() platform_unlock(lock)

void em_reset(void) {
  LOCK();
  memset(&st, 0, sizeof st);
  UNLOCK();
}

void em_snapshot(em_state_t *out) {
  LOCK();
  *out = st;
  UNLOCK();
}

bool em_active(void) {
  LOCK();
  bool on = st.nature != EM_NONE;
  UNLOCK();
  return on;
}

void em_press(em_nature_t n, bool has_pos, double lat, double lon, int64_t now_ms, int64_t epoch_s) {
  LOCK();
  memset(&st, 0, sizeof st);
  st.nature = n;
  st.origin = EM_ORIGIN_LOCAL;
  st.send = EM_SENDING;
  st.has_pos = has_pos && isfinite(lat) && isfinite(lon);
  if (st.has_pos) {
    st.lat = lat;
    st.lon = lon;
  }
  st.captured_ms = now_ms;
  st.captured_epoch_s = epoch_s;
  UNLOCK();
}

void em_set_send(em_send_t s) {
  LOCK();
  st.send = s;
  UNLOCK();
}

void em_set_server_id(const char *id) {
  LOCK();
  snprintf(st.id, sizeof st.id, "%s", id ? id : "");
  UNLOCK();
}

void em_set_waypoint_send(em_send_t s) {
  LOCK();
  st.waypoint_send = s;
  UNLOCK();
}

void em_on_notification(em_nature_t n, bool active, bool has_pos, double lat, double lon, const char *id, int64_t now_ms) {
  if (n == EM_NONE) return;
  bool have_id = id && id[0];
  LOCK();
  if (!active) {
    // A "normal" the server replays (a reconnect) must not wipe a press whose notification is still being sent, and a
    // normal for another notification of this nature (an earlier MOB being cleared) is not about the one we hold.
    bool in_flight = st.origin == EM_ORIGIN_LOCAL && st.send == EM_SENDING;
    bool other = have_id && st.id[0] && strcmp(id, st.id) != 0;
    if (st.nature == n && !in_flight && !other) memset(&st, 0, sizeof st);
  } else if (st.nature == n) {
    // Our own echo, or a replay after a reconnect: keep what we have, but learn the id. A device that adopted a MOB with
    // no position may learn it from a later payload.
    if (!st.id[0] && have_id) snprintf(st.id, sizeof st.id, "%s", id);
    if (st.origin == EM_ORIGIN_NOTIFICATION && !st.has_pos && has_pos && isfinite(lat) && isfinite(lon)) {
      st.has_pos = true;
      st.lat = lat;
      st.lon = lon;
    }
  } else if (st.nature != EM_MOB) {
    memset(&st, 0, sizeof st);
    st.nature = n;
    st.origin = EM_ORIGIN_NOTIFICATION;
    st.has_pos = has_pos && isfinite(lat) && isfinite(lon) && n == EM_MOB;
    if (st.has_pos) {
      st.lat = lat;
      st.lon = lon;
    }
    if (have_id) snprintf(st.id, sizeof st.id, "%s", id);
    st.captured_ms = now_ms;
  }
  UNLOCK();
}

void em_cancel_sent(void) {
  LOCK();
  memset(&st, 0, sizeof st);
  UNLOCK();
}

static const struct { const char *path, *label; } NAMES[] = {
  [EM_MOB] = {"mob", "MAN OVERBOARD"},   [EM_FIRE] = {"fire", "FIRE"}, [EM_FLOOD] = {"flooding", "FLOODING"},
  [EM_SINKING] = {"sinking", "SINKING"}, [EM_OTHER] = {"emergency", "EMERGENCY"},
};

em_nature_t em_nature_from_path(const char *id) {
  if (!id) return EM_NONE;
  for (int i = EM_MOB; i <= EM_OTHER; i++) {
    size_t n = strlen(NAMES[i].path);
    if (!strncmp(id, NAMES[i].path, n) && (id[n] == '\0' || (id[n] == '.' && id[n + 1]))) return (em_nature_t)i;
  }
  return EM_NONE;
}

const char *em_nature_path(em_nature_t n) { return n >= EM_MOB && n <= EM_OTHER ? NAMES[n].path : ""; }
const char *em_nature_label(em_nature_t n) { return n >= EM_MOB && n <= EM_OTHER ? NAMES[n].label : ""; }

static bool utc(int64_t epoch_s, struct tm *tm) {
  time_t t = (time_t)epoch_s;
  return epoch_s > 0 && gmtime_r(&t, tm) != NULL;
}

size_t em_raise_body(const em_state_t *s, char *out, size_t n) {
  char data[160] = "";
  struct tm tm;
  if (s->nature == EM_MOB && s->has_pos) {
    int len = snprintf(data, sizeof data, ",\"data\":{\"position\":{\"latitude\":%.6f,\"longitude\":%.6f}", s->lat, s->lon);
    if (utc(s->captured_epoch_s, &tm))
      len += snprintf(data + len, sizeof data - (size_t)len, ",\"time\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\"", tm.tm_year + 1900,
                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    snprintf(data + len, sizeof data - (size_t)len, "}");
  }
  int w = snprintf(out, n, "{\"state\":\"emergency\",\"message\":\"%s\",\"path\":\"%s\",\"idInPath\":true,\"includeCreatedAt\":true%s}",
                   em_nature_label(s->nature), em_nature_path(s->nature), data);
  return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

size_t em_cancel_body(char *out, size_t n) {
  int w = snprintf(out, n, "{\"state\":\"normal\"}");
  return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

size_t em_waypoint_body(const em_state_t *s, char *out, size_t n) {
  if (!s->has_pos) return 0;
  char name[24] = "MOB";
  struct tm tm;
  if (utc(s->captured_epoch_s, &tm)) snprintf(name, sizeof name, "MOB %02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  int w = snprintf(out, n,
                   "{\"name\":\"%s\",\"feature\":{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f]},"
                   "\"properties\":{}}}",
                   name, s->lon, s->lat);
  return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

void em_format_ddm(double deg, bool is_lat, char *out, size_t n) {
  long long milli = llround(fabs(deg) * 60.0 * 1000.0);   // thousandths of a minute, so rounding carries into the degrees
  long d = (long)(milli / 60000), m = (long)(milli % 60000);
  bool neg = deg < 0 && milli != 0;
  char h = is_lat ? (neg ? 'S' : 'N') : (neg ? 'W' : 'E');
  snprintf(out, n, is_lat ? "%02ld\xC2\xB0%02ld.%03ld'%c" : "%03ld\xC2\xB0%02ld.%03ld'%c", d, m / 1000, m % 1000, h);
}

bool em_target(const em_state_t *s, double own_lat, double own_lon, double *bearing_rad_true, double *dist_m) {
  if (s->nature != EM_MOB || !s->has_pos || !isfinite(own_lat) || !isfinite(own_lon)) return false;
  const double R = 6371000.0, rad = M_PI / 180.0;
  double p1 = own_lat * rad, p2 = s->lat * rad, dl = (s->lon - own_lon) * rad;
  double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  *dist_m = 2 * R * asin(fmin(1.0, sqrt(a)));
  double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = atan2(y, x);
  *bearing_rad_true = b < 0 ? b + 2 * M_PI : b;
  return true;
}
