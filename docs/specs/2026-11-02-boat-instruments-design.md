# boat-instruments: design

Status: draft for review, 2026-11-02.

## What this is

Firmware for ESP32-S3 touch displays that act as purpose-built marine instruments. Each instrument role (Engine, Power, Tanks, Anchor, Wind, Navigation) already knows what data it shows and how it shows it. The user chooses which roles a display carries and swipes between them. Nothing is laid out by the user.

The displays sit on a ship's network beside a Signal K server, chartplotters, and other instruments. They show data and alerts. They do not own alarm logic when the network has it.

The project is new code under the MIT license. It is not derived from any existing firmware.

### Goals

- A display that is useful at a nav desk: swipe to the role the moment calls for (motoring, sailing, at anchor, checking batteries).
- Data-source agnostic. Signal K first, NMEA 2000 later, behind one internal model. A role never knows which source fed it.
- Discover what the boat has, then ask the user to confirm. Typing a data path is the exception.
- A missing value looks failed, per field, and never looks like a reading.
- Avionics-derived visual language: legible at a glance, with a fault flag modeled on the Garmin G5 (red cross and a FAIL label on a dead field, live fields untouched).
- One firmware core across three boards. A new board is a profile, not a fork.
- Most development happens in a desktop simulator against a real Signal K server.

### Non-goals

- A user-editable grid or layout editor.
- Bitmap gauge faces or angle calibration tables.
- Threshold alarm logic on the panel when Signal K is present.
- Arbitrary per-screen data-path assignment.

## Product model

### Roles

A role is a definition compiled into the firmware. It declares:

- **Instance discovery:** a path pattern that finds instances on this boat (`propulsion.*` for engines).
- **Slots per instance:** the Signal K path suffix the slot reads, ordered fallback paths, the default display range, and an optional maximum age.
- **Pages:** hand-designed LVGL code for each size class (see Boards). A page receives a rectangle and the role's bound slots.

A display carries an ordered list of roles from config. Left/right swipe moves between roles. Up/down swipe moves between a role's pages.

### Role catalog and release order

| Order | Role | Data | Pages (first cut) |
|---|---|---|---|
| v1 | Engine | Per engine: `revolutions`, `temperature` (coolant), `oilPressure`, `alternatorVoltage`, `runTime`, `fuel.rate`. One or two engines | Overview: hero RPM dial per engine plus strip gauges for coolant, oil, volts. Detail: strip gauges and readouts for every slot |
| v1 | Power | Per battery bank: `voltage`, `current`, `stateOfCharge`, `timeRemaining`. Per solar/charger: power, current, state | Batteries: SoC strip and V/A/time per bank. Charging: solar watts, charger or shore state, net current |
| v1 | Tanks | Per tank (`tanks.<type>.<id>`): `currentLevel`, `currentVolume`, `capacity` | All tanks as vertical strip gauges with percent and volume, colored by tank type |
| v1 | Anchor | `navigation.anchor.position`, `maxRadius`, `currentRadius`, `navigation.position`, `navigation.headingTrue` | Swing circle with recent track, distance and bearing to anchor, radius. Set, raise, and adjust radius through Signal K |
| after v1 | Wind | Apparent and true wind angle and speed, boat speed | Designed in its own session. Needs true-wind math when the server does not supply it |
| last | Navigation | Depth, speed through water and over ground, heading, COG, position | Designed in its own session |

Exact page designs are settled per role during implementation. The table fixes the data each role reads, not the pixels.

## Visual language

The model is avionics: the G1000 engine indication system and the G5 standby instrument.

