// Signal K as a feed for the electrical resolver: stream deltas (which carry each value's device in "$source") and
// the server's `sources` tree (which names the devices).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "electrical.h"

// Routes electrical values of every delta into `e` (feed 1) and publishes what changed. NULL detaches.
void sk_electrical_attach(el_t *e);

// Reads /signalk/v1/api/sources: for each <bus>.<address> with an n2k section, its canName, modelId and serial name
// the device that `$source` "<bus>.<address>" refers to, and "<bus>.<canName>" too (newer servers stamp that form). False when it is not a JSON object.
bool sk_electrical_load_sources(const char *json, size_t len);

// The connection is gone: what this feed published becomes invalid.
void sk_electrical_feed_down(void);
