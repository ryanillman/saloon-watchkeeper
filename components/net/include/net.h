#pragma once
// Wi-Fi station from config.json. Reconnects on its own; the Signal K client
// sees the loss as a closed socket and flags its fields.
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

typedef struct { char ssid[33]; int8_t rssi; bool secure; } net_ap_t;

void net_wifi_start(const cfg_wifi_t *wifi);     // brings the stack up; joins when an SSID is set
void net_wifi_restart(const cfg_wifi_t *wifi);   // new SSID or password: leave and join again
bool net_wifi_up(void);
bool net_wifi_info(char ip[16], int8_t *rssi);   // false when not connected
int net_wifi_scan(net_ap_t *out, int max);       // blocking, up to ~4 s; strongest first
