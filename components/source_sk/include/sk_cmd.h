#pragma once
// Commands the UI sends to the Signal K server: a POST to a path, or a PUT of a number. The UI submits and polls
// the result; sk_client sends them from its own task. Thread-safe.
//
// Safety rules (a drop sent late sets the anchor at the wrong spot): nothing is queued while disconnected, a
// command not sent within 5 s is dropped, a lost connection fails everything still queued, and nothing is retried.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SKC_PENDING, SKC_OK, SKC_FAILED } sk_cmd_state_t;
typedef enum { SKC_ERR_NONE, SKC_ERR_OFFLINE, SKC_ERR_NO_ACCESS, SKC_ERR_NO_PLUGIN, SKC_ERR_REJECTED } sk_cmd_err_t;

// put_number: PUT {"value": value} to the Signal K path `path` (dotted, relative to vessels.self).
// put_text: the same with a string value, taken from `body` (plain text, not JSON): {"value":"night"}.
// Otherwise POST `body` (JSON, "" for none) to the server path `path` (e.g. "/plugins/anchoralarm/raiseAnchor").
typedef struct {
  bool put_number;
  bool put_text;        // PUT {"value":"<body>"} to the Signal K path `path`; `body` holds the text
  char path[96];
  char body[256];
  double value;
  char method[8];       // "" = POST. "PUT" sends the body to the server path (put_number false). DELETE is not supported by the ESP transport
  uint16_t expire_ms;   // how long it may wait to be sent; 0 = 5000. An emergency uses 3000: late is worse than never
} sk_cmd_t;

typedef uint16_t sk_cmd_handle_t;   // 0 = none
typedef struct { sk_cmd_state_t state; sk_cmd_err_t err; char id[40]; } sk_cmd_result_t;   // id: the "id" in a successful reply (a UUID), else ""

void sk_cmd_init(void);                    // forgets everything; not connected
void sk_cmd_set_connected(bool up);        // sk_client: false fails every queued command (OFFLINE)

// Returns a handle, or 0 when refused: not connected, an identical path already pending (and not
// replaces_same_path), or the queue (4) is full of commands that may not be evicted.
// replaces_same_path marks a radius-style command: it replaces a pending command with the same path in place
// (same handle), and when the queue is full the oldest such command is evicted (failed) to make room. Drop and
// raise (replaces_same_path false) are never evicted.
sk_cmd_handle_t sk_cmd_submit(const sk_cmd_t *c, bool replaces_same_path, int64_t now_ms);

// PENDING until the server has answered. An unknown or long-forgotten handle reads FAILED with SKC_ERR_NONE.
sk_cmd_result_t sk_cmd_result(sk_cmd_handle_t h);

// sk_client side: the oldest command still waiting (commands older than 5 s are failed OFFLINE and skipped).
bool sk_cmd_next(sk_cmd_t *out, sk_cmd_handle_t *h, int64_t now_ms);
// transport_ok false: no answer (OFFLINE). Else status: 2xx OK; 401/403 NO_ACCESS; 404 NO_PLUGIN; others REJECTED.
void sk_cmd_finish(sk_cmd_handle_t h, bool transport_ok, int status);
// As sk_cmd_finish, and a successful result keeps `reply_id` (NULL or "" = none) for sk_cmd_result.
void sk_cmd_finish_id(sk_cmd_handle_t h, bool transport_ok, int status, const char *reply_id);
// The string "id" of a JSON reply body into `out` (cap `n`). False, `out` empty, when there is none, it is not a string,
// it does not fit, or the body is not JSON.
bool sk_cmd_reply_id(const char *json, size_t len, char *out, size_t n);

// The JSON body of a put_number or put_text command: {"value":12.5} or {"value":"night"}. Text escapes
// " and \ and control characters. Returns the length, or 0 when the command is neither kind or the body does
// not fit `n` (including the terminating NUL).
size_t sk_cmd_put_body(const sk_cmd_t *c, char *out, size_t n);