- **Hero dial.** One per page at most, for the quantity that matters most on that page (RPM on Engine Overview). Drawn from a range, a unit, and a named sweep: `full`, `three_quarter`, `half`. A scale can carry several needles in one unit (port and starboard RPM). A second scale can take the remainder of the circle. Ticks and numerals are computed from the range in display units. The needle is a line. No bitmaps.
- **Strip gauges.** The default for detail pages, tanks, and power. Horizontal or vertical tape with colored band segments, a pointer, and a digital readout. The detailed strip design is settled when the first detail page is built.
- **Readouts.** Large digits with a label and a unit, for values that have no meaningful range (hours, position).
- **Bands.** Colored range segments on dials and strips. Source order: Signal K `meta.zones` for that path, then role defaults, then config overrides. Taking zones from Signal K means the band on the glass matches the threshold that raises the network alarm.
- **Range.** Signal K `meta.displayScale` if present, then role default, then config override.
- **Fault flag.** Per field. A needle or strip pointer is hidden. A red cross covers the value area so no old value can be read through it. A yellow label reads `<FIELD> FAIL` (`RPM FAIL`, `COOLANT FAIL`). Neighboring fields stay live. A dash or blank is never used for missing data.
- **Palettes.** Day, dusk, and night (red on black). Palette and brightness are reachable from the top strip and stored in config.
- **Top strip.** Role name, page indicator, source status, alert marker. Tapping it opens setup.

## Architecture

### Repository layout

```text
components/
  board/        BoardProfile and drivers: panel, touch, expander, backlight, SD, buzzer
  boatdata/     Path-keyed model of samples and metadata; discovery inventory
  source_sk/    Signal K client: mDNS, REST discovery, WebSocket, PUT, access request
  source_n2k/   NMEA 2000 over TWAI: PGN to path mapping, alert PGNs (milestone 7)
  alerts/       Normalized alert list; acknowledge and silence back to the source
  config/       /config.json on SD: load, validate, atomic save, fallback, versioning
  roles/        Role definitions, slot binding, pages
  ui/           LVGL 9 page shell, navigation, dial, strip, readout, fault flag, setup screens
  platform/     Time, tasks, locks, file paths. ESP-IDF and Linux implementations
main/           Board selection, startup, task wiring
sim/            Linux/SDL build of the same components
tools/          Signal K test injector, config JSON Schema, flash helpers
docs/
```

### Toolchain

- ESP-IDF 5.x with CMake and `idf.py`. LVGL 9 and the Espressif component registry for `esp_lcd` RGB panel, `esp_lcd_touch_gt911`, and the ST7701 init.
- The simulator is a CMake build for Linux with LVGL's SDL driver. It compiles `boatdata`, `source_sk`, `alerts`, `config`, `roles`, and `ui` unchanged, with `platform/linux` and a simulated board at each supported resolution.
- One firmware image per board, chosen at build time (`idf.py -D BOARD=lcd7 build`).

### Dependency rules

- `roles/` and `ui/` include no ESP-IDF headers. They see `boatdata`, `alerts`, `config`, `platform`, and LVGL.
- Sources write to `boatdata` and `alerts`. Roles and UI only read.
- Nothing above `source_*` knows which source supplied a value.

### Tasks and threading

- One UI task owns every LVGL call. It refreshes values at about 10 Hz from a snapshot of `boatdata`.
- Each source runs in its own task and writes under a short lock. No LVGL call happens from a network callback.
- The config writer runs on the UI task's request and does not block rendering for longer than one frame.

## Data model (`boatdata`)

Keys are Signal K paths relative to `vessels.self`, interned to small integer ids. Values are in SI units, as Signal K defines them.

```c
typedef enum { BD_NUMBER, BD_POSITION, BD_TEXT } bd_kind_t;

typedef struct {
  bd_kind_t kind;
  union {
    double num;
    struct { double lat, lon; } pos;
    char text[32];
  };
  int64_t rx_ms;     // monotonic time of the last real update
  bool    valid;
  uint8_t source;    // id of the source that wrote it
} bd_sample_t;
```

Per-path metadata: units, display name, display scale, zones, taken from Signal K `meta` when available.

### Validity rules

A sample is invalid:

- From boot until the first real update for that path.
- When the received value is JSON `null`, not finite, or the wrong type for the path. An invalid value is never stored as 0.
- When its source goes down: Wi-Fi lost, WebSocket closed, or (milestone 7) NMEA 2000 bus silent past its timeout. Every path that source wrote becomes invalid.
- When a role slot declares a maximum age and the sample is older than that. Initial values: engine RPM 3 s; engine coolant, oil pressure, alternator volts, and fuel rate 5 s; anchor-role `navigation.position` 10 s, `navigation.headingTrue` 5 s. Engine hours, tank levels, and battery values have none.

