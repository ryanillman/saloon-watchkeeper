#include "board_probe.h"
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"

static const char *TAG = "probe";

typedef struct { int sda, scl; const char *name; } probe_bus_t;

// Candidate I2C buses for the boards this firmware supports (from Waveshare's wikis).
static const probe_bus_t buses[] = {
  {8, 9, "GPIO8/GPIO9"},     // 4" expander bus; 7"/7B shared bus
  {15, 7, "GPIO15/GPIO7"},   // 4" touch and RTC bus
};

static void scan(const probe_bus_t *b) {
  i2c_master_bus_config_t cfg = {
    .i2c_port = I2C_NUM_0, .sda_io_num = b->sda, .scl_io_num = b->scl,
    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus;
  if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
    ESP_LOGE(TAG, "bus %s: init failed", b->name);
    return;
  }
  esp_log_level_set("i2c.master", ESP_LOG_NONE);   // a bus with no pull-ups times out on every address
  char found[256] = "";
  int n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (i2c_master_probe(bus, a, 50) == ESP_OK) {
      n += snprintf(found + n, sizeof found - n, " 0x%02X", a);
    }
  }
  esp_log_level_set("i2c.master", ESP_LOG_WARN);
  ESP_LOGI(TAG, "bus %s:%s", b->name, n ? found : " nothing");
  i2c_del_master_bus(bus);
}

void board_probe_run(void) {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flash = 0;
  esp_flash_get_size(NULL, &flash);
  ESP_LOGI(TAG, "chip rev %d.%d, %d cores, flash %lu MB, PSRAM %u MB",
           chip.revision / 100, chip.revision % 100, chip.cores,
           (unsigned long)(flash >> 20), (unsigned)(esp_psram_get_size() >> 20));
  for (size_t i = 0; i < sizeof buses / sizeof buses[0]; i++) scan(&buses[i]);
}
