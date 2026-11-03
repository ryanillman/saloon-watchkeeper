# boat-instruments

Firmware for ESP32-S3 touch displays that act as purpose-built marine instruments: Engine, Power, Tanks, Anchor, and later Wind and Navigation. Data comes from Signal K or NMEA 2000.

Status: design. See [docs/specs/2026-11-02-boat-instruments-design.md](docs/specs/2026-11-02-boat-instruments-design.md) and [docs/hardware/boards.md](docs/hardware/boards.md).

MIT licensed.

## Simulator

```sh
cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON
cmake --build build/sim
ctest --test-dir build/sim --output-on-failure
./build/sim/sim --board sim800 --palette day      # sim480 | sim800 | sim1024; day | dusk | night
./build/sim/sim --snapshot out/snap               # every page as PNG, no window
```

Swipe with the mouse or use the arrow keys. Left/Right change role, Up/Down change page.

## Firmware

Needs ESP-IDF v5.5.1 (`. ~/esp/esp-idf/export.sh`).

```sh
idf.py -D BOARD=square4 -B build/square4 build              # square4 | lcd7 | lcd7b
idf.py -B build/square4 -p /dev/ttyACM0 flash monitor
idf.py -D BOARD=lcd7 -D BI_EXTRA=diag/probe.defaults -B build/lcd7-probe build   # hardware probe
```
