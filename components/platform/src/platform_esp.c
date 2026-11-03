#include "esp_heap_caps.h"
#include "esp_log.h"
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
  if (xTaskCreate(fn, name, stack_bytes, arg, 4, NULL) == pdPASS) return true;
  ESP_LOGE("platform", "task %s (stack %u) not created: internal free %u, largest block %u", name,
           (unsigned)stack_bytes, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return false;
}

void platform_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
