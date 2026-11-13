#include "app_electrical.h"
#include "platform.h"

PLATFORM_EXT_BSS static el_t resolver;   // ~7 KB: not internal RAM
static bool ready;

el_t *app_electrical(void) {
  if (!ready) {
    el_init(&resolver);
    ready = true;
  }
  return &resolver;
}
