# Saloon Watchkeeper (repo directory: boat-instruments)

ESP32-S3 touch-display marine instruments (LVGL, ESP-IDF v5.5.1, Signal K). Components are in `components/`; a Linux simulator and host tests are in `sim/` and `tests/`. README.md is the public document (what it is, simulator, build, config) and carries no bench specifics. The bench-board details are below; `docs/testing.md` has the live scenarios and what each should show. Older docs under `docs/` still say "boat-instruments".

## Working here

- Host tests: `cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON && cmake --build build/sim && ctest --test-dir build/sim --output-on-failure`, then `tools/check-layering.sh`. Run them after any `components/` change.
- Firmware: `. ~/esp/esp-idf/export.sh` first, then `tools/build-firmware.sh` (every directory in `boards/`). A build directory made before 2026-11-03 needs `fullclean` (see `docs/hardware/boards.md`, Internal RAM).
- Bench boards: `tools/deploy.sh` flashes the image matched by USB serial in `tools/board-usb.txt`, not by tty number. Usual map: square4 `14:C1:9F:2B:5F:F8` on `/dev/ttyACM0`, lcd7 `44:BD:8D:F7:39:44` on `/dev/ttyACM1`, lcd7b CH340 `5B91017076` on `/dev/ttyACM2`. Confirm with `udevadm info`.
  - Usage: `tools/deploy.sh /dev/ttyACM0 /dev/ttyACM1 /dev/ttyACM2` in any order (different boards flash together); `--dry-run` prints the match without flashing. A new unit needs one line in `tools/board-usb.txt`; read its serial with `udevadm info -q property -n /dev/ttyACM0`.
  - `tools/build-firmware.sh` writes merged images to `out/<board>.bin` (flash at 0x0). Hardware probe build: `idf.py -D BOARD=lcd7 -D BI_EXTRA=diag/probe.defaults -B build/lcd7-probe build`.
  - Single board: `idf.py -D BOARD=square4 -B build/square4 build`, then `idf.py -B build/square4 -p /dev/ttyACM0 flash monitor`.
- Live check: the local Signal K is on `localhost:3000` (test-only, so zones the tool creates stay). `tools/scenario.sh up` starts the boat, a daemon that sends routine values for every covered path; `tools/scenario.sh` lists overlays and `tools/scenario.sh <name>` plays one on top (a unique prefix is enough); `down`, `status`, `log`, `zones`. Alarms are never written as notifications: the boat checks the path's zones, creates defaults if there are none, and drives the value across a boundary so Signal K raises and clears it. `alarm-coolant` raises at 10 s and clears at 40 s, so ACK within that window. `tools/sk-inject/live-check.sh` runs the raise/clear loop; `tests/sk_inject` (in ctest) covers the rest without a server. A worktree needs `tools/sk-inject/.token` copied in.
- Work inline; avoid heavy subagent runs (they have hit the usage limit).

## Alerts behaviour (`components/alerts`, `components/ui/src/ui_alerts.c`)

- The banner shows only un-acknowledged `alarm`/`emergency`. An acknowledged one moves to a bell-and-count marker in the top strip; the alert/warn count stays a separate amber marker. Both open the same list, which still lists acknowledged alarms (ACK hidden, state dimmed).
- Exception: an alarm whose ACK did not reach Signal K (`send_failed`, e.g. source down or no `ref`) stays on the banner, steady, showing `ACK NOT SENT`. Tests that ACK an alarm and expect the banner to clear must give it a `ref` and have the source up.
