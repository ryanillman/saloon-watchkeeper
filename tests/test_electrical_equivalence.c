// The same situation, told once as abstract readings, must give the same electrical endpoints whether it arrives as
// CAN frames or as Signal K deltas, and whatever order the readings come in. Expectations are by function.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boatdata.h"
#include "cJSON.h"
#include "electrical.h"
#include "n2k.h"
#include "platform.h"
#include "sk_electrical.h"
#include "sk_parse.h"
#include "source_can.h"
#include "unity.h"

typedef struct {
  uint8_t src;            // the device's address (CAN source, Signal K "$source" is can0.<src>)
  const char *model;      // "" = no product information
  uint64_t name;          // 0 = no claim
} device_t;

typedef struct {
  uint8_t dev, inst;
  el_field_t f;
  double v;
  bool can_only;          // Signal K cannot carry it (no DC type there)
} reading_t;

typedef struct {
  const char *name;
  device_t dev[4];
  size_t ndev;
  reading_t r[16];
  size_t nr;
  unsigned batteries, solar, chargers;   // what a person should see
} scenario_t;

#define V(d, i, x) {d, i, EL_F_VOLTAGE, x, false}
#define I(d, i, x) {d, i, EL_F_CURRENT, x, false}
#define SOC(d, i, x) {d, i, EL_F_SOC, x, false}
#define DC(d, i, x) {d, i, EL_F_DC_TYPE, x, true}
#define ST(d, i, x) {d, i, EL_F_CONV_STATE, x, false}

static const scenario_t SCENARIOS[] = {
  {"two_devices_same_instance", {{10, "Some Shunt 500A", 0x111}, {20, "", 0x222}}, 2,
   {V(0, 0, 13.4), I(0, 0, -3.0), SOC(0, 0, 0.82), DC(0, 0, 0), V(1, 0, 12.1), I(1, 0, 0.0), DC(1, 0, 0)}, 7, 2, 0, 0},
  {"pv_input_as_battery", {{10, "Some Shunt 500A", 0x111}, {30, "Sun MPPT 100/15", 0x333}}, 2,
   {V(0, 0, 13.4), I(0, 0, -3.0), SOC(0, 0, 0.82), DC(0, 0, 0), ST(1, 0, 4), V(1, 0, 13.8), I(1, 0, 5.0), DC(1, 0, 0),
    V(1, 1, 20.5), DC(1, 1, 3)}, 10, 1, 1, 1},
  {"mirrored_system_battery", {{10, "Some Shunt 500A", 0x111}, {40, "", 0x444}}, 2,
   {V(0, 0, 13.82), I(0, 0, 0.0), SOC(0, 0, 1.0), DC(0, 0, 0), V(1, 7, 13.82), I(1, 7, 0.0), SOC(1, 7, 1.0), DC(1, 7, 0)}, 8, 1, 0, 0},
  {"voltage_only_battery", {{10, "", 0x111}}, 1, {V(0, 1, 13.0)}, 1, 1, 0, 0},
  {"no_product_info", {{10, "", 0}}, 1, {V(0, 0, 12.7), I(0, 0, -2.0), DC(0, 0, 0)}, 3, 1, 0, 0},
  {"monitor_and_charger_same_bank", {{10, "Some Shunt 500A", 0x111}, {50, "Acme Charger 30A", 0x555}}, 2,
   {V(0, 0, 13.8), I(0, 0, 0.0), SOC(0, 0, 1.0), DC(0, 0, 0), ST(1, 0, 5), V(1, 0, 13.8), I(1, 0, 10.0), DC(1, 0, 0)}, 8, 1, 0, 1},
};

static el_t el;
static source_can_t sc;
static const source_can_id_t no_ids[1];

static void put16(uint8_t *d, int off, unsigned v) { d[off] = (uint8_t)v; d[off + 1] = (uint8_t)(v >> 8); }

static void can_msg(uint8_t src, uint32_t pgn, const uint8_t *d, uint16_t len) {
  n2k_msg_t m = {pgn, src, 255, 6, len, d};
  source_can_on_msg(&m, &sc);
}

static void can_identity(const device_t *d) {
  if (d->name) {
    uint8_t c[8];
    for (int i = 0; i < 8; i++) c[i] = (uint8_t)(d->name >> (8 * i));
    can_msg(d->src, 60928, c, 8);
  }
  if (d->model[0]) {
    uint8_t p[134] = {0};
    memcpy(p + 4, d->model, strlen(d->model));
    can_msg(d->src, 126996, p, sizeof p);
  }
}

