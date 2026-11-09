// Desktop simulator: the same UI and data components in an SDL window,
// talking to a real Signal K server. --snapshot renders every page to PNG.
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "alerts.h"
#include "anchor_ctl.h"
#include "nav_state.h"
#include "anchor_watch.h"
#include "app.h"
#include "app_can.h"
#include "app_cmd.h"
#include "boatdata.h"
#include "can_socketcan.h"
#include "emergency.h"
#include "lvgl.h"
#include "platform.h"
#include "sim_boards.h"
#include "sk_parse.h"
#include "history.h"
#include "snapshot.h"
#include "ui_alerts.h"
#include "ui_quick.h"
#include "ui_setup.h"
#include "ui_shell.h"

static uint32_t tick(void) { return (uint32_t)platform_millis(); }

// ---- headless snapshots ----

static uint16_t *snap_fb;
static uint16_t snap_w;

static void snap_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const uint16_t *src = (const uint16_t *)px;
  int32_t aw = lv_area_get_width(a);
  for (int32_t y = a->y1; y <= a->y2; y++)
    for (int32_t x = a->x1; x <= a->x2; x++)
      snap_fb[(size_t)y * snap_w + x] = src[(size_t)(y - a->y1) * aw + (x - a->x1)];
  lv_display_flush_ready(d);
}

static int mkdirs(const char *path) {
  char buf[512];
  snprintf(buf, sizeof buf, "%s", path);
  for (char *p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = 0;
    if (mkdir(buf, 0755) && errno != EEXIST) return -1;
    *p = '/';
  }
  return mkdir(buf, 0755) && errno != EEXIST ? -1 : 0;
}

// Two engines at cruise, as Signal K would report them (SI units).
static void fixture_engines(void) {
  const struct { const char *path; double v; } d[] = {
    {"propulsion.port.revolutions", 30.0},          {"propulsion.starboard.revolutions", 29.6},
    {"propulsion.port.temperature", 355.15},        {"propulsion.starboard.temperature", 356.15},
    {"propulsion.port.oilPressure", 380000},        {"propulsion.starboard.oilPressure", 372000},
    {"propulsion.port.alternatorVoltage", 14.2},    {"propulsion.starboard.alternatorVoltage", 14.1},
    {"propulsion.port.runTime", 4410000},           {"propulsion.starboard.runTime", 4395600},
    {"propulsion.port.fuel.rate", 0.0000031},       {"propulsion.starboard.fuel.rate", 0.0000030},
  };
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, platform_millis());
  bd_meta_t m = {.zone_count = 2};
  m.zones[0] = (bd_zone_t){.lower = 368.15, .upper = 373.15, .state = 2};   // warn 95-100 °C
  m.zones[1] = (bd_zone_t){.lower = 373.15, .upper = 393.15, .state = 3};   // alarm above 100 °C
  bd_set_meta(bd_find("propulsion.port.temperature"), &m);
  bd_set_meta(bd_find("propulsion.starboard.temperature"), &m);
}

// Five tanks, as Signal K would report them (ratios; capacity in m3).
static void fixture_tanks(void) {
  const struct { const char *path; double v; } d[] = {
    {"tanks.freshWater.0.currentLevel", 0.72}, {"tanks.freshWater.1.currentLevel", 0.40},
    {"tanks.fuel.0.currentLevel", 0.55},       {"tanks.fuel.0.capacity", 0.2},
    {"tanks.blackWater.0.currentLevel", 0.15}, {"tanks.wasteWater.0.currentLevel", 0.30},
  };
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, platform_millis());
  bd_meta_t m = {.zone_count = 2};
  m.zones[0] = (bd_zone_t){.lower = 0.0, .upper = 0.1, .state = 3};   // alarm below 10 %
  m.zones[1] = (bd_zone_t){.lower = 0.1, .upper = 0.2, .state = 2};   // warn below 20 %
  bd_set_meta(bd_find("tanks.fuel.0.currentLevel"), &m);
}

// A sounder reporting below the keel and the surface (not the transducer, so the page has to say which it shows), and a
// tide plugin: rising towards a high at 14:32 UTC, the server's clock at 12:00 UTC.
static void fixture_depth(bool tide) {
  const struct { const char *path; double v; } d[] = {
    {"environment.depth.belowKeel", 2.4}, {"environment.depth.belowSurface", 3.0},
    {"environment.tide.heightNow", 1.8},  {"environment.tide.heightHigh", 3.2}, {"environment.tide.heightLow", 0.4},
  };
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++)
    if (tide || strncmp(d[i].path, "environment.tide", 16)) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, platform_millis());
  bd_meta_t m = {.zone_count = 2};
  m.zones[0] = (bd_zone_t){.lower = 0.0, .upper = 1.0, .state = 3};   // alarm under 1 m
  m.zones[1] = (bd_zone_t){.lower = 1.0, .upper = 1.5, .state = 2};   // warn under 1.5 m
  bd_set_meta(bd_find("environment.depth.belowKeel"), &m);
  // The other fixtures stamp their own times and the server's clock is the newest of them, so the tide times are
  // set relative to it: a high in 2 h 32 min and a low in 8 h 50 min.
  int64_t now_s = bd_newest_source_time();
  if (now_s == 0) now_s = 1793793600;
  bd_id_t clock = bd_intern("navigation.datetime");
  bd_set_text(clock, "2026-11-04T12:00:00.000Z", SK_SOURCE_ID, platform_millis());
  bd_set_source_time(clock, now_s);
  if (tide) {
    const struct { const char *path; int64_t in_s; } ev[] = {{"environment.tide.timeHigh", 152 * 60}, {"environment.tide.timeLow", 530 * 60}};
    for (size_t i = 0; i < 2; i++) {
      time_t t = (time_t)(now_s + ev[i].in_s);
      struct tm tm;
      char iso[32];
      gmtime_r(&t, &tm);
      strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
      bd_set_text(bd_intern(ev[i].path), iso, SK_SOURCE_ID, platform_millis());
    }
  }
}

