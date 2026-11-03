// Simulator: the host's network is always up; a scan returns a fixed list.
#include <stdio.h>
#include <string.h>
#include "net.h"

static unsigned restarts;

void net_wifi_start(const cfg_wifi_t *wifi) { (void)wifi; }
void net_wifi_restart(const cfg_wifi_t *wifi) { (void)wifi; restarts++; }
bool net_wifi_up(void) { return true; }

bool net_wifi_info(char ip[16], int8_t *rssi) {
  snprintf(ip, 16, "127.0.0.1");
  *rssi = -50;
  return true;
}

int net_wifi_scan(net_ap_t *out, int max) {
  static const net_ap_t fixed[] = {{"Boatnet", -48, true}, {"Marina Guest", -71, false}, {"Neighbour", -83, true}};
  int n = 0;
  for (; n < max && n < (int)(sizeof fixed / sizeof fixed[0]); n++) out[n] = fixed[n];
  return n;
}
