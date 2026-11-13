#include <ctype.h>
#include "el_product.h"
#include "el_product_table.h"

static const el_product_row_t *extra;
static size_t extra_n;

void el_product_set_extra(const el_product_row_t *rows, size_t n) {
  extra = rows;
  extra_n = rows ? n : 0;
}

static bool match(const char *pat, const char *s) {
  for (; *pat; pat++) {
    if (*pat == '*') {
      while (pat[1] == '*') pat++;
      if (!pat[1]) return true;
      for (; *s; s++)
        if (match(pat + 1, s)) return true;
      return match(pat + 1, s);
    }
    if (!*s || tolower((unsigned char)*pat) != tolower((unsigned char)*s)) return false;
    s++;
  }
  return !*s;
}

bool el_product_lookup(const char *model_id, el_fn_t *fn, bool *monitor) {
  if (!model_id || !model_id[0]) return false;
  for (size_t i = 0; i < extra_n; i++)
    if (match(extra[i].pattern, model_id)) {
      *fn = extra[i].fn;
      *monitor = extra[i].monitor;
      return true;
    }
  for (size_t i = 0; i < sizeof SHIPPED / sizeof SHIPPED[0]; i++)
    if (match(SHIPPED[i].pattern, model_id)) {
      *fn = SHIPPED[i].fn;
      *monitor = SHIPPED[i].monitor;
      return true;
    }
  return false;
}
