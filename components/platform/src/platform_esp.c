#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform.h"

int64_t platform_millis(void) { return esp_timer_get_time() / 1000; }

void *platform_lock_create(void) { return xSemaphoreCreateMutex(); }
void platform_lock(void *lock) { xSemaphoreTake((SemaphoreHandle_t)lock, portMAX_DELAY); }
void platform_unlock(void *lock) { xSemaphoreGive((SemaphoreHandle_t)lock); }

bool platform_task(void (*fn)(void *), void *arg, const char *name, uint32_t stack_bytes) {
  return xTaskCreate(fn, name, stack_bytes, arg, 4, NULL) == pdPASS;
}

void platform_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
