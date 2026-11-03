#pragma once
#include <stdint.h>

// Monotonic milliseconds since an arbitrary start point.
int64_t platform_millis(void);

// A plain mutex. Created once, never destroyed in practice.
void *platform_lock_create(void);
void platform_lock(void *lock);
void platform_unlock(void *lock);
