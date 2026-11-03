#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "platform.h"

int64_t platform_millis(void) { return esp_timer_get_time() / 1000; }

void *platform_lock_create(void) { return xSemaphoreCreateMutex(); }
void platform_lock(void *lock) { xSemaphoreTake((SemaphoreHandle_t)lock, portMAX_DELAY); }
void platform_unlock(void *lock) { xSemaphoreGive((SemaphoreHandle_t)lock); }