static void can_reading(const device_t *d, const reading_t *r) {
  uint8_t b[11];
  memset(b, 0xFF, sizeof b);
  switch (r->f) {
    case EL_F_VOLTAGE: b[0] = r->inst; put16(b, 1, (unsigned)(r->v * 100 + 0.5)); can_msg(d->src, 127508, b, 8); break;
    case EL_F_CURRENT: b[0] = r->inst; put16(b, 1, 0xFFFF); put16(b, 3, (unsigned)(int)(r->v * 10) & 0xFFFF); can_msg(d->src, 127508, b, 8); break;
    case EL_F_SOC: b[1] = r->inst; b[3] = (uint8_t)(r->v * 100 + 0.5); can_msg(d->src, 127506, b, 11); break;
    case EL_F_DC_TYPE: b[1] = r->inst; b[2] = (uint8_t)r->v; can_msg(d->src, 127506, b, 9); break;
    case EL_F_CONV_STATE: b[1] = r->inst; b[2] = (uint8_t)r->v; can_msg(d->src, 127750, b, 8); break;
    default: break;
  }
}

static void sk_delta(const device_t *d, const char *path, const char *value) {
  char j[400];
  snprintf(j, sizeof j, "{\"updates\":[{\"$source\":\"can0.%u\",\"values\":[{\"path\":\"%s\",\"value\":%s}]}]}", d->src, path, value);
  sk_parse_message(j, strlen(j), platform_millis());
}

static void sk_reading(const device_t *d, const reading_t *r) {
  static const char *const states[] = {"off", "low power", "fault", "bulk", "absorption", "float"};
  char path[96], val[32];
  snprintf(val, sizeof val, "%g", r->v);
  switch (r->f) {
    case EL_F_VOLTAGE: snprintf(path, sizeof path, "electrical.batteries.%u.voltage", r->inst); break;
    case EL_F_CURRENT: snprintf(path, sizeof path, "electrical.batteries.%u.current", r->inst); break;
    case EL_F_SOC: snprintf(path, sizeof path, "electrical.batteries.%u.capacity.stateOfCharge", r->inst); break;
    case EL_F_CONV_STATE:
      snprintf(path, sizeof path, "electrical.converter.%u.%u.operatingState", d->src, r->inst);
      snprintf(val, sizeof val, "\"%s\"", states[(int)r->v]);
      break;
    default: return;   // DC type: not on Signal K
  }
  sk_delta(d, path, val);
}

static void sk_sources(const scenario_t *s) {
  char j[1000] = "{\"can0\":{";
  for (size_t i = 0; i < s->ndev; i++) {
    char one[200];
    snprintf(one, sizeof one, "%s\"%u\":{\"n2k\":{", i ? "," : "", s->dev[i].src);
    strcat(j, one);
    bool any = false;
    if (s->dev[i].name) { snprintf(one, sizeof one, "\"canName\":\"%016llx\"", (unsigned long long)s->dev[i].name); strcat(j, one); any = true; }
    if (s->dev[i].model[0]) { snprintf(one, sizeof one, "%s\"modelId\":\"%s\"", any ? "," : "", s->dev[i].model); strcat(j, one); }
    strcat(j, "}}");
  }
  strcat(j, "}}");
  sk_electrical_load_sources(j, strlen(j));
}

typedef struct { unsigned batteries, solar, chargers; double max_battery_volts; } outcome_t;

static outcome_t outcome(void) {
  outcome_t o = {0, 0, 0, 0};
  bd_id_t ids[16];
  bd_sample_t s;
  size_t n = bd_match("electrical.resolved.batteries.*.voltage", ids, 16);
  for (size_t i = 0; i < n; i++)
    if (bd_get(ids[i], &s) && s.valid) { o.batteries++; if (s.num > o.max_battery_volts) o.max_battery_volts = s.num; }
  n = bd_match("electrical.resolved.solar.*.voltage", ids, 16);
  for (size_t i = 0; i < n; i++) if (bd_get(ids[i], &s) && s.valid) o.solar++;
  n = bd_match("electrical.resolved.chargers.*.voltage", ids, 16);
  for (size_t i = 0; i < n; i++) if (bd_get(ids[i], &s) && s.valid) o.chargers++;
  return o;
}

typedef enum { VIA_CAN, VIA_SK } feed_t;

