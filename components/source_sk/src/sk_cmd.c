#include "sk_cmd.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "platform.h"

#define ACTIVE_MAX 4          // waiting or being sent
#define SLOTS 8               // plus finished ones kept so the UI can read the result
#define EXPIRE_MS 5000

typedef enum { S_EMPTY, S_QUEUED, S_SENDING, S_DONE } slot_state_t;
typedef struct {
  slot_state_t st;
  sk_cmd_handle_t h;
  sk_cmd_t cmd;
  bool radius;              // submitted with replaces_same_path
  int64_t at_ms;            // submitted
  uint32_t seq;             // submit order, for oldest-first
  sk_cmd_result_t res;
} slot_t;

static void *lock;
static slot_t slot[SLOTS];
static bool connected;
static sk_cmd_handle_t next_h;
static uint32_t next_seq;

#define LOCK() platform_lock(lock)
#define UNLOCK() platform_unlock(lock)

static void ensure_lock(void) {
  if (!lock) lock = platform_lock_create();
}

void sk_cmd_init(void) {
  ensure_lock();
  LOCK();
  memset(slot, 0, sizeof slot);
  connected = false;
  next_h = 0;
  next_seq = 0;
  UNLOCK();
}

static void fail(slot_t *s, sk_cmd_err_t err) {
  s->st = S_DONE;
  s->res = (sk_cmd_result_t){SKC_FAILED, err, ""};
}

static int active_count(void) {
  int n = 0;
  for (int i = 0; i < SLOTS; i++) n += slot[i].st == S_QUEUED || slot[i].st == S_SENDING;
  return n;
}

// A slot to write into: an empty one, else the oldest finished one. -1 when every slot is active.
static int free_slot(void) {
  int best = -1;
  for (int i = 0; i < SLOTS; i++) {
    if (slot[i].st == S_EMPTY) return i;
    if (slot[i].st == S_DONE && (best < 0 || slot[i].seq < slot[best].seq)) best = i;
  }
  return best;
}

void sk_cmd_set_connected(bool up) {
  ensure_lock();
  LOCK();
  connected = up;
  if (!up)
    for (int i = 0; i < SLOTS; i++)
      if (slot[i].st == S_QUEUED || slot[i].st == S_SENDING) fail(&slot[i], SKC_ERR_OFFLINE);
  UNLOCK();
}

sk_cmd_handle_t sk_cmd_submit(const sk_cmd_t *c, bool replaces_same_path, int64_t now_ms) {
  ensure_lock();
  LOCK();
  sk_cmd_handle_t h = 0;
  if (!connected) goto out;
  for (int i = 0; i < SLOTS; i++) {
    if (slot[i].st != S_QUEUED || strcmp(slot[i].cmd.path, c->path) != 0) continue;
    if (!replaces_same_path) goto out;   // an identical drop or raise is already waiting
    slot[i].cmd = *c;
    slot[i].at_ms = now_ms;
    h = slot[i].h;
    goto out;
  }
  if (active_count() >= ACTIVE_MAX) {
    int victim = -1;   // the oldest radius-style command still waiting
    for (int i = 0; i < SLOTS; i++)
      if (slot[i].st == S_QUEUED && slot[i].radius && (victim < 0 || slot[i].seq < slot[victim].seq)) victim = i;
    if (victim < 0) goto out;
    fail(&slot[victim], SKC_ERR_OFFLINE);
  }
  int i = free_slot();
  if (i < 0) goto out;
  if (++next_h == 0) next_h = 1;
  slot[i] = (slot_t){.st = S_QUEUED, .h = next_h, .cmd = *c, .radius = replaces_same_path, .at_ms = now_ms,
                     .seq = ++next_seq, .res = {SKC_PENDING, SKC_ERR_NONE}};
  h = next_h;
out:
  UNLOCK();
  return h;
}

