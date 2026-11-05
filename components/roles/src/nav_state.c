#include "nav_state.h"
#include <math.h>

bool nav_true_wind(double awa_rad, double aws, double stw, double *twa_rad, double *tws) {
  if (!isfinite(awa_rad) || !isfinite(aws) || !isfinite(stw) || aws < 0 || stw < 0) return false;
  double y = aws * sin(awa_rad), x = aws * cos(awa_rad) - stw;
  double speed = hypot(x, y);
  *tws = speed;
  *twa_rad = speed > 0 ? atan2(y, x) : 0;   // atan2(0, -0) is pi: a calm is dead ahead
  return true;
}

#include <string.h>
#include <strings.h>
#include "history.h"

#define AGE_FAST_MS 5000
#define AGE_COURSE_MS 10000
#define AGE_CURRENT_MS 30000
#define AGE_VARIATION_MS 60000
#define TWO_PI (2 * M_PI)

void nav_ids_bind(nav_ids_t *ids) {
  ids->heading_true = bd_intern("navigation.headingTrue");
  ids->heading_mag = bd_intern("navigation.headingMagnetic");
  ids->variation = bd_intern("navigation.magneticVariation");
  ids->cog = bd_intern("navigation.courseOverGroundTrue");
  ids->sog = bd_intern("navigation.speedOverGround");
  ids->stw = bd_intern("navigation.speedThroughWater");
  ids->awa = bd_intern("environment.wind.angleApparent");
  ids->aws = bd_intern("environment.wind.speedApparent");
  ids->tw_angle = bd_intern("environment.wind.angleTrueWater");
  ids->tw_speed = bd_intern("environment.wind.speedTrue");
  ids->dtk = bd_intern("navigation.course.calcValues.bearingTrackTrue");
  ids->xte = bd_intern("navigation.course.calcValues.crossTrackError");
  ids->dtw = bd_intern("navigation.course.calcValues.distance");
  ids->ap_state = bd_intern("steering.autopilot.state");
  ids->ap_target_true = bd_intern("steering.autopilot.target.headingTrue");
  ids->ap_target_mag = bd_intern("steering.autopilot.target.headingMagnetic");
  ids->set_true = bd_intern("environment.current.setTrue");
  ids->drift = bd_intern("environment.current.drift");
  hist_track(ids->aws);
  hist_track(ids->tw_speed);
}

size_t nav_sub_ids(const nav_ids_t *ids, bd_id_t *out, size_t max) {
  const bd_id_t all[] = {ids->heading_true, ids->heading_mag, ids->variation, ids->cog, ids->sog, ids->stw, ids->awa,
                         ids->aws, ids->tw_angle, ids->tw_speed, ids->dtk, ids->xte, ids->dtw, ids->ap_state,
                         ids->ap_target_true, ids->ap_target_mag, bd_intern("environment.current")};
  size_t n = 0;
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    if (all[i] != BD_NONE && n < max) out[n++] = all[i];
  return n;
}

static double wrap_2pi(double a) {
  a = fmod(a, TWO_PI);
  return a < 0 ? a + TWO_PI : a;
}
static double wrap_pi(double a) {
  a = fmod(a + M_PI, TWO_PI);
  if (a < 0) a += TWO_PI;
  return a - M_PI;   // (-pi, pi] up to rounding: exactly pi comes back as -pi, which draws the same
}

static bool fresh_num(bd_id_t id, int64_t max_age_ms, int64_t now_ms, double *out) {
  bd_sample_t s;
  if (id == BD_NONE || !bd_get(id, &s) || s.kind != BD_NUMBER || !bd_is_fresh(&s, max_age_ms, now_ms)) return false;
  *out = s.num;
  return true;
}

void nav_view_build(const nav_ids_t *ids, int64_t now, nav_view_t *o) {
  memset(o, 0, sizeof *o);
  o->xte = o->dtw = NAN;
  double x, y, var;
  bool have_var = fresh_num(ids->variation, AGE_VARIATION_MS, now, &var);

  if (fresh_num(ids->heading_true, AGE_FAST_MS, now, &x)) {
    o->have_heading = true;
    o->heading = wrap_2pi(x);
  } else if (have_var && fresh_num(ids->heading_mag, AGE_FAST_MS, now, &x)) {
    o->have_heading = true;
    o->heading = wrap_2pi(x + var);
  }
  if (fresh_num(ids->cog, AGE_FAST_MS, now, &x)) o->have_cog = true, o->cog = wrap_2pi(x);
  if (fresh_num(ids->sog, AGE_FAST_MS, now, &x)) o->have_sog = true, o->sog = x;
  if (fresh_num(ids->stw, AGE_FAST_MS, now, &x)) o->have_stw = true, o->stw = x;
  if (fresh_num(ids->awa, AGE_FAST_MS, now, &x)) o->have_awa = true, o->awa = wrap_pi(x);
  if (fresh_num(ids->aws, AGE_FAST_MS, now, &x)) o->have_aws = true, o->aws = x;

  if (fresh_num(ids->tw_angle, AGE_FAST_MS, now, &x) && fresh_num(ids->tw_speed, AGE_FAST_MS, now, &y)) {
    o->tw_src = NAV_TW_SERVER;
    o->twa = wrap_pi(x);
    o->tws = y;
  } else if (o->have_awa && o->have_aws && o->have_stw && nav_true_wind(o->awa, o->aws, o->stw, &o->twa, &o->tws)) {
    o->tw_src = NAV_TW_COMPUTED;
  }

  if (fresh_num(ids->dtk, AGE_COURSE_MS, now, &x)) {
    o->have_course = true;
    o->dtk = wrap_2pi(x);
    if (fresh_num(ids->xte, AGE_COURSE_MS, now, &y)) o->xte = NAV_XTE_RIGHT_POSITIVE ? y : -y;
    if (fresh_num(ids->dtw, AGE_COURSE_MS, now, &y)) o->dtw = y;
  }

  bd_sample_t st;
  if (ids->ap_state != BD_NONE && bd_get(ids->ap_state, &st) && st.kind == BD_TEXT && bd_is_fresh(&st, AGE_COURSE_MS, now)) {
    static const char *const idle[] = {"", "standby", "off", "disabled"};
    o->ap_engaged = true;
    for (size_t i = 0; i < sizeof idle / sizeof idle[0]; i++)
      if (!strcasecmp(st.text, idle[i])) o->ap_engaged = false;
  }
  if (o->ap_engaged) {
    if (fresh_num(ids->ap_target_true, AGE_COURSE_MS, now, &x)) {
      o->have_ap_target = true;
      o->ap_target = wrap_2pi(x);
    } else if (have_var && fresh_num(ids->ap_target_mag, AGE_COURSE_MS, now, &x)) {
      o->have_ap_target = true;
      o->ap_target = wrap_2pi(x + var);
    }
  }

  if (fresh_num(ids->set_true, AGE_CURRENT_MS, now, &x) && fresh_num(ids->drift, AGE_CURRENT_MS, now, &y)) {
    o->have_current = true;
    o->set = wrap_2pi(x);
    o->drift = y;
  }
}
