#pragma once
#include <stdint.h>
#include "ui_theme.h"

typedef struct {
  const char *id;
  uint16_t width, height;
  size_class_t size_class;
} sim_board_t;

// "sim480" | "sim800" | "sim1024"; NULL for anything else.
const sim_board_t *sim_board_find(const char *id);

// All simulated boards, for snapshot runs.
const sim_board_t *sim_boards(uint8_t *count);
