#include "sk_history.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "history.h"
#include "platform.h"
#include "sk_transport.h"

size_t sk_history_parse(const char *json, size_t len, size_t path_index, float *out, size_t max) {
  cJSON *root = len ? cJSON_ParseWithLength(json, len) : NULL;
  const cJSON *data = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "data") : NULL;
  size_t rows = cJSON_IsArray(data) ? (size_t)cJSON_GetArraySize(data) : 0;
  size_t n = 0;
  size_t skip = rows > max ? rows - max : 0;   // keep the newest rows
  size_t k = 0;
  const cJSON *row;
  cJSON_ArrayForEach(row, data) {
    if (k++ < skip) continue;
    if (!cJSON_IsArray(row)) {   // a row that is not [timestamp, ...] makes the whole reply unusable
      n = 0;
      break;
    }
    const cJSON *v = cJSON_GetArrayItem(row, (int)path_index + 1);
    out[n++] = cJSON_IsNumber(v) ? (float)v->valuedouble : NAN;
  }
  cJSON_Delete(root);
  return n;
}

static bool plain_path(const char *p) {   // goes into a URL unescaped
  if (!*p) return false;
  for (; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_')) return false;
  return true;
}

void sk_history_backfill(const char *base_url, const char *token) {
  PLATFORM_EXT_BSS static float vals[HIST_MINUTES];   // 2.9 KB: PSRAM; only the Signal K task runs this
  bd_id_t ids[HIST_MAX_PATHS];
  size_t n = hist_tracked(ids, HIST_MAX_PATHS);
  for (size_t i = 0; i < n; i++) {
    const char *path = bd_path(ids[i]);
    if (!path || !plain_path(path)) continue;
    char url[320];
    snprintf(url, sizeof url, "%s/signalk/v2/api/history/values?paths=%s&duration=PT12H&resolution=60", base_url, path);
    char *body = NULL;
    size_t len = 0;
    if (!sk_http_get(url, token, &body, &len)) break;   // no answer: asking again for the rest would only wait
    size_t rows = sk_history_parse(body, len, 0, vals, HIST_MINUTES);
    free(body);
    if (rows) hist_backfill(ids[i], vals, rows);
  }
}
