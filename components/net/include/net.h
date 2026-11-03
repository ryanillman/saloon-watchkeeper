#pragma once
// Wi-Fi station from config.json. Reconnects on its own; the Signal K client
// sees the loss as a closed socket and flags its fields.
#include <stdbool.h>
#include "config.h"

void net_wifi_start(const cfg_wifi_t *wifi);
bool net_wifi_up(void);
