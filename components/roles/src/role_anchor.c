#include "anchor_page.h"

const role_def_t role_anchor = {
  .name = "anchor",
  .title = "ANCHOR",
  .groups = NULL,
  .group_count = 0,
  .page_count = 1,
  .build_page = anchor_build_page,
  .refresh = anchor_refresh,
};
