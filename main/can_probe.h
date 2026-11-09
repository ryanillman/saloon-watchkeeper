#pragma once
// Bench probe: brings the board up, claims an NMEA 2000 address, sends a test alert every 5 s
// and logs what it hears. Never returns. Enabled by CONFIG_BI_DIAG_CAN_PROBE.
void can_probe_run(void);
