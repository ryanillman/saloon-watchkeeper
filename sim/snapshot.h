#pragma once
#include <stdbool.h>
#include <stdint.h>

// Writes an RGB565 framebuffer as an 8-bit RGB PNG. Creates no directories.
bool snapshot_write_png(const char *path, const uint16_t *rgb565, uint16_t w, uint16_t h);