A path that is quiet on a live connection stays valid. Signal K subscriptions send on change, and a steady tank level is healthy.

### Engine running state

An engine controller transmits continuously while it is on. When it goes quiet, Signal K stops sending and keeps the last values, so silence alone cannot tell "key off" from "controller failed". Each engine instance is assessed on every refresh (`engine_state.c`). Oil pressure is the second witness, because it collapses within seconds of a stop and is independent of the RPM pickup:

| Inputs | Mode | Glass |
|---|---|---|
| `propulsion.<id>.state` = `stopped` (when a server provides it) and oil pressure not above 0.5 bar | OFF | Dim "OFF" on that engine's running fields; hours still shown |
| RPM fresh and above 30 RPM | RUNNING | Normal |
| RPM fresh and 0, oil pressure fresh and above 0.5 bar | RUNNING, RPM suspect | `RPM FAIL` on that engine only; other fields live |
| RPM fresh and 0, oil pressure low or absent | OFF | As above |
| RPM silent, oil pressure fresh and above 0.5 bar | RUNNING | RPM flags by age; other fields live |
| RPM silent after reading 0 | OFF | Key off |
| RPM silent after reading above 0, or never seen | NO DATA | Every running field flagged |
| `state` = `unusable` | NO DATA | Every running field flagged |

The standard n2k-signalk mapping does not set `propulsion.<id>.state`; it is used when present. Without an oil pressure sender, RPM 0 has to be taken as off. Signal K resends cached values on subscribe, so a display that reboots with the key off sees the last RPM (0) and shows OFF.

### Units

The model stays in SI. Conversion happens only when a value is drawn, using the units section of config. Initial units: temperature °C/°F, pressure bar/psi/kPa, volume L/gal, speed kn/m/s/km/h, distance m/ft/nm, and rotation stored in Hz but always displayed as RPM.

## Sources

### Signal K (`source_sk`)

- **Server:** mDNS `_signalk-ws._tcp` when config says `"host": "auto"`, otherwise the configured `host:port`.
- **Discovery:** one REST GET of `/signalk/v1/api/vessels/self`, walked into the inventory of paths and metadata. Repeated when the user opens discovery in setup.
- **Live data:** a WebSocket subscription to the bound paths only, plus `notifications.*`. No subscription to `*` on a busy network.
- **Auth:** Signal K device access request. The panel requests access, the user approves it in the Signal K admin UI, and the token is written to config. The token authorizes PUTs (alert acknowledge and silence, anchor set and raise).
- **Reconnect:** exponential backoff with a cap. Every Signal K-sourced path is invalid while disconnected.

### NMEA 2000 (`source_n2k`, milestone 7)

- TWAI driver on the board's CAN pins. On the 7" boards this disables native USB (see `docs/hardware/boards.md`).
- PGNs decode to the same Signal K paths and units, following the published `n2k-signalk` mapping.
- NMEA 2000 identifies devices by instance number. Config maps instances to names (`engine instance 0 → port`).
- Discovery inventory is the set of PGN and instance pairs heard.
- Source priority rules for a path fed by both sources are designed in this milestone. Until then, a display runs one source.

## Binding and discovery

1. On boot, or when the user opens discovery, the source builds the inventory.
2. Each configured role matches its instance pattern against the inventory and proposes instances and slot bindings, including fallback paths.
3. Setup shows the proposal per role ("Engine: found port, starboard. Power: found house, start, solar"). The user confirms, reassigns a slot from a list of seen paths, or excludes an instance.
4. Confirming writes the instances and any explicit bindings to config. From then on those instances are pinned. A pinned instance that disappears shows fault flags. It is not silently dropped.
5. A role with no pinned instances uses whatever discovery finds on each boot.

A slot that cannot be bound shows "not found" in setup and a fault flag on the glass.

## Alerts

### Model

```c
typedef enum { AL_NORMAL, AL_ALERT, AL_WARN, AL_ALARM, AL_EMERGENCY } alert_state_t;

typedef struct {
  char          id[64];       // Signal K notification path, or NMEA 2000 alert id + source
  alert_state_t state;
  char          message[96];
  bool          audible;      // the source asked for sound
  bool          acked;
  int64_t       since_ms;
} alert_t;
```