sk_cmd_result_t sk_cmd_result(sk_cmd_handle_t h) {
  ensure_lock();
  LOCK();
  sk_cmd_result_t r = {SKC_FAILED, SKC_ERR_NONE, ""};
  for (int i = 0; i < SLOTS; i++)
    if (slot[i].st != S_EMPTY && slot[i].h == h && h) r = slot[i].res;
  UNLOCK();
  return r;
}

bool sk_cmd_next(sk_cmd_t *out, sk_cmd_handle_t *h, int64_t now_ms) {
  ensure_lock();
  LOCK();
  bool have = false;
  for (;;) {
    int oldest = -1;
    for (int i = 0; i < SLOTS; i++)
      if (slot[i].st == S_QUEUED && (oldest < 0 || slot[i].seq < slot[oldest].seq)) oldest = i;
    if (oldest < 0) break;
    if (now_ms - slot[oldest].at_ms > (slot[oldest].cmd.expire_ms ? slot[oldest].cmd.expire_ms : EXPIRE_MS)) {   // too old to be what the user meant now
      fail(&slot[oldest], SKC_ERR_OFFLINE);
      continue;
    }
    slot[oldest].st = S_SENDING;
    *out = slot[oldest].cmd;
    *h = slot[oldest].h;
    have = true;
    break;
  }
  UNLOCK();
  return have;
}

void sk_cmd_finish(sk_cmd_handle_t h, bool transport_ok, int status) { sk_cmd_finish_id(h, transport_ok, status, NULL); }

void sk_cmd_finish_id(sk_cmd_handle_t h, bool transport_ok, int status, const char *reply_id) {
  ensure_lock();
  LOCK();
  for (int i = 0; i < SLOTS; i++) {
    if (slot[i].st != S_SENDING || slot[i].h != h) continue;
    if (!transport_ok) fail(&slot[i], SKC_ERR_OFFLINE);
    else if (status >= 200 && status < 300) {
      slot[i].st = S_DONE;
      slot[i].res = (sk_cmd_result_t){SKC_OK, SKC_ERR_NONE, ""};
      if (reply_id) snprintf(slot[i].res.id, sizeof slot[i].res.id, "%s", reply_id);
    } else if (status == 401 || status == 403) fail(&slot[i], SKC_ERR_NO_ACCESS);
    else if (status == 404) fail(&slot[i], SKC_ERR_NO_PLUGIN);
    else fail(&slot[i], SKC_ERR_REJECTED);
  }
  UNLOCK();
}

bool sk_cmd_reply_id(const char *json, size_t len, char *out, size_t n) {
  if (n) out[0] = 0;
  if (!json || !n) return false;
  cJSON *root = cJSON_ParseWithLength(json, len);
  const cJSON *id = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "id") : NULL;
  bool ok = cJSON_IsString(id) && id->valuestring && strlen(id->valuestring) < n;
  if (ok) snprintf(out, n, "%s", id->valuestring);
  cJSON_Delete(root);
  return ok;
}

size_t sk_cmd_put_body(const sk_cmd_t *c, char *out, size_t n) {
  if (!n) return 0;
  out[0] = 0;
  size_t len = 0;
  if (c->put_number) {
    int w = snprintf(out, n, "{\"value\":%g}", c->value);
    if (w < 0 || (size_t)w >= n) { out[0] = 0; return 0; }
    return (size_t)w;
  }
  if (!c->put_text) return 0;
#define PUT(ch) do { if (len + 1 >= n) { out[0] = 0; return 0; } out[len++] = (ch); } while (0)
  for (const char *p = "{\"value\":\""; *p; p++) PUT(*p);
  for (const char *p = c->body; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    if (ch == '"' || ch == '\\') { PUT('\\'); PUT((char)ch); }
    else if (ch < 0x20) {
      static const char hex[] = "0123456789abcdef";
      for (const char *q = "\\u00"; *q; q++) PUT(*q);
      PUT(hex[ch >> 4]); PUT(hex[ch & 15]);
    } else PUT((char)ch);
  }
  PUT('"'); PUT('}');
#undef PUT
  out[len] = 0;
  return len;
}
