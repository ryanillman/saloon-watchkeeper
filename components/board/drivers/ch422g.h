#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// WCH CH422G. It has no register map: each command is its own I2C address.
// IO0-IO7 are used as push-pull outputs (EXIO0-EXIO7 on Waveshare boards).
esp_err_t ch422g_init(i2c_master_bus_handle_t bus, uint8_t initial_levels);
esp_err_t ch422g_set(uint8_t pin, bool level);
uint8_t ch422g_levels(void);
