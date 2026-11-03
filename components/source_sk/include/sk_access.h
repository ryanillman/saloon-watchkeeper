#pragma once
// Signal K device access request: ask the server for a token, then poll until an admin
// approves or denies it. Starts only when asked. Runs on the Signal K task (sk_access_poll);
// start and state are safe from any task.
#include <stdbool.h>
#include <stdint.h>

typedef enum { SKA_IDLE, SKA_PENDING, SKA_APPROVED, SKA_DENIED, SKA_EXPIRED, SKA_ERROR } ska_state_t;

#define SKA_POLL_MS 5000
#define SKA_EXPIRE_MS (30 * 60 * 1000)

void sk_access_reset(void);                                      // tests
void sk_access_set_identity(const char *client_id, const char *description);
void sk_access_on_token(void (*fn)(const char *token));          // approved: save and use it
void sk_access_on_href(void (*fn)(const char *href));            // pending href to store ("" = none)

void sk_access_start(void);                                      // any task
void sk_access_resume(const char *href, int64_t now_ms);         // a request stored before a restart
void sk_access_poll(const char *base_url, int64_t now_ms);       // Signal K task: sends and polls as due

ska_state_t sk_access_state(int *http_error);                    // http_error: status for SKA_ERROR, 0 = unreachable
const char *sk_access_state_name(ska_state_t s);                 // "idle", "pending", ...
const char *sk_access_message(void);                             // one line for status and setup

void sk_access_client_id_from_mac(const uint8_t mac[6], char out[37]);   // stable, UUID-shaped
