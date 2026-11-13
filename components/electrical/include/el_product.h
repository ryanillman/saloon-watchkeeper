// Optional product knowledge: maps a device's model id (from its product information) to the function its
// endpoints serve. It only breaks ties and fills gaps; the resolver classifies without it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "electrical.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *pattern;   // case-insensitive, anchored; '*' matches any run of characters
  el_fn_t fn;            // what the device's endpoints are when nothing stronger says otherwise
  bool monitor;          // the device measures the bank (authoritative for state of charge and current)
} el_product_row_t;

// Rows supplied by configuration; they are tried before the shipped ones. The caller owns `rows` (NULL, 0 clears).
void el_product_set_extra(const el_product_row_t *rows, size_t n);

// First matching row. False for an empty or unknown model.
bool el_product_lookup(const char *model_id, el_fn_t *fn, bool *monitor);

#ifdef __cplusplus
}
#endif
