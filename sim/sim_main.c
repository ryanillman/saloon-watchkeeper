// Desktop simulator: the same UI and data components in an SDL window,
// talking to a real Signal K server. --snapshot renders every page to PNG.
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app.h"
#include "boatdata.h"
#include "lvgl.h"
#include "platform.h"
#include "sim_boards.h"
#include "sk_parse.h"
#include "snapshot.h"
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

static const char *palette_names[] = {"day", "dusk", "night"};

static int write_page(lv_display_t *disp, const char *dir, const char *suffix, const sim_board_t *b) {
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
      fixture_engines();
      app_bind();
      for (uint8_t r = 0; r < cfg->role_count; r++) {
        snprintf(cfg->start_role, sizeof cfg->start_role, "%s", cfg->roles[r].role);
        app_start_ui(disp, b->size_class);
        const ui_nav_t *n = ui_shell_nav();
        uint8_t pages = n->page_count[n->role];
        bool engine = !strcmp(cfg->roles[r].role, "engine");
        for (uint8_t p = 0; p < pages; p++) {
          if (engine) {
            fixture_engines();
            if (write_page(disp, dir, "-live", b)) return 1;
            bd_set_invalid(bd_find("propulsion.port.temperature"));   // the port coolant sender fails
            if (write_page(disp, dir, "-fault", b)) return 1;
            fixture_engines();                                        // starboard shut down
            bd_set_number(bd_find("propulsion.starboard.revolutions"), 0, SK_SOURCE_ID, platform_millis());
            bd_set_number(bd_find("propulsion.starboard.oilPressure"), 0, SK_SOURCE_ID, platform_millis());
            if (write_page(disp, dir, "-off", b)) return 1;
          } else if (write_page(disp, dir, "", b)) {
            return 1;
          }
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

static palette_t parse_palette(const char *s) {
  if (strcmp(s, "dusk") == 0) return PALETTE_DUSK;
  if (strcmp(s, "night") == 0) return PALETTE_NIGHT;
  return PALETTE_DAY;
}

int main(int argc, char **argv) {
  const char *board_id = "sim800", *snapshot_dir = NULL, *config_dir = "simcfg", *palette = NULL;
  const char *capture = NULL;
  int after_s = 10;
  int page = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) board_id = argv[++i];
    else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) palette = argv[++i];
    else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snapshot_dir = argv[++i];
    else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) config_dir = argv[++i];
    else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
    else if (strcmp(argv[i], "--after") == 0 && i + 1 < argc) after_s = atoi(argv[++i]);
    else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) page = atoi(argv[++i]);
    else {
      fprintf(stderr, "usage: sim [--board sim480|sim800|sim1024] [--palette day|dusk|night]\n"
                      "           [--config <dir with config.json>] [--snapshot <dir>]\n"
                      "           [--capture <png> [--after <seconds>] [--page <n>]]   live, headless\n");
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
  if (snapshot_dir) return run_snapshots(snapshot_dir);

  cfg_report_t rep;
  struct stat st;
  cfg_t *cfg = (cfg_t *)app_init(stat(config_dir, &st) == 0 ? config_dir : NULL, &rep);
  for (uint8_t i = 0; i < rep.count; i++)
    fprintf(stderr, "config: %s (line %d): %s\n", rep.issues[i].key_path, rep.issues[i].line, rep.issues[i].message);
  if (palette) snprintf(cfg->palette, sizeof cfg->palette, "%s", palette_names[parse_palette(palette)]);
  app_bind();

  if (capture) {   // live data, no window: run for a while, then save the screen
    size_t px = (size_t)b->width * b->height;
    snap_fb = calloc(px, 2);
    snap_w = b->width;
    lv_display_t *disp = lv_display_create(b->width, b->height);
    lv_display_set_flush_cb(disp, snap_flush);
    lv_display_set_buffers(disp, malloc(px * 2), NULL, px * 2, LV_DISPLAY_RENDER_MODE_FULL);
    app_start_ui(disp, b->size_class);
    for (int p = 0; p < page; p++) ui_shell_move(NAV_UP);
    app_start_network();
    int64_t end = platform_millis() + (int64_t)after_s * 1000;
    while (platform_millis() < end) {
      app_tick();
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
  app_start_network();
  lv_timer_create(tick_cb, 100, NULL);

  for (;;) {
    uint32_t wait = lv_timer_handler();
    usleep((wait > 50 ? 50 : wait) * 1000);
  }
}
