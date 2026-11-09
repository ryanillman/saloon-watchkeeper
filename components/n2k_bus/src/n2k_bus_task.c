#include "n2k_bus_task.h"
#include "freertos/task.h"

static void bus_task(void *arg) {
  n2k_bus_t *b = arg;
  for (;;) {
    n2k_bus_step(b, 32);
    vTaskDelay(1);
  }
}

esp_err_t n2k_bus_task_start(n2k_bus_t *b, uint32_t stack_bytes, UBaseType_t prio) {
  return xTaskCreate(bus_task, "n2k_bus", stack_bytes, b, prio, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
