#include "app_can.h"
#include <string.h>
#include "platform.h"
#include "app_electrical.h"
#include "source_can.h"

static bool started;
static source_can_t src;
PLATFORM_EXT_BSS static source_can_id_t ids[CFG_MAX_N2K_IDS];
PLATFORM_EXT_BSS static cfg_n2k_id_t id_text[CFG_MAX_N2K_IDS];   // the strings ids[] points at must outlive a config change
static n2k_bus_t bus;
PLATFORM_EXT_BSS static n2k_fp_slot_t slots[8];   // ~2 KB of fast-packet reassembly: not internal RAM

static uint32_t now_ms(void *ctx) {
  (void)ctx;
  return (uint32_t)platform_millis();
}

bool app_can_start(const cfg_t *cfg, n2k_can_ops_t ops) {
  if (started) return false;
  memcpy(id_text, cfg->n2k.ids, sizeof id_text);
  for (uint8_t i = 0; i < cfg->n2k.id_count; i++) ids[i] = (source_can_id_t){id_text[i].key, id_text[i].id};
  source_can_cfg_t sc = {cfg->n2k.only, ids, cfg->n2k.id_count, app_electrical()};
  source_can_init(&src, &sc);
  n2k_bus_init(&bus, &ops, NULL, slots, sizeof slots / sizeof slots[0], source_can_on_msg, &src, now_ms, NULL);
  started = true;
  return true;
}

void app_can_stop(void) { started = false; }

void app_can_poll(void) {
  if (!started) return;
  n2k_bus_step(&bus, 256);
  source_can_tick(&src);
}

void app_can_status(app_can_status_t *out) {
  memset(out, 0, sizeof *out);
  if (!started) return;
  const n2k_bus_stats_t *b = n2k_bus_stats(&bus);
  out->started = true;
  out->state = src.state;
  out->rx_frames = b->rx_frames;
  out->values = src.values;
  out->unmapped = src.unmapped;
  out->bus_off_events = b->bus_off_events;
}

bool app_network_wanted(const cfg_t *cfg) { return !(cfg->n2k.enabled && cfg->n2k.only); }
