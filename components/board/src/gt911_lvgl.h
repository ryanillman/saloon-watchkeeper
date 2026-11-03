#pragma once
#include "driver/i2c_master.h"
#include "lvgl.h"

// GT911 on an existing I2C bus, registered as an LVGL pointer. Reset must already be done.
lv_indev_t *gt911_lvgl_indev(i2c_master_bus_handle_t bus, uint16_t w, uint16_t h, int int_gpio);
