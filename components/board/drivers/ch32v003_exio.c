#include "ch32v003_exio.h"

#define CH32_ADDR    0x24
#define REG_DIR      0x02
#define REG_OUT      0x03
#define REG_PWM      0x05
#define REG_ADC      0x06

static i2c_master_dev_handle_t dev;
static uint8_t levels;

static esp_err_t wr(uint8_t reg, uint8_t v) {
  uint8_t b[2] = {reg, v};
  return i2c_master_transmit(dev, b, 2, 50);
}

esp_err_t ch32v003_init(i2c_master_bus_handle_t bus, uint8_t output_mask, uint8_t initial) {
  i2c_device_config_t c = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CH32_ADDR, .scl_speed_hz = 400000,
  };
  esp_err_t err = i2c_master_bus_add_device(bus, &c, &dev);
  if (err) return err;
  levels = initial;
  if ((err = wr(REG_OUT, levels))) return err;
  if ((err = wr(REG_DIR, output_mask))) return err;
  return wr(REG_OUT, levels);
}

esp_err_t ch32v003_set(uint8_t pin, bool level) {
  uint8_t next = level ? (uint8_t)(levels | (1u << pin)) : (uint8_t)(levels & ~(1u << pin));
  esp_err_t err = wr(REG_OUT, next);
  if (!err) levels = next;
  return err;
}

esp_err_t ch32v003_set_outputs(uint8_t output_mask) { return wr(REG_DIR, output_mask); }

esp_err_t ch32v003_pwm(uint8_t value) { return wr(REG_PWM, value); }

esp_err_t ch32v003_adc(uint16_t *raw) {
  uint8_t reg = REG_ADC, d[2];
  esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, d, 2, 50);
  if (!err) *raw = (uint16_t)(d[1] << 8 | d[0]);
  return err;
}
