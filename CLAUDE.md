# boat-instruments

ESP32-S3 touch-display marine instruments (LVGL, ESP-IDF v5.5.1, Signal K). Components are in `components/`; a Linux simulator and host tests are in `sim/` and `tests/`. README.md has the build, flash, and bench-port commands; `docs/testing.md` has the live scenarios and what each should show.

## Working here

- Host tests: `cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON && cmake --build build/sim && ctest --test-dir build/sim --output-on-failure`, then `tools/check-layering.sh`. Run them after any `components/` change.
- Firmware: `. ~/esp/esp-idf/export.sh` first. A build directory made before 2026-11-03 needs `fullclean` (see `docs/hardware/boards.md`, Internal RAM).
- Bench boards: square4 `/dev/ttyACM0`, lcd7 `/dev/ttyACM1`, lcd7b `/dev/ttyACM2` (UART). Confirm with `ls /dev/ttyACM*` and `udevadm info`.
- Live check: the local Signal K is on `localhost:3000`; play scenarios with `python3 -I tools/sk-inject/inject.py tools/sk-inject/scenarios/<name>.json`. `alarm-coolant` raises at 10 s and clears at 40 s, so ACK within that window.
- Work inline; avoid heavy subagent runs (they have hit the usage limit).

## Alerts behaviour (`components/alerts`, `components/ui/src/ui_alerts.c`)

- The banner shows only un-acknowledged `alarm`/`emergency`. An acknowledged one moves to a bell-and-count marker in the top strip; the alert/warn count stays a separate amber marker. Both open the same list, which still lists acknowledged alarms (ACK hidden, state dimmed).
- Exception: an alarm whose ACK did not reach Signal K (`send_failed`, e.g. source down or no `ref`) stays on the banner, steady, showing `ACK NOT SENT`. Tests that ACK an alarm and expect the banner to clear must give it a `ref` and have the source up.
- `docs/specs/2026-11-03-milestone-4-design.md` still describes the older "banner stays, stops flashing" behaviour.
