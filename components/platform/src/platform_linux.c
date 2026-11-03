#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "platform.h"

int64_t platform_millis(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void *platform_lock_create(void) {
  pthread_mutex_t *m = malloc(sizeof *m);
  if (m) pthread_mutex_init(m, NULL);
  return m;
}

void platform_lock(void *lock) { pthread_mutex_lock(lock); }
void platform_unlock(void *lock) { pthread_mutex_unlock(lock); }

typedef struct { void (*fn)(void *); void *arg; } start_t;

static void *trampoline(void *p) {
  start_t st = *(start_t *)p;
  free(p);
  st.fn(st.arg);
  return NULL;
}

bool platform_task(void (*fn)(void *), void *arg, const char *name, uint32_t stack_bytes) {
  (void)name; (void)stack_bytes;
  start_t *st = malloc(sizeof *st);
  if (!st) return false;
  *st = (start_t){fn, arg};
  pthread_t t;
  if (pthread_create(&t, NULL, trampoline, st)) {
    free(st);
    return false;
  }
  pthread_detach(t);
  return true;
}

void platform_sleep_ms(uint32_t ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

void platform_reboot(void) { exit(0); }

void platform_mem(size_t *internal, size_t *psram) {
  *internal = 0;
  *psram = 0;
}

void platform_mac(uint8_t mac[6]) {
  static const uint8_t sim[6] = {0x02, 0x00, 0x00, 0x00, 0x51, 0x4d};
  for (int i = 0; i < 6; i++) mac[i] = sim[i];
}
