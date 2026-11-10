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

static int32_t i32(const uint8_t *d, int i) { return (int32_t)u32(d, i); }
static int64_t i64(const uint8_t *d, int i) { return (int64_t)((uint64_t)u32(d, i) | (uint64_t)u32(d, i + 4) << 32); }
static bool ok_i32(int32_t v) { return v < 0x7FFFFFFE; }
static bool ok_i64(int64_t v) { return v < 0x7FFFFFFFFFFFFFFELL; }

static void emit(n2k_value_cb cb, void *ctx, n2k_field_t f, uint8_t inst, uint8_t fluid, double v) {
  n2k_value_t x = {f, inst, fluid, v, 0};
  cb(&x, ctx);
}
static void emit_position(n2k_value_cb cb, void *ctx, double lat, double lon) {
  n2k_value_t x = {N2K_F_POSITION, 0, 0, lat, lon};
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
  if (m->len >= 3 && ok_i16(i16(d, 1))) {
    double pct = i16(d, 1) * 0.004;   // percent of capacity; a sender can report outside 0-100
    if (pct >= 0 && pct <= 100) emit(cb, ctx, N2K_F_TANK_LEVEL, inst, fluid, pct / 100.0);
  }
  if (m->len >= 7 && ok_u32(u32(d, 3))) emit(cb, ctx, N2K_F_TANK_CAPACITY, inst, fluid, u32(d, 3) * 0.1 / 1000.0);
}

static void emit_time(n2k_value_cb cb, void *ctx, uint16_t days, uint32_t t10k) {
  if (ok_u16(days) && ok_u32(t10k)) emit(cb, ctx, N2K_F_TIME_EPOCH, 0, 0, days * 86400.0 + (double)(t10k / 10000));
}

static void engine_dynamic(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127489 (fast-packet)  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 1) return;
  uint8_t inst = d[0];
  if (m->len >= 3 && ok_u16(u16(d, 1))) emit(cb, ctx, N2K_F_ENGINE_OIL_PRESSURE, inst, 0, u16(d, 1) * 100.0);
  if (m->len >= 5 && ok_u16(u16(d, 3))) emit(cb, ctx, N2K_F_ENGINE_OIL_TEMPERATURE, inst, 0, u16(d, 3) * 0.1);
  if (m->len >= 7 && ok_u16(u16(d, 5))) emit(cb, ctx, N2K_F_ENGINE_COOLANT_TEMPERATURE, inst, 0, u16(d, 5) * 0.01);
  if (m->len >= 9 && ok_i16(i16(d, 7))) emit(cb, ctx, N2K_F_ENGINE_ALTERNATOR_VOLTAGE, inst, 0, i16(d, 7) * 0.01);
  if (m->len >= 11 && ok_i16(i16(d, 9))) emit(cb, ctx, N2K_F_ENGINE_FUEL_RATE, inst, 0, i16(d, 9) * 0.1 / 3.6e6);  // L/h to m3/s
  if (m->len >= 15 && ok_u32(u32(d, 11))) emit(cb, ctx, N2K_F_ENGINE_HOURS, inst, 0, (double)u32(d, 11));
}

static void dc_status(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127506 (fast-packet)  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 4) return;
  uint8_t inst = d[1];
  if (d[3] <= 100) emit(cb, ctx, N2K_F_BATT_SOC, inst, 0, d[3] / 100.0);   // percent, 0-100; above is a fault or not-available
  if (m->len >= 7 && ok_u16(u16(d, 5))) emit(cb, ctx, N2K_F_BATT_TIME_REMAINING, inst, 0, u16(d, 5) * 60.0);
}

static void heading(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 127250  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 8) return;
  uint8_t ref = d[7] & 3;
  if (ok_u16(u16(d, 1)) && ref <= 1) emit(cb, ctx, ref == 0 ? N2K_F_HEADING_TRUE : N2K_F_HEADING_MAGNETIC, 0, 0, u16(d, 1) * 0.0001);
  if (ok_i16(i16(d, 5))) emit(cb, ctx, N2K_F_MAG_VARIATION, 0, 0, i16(d, 5) * 0.0001);
}

static void cog_sog(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 129026  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len < 6) return;
  uint8_t ref = d[1] & 3;
  if (ok_u16(u16(d, 2)) && ref <= 1) emit(cb, ctx, ref == 0 ? N2K_F_COG_TRUE : N2K_F_COG_MAGNETIC, 0, 0, u16(d, 2) * 0.0001);
  if (ok_u16(u16(d, 4))) emit(cb, ctx, N2K_F_SOG, 0, 0, u16(d, 4) * 0.01);
}

static void speed(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 128259  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len >= 3 && ok_u16(u16(d, 1))) emit(cb, ctx, N2K_F_STW, 0, 0, u16(d, 1) * 0.01);
}

static void position_rapid(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 129025  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len >= 8 && ok_i32(i32(d, 0)) && ok_i32(i32(d, 4))) emit_position(cb, ctx, i32(d, 0) * 1e-7, i32(d, 4) * 1e-7);
}

static void gnss(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 129029 (fast-packet)  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len >= 7) emit_time(cb, ctx, u16(d, 1), u32(d, 3));
  if (m->len >= 23 && ok_i64(i64(d, 7)) && ok_i64(i64(d, 15))) emit_position(cb, ctx, i64(d, 7) * 1e-16, i64(d, 15) * 1e-16);
}

static void system_time(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {  // 126992  // verify: canboat
  const uint8_t *d = m->data;
  if (m->len >= 8) emit_time(cb, ctx, u16(d, 2), u32(d, 4));
}

bool n2k_decode(const n2k_msg_t *m, n2k_value_cb cb, void *ctx) {
  switch (m->pgn) {
    case 128267: depth(m, cb, ctx); return true;
    case 130306: wind(m, cb, ctx); return true;
    case 127508: battery(m, cb, ctx); return true;
    case 127488: engine_rapid(m, cb, ctx); return true;
    case 127505: tank(m, cb, ctx); return true;
    case 127489: engine_dynamic(m, cb, ctx); return true;
    case 127506: dc_status(m, cb, ctx); return true;
    case 127250: heading(m, cb, ctx); return true;
    case 129026: cog_sog(m, cb, ctx); return true;
    case 128259: speed(m, cb, ctx); return true;
    case 129025: position_rapid(m, cb, ctx); return true;
    case 129029: gnss(m, cb, ctx); return true;
    case 126992: system_time(m, cb, ctx); return true;
    default: return false;
  }
}

const char *n2k_fluid_name(uint8_t fluid) {
  static const char *const names[] = {"fuel", "freshWater", "wasteWater", "liveWell", "oil", "blackWater", "gasoline"};
  return fluid < sizeof names / sizeof *names ? names[fluid] : NULL;
}
