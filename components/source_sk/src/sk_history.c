#include "sk_history.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"
#include "platform.h"
#include "sk_transport.h"

// A 12 h reply is hundreds of rows. Building a JSON tree of it would take on the order of a hundred bytes a row
// in many small allocations, which come from the scarce internal RAM, so the reply is read in place instead: no
// allocation beyond the response body the transport already holds.

static const char *ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

static const char *skip_string(const char *p, const char *end) {   // p is at the opening quote
  if (p >= end || *p != '"') return NULL;
  for (p++; p < end; p++) {
    if (*p == '\\') p++;
    else if (*p == '"') return p + 1;
  }
  return NULL;
}

// The end of the JSON value starting at p (after any whitespace), or NULL when it is malformed or cut short.
static const char *skip_value(const char *p, const char *end, int depth) {
  p = ws(p, end);
  if (p >= end || depth > 16) return NULL;
  if (*p == '"') return skip_string(p, end);
  if (*p == '{' || *p == '[') {
    char close = *p == '{' ? '}' : ']';
    p = ws(p + 1, end);
    if (p < end && *p == close) return p + 1;
    for (;;) {
      if (close == '}') {   // "key":
        p = skip_string(ws(p, end), end);
        if (!p) return NULL;
        p = ws(p, end);
        if (p >= end || *p != ':') return NULL;
        p++;
      }
      p = skip_value(p, end, depth + 1);
      if (!p) return NULL;
      p = ws(p, end);
      if (p >= end) return NULL;
      if (*p == ',') {
        p = ws(p + 1, end);
        continue;
      }
      return *p == close ? p + 1 : NULL;
    }
  }
  const char *q = p;   // a number, true, false or null
  while (q < end && *q != ',' && *q != ']' && *q != '}' && !isspace((unsigned char)*q)) q++;
  return q > p ? q : NULL;
}

static float number_or_nan(const char *p, const char *end) {
  if (p >= end || (*p != '-' && !isdigit((unsigned char)*p))) return NAN;   // null, true, a string...
  char buf[40];
  size_t n = 0;
  while (p + n < end && n < sizeof buf - 1 && p[n] != ',' && p[n] != ']' && !isspace((unsigned char)p[n])) buf[n] = p[n], n++;
  buf[n] = 0;
  char *stop;
  double v = strtod(buf, &stop);
  return *stop || !isfinite(v) ? NAN : (float)v;
}

// p is just after the "data": the array of rows [timestamp, v0, v1, ...].
static size_t read_rows(const char *p, const char *end, size_t column, float *out, size_t max) {
  p = ws(p, end);
  if (p >= end || *p != '[') return 0;
  p = ws(p + 1, end);
  size_t n = 0;
  if (p < end && *p == ']') return 0;
  for (;;) {
    if (p >= end || *p != '[') return 0;   // every row is an array; anything else is not a history reply
    p = ws(p + 1, end);
    float v = NAN;
    if (p < end && *p == ']') {
      p++;
    } else {
      for (size_t e = 0;; e++) {
        if (e == column + 1) v = number_or_nan(p, end);
        p = skip_value(p, end, 0);
        if (!p) return 0;
        p = ws(p, end);
        if (p >= end) return 0;
        if (*p == ',') {
          p = ws(p + 1, end);
          continue;
        }
        if (*p != ']') return 0;
        p++;
        break;
      }
    }
    if (n < max) {
      out[n++] = v;
    } else {   // more rows than room: keep the newest
      memmove(out, out + 1, (max - 1) * sizeof *out);
      out[max - 1] = v;
    }
    p = ws(p, end);
    if (p >= end) return 0;
    if (*p == ',') {
      p = ws(p + 1, end);
      continue;
    }
    return *p == ']' ? n : 0;
  }
}

size_t sk_history_parse(const char *json, size_t len, size_t path_index, float *out, size_t max) {
  if (!json || !len || !max) return 0;
  const char *end = json + len, *p = ws(json, end);
  if (p >= end || *p != '{') return 0;
  for (p++;;) {   // the top-level keys, looking for "data"
    p = ws(p, end);
    if (p >= end || *p != '"') return 0;   // '}' too: there is no data key
    const char *key = p + 1;
    const char *after = skip_string(p, end);
    if (!after) return 0;
    size_t key_len = (size_t)(after - 1 - key);
    p = ws(after, end);
    if (p >= end || *p != ':') return 0;
    p++;
    if (key_len == 4 && !memcmp(key, "data", 4)) return read_rows(p, end, path_index, out, max);
    p = skip_value(p, end, 0);
    if (!p) return 0;
    p = ws(p, end);
    if (p < end && *p == ',') p++;
  }
}

static bool plain_path(const char *p) {   // goes into a URL unescaped
  if (!*p) return false;
  for (; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_')) return false;
  return true;
}

sk_hist_result_t sk_history_backfill(const char *base_url, const char *token) {
  PLATFORM_EXT_BSS static float vals[HIST_MINUTES];   // 2.9 KB: PSRAM; only the Signal K task runs this
  bd_id_t id;
  if (!hist_unfilled(&id, 1)) return SK_HIST_IDLE;
  const char *path = bd_path(id);
  if (!path || !plain_path(path)) {   // cannot be asked for: done with it
    hist_note_backfill(id, true);
    return SK_HIST_IDLE;
  }
  char url[320];
  snprintf(url, sizeof url, "%s/signalk/v2/api/history/values?paths=%s&duration=PT12H&resolution=60", base_url, path);
  char *body = NULL;
  size_t len = 0;
  if (!sk_http_get(url, token, &body, &len)) {
    hist_note_backfill(id, false);
    return SK_HIST_NO_ANSWER;
  }
  size_t rows = sk_history_parse(body, len, 0, vals, HIST_MINUTES);
  free(body);
  if (rows) hist_backfill(id, vals, rows);
  hist_note_backfill(id, true);
  size_t internal = 0, psram = 0;
  platform_mem(&internal, &psram);
  printf("signalk: history %s: %u rows (internal heap free %u, lowest since boot %u)\n", path, (unsigned)rows, (unsigned)internal,
         (unsigned)platform_min_internal());
  return SK_HIST_ASKED;
}