### Ownership

- **Signal K present:** the panel raises no alerts. It mirrors `notifications.*`. Acknowledge and silence are sent back as Signal K PUTs to that notification, so every other consumer sees the same state. Thresholds, anchor watch logic, and sounding belong to Signal K and whatever acts on its notifications.
- **NMEA 2000 only (milestone 7):** the panel reads PGN 126983 (alert) and 126985 (alert text) and sends 126984 (alert response) for acknowledge and silence. It raises an alert only for the anchor watch, which it computes itself in this mode, and it answers acknowledgements for that alert.

### On the glass

- `alarm` and `emergency`: a full-width red banner over the current page, flashing until acknowledged, with ACK and SILENCE. If the alert path maps to a role (engine paths to Engine, `navigation.anchor` to Anchor), the banner has a GO button. The display never changes pages on its own.
- `alert` and `warn`: an amber marker with a count in the top strip. Tapping it opens the alert list.
- A field whose path has an active alert takes the alert color on its readout.
- Loss of the alert source is a persistent marker (`SK ALERTS OFFLINE`). An empty alert list never stands in for "all clear" when alerts cannot be received.

### Local buzzer

Optional per board and off by default. When enabled, it sounds for unacknowledged `alarm` and `emergency` alerts that asked for sound, and it stops on acknowledge or silence. It never originates an alarm. The beep is a non-blocking state machine advanced by the UI task.

## Configuration

`/config.json` on the SD card is the source of truth. The SD card is required for anything to persist. Without a card, the device boots on built-in defaults, shows a "no SD card" notice, and still allows on-device setup for that session.

### Example

```json
{
  "version": 1,
  "wifi": { "ssid": "Boatnet", "password": "..." },
  "sources": {
    "signalk": { "host": "auto", "token": "..." },
    "n2k": { "enabled": false }
  },
  "units": { "temperature": "C", "pressure": "bar", "volume": "L", "speed": "kn", "distance": "nm" },
  "display": { "palette": "auto", "brightness": 80, "start_role": "engine" },
  "buzzer": { "enabled": false },
  "roles": [
    {
      "role": "engine",
      "instances": [
        { "id": "port", "label": "PORT" },
        { "id": "starboard", "label": "STBD" }
      ],
      "overrides": { "revolutions": { "max": 3600 } },
      "bind": { "port.temperature": "propulsion.port.exhaustTemperature" }
    },
    { "role": "power" },
    { "role": "tanks", "exclude": ["blackWater.0"] },
    { "role": "anchor" }
  ]
}
```

The minimum working file is `wifi` plus one entry in `roles`.

### Rules

- `roles` order is swipe order. `display.start_role` picks the page shown at boot.
- `instances`, `overrides`, `bind`, and `exclude` are optional.
- **Save:** write `config.tmp`, fsync, rename `config.json` to `config.bak.json`, rename `config.tmp` to `config.json`.
- **Load:** `config.json`, then `config.bak.json`, then built-in defaults. A parse or validation error is shown on screen with the key path and line number.
- **Unknown keys survive a rewrite.** The file is held as a JSON tree and edited in place, so hand-added keys are kept.
- **Version:** each firmware upgrades older versions on load and writes the current version on the next save.
- Wi-Fi password and Signal K token are stored in plain text. The file is meant to be readable on a computer.
- A JSON Schema in `tools/config.schema.json` documents every key for editor completion and validation.

### On-device setup

Reached from the top strip:

- Wi-Fi: scan list and on-screen keyboard. The usual first join is credentials written to `config.json` on a computer. There is no setup access point.
- Signal K: server (auto or manual), access request status.
- Roles: add, remove, reorder.
- Discovery: run, review, confirm (see Binding and discovery).
- Units, palette, brightness, buzzer.
- About: firmware version, board, SD status, IP address.

There is no web configuration page in v1.

## Boards

Facts and open questions per board are in `docs/hardware/boards.md`.

