#include "n2k_decode.h"

#define PI 3.14159265358979323846

static uint16_t u16(const uint8_t *d, int i) { return (uint16_t)(d[i] | d[i + 1] << 8); }
static int16_t i16(const uint8_t *d, int i) { return (int16_t)u16(d, i); }
static uint32_t u32(const uint8_t *d, int i) {
  return d[i] | (uint32_t)d[i + 1] << 8 | (uint32_t)d[i + 2] << 16 | (uint32_t)d[i + 3] << 24;
}
// Not available is all ones; the two values below it are error codes.
static bool ok_u16(uint16_t v) { return v < 0xFFFE; }
static bool ok_i16(int16_t v) { return v < 0x7FFE; }
static bool ok_u32(uint32_t v) { return v < 0xFFFFFFFEu; }

static void emit(n2k_value_cb cb, void *ctx, n2k_field_t f, uint8_t inst, uint8_t fluid, double v) {
  n2k_value_t x = {f, inst, fluid, v};
  cb(&x, ctx);
}

static void depth(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 128267  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len >= 5 && ok_u32(u32(d, 1))) emit(cb, ctx, N2K_F_DEPTH_BELOW_TRANSDUCER, 0, 0, u32(d, 1) * 0.01);
  if (m->len >= 7 && ok_i16(i16(d, 5))) {
    int16_t off = i16(d, 5);
    if (off > 0) emit(cb, ctx, N2K_F_DEPTH_SURFACE_TO_TRANSDUCER, 0, 0, off * 0.001);
    if (off < 0) emit(cb, ctx, N2K_F_DEPTH_TRANSDUCER_TO_KEEL, 0, 0, -off * 0.001);
  }
}

static void wind(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 130306  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 6 || (d[5] & 7) != 2) return;  // apparent only; true wind is computed locally
  if (ok_u16(u16(d, 1))) emit(cb, ctx, N2K_F_WIND_SPEED_APPARENT, 0, 0, u16(d, 1) * 0.01);
  if (ok_u16(u16(d, 3))) {
    double a = u16(d, 3) * 0.0001;
    emit(cb, ctx, N2K_F_WIND_ANGLE_APPARENT, 0, 0, a > PI ? a - 2 * PI : a);
  }
}

static void battery(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127508  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 1) return;
  uint8_t inst = d[0];
  if (m->len >= 3 && ok_u16(u16(d, 1))) emit(cb, ctx, N2K_F_BATT_VOLTAGE, inst, 0, u16(d, 1) * 0.01);
  if (m->len >= 5 && ok_i16(i16(d, 3))) emit(cb, ctx, N2K_F_BATT_CURRENT, inst, 0, i16(d, 3) * 0.1);
  if (m->len >= 7 && ok_u16(u16(d, 5))) emit(cb, ctx, N2K_F_BATT_TEMPERATURE, inst, 0, u16(d, 5) * 0.01);
}

static void engine_rapid(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127488  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 1) return;
  if (m->len >= 3 && ok_u16(u16(d, 1))) emit(cb, ctx, N2K_F_ENGINE_REVOLUTIONS, d[0], 0, u16(d, 1) * 0.25 / 60.0);
  if (m->len >= 5 && ok_u16(u16(d, 3))) emit(cb, ctx, N2K_F_ENGINE_BOOST, d[0], 0, u16(d, 3) * 100.0);
}

static void tank(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127505  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 1) return;
  uint8_t inst = d[0] & 0x0F, type = d[0] >> 4;
  uint8_t fluid = type <= 6 ? type : 255;
  if (m->len >= 3 && ok_i16(i16(d, 1))) emit(cb, ctx, N2K_F_TANK_LEVEL, inst, fluid, i16(d, 1) * 0.004 / 100.0);
  if (m->len >= 7 && ok_u32(u32(d, 3))) emit(cb, ctx, N2K_F_TANK_CAPACITY, inst, fluid, u32(d, 3) * 0.1 / 1000.0);
}

bool n2k_decode(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {
  switch (m->pgn) {
    case 128267: depth(m, cb, ctx); return true;
    case 130306: wind(m, cb, ctx); return true;
    case 127508: battery(m, cb, ctx); return true;
    case 127488: engine_rapid(m, cb, ctx); return true;
    case 127505: tank(m, cb, ctx); return true;
    default: return false;
  }
}

const char *n2k_fluid_name(uint8_t fluid) {
  static const char *const names[] = {"fuel", "freshWater", "wasteWater", "liveWell", "oil", "blackWater", "gasoline"};
  return fluid < sizeof names / sizeof *names ? names[fluid] : NULL;
}
