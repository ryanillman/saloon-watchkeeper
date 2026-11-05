#include "anchor_record.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"

static bool read_one(const char *path, anchor_record_t *out) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  char buf[256];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  cJSON *root = cJSON_Parse(buf);
  const cJSON *armed = cJSON_GetObjectItem(root, "armed");
  bool ok = cJSON_IsObject(root) && cJSON_IsBool(armed);   // a record always says whether an anchor is set
  if (ok) {
    const cJSON *limit = cJSON_GetObjectItem(root, "limit_m"), *set = cJSON_GetObjectItem(root, "set_epoch_s");
    out->armed = cJSON_IsTrue(armed);
    out->limit_m = cJSON_IsNumber(limit) ? limit->valuedouble : 0;
    out->set_epoch_s = cJSON_IsNumber(set) ? (int64_t)set->valuedouble : 0;
  }
  cJSON_Delete(root);
  return ok;
}

bool anchor_record_load(const char *dir, anchor_record_t *out) {
  if (!dir) return false;
  char path[160];
  anchor_record_t r;
  snprintf(path, sizeof path, "%s/anchor.json", dir);
  if (!read_one(path, &r)) {
    snprintf(path, sizeof path, "%s/anchor.bak.json", dir);
    if (!read_one(path, &r)) return false;
  }
  *out = r;
  return true;
}

bool anchor_record_save(const char *dir, const anchor_record_t *r) {
  if (!dir) return false;
  char tmp[160], main_[160], bak[160];
  snprintf(tmp, sizeof tmp, "%s/anchor.tmp", dir);
  snprintf(main_, sizeof main_, "%s/anchor.json", dir);
  snprintf(bak, sizeof bak, "%s/anchor.bak.json", dir);
  FILE *f = fopen(tmp, "w");
  if (!f) return false;
  bool ok = fprintf(f, "{\n  \"armed\": %s,\n  \"limit_m\": %g,\n  \"set_epoch_s\": %lld\n}\n", r->armed ? "true" : "false",
                    r->limit_m, (long long)r->set_epoch_s) > 0;
  ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    remove(tmp);
    return false;
  }
  remove(bak);   // FAT will not rename over an existing file
  if (rename(main_, bak) != 0 && errno != ENOENT) fprintf(stderr, "anchor: rename %s: %s\n", main_, strerror(errno));
  if (rename(tmp, main_) != 0) {
    fprintf(stderr, "anchor: save failed: rename %s: %s (the old record is in %s)\n", tmp, strerror(errno), bak);
    return false;
  }
  return true;
}