| Profile | Board | Resolution | Size class | Expander | Buzzer |
|---|---|---|---|---|---|
| `square4` | Waveshare ESP32-S3 4" square | 480×480 | square | TCA9554 (v3) or CH32V003 (v4), probed | Yes |
| `lcd7` | ESP32-S3-Touch-LCD-7 | 800×480 | wide | CH422G | No |
| `lcd7b` | ESP32-S3-Touch-LCD-7B | 1024×600 | wide | Unnamed, at 0x24 (confirm) | No |

`BoardProfile` is data plus driver selection: resolution, size class, RGB pins and timings, optional panel init sequence, touch chip and pins, expander kind and pin roles, backlight method, SD bus, buzzer output, console routing, CAN pins.

**Size classes.** Pages are designed per size class, not per board: `square` (about 1:1) and `wide` (about 5:3). Pages lay out from the rectangle they are given, so 800×480 and 1024×600 share the `wide` designs. Font sizes step by the short edge.

**Flash and partitions.** 16 MB flash, two OTA app slots. Each new unit is probed with `esptool.py flash_id` before the first image.

**Updates (v1).** USB flashing through a browser installer (ESP Web Tools), and `update.bin` on the SD card, applied on boot and renamed after success. Network OTA comes after v1.

## Error handling summary

| Condition | Glass | Data |
|---|---|---|
| No sample since boot | Fault flag on that field | Invalid |
| `null`, non-finite, wrong type | Fault flag on that field | Invalid, never stored as 0 |
| Signal K disconnected | Fault flags on every Signal K field, source marker in strip, `SK ALERTS OFFLINE` | All Signal K paths invalid |
| Slot older than its max age | Fault flag on that field | Invalid for that slot |
| Slot not bound | Fault flag; "not found" in setup | No binding |
| Config unreadable | Error screen with key path and line, then fall back to `.bak` or defaults | Loaded from fallback |
| No SD card | Persistent notice; changes last until reboot | Defaults |

## Testing

- **Host unit tests** (Linux, in CI): `boatdata` validity and max-age, Signal K delta and REST parsing including `null`, objects, and NaN, config load/save/fallback/unknown-key preservation and version upgrade, unit conversion, dial and strip tick computation, alert state transitions.
- **Integration against a real Signal K server.** A local Signal K server (2.35 at the time of writing) is the test target. `tools/sk-inject` writes scripted deltas and notifications into it with a device token: two engines, batteries, tanks, an alarm notification, anchor drag. Disconnect tests stop the server or block its port. Malformed values that a real server will not relay are covered by unit tests.
- **Page snapshots.** The simulator renders every page for each size class, palette, and {live, faulted} state to PNG in CI, so a change shows its effect on the glass.
- **Bench checklist per board:** `flash_id`, color bars, touch, backlight on/off and dimming if supported, SD read and write, Wi-Fi join, a live Signal K value on Engine, stopping the server flags the field, alert banner from an injected notification, ACK reaches Signal K.

## Milestones

1. **Skeleton.** Repo, CI, simulator builds and shows the empty page shell with swipe navigation.
2. **Boards.** All three profiles boot to the page shell with touch, backlight, and SD. Bench checklist steps 1–5. The 7B's open items in `boards.md` are resolved here.
3. **Data path.** `boatdata`, Signal K source with discovery and live data, config load and save, the Engine role with hero dial, strips, and fault flags. Working in the simulator and on every board.
4. **v1 roles.** Power and Tanks, alerts over Signal K, palettes.
5. **Anchor and on-device setup.** Wi-Fi entry, discovery confirm, Signal K access request, SD update. **v1 release.**
6. Wind.
7. NMEA 2000 source and NMEA 2000 alerts.
8. Navigation.

The first implementation plan covers milestones 1–3. Each later milestone gets its own plan.

## Open questions

- 7B panel driver, timings, expander chip, and dimming method. Resolved on the bench in milestone 2.
- Whether the `lcd7` backlight can be dimmed or is on/off only.
- Strip gauge visual design. Settled with the first detail page in milestone 3.
- True-wind computation when the server does not supply it. Settled in milestone 6.
- Source priority when Signal K and NMEA 2000 both supply a path. Settled in milestone 7.
