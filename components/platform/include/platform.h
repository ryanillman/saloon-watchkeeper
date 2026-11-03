#pragma once
#include <stdbool.h>
#include <stdint.h>

// Large zero-initialised statics that no ISR or DMA engine touches: on the boards they go to
// PSRAM, keeping internal RAM for task stacks, Wi-Fi and the RGB panel's bounce buffers.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PLATFORM_EXT_BSS EXT_RAM_BSS_ATTR
#else
#define PLATFORM_EXT_BSS
#endif

// Monotonic milliseconds since an arbitrary start point.
int64_t platform_millis(void);

// A plain mutex. Created once, never destroyed in practice.
void *platform_lock_create(void);
void platform_lock(void *lock);
void platform_unlock(void *lock);

// Runs fn(arg) on its own thread/task. stack_bytes is ignored on Linux.
bool platform_task(void (*fn)(void *), void *arg, const char *name, uint32_t stack_bytes);
void platform_sleep_ms(uint32_t ms);
