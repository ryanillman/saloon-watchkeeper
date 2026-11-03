#include "ui_theme.h"

#define C(hex) LV_COLOR_MAKE(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)

// Black backgrounds in every palette, after avionics displays.
static const ui_colors_t palettes[] = {
  [PALETTE_DAY] = {
    .bg = C(0x000000), .fg = C(0xFFFFFF), .dim = C(0x8A8A8A), .accent = C(0x00C8FF),
    .strip_bg = C(0x1A1A1A), .warn = C(0xFFB000), .alarm = C(0xFF0000),
    .fault_cross = C(0xFF0000), .fault_label = C(0xFFD000)},
  [PALETTE_DUSK] = {
    .bg = C(0x000000), .fg = C(0xB0B0B0), .dim = C(0x5A5A5A), .accent = C(0x0088AA),
    .strip_bg = C(0x101010), .warn = C(0xB07A00), .alarm = C(0xC00000),
    .fault_cross = C(0xFF0000), .fault_label = C(0xFFD000)},
  // Night is all red. The fault flag relies on the cross shape and the word FAIL.
  [PALETTE_NIGHT] = {
    .bg = C(0x000000), .fg = C(0xFF2A00), .dim = C(0x7A1400), .accent = C(0xFF2A00),
    .strip_bg = C(0x140300), .warn = C(0xFF2A00), .alarm = C(0xFF2A00),
    .fault_cross = C(0xFF0000), .fault_label = C(0xFF2A00)},
};

const ui_colors_t *ui_theme_colors(palette_t p) {
  if (p > PALETTE_NIGHT) p = PALETTE_DAY;
  return &palettes[p];
}