// A Victron boat, as signalk-venus reports it (SI units; ratios; seconds).
static void fixture_power(void) {
  const struct { const char *path; double v; } d[] = {
    {"electrical.batteries.279.voltage", 13.4}, {"electrical.batteries.279.current", 12.0},
    {"electrical.batteries.279.capacity.stateOfCharge", 0.82},
    {"electrical.solar.288.voltage", 13.5},     {"electrical.solar.288.current", 18.0},
    {"electrical.solar.288.panelPower", 260},
    {"electrical.chargers.276.voltage", 13.6},  {"electrical.chargers.276.current", 10.0},
    {"electrical.alternators.0.voltage", 14.1}, {"electrical.alternators.0.current", 30.0},
  };
  const struct { const char *path; const char *v; } t[] = {
    {"electrical.solar.288.chargingMode", "bulk"},
    {"electrical.chargers.276.chargingMode", "absorption"},
    {"electrical.inverters.275.acin.acState.acSource", "grid"},
    {"electrical.inverters.275.inverterMode", "charging"},
    {"electrical.alternators.0.chargingMode", "bulk"},
  };
  int64_t now = platform_millis();
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, now);
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) bd_set_text(bd_intern(t[i].path), t[i].v, SK_SOURCE_ID, now);
  bd_set_invalid(bd_intern("electrical.batteries.279.capacity.timeRemaining"));   // charging: Venus sends null
}

// The three-bank boat of docs/specs/2026-11-04-multi-bank-power-design.md, on top of fixture_power():
// house 279 (from fixture_power), start 280, nav 281, the two converters, and the start-bank alternator.
static void fixture_banks(void) {
  fixture_power();
  const struct { const char *path; double v; } d[] = {
    {"electrical.batteries.279.current", 12.0},
    {"electrical.batteries.280.voltage", 12.8}, {"electrical.batteries.280.current", 0.5},
    {"electrical.batteries.280.capacity.stateOfCharge", 0.96},
    {"electrical.batteries.281.voltage", 13.1}, {"electrical.batteries.281.current", 1.0},
    {"electrical.batteries.281.capacity.stateOfCharge", 0.90},
    {"electrical.dcdc.1.voltage", 13.6}, {"electrical.dcdc.1.current", 8.0},
    {"electrical.dcdc.2.voltage", 13.4}, {"electrical.dcdc.2.current", 4.0},
    {"electrical.dcdc.2.input.voltage", 13.3}, {"electrical.dcdc.2.input.current", 4.4},
    {"electrical.alternators.0.current", 0.0},
  };
  int64_t now = platform_millis();
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, now);
  bd_set_text(bd_intern("electrical.dcdc.1.chargingMode"), "bulk", SK_SOURCE_ID, now);
  bd_set_text(bd_intern("electrical.dcdc.2.chargingMode"), "float", SK_SOURCE_ID, now);
  bd_set_invalid(bd_intern("electrical.batteries.280.capacity.timeRemaining"));
  bd_set_invalid(bd_intern("electrical.batteries.281.capacity.timeRemaining"));
}

static void set_banks(cfg_role_t *r) {
  static const struct { const char *id, *name, *kind; } banks[] = {
    {"279", "HOUSE", "house"}, {"280", "START", "start"}, {"281", "NAV", "nav"}};
  static const struct { const char *src; uint8_t output; const char *from, *to; } links[] = {
    {"charger:276", 0, "", "house"}, {"charger:276", 1, "", "start"}, {"alternator:0", 0, "", "start"},
    {"solar:288", 0, "", "house"}, {"dcdc:1", 0, "start", "house"}, {"dcdc:2", 0, "house", "nav"}};
  r->bank_count = r->link_count = 0;
  for (size_t i = 0; i < sizeof banks / sizeof banks[0]; i++) {
    cfg_bank_t *k = &r->bank[r->bank_count++];
    snprintf(k->id, sizeof k->id, "%s", banks[i].id);
    snprintf(k->name, sizeof k->name, "%s", banks[i].name);
    snprintf(k->kind, sizeof k->kind, "%s", banks[i].kind);
  }
  for (size_t i = 0; i < sizeof links / sizeof links[0]; i++) {
    cfg_link_t *l = &r->link[r->link_count++];
    snprintf(l->src, sizeof l->src, "%s", links[i].src);
    l->output = links[i].output;
    snprintf(l->from, sizeof l->from, "%s", links[i].from);
    snprintf(l->to, sizeof l->to, "%s", links[i].to);
  }
}