static outcome_t replay(const scenario_t *s, feed_t via, bool reversed) {
  bd_init();
  el_init(&el);
  if (via == VIA_CAN) {
    source_can_cfg_t c = {true, no_ids, 0, &el};
    source_can_init(&sc, &c);
    for (size_t i = 0; i < s->ndev; i++) can_identity(&s->dev[i]);
  } else {
    sk_parse_reset();
    sk_electrical_attach(&el);
    sk_sources(s);
  }
  for (int sec = 0; sec <= 25; sec++) {   // a reading a second for 25 s: a stability window, then a merge window
    for (size_t k = 0; k < s->nr; k++) {
      const reading_t *r = &s->r[reversed ? s->nr - 1 - k : k];
      if (via == VIA_CAN) can_reading(&s->dev[r->dev], r);
      else if (!r->can_only) sk_reading(&s->dev[r->dev], r);
    }
    if (via == VIA_CAN) source_can_tick(&sc);
    platform_linux_advance_ms(1000);
  }
  if (via == VIA_CAN) source_can_tick(&sc);
  outcome_t o = outcome();
  if (via == VIA_SK) sk_electrical_attach(NULL);
  return o;
}

static void check(const scenario_t *s, feed_t via, bool reversed) {
  outcome_t o = replay(s, via, reversed);
  char msg[160];
  snprintf(msg, sizeof msg, "%s via %s%s", s->name, via == VIA_CAN ? "CAN" : "Signal K", reversed ? " reversed" : "");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(s->batteries, o.batteries, msg);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(s->solar, o.solar, msg);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(s->chargers, o.chargers, msg);
  TEST_ASSERT_TRUE_MESSAGE(o.max_battery_volts <= 16.0, msg);   // a panel input is never shown as a battery
}

void setUp(void) {}
void tearDown(void) {}

static void test_scenarios_via_can(void) {
  for (size_t i = 0; i < sizeof SCENARIOS / sizeof SCENARIOS[0]; i++) check(&SCENARIOS[i], VIA_CAN, false);
}
static void test_scenarios_via_signal_k(void) {
  for (size_t i = 0; i < sizeof SCENARIOS / sizeof SCENARIOS[0]; i++) check(&SCENARIOS[i], VIA_SK, false);
}
static void test_order_independence(void) {
  for (size_t i = 0; i < sizeof SCENARIOS / sizeof SCENARIOS[0]; i++) {
    check(&SCENARIOS[i], VIA_CAN, true);
    check(&SCENARIOS[i], VIA_SK, true);
  }
}

// ---- A real capture, replayed through the real decode path ----
static void on_msg(const n2k_msg_t *m, void *ctx) { source_can_on_msg(m, ctx); }

static void test_real_capture_by_function(void) {
  FILE *f = fopen(FIXTURE_DIR "/capture.candump", "r");
  TEST_ASSERT_NOT_NULL(f);
  bd_init();
  el_init(&el);
  source_can_cfg_t c = {true, no_ids, 0, &el};
  source_can_init(&sc, &c);
  static n2k_fp_slot_t slots[16];
  n2k_rx_t rx;
  n2k_rx_init(&rx, slots, 16, on_msg, &sc);
  char line[200];
  double t0 = -1;
  int64_t advanced = 0;
  while (fgets(line, sizeof line, f)) {
    double ts;
    char hex[32], data[40];
    if (sscanf(line, "(%lf) can0 %31[0-9A-Fa-f]#%39s", &ts, hex, data) != 3) continue;
    if (t0 < 0) t0 = ts;
    int64_t want = (int64_t)((ts - t0) * 1000);
    if (want > advanced) { platform_linux_advance_ms(want - advanced); advanced = want; }
    uint8_t d[8];
    size_t len = strlen(data) / 2;
    for (size_t i = 0; i < len && i < 8; i++) { char b[3] = {data[2 * i], data[2 * i + 1], 0}; d[i] = (uint8_t)strtoul(b, NULL, 16); }
    n2k_feed_frame(&rx, (uint32_t)strtoul(hex, NULL, 16), d, (uint8_t)len, (uint32_t)platform_millis());
    source_can_tick(&sc);
  }
  fclose(f);
  outcome_t o = outcome();
  TEST_ASSERT_EQUAL_UINT(2, o.batteries);   // the house bank and the start battery; the Cerbo's copy is merged away
  TEST_ASSERT_EQUAL_UINT(2, o.solar);       // two panel inputs, not batteries
  TEST_ASSERT_EQUAL_UINT(3, o.chargers);    // two solar controllers and an AC charger
  TEST_ASSERT_TRUE(o.max_battery_volts <= 16.0);
}

