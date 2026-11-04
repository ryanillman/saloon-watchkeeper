#include "glance.h"
#include <math.h>
#include <stdio.h>
#include "alerts.h"

gt_state_t glance_worst(const gt_state_t *s, size_t n) {
  gt_state_t w = GT_OFF;
  for (size_t i = 0; i < n; i++)
    if (s[i] > w) w = s[i];
  return w;
}

static gt_state_t from_alert_level(unsigned level) {   // alert_state_t and the zone states share an order
  switch (level) {
    case AL_ALERT: return GT_ALERT;
    case AL_WARN: return GT_WARN;
    case AL_ALARM:
    case AL_EMERGENCY: return GT_ALARM;
    default: return GT_OK;
  }
}

gt_state_t glance_path_state(bd_id_t id, int32_t max_age_ms, int64_t now_ms) {
  const char *path = id != BD_NONE ? bd_path(id) : NULL;
  gt_state_t s = path && *path ? from_alert_level(alerts_for_path(path)) : GT_OK;
  bd_sample_t smp;
  if (id == BD_NONE || !bd_get(id, &smp) || !bd_is_fresh(&smp, max_age_ms, now_ms)) return s > GT_NO_DATA ? s : GT_NO_DATA;
  if (smp.kind == BD_NUMBER) {
    bd_meta_t m;
    if (bd_get_meta(id, &m))
      for (uint8_t z = 0; z < m.zone_count; z++)
        if (smp.num >= m.zones[z].lower && smp.num < m.zones[z].upper) {
          gt_state_t zs = from_alert_level(m.zones[z].state);
          if (zs > s) s = zs;
        }
  }
  return s;
}

const char *glance_state_word(gt_state_t s) {
  switch (s) {
    case GT_OFF: return "OFF";
    case GT_OK: return "OK";
    case GT_ALERT: return "ALERT";
    case GT_WARN: return "WARN";
    case GT_NO_DATA: return "NO DATA";
    default: return "ALARM";
  }
}

void glance_tile_state(glance_tile_t *t, gt_state_t s) {
  t->state = s;
  snprintf(t->word, sizeof t->word, "%s", glance_state_word(s));
}
