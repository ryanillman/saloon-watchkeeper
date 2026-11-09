// ESP-IDF task that runs n2k_bus_step in a loop. ESP only; not part of the pure library core.
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "n2k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// The bus (and everything it points to) must outlive the task. Each pass takes up to 32 frames
// (above the worst-case frame rate of a 250 kbit/s bus per 10 ms tick), then sleeps one tick.
esp_err_t n2k_bus_task_start(n2k_bus_t *b, uint32_t stack_bytes, UBaseType_t prio);

#ifdef __cplusplus
}
#endif