// ---- The injector's real-boat fixture, as a server that knows each device's NAME would stamp it ----
static int cmp_str(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void test_injected_real_boat_is_named_by_device(void) {
  FILE *f = fopen(INJECT_DIR "/real-electrical.json", "r");
  TEST_ASSERT_NOT_NULL(f);
  static char json[40000];
  size_t len = fread(json, 1, sizeof json - 1, f);
  json[len] = 0;
  fclose(f);
  cJSON *doc = cJSON_Parse(json);
  TEST_ASSERT_NOT_NULL(doc);
  bd_init();
  el_init(&el);
  sk_parse_reset();
  sk_electrical_attach(&el);
  const cJSON *updates = cJSON_GetObjectItem(doc, "updates"), *u;
  char tree[3000] = "{\"can0\":{";   // the sources tree, keyed by address, as the server builds it
  const char *sep = "";
  cJSON_ArrayForEach(u, updates) {
    const cJSON *s = cJSON_GetObjectItem(u, "source");
    const char *src = cJSON_GetObjectItem(s, "src")->valuestring;
    char seen[16];
    snprintf(seen, sizeof seen, "\"%s\":{", src);
    if (strstr(tree, seen)) continue;
    char one[300];
    snprintf(one, sizeof one, "%s\"%s\":{\"n2k\":{\"canName\":\"%s\",\"modelId\":\"%s\"}}", sep, src,
             cJSON_GetObjectItem(s, "canName")->valuestring, cJSON_GetObjectItem(s, "modelId")->valuestring);
    strcat(tree, one);
    sep = ",";
  }
  strcat(tree, "}}");
  sk_electrical_load_sources(tree, strlen(tree));
  for (int sec = 0; sec <= 25; sec++) {
    cJSON_ArrayForEach(u, updates) {
      const cJSON *s = cJSON_GetObjectItem(u, "source"), *v;
      cJSON_ArrayForEach(v, cJSON_GetObjectItem(u, "values")) {
        if (cJSON_IsNull(cJSON_GetObjectItem(v, "value"))) continue;
        char *val = cJSON_PrintUnformatted(cJSON_GetObjectItem(v, "value"));
        char msg[500];
        snprintf(msg, sizeof msg,
                 "{\"updates\":[{\"$source\":\"can0.%s\",\"values\":[{\"path\":\"%s\",\"value\":%s}]}]}",
                 cJSON_GetObjectItem(s, "canName")->valuestring, cJSON_GetObjectItem(v, "path")->valuestring, val);
        sk_parse_message(msg, strlen(msg), platform_millis());
        free(val);
      }
    }
    platform_linux_advance_ms(1000);
  }
  char got[16][48];
  size_t n = 0;
  bd_id_t ids[16];
  const char *groups[] = {"batteries", "solar", "chargers"};
  for (int g = 0; g < 3; g++) {
    char pat[64];
    snprintf(pat, sizeof pat, "electrical.resolved.%s.*.label", groups[g]);
    size_t k = bd_match(pat, ids, 16);
    for (size_t i = 0; i < k && n < 16; i++) {
      bd_sample_t sm;
      if (bd_get(ids[i], &sm) && sm.valid) snprintf(got[n++], sizeof got[0], "%s: %s", groups[g], sm.text);
    }
  }
  qsort(got, n, sizeof got[0], cmp_str);
  static const char *const want[] = {"batteries: SmartShunt 0", "batteries: SmartShunt 1", "chargers: BlueSolar f5",
                                     "chargers: BlueSolar f6", "chargers: Phoenix", "solar: BlueSolar f5", "solar: BlueSolar f6"};
  char msg[400] = "got:";
  for (size_t i = 0; i < n; i++) { strcat(msg, " ["); strcat(msg, got[i]); strcat(msg, "]"); }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(sizeof want / sizeof want[0], n, msg);
  for (size_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i], got[i], msg);   // want is in sorted order
  sk_electrical_attach(NULL);
  cJSON_Delete(doc);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_scenarios_via_can);
  RUN_TEST(test_scenarios_via_signal_k);
  RUN_TEST(test_order_independence);
  RUN_TEST(test_real_capture_by_function);
  RUN_TEST(test_injected_real_boat_is_named_by_device);
  return UNITY_END();
}
