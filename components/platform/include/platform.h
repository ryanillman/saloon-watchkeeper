#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Large zero-initialised statics that no ISR or DMA engine touches: on the boards they go to
// PSRAM, keeping internal RAM for task stacks, Wi-Fi and the RGB panel's bounce buffers.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "sdkconfig.h"
// Without this option EXT_RAM_BSS_ATTR expands to nothing and every tagged static lands
// back in internal RAM: the boot runs out and the Signal K task never starts. It is in
// sdkconfig.defaults, but an sdkconfig generated before it was added keeps it off.
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is off: this build dir's sdkconfig predates it. Run `idf.py -B <build dir> fullclean`, then build again with `-D BOARD=<board>`."
#endif
#define PLATFORM_EXT_BSS EXT_RAM_BSS_ATTR
#else
#define PLATFORM_EXT_BSS
#endif

// Monotonic milliseconds since an arbitrary start point.
int64_t platform_millis(void);

// "YYYY-MM-DDTHH:MM:SS[.fff]" followed by "Z" or "+hh:mm" / "-hh:mm" to seconds since 1970. False on anything else
// (including a time with no zone), leaving *epoch_s untouched. Pure.
bool platform_parse_iso8601(const char *iso, int64_t *epoch_s);

// A plain mutex. Created once, never destroyed in practice.
void *platform_lock_create(void);
void platform_lock(void *lock);
void platform_unlock(void *lock);

// Runs fn(arg) on its own thread/task. stack_bytes is ignored on Linux.
bool platform_task(void (*fn)(void *), void *arg, const char *name, uint32_t stack_bytes);
void platform_sleep_ms(uint32_t ms);

// Restarts the device (Linux: exits the process).
void platform_reboot(void);

// Free heap in bytes: internal RAM and PSRAM (Linux: both 0).
void platform_mem(size_t *internal, size_t *psram);

// The least internal RAM that has been free at any moment since boot, in bytes: catches a transient peak that
// platform_mem, read afterwards, misses (Linux: 0).
size_t platform_min_internal(void);

// Largest single block allocatable from internal RAM: total free minus this is fragmentation (Linux: 0).
size_t platform_largest_internal(void);

// Stack headroom of the named tasks, as the least free bytes each has ever had (Linux: none). Fills out[]
// with up to max entries and returns how many.
typedef struct { char name[12]; uint32_t min_free; } platform_stack_t;
size_t platform_stacks(platform_stack_t *out, size_t max);

// The device's base MAC address (Linux: a fixed made-up one).
void platform_mac(uint8_t mac[6]);
