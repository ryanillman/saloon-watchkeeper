#include <string.h>
#include "sim_boards.h"

static const sim_board_t boards[] = {
  {"sim480", 480, 480, SIZE_SQUARE},
  {"sim800", 800, 480, SIZE_WIDE},
  {"sim1024", 1024, 600, SIZE_WIDE},
};

const sim_board_t *sim_board_find(const char *id) {
  for (size_t i = 0; i < sizeof boards / sizeof boards[0]; i++)
    if (strcmp(boards[i].id, id) == 0) return &boards[i];
  return NULL;
}

const sim_board_t *sim_boards(uint8_t *count) {
  *count = sizeof boards / sizeof boards[0];
  return boards;
}
