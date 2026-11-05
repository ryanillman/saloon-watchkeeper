#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";
static volatile bool up, joining;   // joining: an SSID is set, so reconnect after a drop
static esp_netif_t *sta;

static const char *reason_name(uint8_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_EXPIRE: return "auth expired";
    case WIFI_REASON_AUTH_LEAVE: return "auth leave";
    case WIFI_REASON_ASSOC_LEAVE: return "left by AP/self";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout (wrong password?)";
    case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout (signal lost)";
    case WIFI_REASON_NO_AP_FOUND: return "no AP with that SSID found";
    case WIFI_REASON_AUTH_FAIL: return "auth failed (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL: return "association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout (wrong password?)";
    case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "AP found but auth mode too weak";
    default: return "";
  }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (joining) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *d = data;
    ESP_LOGW(TAG, "disconnected: reason %u %s%s", d->reason, reason_name(d->reason), joining ? "; retrying" : "");
    up = false;
    if (joining) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
    const wifi_event_sta_connected_t *c = data;
    ESP_LOGI(TAG, "associated with %.*s on channel %u", c->ssid_len, (const char *)c->ssid, c->channel);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *e = data;
    ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
    up = true;
  }
}

static void set_station(const cfg_wifi_t *w) {
  wifi_config_t wc = {0};
  if (strlen(w->ssid) > sizeof wc.sta.ssid || strlen(w->password) > sizeof wc.sta.password)
    ESP_LOGW(TAG, "SSID (%u chars) or password (%u chars) too long; truncated", (unsigned)strlen(w->ssid), (unsigned)strlen(w->password));
  strncpy((char *)wc.sta.ssid, w->ssid, sizeof wc.sta.ssid);
  strncpy((char *)wc.sta.password, w->password, sizeof wc.sta.password);
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
  ESP_LOGI(TAG, "station config: ssid \"%s\", password %u chars: %s", w->ssid, (unsigned)strlen(w->password), esp_err_to_name(err));
  ESP_ERROR_CHECK(err);
  joining = w->ssid[0] != 0;
}

// The stack always starts (a scan needs it), but joins only when config.json has an SSID.
void net_wifi_start(const cfg_wifi_t *w) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  sta = esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   // config.json is the only store
  set_station(w);
  ESP_ERROR_CHECK(esp_wifi_start());
  if (joining) ESP_LOGI(TAG, "joining %s", w->ssid);
  else ESP_LOGW(TAG, "no SSID in config.json; not joining");
}

void net_wifi_restart(const cfg_wifi_t *w) {
  ESP_LOGI(TAG, "restart requested");
  joining = false;   // the disconnect below must not reconnect to the old network
  esp_wifi_disconnect();
  up = false;
  set_station(w);
  if (joining) {
    ESP_LOGI(TAG, "joining %s", w->ssid);
    esp_wifi_connect();
  }
}

bool net_wifi_up(void) { return up; }

bool net_wifi_info(char ip[16], int8_t *rssi) {
  esp_netif_ip_info_t info;
  wifi_ap_record_t ap;
  if (!up || !sta || esp_netif_get_ip_info(sta, &info) != ESP_OK) return false;
  snprintf(ip, 16, IPSTR, IP2STR(&info.ip));
  *rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
  return true;
}

int net_wifi_scan(net_ap_t *out, int max) {
  wifi_scan_config_t sc = {.show_hidden = false};
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;   // blocks until the scan completes
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  if (n > 32) n = 32;
  wifi_ap_record_t *rec = calloc(n ? n : 1, sizeof *rec);
  if (!rec) return 0;
  esp_wifi_scan_get_ap_records(&n, rec);   // strongest first
  int k = 0;
  for (uint16_t i = 0; i < n && k < max; i++) {
    if (!rec[i].ssid[0]) continue;
    bool dup = false;   // one entry per network name: the strongest access point
    for (int j = 0; j < k && !dup; j++) dup = !strcmp(out[j].ssid, (const char *)rec[i].ssid);
    if (dup) continue;
    snprintf(out[k].ssid, sizeof out[k].ssid, "%s", (const char *)rec[i].ssid);
    out[k].rssi = rec[i].rssi;
    out[k].secure = rec[i].authmode != WIFI_AUTH_OPEN;
    k++;
  }
  free(rec);
  return k;
}
