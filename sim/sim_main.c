// Desktop simulator: the same UI components in an SDL window.
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "lvgl.h"
#include "platform.h"
#include "sim_boards.h"
#include "snapshot.h"
#include "ui_shell.h"

static void placeholder_page(lv_obj_t *area, uint8_t page, size_class_t sc, void *ctx) {
  const ui_role_view_t *r = ctx;
  lv_obj_t *l = lv_label_create(area);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text_fmt(l, "%s\npage %u of %u\n%s", r->name, page + 1, r->page_count,
                        sc == SIZE_WIDE ? "wide" : "square");
  lv_obj_center(l);
}

static ui_role_view_t roles[] = {
  {"ENGINE", 2, placeholder_page, NULL},
  {"POWER", 2, placeholder_page, NULL},
  {"TANKS", 1, placeholder_page, NULL},
  {"ANCHOR", 1, placeholder_page, NULL},
};
#define ROLE_COUNT (sizeof roles / sizeof roles[0])

static uint32_t tick(void) { return (uint32_t)platform_millis(); }

// ---- Headless snapshots: every board x palette x role x page, no window ----

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

static const char *palette_names[] = {"day", "dusk", "night"};

static int run_snapshots(const char *out) {
  uint8_t nboards;
  const sim_board_t *boards = sim_boards(&nboards);
  for (uint8_t bi = 0; bi < nboards; bi++) {
    const sim_board_t *b = &boards[bi];
    size_t px = (size_t)b->width * b->height;
    snap_fb = calloc(px, 2);
    void *draw = malloc(px * 2);
    snap_w = b->width;
    lv_display_t *disp = lv_display_create(b->width, b->height);
    lv_display_set_flush_cb(disp, snap_flush);
    lv_display_set_buffers(disp, draw, NULL, px * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    for (int pal = PALETTE_DAY; pal <= PALETTE_NIGHT; pal++) {
      char dir[512];
      snprintf(dir, sizeof dir, "%s/%s/%s", out, b->id, palette_names[pal]);
      if (mkdirs(dir)) {
        fprintf(stderr, "cannot create %s\n", dir);
        return 1;
      }
      for (uint8_t r = 0; r < ROLE_COUNT; r++) {
        ui_shell_create(disp, b->size_class, (palette_t)pal, roles, ROLE_COUNT, r);
        for (uint8_t p = 0; p < roles[r].page_count; p++) {
          lv_obj_invalidate(lv_display_get_screen_active(disp));
          lv_refr_now(disp);
          char name[32], path[600];
          snprintf(name, sizeof name, "%s", roles[r].name);
          for (char *c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
          snprintf(path, sizeof path, "%s/%s-p%u.png", dir, name, ui_shell_nav()->page);
          if (!snapshot_write_png(path, snap_fb, b->width, b->height)) {
            fprintf(stderr, "cannot write %s\n", path);
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

static palette_t parse_palette(const char *s) {
  if (strcmp(s, "dusk") == 0) return PALETTE_DUSK;
  if (strcmp(s, "night") == 0) return PALETTE_NIGHT;
  return PALETTE_DAY;
}

int main(int argc, char **argv) {
  const char *board_id = "sim800";
  palette_t pal = PALETTE_DAY;
  const char *snapshot_dir = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) board_id = argv[++i];
    else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) pal = parse_palette(argv[++i]);
    else if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snapshot_dir = argv[++i];
    else {
      fprintf(stderr, "usage: sim [--board sim480|sim800|sim1024] [--palette day|dusk|night] [--snapshot <dir>]\n");
      return 2;
    }
  }
  const sim_board_t *b = sim_board_find(board_id);
  if (!b) {
    fprintf(stderr, "unknown board %s\n", board_id);
    return 2;
  }
  for (size_t i = 0; i < ROLE_COUNT; i++) roles[i].ctx = &roles[i];

  lv_init();
  lv_tick_set_cb(tick);
  if (snapshot_dir) return run_snapshots(snapshot_dir);

  lv_display_t *disp = lv_sdl_window_create(b->width, b->height);
  lv_sdl_mouse_create();
  lv_group_set_default(lv_group_create());
  lv_indev_set_group(lv_sdl_keyboard_create(), lv_group_get_default());

  ui_shell_create(disp, b->size_class, pal, roles, ROLE_COUNT, 0);

  for (;;) {
    uint32_t wait = lv_timer_handler();
    usleep((wait > 50 ? 50 : wait) * 1000);
  }
}
