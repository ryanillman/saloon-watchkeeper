#include "ch422g.h"

#define CH422G_ADDR_SYSTEM 0x24   // system parameters; bit 0 = IO0-IO7 are outputs
#define CH422G_ADDR_IO_OUT 0x38   // IO0-IO7 output levels
#define CH422G_SYS_IO_OE   0x01

static i2c_master_dev_handle_t sys_dev, out_dev;
static uint8_t levels;

static esp_err_t write1(i2c_master_dev_handle_t d, uint8_t v) {
  return i2c_master_transmit(d, &v, 1, 50);
}

esp_err_t ch422g_init(i2c_master_bus_handle_t bus, uint8_t initial_levels) {
  i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = 400000};
  c.device_address = CH422G_ADDR_SYSTEM;
  esp_err_t err = i2c_master_bus_add_device(bus, &c, &sys_dev);
  if (err) return err;
  c.device_address = CH422G_ADDR_IO_OUT;
  err = i2c_master_bus_add_device(bus, &c, &out_dev);
  if (err) return err;
  // Set levels before enabling outputs so no pin glitches (EXIO5 low keeps native USB).
  levels = initial_levels;
  err = write1(out_dev, levels);
  if (err) return err;
  err = write1(sys_dev, CH422G_SYS_IO_OE);
  if (err) return err;
  return write1(out_dev, levels);
}

esp_err_t ch422g_set(uint8_t pin, bool level) {
  uint8_t next = level ? (uint8_t)(levels | (1u << pin)) : (uint8_t)(levels & ~(1u << pin));
  esp_err_t err = write1(out_dev, next);
  if (!err) levels = next;
  return err;
}

uint8_t ch422g_levels(void) { return levels; }