// Twelve hours of history for the Glance trends: the house charge falling, the water level slowly dropping.
// The Navigation page in each state (the spec's scenario: heading 047, course 070 with the boat 0.12 nm right of track, the
// autopilot at 052, apparent wind 38 deg starboard at 14.2 kn, 6.4 kn through the water, server true wind 61 deg at 10 kn,
// a 1.2 kn current setting 137). Set just before each picture, so every value is fresh when the page refreshes.
static void fixture_nav(const char *state) {
  int64_t now = platform_millis();
  double hdg = 0.8203, ap = 0.9076, awa = 0.6632, twa = 1.0696;
  if (!strcmp(state, "bugnear")) ap = hdg + 0.03;
  if (!strcmp(state, "windahead")) awa = 0.05, twa = 0.09;
  const struct { const char *path; double v; } d[] = {
    {"navigation.headingTrue", hdg},
    {"navigation.courseOverGroundTrue", 1.2217},
    {"navigation.speedOverGround", 3.292},
    {"navigation.speedThroughWater", 3.292},
    {"environment.wind.angleApparent", awa},
    {"environment.wind.speedApparent", 7.305},
    {"environment.wind.angleTrueWater", twa},
    {"environment.wind.speedTrue", 5.14},
    {"navigation.course.calcValues.bearingTrackTrue", 1.2217},
    {"navigation.course.calcValues.crossTrackError", 222.2},
    {"navigation.course.calcValues.distance", 13700},
    {"steering.autopilot.target.headingTrue", ap},
    {"environment.current.setTrue", 2.391},
    {"environment.current.drift", 0.617},
  };
  for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) bd_set_number(bd_intern(d[i].path), d[i].v, SK_SOURCE_ID, now);
  bd_set_text(bd_intern("steering.autopilot.state"), !strcmp(state, "noap") ? "standby" : "auto", SK_SOURCE_ID, now);
  if (!strcmp(state, "nocourse")) bd_set_invalid(bd_find("navigation.course.calcValues.bearingTrackTrue"));
  if (!strcmp(state, "nohdg")) bd_set_invalid(bd_find("navigation.headingTrue"));
  if (!strcmp(state, "computed")) bd_set_invalid(bd_find("environment.wind.angleTrueWater"));   // the display works it out
  if (!strcmp(state, "noset")) bd_set_invalid(bd_find("environment.current.drift"));
  bd_set_position(bd_intern("navigation.position"), 41.5402833, -70.25, SK_SOURCE_ID, now);   // the present position shows in every state
  {
    const char *vessel = "{\"name\":\"SALOON\",\"mmsi\":\"316001234\"}";
    sk_parse_full(vessel, strlen(vessel));   // the script page names the boat
  }
  em_reset();
  if (!strcmp(state, "mob")) em_press(EM_MOB, true, 41.5902833, -70.18, now - 125000, 1700000000);   // marked 2 min ago, NE of us
  if (!strcmp(state, "mobnopos")) em_on_notification(EM_MOB, true, false, 0, 0, NULL, now);               // raised elsewhere, no position in it
  ui_shell_set_sos(!strcmp(state, "mob") || !strcmp(state, "mobnopos"));                            // the app does this each tick
}

static const char *const nav_states[] = {"full", "nocourse", "noap", "nohdg", "computed", "noset", "windahead", "bugnear", "mob", "mobnopos"};

static void fixture_history(void) {
  {   // twelve hours of wind for the Navigation tile, building from 6 to 10 m/s over the last hour
    nav_ids_t ids;
    nav_ids_bind(&ids);
    static float wind[HIST_MINUTES];
    for (int i = 0; i < HIST_MINUTES; i++) {
      float ramp = i < HIST_MINUTES - 60 ? 0.0f : 4.0f * (float)(i - (HIST_MINUTES - 60)) / 59.0f;
      wind[i] = 6.0f + ramp + 0.6f * sinf((float)i / 40.0f) + 0.15f * sinf((float)i / 4.0f);
    }
    hist_backfill(ids.tw_speed, wind, HIST_MINUTES);
  }
  static float v[HIST_MINUTES];
  const struct { const char *path; float from, to; } d[] = {
    {"electrical.batteries.279.capacity.stateOfCharge", 0.97f, 0.82f},
    {"tanks.freshWater.0.currentLevel", 0.80f, 0.72f},
  };
  for (size_t k = 0; k < sizeof d / sizeof d[0]; k++) {
    bd_id_t id = bd_find(d[k].path);
    if (id == BD_NONE) continue;
    for (int i = 0; i < HIST_MINUTES; i++) v[i] = d[k].from + (d[k].to - d[k].from) * (float)i / (HIST_MINUTES - 1) + 0.004f * sinf((float)i / 25.0f);
    hist_backfill(id, v, HIST_MINUTES);
  }
  {   // the depth under the keel follows the tide of the depth fixture: 2.4 m now, rising to a high 152 min on
    bd_id_t id = bd_find("environment.depth.belowKeel");
    if (id != BD_NONE) {
      const double half = 378.0, high_in = 152.0, h0 = cos(M_PI * -high_in / half);
      for (int i = 0; i < HIST_MINUTES; i++) v[i] = (float)(2.4 + 1.4 * (cos(M_PI * ((double)(i - (HIST_MINUTES - 1)) - high_in) / half) - h0));
      hist_backfill(id, v, HIST_MINUTES);
    }
  }
}

