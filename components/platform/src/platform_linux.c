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
