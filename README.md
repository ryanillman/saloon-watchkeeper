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
tools/build-firmware.sh                                     # all three boards, merged images in out/
idf.py -D BOARD=lcd7 -D BI_EXTRA=diag/probe.defaults -B build/lcd7-probe build   # hardware probe
```

### Bench ports

The three bench boards enumerate in a fixed order. Check with `ls /dev/ttyACM*` before flashing.

| Board | Port | Connector |
|---|---|---|
| `square4` | `/dev/ttyACM0` | USB-JTAG |
| `lcd7` | `/dev/ttyACM1` | USB-JTAG |
| `lcd7b` | `/dev/ttyACM2` | UART (CH340), the 7B's console |

Build and flash all three (flashing in parallel works):

```sh
. ~/esp/esp-idf/export.sh
for b in square4 lcd7 lcd7b; do idf.py -D BOARD=$b -B build/$b build; done
i=0; for b in square4 lcd7 lcd7b; do idf.py -B build/$b -p /dev/ttyACM$i flash & i=$((i+1)); done; wait
```

Then play a scenario at the local Signal K (needs `tools/sk-inject/.token`, see `docs/testing.md`):

```sh
python3 -I tools/sk-inject/inject.py tools/sk-inject/scenarios/alarm-coolant.json   # 60 s; alarm at 10 s, cleared at 40 s
```