static void alert_raise(const char *id, alert_state_t st, const char *msg) {
  alert_t al = {0};
  snprintf(al.id, sizeof al.id, "%s", id);
  al.state = st;
  snprintf(al.message, sizeof al.message, "%s", msg);
  al.audible = true;
  snprintf(al.ref, sizeof al.ref, "ref-%s", id);
  alerts_upsert(&al, 0);
}

// Shows the current alerts at t = 0, so a flashing banner is always in its lit half.
static void alerts_show(void) {
  static alerts_snapshot_t snap;
  alerts_snapshot(&snap);
  ui_alerts_update(&snap, 0);
}


// The Anchor page and tile in each state. The sim has no server, so the data a plugin would publish is set here: a
// night at anchor, 150 minutes ago, swinging gently about its limit of 45 m.
static anchor_watch_t sim_watch;

static void anchor_start(const cfg_t *cfg) {
  anchor_watch_init(&sim_watch, NULL, 0, 60000);
  sim_watch.have_sd = true;   // a card is present: no NO SD flag in the pictures
  anchor_ctl_setup(NULL, ANCHOR_PLUGIN_HOEKEN, &sim_watch, NULL, &cfg->units);
}

static void anchor_history(void) {
  static float d[HIST_MINUTES], b[HIST_MINUTES];
  for (int i = 0; i < HIST_MINUTES; i++) {
    float t = (float)i / (HIST_MINUTES - 1);
    d[i] = 29.0f + 7.0f * sinf(t * 26.0f) + 3.0f * sinf(t * 61.0f);
    b[i] = 2.4f + 1.0f * sinf(t * 9.0f);   // the boat swings about 115 degrees either side
  }
  hist_backfill(bd_intern("navigation.anchor.currentRadius"), d, HIST_MINUTES);
  hist_backfill(bd_intern("navigation.anchor.bearingTrue"), b, HIST_MINUTES);
}

static void anchor_state(const char *state) {
  static const char *const paths[] = {"navigation.anchor.position", "navigation.anchor.maxRadius",
                                      "navigation.anchor.currentRadius", "navigation.anchor.bearingTrue",
                                      "navigation.anchor.rodeLength"};
  int64_t now = platform_millis();
  alerts_source_up(true);
  alerts_clear("navigation.anchor");
  sim_watch.restoring = false;
  for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) bd_set_invalid(bd_intern(paths[i]));
  if (!strcmp(state, "restoring")) {
    sim_watch.restoring = true;
    return;
  }
  if (!strcmp(state, "notset")) {
    bd_set_number(bd_intern(paths[0]), NAN, SK_SOURCE_ID, now);   // the plugin says null
    return;
  }
  const int64_t set_s = 1793840000;
  bd_id_t pos = bd_intern(paths[0]), dist = bd_intern(paths[2]);
  bd_set_position(pos, 48.5343, -123.0170, SK_SOURCE_ID, now);
  bd_set_source_time(pos, set_s);
  bd_set_number(bd_intern(paths[1]), 45, SK_SOURCE_ID, now);
  bd_set_number(dist, !strcmp(state, "dragging") ? 52 : 31, SK_SOURCE_ID, now);
  bd_set_source_time(dist, set_s + 150 * 60);
  bd_set_number(bd_intern(paths[3]), 2.4, SK_SOURCE_ID, now);
  bd_set_number(bd_intern(paths[4]), 12, SK_SOURCE_ID, now);
  if (!strcmp(state, "warn")) alert_raise("navigation.anchor", AL_WARN, "Anchor near its limit");
  if (!strcmp(state, "dragging")) alert_raise("navigation.anchor", AL_ALARM, "Anchor dragging");
  if (!strcmp(state, "nodata")) alerts_source_up(false);   // the connection is gone
}

static const char *const anchor_states[] = {"notset", "holding", "warn", "dragging", "nodata", "restoring"};

static const char *palette_names[] = {"day", "dusk", "night"};

static void sim_display_reset(void) { lv_obj_invalidate(lv_screen_active()); }   // stands in for the panel restart

