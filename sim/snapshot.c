#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include "snapshot.h"

bool snapshot_write_png(const char *path, const uint16_t *rgb565, uint16_t w, uint16_t h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info = png ? png_create_info_struct(png) : NULL;
  uint8_t *row = malloc((size_t)w * 3);
  bool ok = false;
  if (!png || !info || !row || setjmp(png_jmpbuf(png))) goto done;

  png_init_io(png, f);
  png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      uint16_t p = rgb565[(size_t)y * w + x];
      uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
      row[x * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
      row[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
      row[x * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
    }
    png_write_row(png, row);
  }
  png_write_end(png, NULL);
  ok = true;

done:
  if (png) png_destroy_write_struct(&png, info ? &info : NULL);
  free(row);
  fclose(f);
  return ok;
}
