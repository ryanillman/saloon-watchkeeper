// The shipped product rows: the only place in the resolver that names a product line. Order matters (first match).
#pragma once
static const el_product_row_t SHIPPED[] = {
  {"*Shunt*", EL_FN_BATTERY, true},      // battery monitors
  {"BMV*", EL_FN_BATTERY, true},
  {"*MPPT*", EL_FN_PV, false},           // solar charge controllers: their non-output instances are panel inputs
  {"*Charger*", EL_FN_CHARGER, false},
  {"Orion*", EL_FN_CONVERTER, false},    // DC-DC converters
};