static int write_page(lv_display_t *disp, const char *dir, const char *suffix, const sim_board_t *b) {
  ui_shell_set_clock("02:14");
  ui_shell_set_source(UI_SRC_LIVE);
  ui_shell_refresh();
  lv_obj_invalidate(lv_display_get_screen_active(disp));
  lv_refr_now(disp);
  const ui_nav_t *n = ui_shell_nav();
  cfg_report_t r;
  (void)r;
  char name[32], path[640];
  snprintf(name, sizeof name, "%s", lv_label_get_text(lv_obj_get_child(lv_obj_get_child(lv_display_get_screen_active(disp), 0), 0)));
  for (char *c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
  snprintf(path, sizeof path, "%s/%s-p%u%s.png", dir, name, n->page, suffix);
  if (!snapshot_write_png(path, snap_fb, b->width, b->height)) {
    fprintf(stderr, "cannot write %s\n", path);
    return 1;
  }
  return 0;
}

static int run_snapshots(const char *out) {
  uint8_t nboards;
  const sim_board_t *boards = sim_boards(&nboards);
  app_set_backlight(NULL, true);   // the panel shows its slider
  app_set_display_reset(sim_display_reset);
  for (uint8_t bi = 0; bi < nboards; bi++) {
    const sim_board_t *b = &boards[bi];
    size_t px = (size_t)b->width * b->height;
    snap_fb = calloc(px, 2);
    void *draw = malloc(px * 2);
    if (!snap_fb || !draw) return 1;
    snap_w = b->width;
    lv_display_t *disp = lv_display_create(b->width, b->height);
    lv_display_set_flush_cb(disp, snap_flush);
    lv_display_set_buffers(disp, draw, NULL, px * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    for (int pal = PALETTE_DAY; pal <= PALETTE_NIGHT; pal++) {
      char dir[512];
      snprintf(dir, sizeof dir, "%s/%s/%s", out, b->id, palette_names[pal]);
      if (mkdirs(dir)) return 1;
      cfg_report_t rep;
      cfg_t *cfg = (cfg_t *)app_init(NULL, &rep);
      snprintf(cfg->palette, sizeof cfg->palette, "%s", palette_names[pal]);
      alerts_source_up(true);
      if (cfg->role_count < CFG_MAX_ROLES)   // not a default role: add it so it is drawn and has its Glance tile
        snprintf(cfg->roles[cfg->role_count++].role, sizeof cfg->roles[0].role, "depth");
      fixture_engines();
      fixture_tanks();
      fixture_power();
      fixture_nav("full");
      strcpy(cfg->roles[cfg->role_count++].role, "navigation");   // not in the default roles
      fixture_depth(true);
      app_bind();
      fixture_history();
      anchor_start(cfg);
      anchor_history();
      char path[640];
      for (uint8_t r = 0; r < cfg->role_count; r++) {
        snprintf(cfg->start_role, sizeof cfg->start_role, "%s", cfg->roles[r].role);
        app_start_ui(disp, b->size_class);
        const ui_nav_t *n = ui_shell_nav();
        uint8_t pages = n->page_count[n->role];
        bool engine = !strcmp(cfg->roles[r].role, "engine"), tanks = !strcmp(cfg->roles[r].role, "tanks"),
             power = !strcmp(cfg->roles[r].role, "power"), glance = !strcmp(cfg->roles[r].role, "glance"),
             anchor = !strcmp(cfg->roles[r].role, "anchor"), navigation = !strcmp(cfg->roles[r].role, "navigation"),
             depth = !strcmp(cfg->roles[r].role, "depth");
        for (uint8_t p = 0; p < pages; p++) {
          if (engine) {
            fixture_engines();
            if (write_page(disp, dir, "-live", b)) return 1;
            bd_set_invalid(bd_find("propulsion.port.temperature"));   // the port coolant sender fails
            bd_set_invalid(bd_find("tanks.fuel.0.currentLevel"));     // and so does the fuel tank sender
            if (write_page(disp, dir, "-fault", b)) return 1;
            fixture_tanks();
            fixture_engines();                                        // starboard shut down
            bd_set_number(bd_find("propulsion.starboard.revolutions"), 0, SK_SOURCE_ID, platform_millis());
            bd_set_number(bd_find("propulsion.starboard.oilPressure"), 0, SK_SOURCE_ID, platform_millis());
            if (write_page(disp, dir, "-off", b)) return 1;
            if (p == 0) {   // the port coolant alarm, short and then too long for the banner
              fixture_engines();
              alert_raise("propulsion.port.temperature", AL_ALARM, "Port coolant high");
              alerts_show();
              if (write_page(disp, dir, "-alarm", b)) return 1;
              char longmsg[97];
              for (int k = 0; k < 96; k++) longmsg[k] = (char)("coolant temperature high on the port engine "[k % 44]);
              longmsg[96] = 0;
              alert_raise("propulsion.port.temperature", AL_EMERGENCY, longmsg);
              alerts_show();
              if (write_page(disp, dir, "-alarm-long", b)) return 1;
              alerts_clear("propulsion.port.temperature");
              alerts_show();
            }
          } else if (glance) {
            fixture_engines();
            fixture_tanks();
            fixture_power();
            fixture_nav("full");
            fixture_depth(true);
            if (write_page(disp, dir, "-live", b)) return 1;
            {   // the depth forecast falls into an alarm zone: a red line and a LOW TIDE tile, still amber
              bd_meta_t lt = {.zone_count = 1};
              lt.zones[0] = (bd_zone_t){.lower = 2.0, .upper = 2.3, .state = 3};
              bd_set_meta(bd_find("environment.depth.belowKeel"), &lt);
              if (write_page(disp, dir, "-low-tide", b)) return 1;
              fixture_depth(true);
            }
            bd_set_invalid(bd_find("electrical.batteries.279.capacity.stateOfCharge"));   // the house charge goes
            bd_set_invalid(bd_find("propulsion.port.temperature"));                         // and the port coolant sender
            bd_set_invalid(bd_find("tanks.freshWater.1.currentLevel"));                     // and a water tank sender
            bd_set_invalid(bd_find("environment.depth.belowKeel"));                         // and the sounder
            bd_set_invalid(bd_find("environment.depth.belowSurface"));
            if (write_page(disp, dir, "-fault", b)) return 1;
            fixture_power();
            fixture_depth(true);
            static const char *const tile_states[] = {"holding", "dragging", "nodata"};   // the Anchor tile
            for (size_t k = 0; k < sizeof tile_states / sizeof tile_states[0]; k++) {
              anchor_state(tile_states[k]);
              char suffix[32];
              snprintf(suffix, sizeof suffix, "-anchor-%s", tile_states[k]);
              if (write_page(disp, dir, suffix, b)) return 1;
            }
            anchor_state("notset");
          } else if (navigation) {
            for (size_t k = 0; k < sizeof nav_states / sizeof nav_states[0]; k++) {
              fixture_nav(nav_states[k]);
              char suffix[32];
              snprintf(suffix, sizeof suffix, "-%s", nav_states[k]);
              if (write_page(disp, dir, suffix, b)) return 1;
            }
          } else if (anchor) {
            for (size_t k = 0; k < sizeof anchor_states / sizeof anchor_states[0]; k++) {
              anchor_state(anchor_states[k]);
              char suffix[32];
              snprintf(suffix, sizeof suffix, "-%s", anchor_states[k]);
              if (write_page(disp, dir, suffix, b)) return 1;
            }
            anchor_state("notset");
          } else if (depth) {
            fixture_depth(true);
            if (write_page(disp, dir, "-live", b)) return 1;
            {   // a warn zone the forecast line (2.4 m, up to 3.8 m, then down to about 2.2 m) falls into, and then an alarm one
              bd_meta_t lt = {.zone_count = 1};
              lt.zones[0] = (bd_zone_t){.lower = 2.0, .upper = 2.3, .state = 2};
              bd_set_meta(bd_find("environment.depth.belowKeel"), &lt);
              if (write_page(disp, dir, "-low-tide-warn", b)) return 1;
              lt.zones[0].state = 3;
              bd_set_meta(bd_find("environment.depth.belowKeel"), &lt);
              if (write_page(disp, dir, "-low-tide-alarm", b)) return 1;
              fixture_depth(true);   // the default zones again
            }
            bd_set_invalid(bd_find("environment.tide.heightNow"));   // the tide plugin stops: the height goes, the next high stays
            if (write_page(disp, dir, "-tide-partial", b)) return 1;
            bd_set_invalid(bd_find("environment.depth.belowKeel"));   // the sounder fails: surface depth takes over, named as such
            if (write_page(disp, dir, "-surface", b)) return 1;
            bd_set_invalid(bd_find("environment.depth.belowSurface"));
            if (write_page(disp, dir, "-fault", b)) return 1;
            // A boat with a sounder and no tide plugin: nothing on the tide paths was ever received (rx_ms 0).
            static const char *const tide_paths[] = {"heightNow", "heightHigh", "heightLow", "timeHigh", "timeLow"};
            for (size_t k = 0; k < sizeof tide_paths / sizeof tide_paths[0]; k++) {
              char tp[48];
              snprintf(tp, sizeof tp, "environment.tide.%s", tide_paths[k]);
              bd_set_number(bd_intern(tp), NAN, SK_SOURCE_ID, 0);
            }
            fixture_depth(false);
            if (write_page(disp, dir, "-no-tide", b)) return 1;
            fixture_depth(true);
          } else if (tanks) {
            fixture_tanks();
            if (write_page(disp, dir, "-live", b)) return 1;
            bd_set_invalid(bd_find("tanks.freshWater.1.currentLevel"));   // the second fresh water sender fails
            if (write_page(disp, dir, "-fault", b)) return 1;
          } else if (power) {
            fixture_power();
            if (write_page(disp, dir, "-live", b)) return 1;
            if (p == 0) {   // the solar controller's current goes null: IN and LOAD fail, the battery stays live
              bd_set_invalid(bd_find("electrical.solar.288.current"));
              if (write_page(disp, dir, "-fault", b)) return 1;
            }
          } else if (write_page(disp, dir, "", b)) {
            return 1;
          }
          ui_shell_move(NAV_UP);
        }
        if (r == 0) {   // two warnings, with the list open
          alert_raise("tanks.freshWater.0.currentLevel", AL_WARN, "Fresh water low");
          alert_raise("tanks.fuel.0.currentLevel", AL_ALERT, "Fuel below 20 percent");
          alerts_show();
          lv_obj_send_event(ui_alerts_part(UI_ALERTS_MARKER), LV_EVENT_CLICKED, NULL);
          alerts_show();
          lv_refr_now(disp);
          snprintf(path, sizeof path, "%s/alerts-list.png", dir);
          if (!snapshot_write_png(path, snap_fb, b->width, b->height)) return 1;
          lv_obj_send_event(ui_alerts_part(UI_ALERTS_CLOSE), LV_EVENT_CLICKED, NULL);
          alerts_clear("tanks.freshWater.0.currentLevel");
          alerts_clear("tanks.fuel.0.currentLevel");
          alerts_show();
          if ((!strcmp(b->id, "sim800") && pal == PALETTE_DAY) || (!strcmp(b->id, "sim480") && pal == PALETTE_NIGHT)) {
            lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);   // the quick panel
            lv_refr_now(disp);
            snprintf(path, sizeof path, "%s/quick-panel.png", dir);
            if (!snapshot_write_png(path, snap_fb, b->width, b->height)) return 1;
            lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);
            lv_timer_handler();
          }
          if ((!strcmp(b->id, "sim800") || !strcmp(b->id, "sim480")) && pal != PALETTE_DUSK) {   // setup
            lv_obj_send_event(ui_shell_strip_tap(), LV_EVENT_CLICKED, NULL);
            lv_obj_send_event(ui_quick_part(UI_QUICK_SETUP), LV_EVENT_CLICKED, NULL);
            lv_timer_handler();
            static const struct { ui_setup_screen_t s; const char *name; } shots[] = {
              {SETUP_MENU, "setup-menu"}, {SETUP_DISPLAY, "setup-display"}, {SETUP_ABOUT, "setup-about"},
              {SETUP_ROLES, "setup-roles"}, {SETUP_DISCOVERY, "setup-discovery"}};
            for (size_t k = 0; k < sizeof shots / sizeof shots[0]; k++) {
              ui_setup_show(shots[k].s);
              lv_obj_invalidate(lv_display_get_screen_active(disp));
              lv_refr_now(disp);
              snprintf(path, sizeof path, "%s/%s.png", dir, shots[k].name);
              if (!snapshot_write_png(path, snap_fb, b->width, b->height)) return 1;
            }
            static const ui_ap_t aps[] = {{"Boatnet", -48, true}, {"Marina Guest", -71, false}, {"Neighbour", -83, true}};
            ui_setup_show(SETUP_WIFI);
            ui_setup_wifi_results(aps, 3);
            lv_timer_handler();
            static const char *const extra[] = {"setup-wifi", "setup-wifi-keyboard", "setup-signalk"};
            for (size_t k = 0; k < 3; k++) {
              if (k == 1) {
                lv_obj_send_event(ui_setup_part("ap_Boatnet"), LV_EVENT_CLICKED, NULL);
                lv_timer_handler();
              }
              if (k == 2) {
                ui_setup_show(SETUP_SIGNALK);
                lv_timer_handler();
              }
              lv_obj_invalidate(lv_display_get_screen_active(disp));
              lv_refr_now(disp);
              snprintf(path, sizeof path, "%s/%s.png", dir, extra[k]);
              if (!snapshot_write_png(path, snap_fb, b->width, b->height)) return 1;
            }
            ui_setup_close();
            lv_timer_handler();
          }
        }
      }
      // Power with banks configured: Summary (START link, NAV), Banks, Batteries, Charging. The fault render has
      // the charger's current and the nav shunt's current invalid: HOUSE IN and NAV LOAD fail, the rest stay live.
      for (uint8_t r = 0; r < cfg->role_count; r++) {
        if (strcmp(cfg->roles[r].role, "power")) continue;
        fixture_banks();
        set_banks(&cfg->roles[r]);
        snprintf(cfg->start_role, sizeof cfg->start_role, "power");
        app_bind();
        app_start_ui(disp, b->size_class);
        const ui_nav_t *n = ui_shell_nav();
        uint8_t pages = n->page_count[n->role];
        for (uint8_t p = 0; p < pages; p++) {
          fixture_banks();
          if (write_page(disp, dir, "-banks-live", b)) return 1;
          bd_set_invalid(bd_find("electrical.chargers.276.current"));
          bd_set_invalid(bd_find("electrical.batteries.281.current"));
          if (write_page(disp, dir, "-banks-fault", b)) return 1;
          ui_shell_move(NAV_UP);
        }
      }
    }
    lv_display_delete(disp);
    free(draw);
    free(snap_fb);
  }
  return 0;
}

