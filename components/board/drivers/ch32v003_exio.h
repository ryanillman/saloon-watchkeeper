#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Waveshare's CH32V003 I/O helper at 0x24 (4" v4, and likely the 7B).
// A register file: write [reg][value], read [reg] then repeated-start read.
// Direction bit 1 = output. Register map from Waveshare's custom_io_expander_ch32v003 (Apache-2.0).
esp_err_t ch32v003_init(i2c_master_bus_handle_t bus, uint8_t output_mask, uint8_t levels);
esp_err_t ch32v003_set(uint8_t pin, bool level);
esp_err_t ch32v003_set_outputs(uint8_t output_mask);   // direction register; other pins become inputs
esp_err_t ch32v003_pwm(uint8_t value);          // 0..255
esp_err_t ch32v003_adc(uint16_t *raw);          // 10-bit
