#pragma once
// The Signal K connection: discover (REST), connect (WebSocket), subscribe to the
// bound paths, and on any loss invalidate every Signal K path and retry with backoff.
#include <stddef.h>
#include "boatdata.h"
#include "config.h"

typedef enum { SK_IDLE, SK_DISCOVERING, SK_CONNECTING, SK_LIVE, SK_BACKOFF } sk_state_t;

void sk_client_init(const cfg_sk_t *cfg, const bd_id_t *bound, size_t n);
void sk_client_start(void);                           // init first; runs sk_client_run_once in a loop
void sk_client_run_once(void);                        // one discover/connect/live cycle; returns when closed
void sk_client_rebind(const bd_id_t *bound, size_t n);   // resubscribe with a new path set
sk_state_t sk_client_state(void);
int sk_client_backoff_ms(int attempt);

// Increments each time a discovery (REST snapshot) completes; roles rebind when it changes.
uint32_t sk_client_discovery_count(void);

// While live, the REST snapshot is re-read this often (default 20 s), so engines
// and batteries that power up after the display are found.
void sk_client_set_rediscover_ms(int32_t ms);                // 1000, 2000, 4000 ... capped at 30000