// ---- interactive ----

static void tick_cb(lv_timer_t *t) {
  (void)t;
  app_tick();
}

// --console: the same commands as the boards' serial prompt, typed into this terminal.
static void line_out(const char *line) { printf("%s\n", line); }
static void console_loop(void *arg) {
  (void)arg;
  char line[512];
  printf("bi> ");
  fflush(stdout);
  while (fgets(line, sizeof line, stdin)) {
    char *argv[16];
    int argc = 0;
    for (char *p = line; *p && argc < 16;) {   // words, or "quoted words"
      while (*p == ' ' || *p == '\t' || *p == '\n') p++;
      if (!*p) break;
      char end = *p == '"' ? '"' : ' ';
      if (end == '"') p++;
      argv[argc++] = p;
      while (*p && *p != end && *p != '\n') p++;
      if (*p) *p++ = 0;
    }
    if (argc) app_cmd_run(argc, argv, line_out);
    printf("bi> ");
    fflush(stdout);
  }
}

static palette_t parse_palette(const char *s) {
  if (strcmp(s, "dusk") == 0) return PALETTE_DUSK;
  if (strcmp(s, "night") == 0) return PALETTE_NIGHT;
  return PALETTE_DAY;
}

// ---- NMEA 2000 bus (--can <ifname>): sources.nmea2000 from the config, read from a SocketCAN interface ----
// Listen-only: no node, so the simulator never transmits. The same app_can glue the firmware runs.
static can_socketcan_t *can_sock;
static bool can_start(const cfg_t *cfg, const char *ifname) {
  can_sock = can_socketcan_open(ifname);
  if (!can_sock) {
    fprintf(stderr, "can: cannot open %s: %s (bring it up first: sudo ip link set %s up type can bitrate 250000)\n",
            ifname, strerror(errno), ifname);
    return false;
  }
  app_can_start(cfg, can_socketcan_ops(can_sock));
  fprintf(stderr, "can: %s listening (%s, %u mapped ids)\n", ifname,
          cfg->n2k.only ? "only: the bus creates its own paths and there is no Signal K" : "fallback: Signal K first",
          cfg->n2k.id_count);
  return true;
}

