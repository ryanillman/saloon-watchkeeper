#pragma once
// Power role: slot and group order, shared by role_power.c and the page files.
#include "power_calc.h"
#include "role.h"

enum { G_BATTERY, G_SOLAR, G_CHARGER, G_INVERTER, G_DCDC, G_ALTERNATOR, G_SYSTEM };

enum { B_VOLTS, B_AMPS, B_POWER, B_SOC, B_TIME };
enum { S_VOLTS, S_AMPS, S_PANEL, S_MODE };          // solar
enum { C_VOLTS, C_AMPS, C_MODE };                   // charger, dcdc, alternator
enum { I_SHORE, I_MODE };                           // inverter
enum { Y_DCPOWER };                                 // system

// Summary page (page_power_summary.c).
void power_summary_build(lv_obj_t *area, size_class_t sc, role_binding_t *b);
void power_summary_refresh(role_binding_t *b, int64_t now_ms);

// Helpers shared by the pages (page_power.c).
bool power_real(const role_binding_t *b, uint8_t g, uint8_t i);   // not a placeholder
bool power_sample(const role_binding_t *b, uint8_t g, uint8_t i, uint8_t k, bd_sample_t *out);
bool power_seen(const bd_sample_t *s);                             // the server has sent it at least once
// Fills a power_src_t from three slots of one instance (-1 = none); store backs the pointers.
void power_src_of(const role_binding_t *b, uint8_t g, uint8_t i, int kv, int ki, int kp, bd_sample_t store[3],
                  power_src_t *out);
// Signed watts of one source: reported power, else volts x amps.
bool power_watts(const power_src_t *s, int64_t now_ms, double *w);
// Row label: "SOLAR 288", or the configured label when the config renamed the instance.
void power_label(const role_binding_t *b, uint8_t g, uint8_t i, char *out, size_t n);
// Index of the primary bank among real banks, and how many there are (indices of real banks in idx).
uint8_t power_banks(const role_binding_t *b, uint8_t idx[ROLE_MAX_INST], uint8_t *primary);
