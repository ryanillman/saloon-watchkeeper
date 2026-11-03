// Desktop simulator: the same UI components in an SDL window.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lvgl.h"
#include "platform.h"
#include "sim_boards.h"
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

static palette_t parse_palette(const char *s) {
  if (strcmp(s, "dusk") == 0) return PALETTE_DUSK;
  if (strcmp(s, "night") == 0) return PALETTE_NIGHT;
  return PALETTE_DAY;
}

int main(int argc, char **argv) {
  const char *board_id = "sim800";
  palette_t pal = PALETTE_DAY;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) board_id = argv[++i];
    else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) pal = parse_palette(argv[++i]);
    else {
      fprintf(stderr, "usage: sim [--board sim480|sim800|sim1024] [--palette day|dusk|night]\n");
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