int main(int argc, char **argv) {
  const char *board_id = "sim800", *snapshot_dir = NULL, *config_dir = "simcfg", *palette = NULL;
  const char *capture = NULL, *can_if = NULL;
  bool can_only = false;
  int after_s = 10;
  int page = 0;
  bool console = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) board_id = argv[++i];
    else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) palette = argv[++i];
    else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snapshot_dir = argv[++i];
    else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) config_dir = argv[++i];
    else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
    else if (strcmp(argv[i], "--after") == 0 && i + 1 < argc) after_s = atoi(argv[++i]);
    else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) page = atoi(argv[++i]);
    else if (strcmp(argv[i], "--console") == 0) console = true;
    else if (strcmp(argv[i], "--can") == 0 && i + 1 < argc) can_if = argv[++i];
    else if (strcmp(argv[i], "--can-only") == 0) can_only = true;
    else {
      fprintf(stderr, "usage: sim [--board sim480|sim800|sim1024] [--palette day|dusk|night]\n"
                      "           [--config <dir with config.json>] [--snapshot <dir>]\n"
                      "           [--capture <png> [--after <seconds>] [--page <n>]]   live, headless\n"
                      "           [--console]   serial commands on this terminal\n"
                      "           [--can <ifname> [--can-only]]   read NMEA 2000 from a SocketCAN interface (listen-only); sources.nmea2000\n"
                      "                                           in the config gives the mode and ids, --can-only forces mode only\n");
      return 2;
    }
  }
  const sim_board_t *b = sim_board_find(board_id);
  if (!b) {
    fprintf(stderr, "unknown board %s\n", board_id);
    return 2;
  }

  lv_init();
  lv_tick_set_cb(tick);
  app_set_backlight(NULL, true);
  app_set_display_reset(sim_display_reset);
  if (snapshot_dir) return run_snapshots(snapshot_dir);

  cfg_report_t rep;
  struct stat st;
  cfg_t *cfg = (cfg_t *)app_init(stat(config_dir, &st) == 0 ? config_dir : NULL, &rep);
  for (uint8_t i = 0; i < rep.count; i++)
    fprintf(stderr, "config: %s (line %d): %s\n", rep.issues[i].key_path, rep.issues[i].line, rep.issues[i].message);
  if (palette) snprintf(cfg->palette, sizeof cfg->palette, "%s", palette_names[parse_palette(palette)]);
  app_set_info(board_id, "sim");
  app_bind();
  bool use_can = can_if != NULL;
  if (use_can) {   // --can names the interface; the mode and ids come from sources.nmea2000
    cfg->n2k.enabled = true;
    if (can_only) cfg->n2k.only = true;
    if (!can_start(cfg, can_if)) return 2;
  }
  bool want_network = !use_can || app_network_wanted(cfg);

  if (capture) {   // live data, no window: run for a while, then save the screen
    size_t px = (size_t)b->width * b->height;
    snap_fb = calloc(px, 2);
    snap_w = b->width;
    lv_display_t *disp = lv_display_create(b->width, b->height);
    lv_display_set_flush_cb(disp, snap_flush);
    lv_display_set_buffers(disp, malloc(px * 2), NULL, px * 2, LV_DISPLAY_RENDER_MODE_FULL);
    app_start_ui(disp, b->size_class);
    for (int p = 0; p < page; p++) ui_shell_move(NAV_UP);
    if (want_network) app_start_network();
    if (console) platform_task(console_loop, NULL, "console", 0);
    int64_t end = platform_millis() + (int64_t)after_s * 1000;
    while (platform_millis() < end) {
      app_tick();
      app_can_poll();
      lv_timer_handler();
      usleep(100 * 1000);
    }
    lv_obj_invalidate(lv_display_get_screen_active(disp));
    lv_refr_now(disp);
    return snapshot_write_png(capture, snap_fb, b->width, b->height) ? 0 : 1;
  }

  lv_display_t *disp = lv_sdl_window_create(b->width, b->height);
  lv_sdl_mouse_create();
  lv_group_set_default(lv_group_create());
  lv_indev_set_group(lv_sdl_keyboard_create(), lv_group_get_default());
  app_start_ui(disp, b->size_class);
  if (want_network) app_start_network();
  if (console) platform_task(console_loop, NULL, "console", 0);
  lv_timer_create(tick_cb, 100, NULL);

  for (;;) {
    app_can_poll();
    uint32_t wait = lv_timer_handler();
    usleep((wait > 50 ? 50 : wait) * 1000);
  }
}
