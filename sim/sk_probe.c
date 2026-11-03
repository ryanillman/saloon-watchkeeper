// Developer tool: connect to Signal K, subscribe to the given paths, print them.
// Usage: sk_probe [host] [path...]
#include <stdio.h>
#include <string.h>
#include "boatdata.h"
#include "platform.h"
#include "sk_client.h"
#include "sk_parse.h"

int main(int argc, char **argv) {
  cfg_t cfg;
  cfg_defaults(&cfg);
  if (argc > 1) snprintf(cfg.sk.host, sizeof cfg.sk.host, "%s", argv[1]);
  bd_init();
  bd_id_t ids[32];
  sk_sub_t subs[32];
  size_t n = 0;
  for (int i = 2; i < argc && n < 32; i++) {
    ids[n] = bd_intern(argv[i]);
    subs[n] = (sk_sub_t){ids[n], 0};
    n++;
  }
  sk_client_init(&cfg.sk, subs, n);
  sk_client_start();
  for (int t = 0; t < 10; t++) {
    platform_sleep_ms(1000);
    printf("state=%d", sk_client_state());
    for (size_t i = 0; i < n; i++) {
      bd_sample_t s;
      bd_get(ids[i], &s);
      if (s.valid && s.kind == BD_NUMBER) printf("  %s=%g", bd_path(ids[i]), s.num);
      else printf("  %s=%s", bd_path(ids[i]), s.valid ? "(non-number)" : "FAIL");
    }
    bd_id_t all[256];
    printf("  [known paths: %zu]\n", bd_match("*", all, 256) + bd_match("*.*", all, 256) + bd_match("*.*.*", all, 256) + bd_match("*.*.*.*", all, 256));
    fflush(stdout);
  }
  return 0;
}
