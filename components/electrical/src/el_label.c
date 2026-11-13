#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "el_internal.h"

static const char *function_word(el_fn_t fn) {
  switch (fn) {
    case EL_FN_BATTERY: return "Battery";
    case EL_FN_PV: return "Solar";
    case EL_FN_CHARGER: return "Charger";
    default: return "Device";
  }
}

static const char *model_of(const el_t *e, uint64_t devkey) {
  for (size_t i = 0; i < EL_MAX_DEVICES; i++)
    if (e->dev[i].used && e->dev[i].key == devkey && e->dev[i].id.model_id[0]) return e->dev[i].id.model_id;
  return NULL;
}

// The first word of the model with no digit in it and more than one character; the function when there is none.
static void base_of(const el_t *e, const el_endpoint_t *p, char *out, size_t n) {
  const char *m = model_of(e, p->devkey);
  while (m && *m) {
    while (*m && !isalnum((unsigned char)*m)) m++;
    const char *start = m;
    bool digit = false;
    while (*m && isalnum((unsigned char)*m)) digit |= isdigit((unsigned char)*m++) != 0;
    size_t len = (size_t)(m - start);
    if (len > 1 && !digit) {
      snprintf(out, n, "%.*s", (int)len, start);
      return;
    }
  }
  snprintf(out, n, "%s", function_word(p->fn));
}

size_t el_endpoint_label(const el_t *e, const el_endpoint_t *p, char *out, size_t n) {
  if (!n) return 0;
  char base[40], other[40], qual[12] = "";
  base_of(e, p, base, sizeof base);
  bool devices_differ = false, same_device_more = false;
  for (size_t i = 0; i < e->nep; i++) {   // the endpoints a person sees, of this function, that would read the same
    const el_endpoint_t *q = &e->ep[i];
    if (q == p || q->hidden || q->fn != p->fn) continue;
    base_of(e, q, other, sizeof other);
    if (strcmp(base, other)) continue;
    if (q->devkey != p->devkey) devices_differ = true;
    else same_device_more = true;
  }
  if (devices_differ && same_device_more) snprintf(qual, sizeof qual, "%02x.%u", (unsigned)(p->devkey & 0xFF), p->instance);
  else if (devices_differ) snprintf(qual, sizeof qual, "%02x", (unsigned)(p->devkey & 0xFF));
  else if (same_device_more) snprintf(qual, sizeof qual, "%u", p->instance);
  size_t room = n - 1, ql = strlen(qual);
  if (ql) {
    size_t keep = room > ql + 1 ? room - ql - 1 : 0;   // the word gives up its end, never the qualifier
    if (strlen(base) > keep) base[keep] = 0;
    return (size_t)snprintf(out, n, "%s %s", base, qual);
  }
  return (size_t)snprintf(out, n, "%.*s", (int)room, base);
}
