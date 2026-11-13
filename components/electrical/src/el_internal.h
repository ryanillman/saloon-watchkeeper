#pragma once
#include "electrical.h"

el_device_t *el_dev_find_or_add(el_t *e, uint8_t feed, el_ref_t ref);
uint64_t el_dev_key(const el_device_t *d);
el_endpoint_t *el_ep_find(el_t *e, uint64_t devkey, uint8_t domain, uint8_t instance);
el_endpoint_t *el_ep_find_or_add(el_t *e, uint64_t devkey, uint8_t domain, uint8_t instance);
void el_rekey(el_t *e, uint64_t from, uint64_t to);

bool el_device_emits(const el_t *e, uint64_t devkey, el_field_t f);
void el_classify_all(el_t *e);
void el_merge_run(el_t *e, int64_t now_ms);

// One process-wide mutex: the CAN task and the Signal K task both feed the same resolver.
void el_lock(void);
void el_unlock(void);
void el_observe_locked(el_t *e, uint8_t feed, el_ref_t ref, uint8_t instance, el_field_t f, double value, int64_t now_ms);
