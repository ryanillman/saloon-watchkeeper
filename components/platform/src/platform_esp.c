#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
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

// A FreeRTOS task must never return from its function (the chip aborts), but on Linux a
// thread may. The trampoline lets a one-shot task (a Wi-Fi scan) simply return.
typedef struct { void (*fn)(void *); void *arg; } start_t;

static void trampoline(void *p) {
  start_t s = *(start_t *)p;
  free(p);
  s.fn(s.arg);
  vTaskDelete(NULL);
}

bool platform_task(void (*fn)(void *), void *arg, const char *name, uint32_t stack_bytes) {
  start_t *s = malloc(sizeof *s);
  if (s) {
    s->fn = fn;
    s->arg = arg;
    if (xTaskCreate(trampoline, name, stack_bytes, s, 4, NULL) == pdPASS) return true;
    free(s);
  }
  ESP_LOGE("platform", "task %s (stack %u) not created: internal free %u, largest block %u", name,
           (unsigned)stack_bytes, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return false;
}

void platform_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void platform_reboot(void) { esp_restart(); }

size_t platform_min_internal(void) { return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }

size_t platform_largest_internal(void) { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }

size_t platform_stacks(platform_stack_t *out, size_t max) {
  static const char *const names[] = {"ui", "signalk", "config", "wifiscan", "main", "IDLE0", "IDLE1"};
  size_t n = 0;
  for (size_t i = 0; i < sizeof names / sizeof names[0] && n < max; i++) {
    TaskHandle_t t = xTaskGetHandle(names[i]);   // NULL when that task isn't running
    if (!t) continue;
    snprintf(out[n].name, sizeof out[n].name, "%s", names[i]);
    out[n++].min_free = uxTaskGetStackHighWaterMark(t);   // bytes on ESP-IDF
  }
  return n;
}

void platform_mem(size_t *internal, size_t *psram) {
  *internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  *psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void platform_mac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }
