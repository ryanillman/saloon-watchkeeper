# NMEA 2000 plan

Status: plan, 2026-11-05. Nothing here is built. Facts about the existing code are checked against the tree; PGN and Signal K path details marked **verify** come from memory and must be checked against canboat and `n2k-signalk` before the table is written.

## Where to pick up (2026-11-05)

Done: phase 0 (see Phases). Uncommitted at the time of writing: `boatdata` source table and arbitration, `bd_admit` in `sk_parse`, app registration of Signal K, tests, and this document. Host build and all 48 ctest tests pass, `tools/check-layering.sh` is clean. **Not verified:** the firmware build. `app.c` gained two small edits (a `bd_source_register` call after `bd_init`, and `bd_source_set_state` calls beside `ui_shell_set_source`); ESP-IDF is not installed on this machine (`~/esp` is missing), so compile them with `tools/build-firmware.sh` on a machine that has it.

Next: **phase 1**, in this order. None of it needs CAN code or boat data.
1. `-fallback` snapshots in `snapshot_smoke` for every page (SK-only paths invalidated, CAN-suppliable paths live). Look at each PNG first; the findings set the rest of the phase's scope.
2. Anchor page and Glance tile FAIL when Signal K is not live. Role not offered without Signal K (config load and setup's ADD list).
3. Tide FAIL flag on the Depth page (rule in the degradation table).
4. Strip: `ALARMS DISABLED` text, and the multi-source indicator (`ui_src_t` is Signal K only today).
5. Anchor watch-lost message names the cause (`SK OFFLINE` against plugin silent); a test with a second source live and Signal K down; the `docs/testing.md` scenario.

Update 2026-11-08: the Victron bench capture is skipped and the phase order below is superseded by `docs/specs/2026-11-08-n2k-library-design.md`. Decoders and default ids are written from canboat and stay **unverified** until a real capture exists. The library's `n2k` and `n2k_node` components are built and host-tested (`docs/superpowers/plans/2026-11-08-n2k-core-and-node.md`); the Power-in-CAN-only question stays open.

Build notes for this machine: it needed `ninja-build pkg-config libsdl2-dev libcurl4-openssl-dev libpng-dev`. The first `cmake` configure fetches LVGL, Unity and cJSON from GitHub. `build/sim` is the build directory.

## Goals and non-goals

Goals
- Signal K stays the primary source. A CAN-bus source is a true fallback: when Signal K is down, raw sensor data keeps the instruments alive.
- A boat with no Signal K server can run on CAN alone.
- Nothing in `components/roles` or `components/ui` learns which source fed a value, except where a widget is explicitly Signal K-only.

Non-goals
- NMEA 2000 over Wi-Fi. Gateways differ (Yacht Devices RAW, Actisense ASCII, canboat JSON, 0183-only). We leave a hook (below) and build nothing until someone asks. Signal K gateways need no code: they are Signal K.
- Alarms from NMEA 2000. There are no thresholds or alarms in CAN data. Pure CAN mode has no alarms.
- A local anchor watch. Signal K owns the anchor point, radius and polygon zones, so the display cannot watch without it. Anchor is the one exception to "no local alarms" (see Anchor).
- Transmitting from the display. The display stays listen-only with no address claim. The library does transmit (address claim, ISO Request replies, fast-packet send) for the signals controller, which sends NMEA 2000 alerts when it loses Signal K; see `docs/specs/2026-11-08-n2k-library-design.md`.
- Showing NMEA 2000 Alert PGNs (126983 to 126986) on the display. Not for now; the controller's alerts are for other devices on the bus.

## Source modes

Derived from config, not a mode switch. `sources.signalk` (exists) and a new `sources.nmea2000.enabled`.

| Mode | Data | Alerts | Anchor role |
|---|---|---|---|
| SK only | Signal K | from SK | offered |
| SK + CAN | per path: SK preferred, CAN fills in | from SK; none while SK is down | offered; page FAILs when SK is down |
| CAN only | CAN | none, ever | not offered |

Rule that keeps this simple: **what is offered depends on configuration (static); what shows FAIL depends on runtime state (dynamic).** A widget that can never have data in the configured mode is not drawn. A widget that has data when everything is up shows FAIL when its source is not.

Enabling CAN on lcd7 and lcd7b flips EXIO5 and disables native USB (`docs/hardware/boards.md`). The board code must read the flag at boot, before USB comes up, so the setting takes effect after a reboot. Logs and flashing then go through the UART port. Ship it off by default.

## Architecture

New components, same layering rules as `source_sk` (`tools/check-layering.sh` applies):

- **`n2k`** (pure C, host-tested): fast-packet reassembly, per-PGN decoders, and the table that turns decoded fields into boatdata writes in SI units. Two entry points, which are the Wi-Fi hook:
  - `n2k_feed_frame(can_id, data, len, now_ms)` for raw 29-bit frames (CAN, and gateways that send frames);
  - `n2k_feed_pgn(pgn, src, data, len, now_ms)` for gateways that send whole PGNs.
  A future Wi-Fi source is a transport plus a text parser calling one of these. It needs no changes elsewhere.
- **`source_can`** (ESP only, `source_can_esp.c`): TWAI in listen-only mode, one RX task, a frame queue. A Linux `source_can_linux.c` reads SocketCAN (`vcan0`) so the simulator and CI can replay `candump` logs.
- **`boatdata`**: source registry and arbitration (next section).

Hot path: the decoder resolves path ids once when instances are first seen and writes by id. `bd_intern` is a linear `strcmp` over up to 512 paths and must not run per frame.

## Source arbitration (`boatdata`)

`bd_sample_t` already has `source`, and `sk_client.c:237` already calls `bd_invalidate_source` when Signal K drops. Add:

- A source table: id, name, priority, configured flag, state (LIVE / CONNECTING / OFFLINE). Roles read the state with `bd_source_state(id)`, so they need no new dependency.
- In `bd_set_*`: a write from a lower-priority source is dropped while the path holds a valid sample from a higher-priority source younger than the hold window (default 30 s: slow Signal K paths such as tank levels and SOC can be quiet that long, and a shorter window would let the bus override them between updates). Per path, not global: a Signal K server that lacks the engine paths still gets engine data from CAN.
- No extra per-path RAM. The arbitration data is one `uint8_t` source id that already exists.

Gotcha: on reconnect Signal K replays the last value of every path with its old timestamp (`anchor_state.c` already guards against this). A replayed value must not displace a fresher CAN value. A Signal K write whose `src_s` is more than 10 s behind `bd_newest_source_time()` is stored only if no fresher sample from another source exists (`bd_admit`, called by `sk_parse` before it writes). A null is weak in the same way: a Signal K null never hides a fresh bus value, and a bus null never hides a Signal K value.

## Instance naming

Identity of a thing on the bus is `(class, instance)`, plus the fluid type for tanks. Never the source address, which changes when devices re-claim.

**Default ids** are what the standard Signal K N2K provider produces, so a server fed from the same bus lines up with no configuration: `propulsion.<instance>`, `tanks.<fluid>.<instance>`, `electrical.batteries.<instance>`. **Verify** against `n2k-signalk`. Fluid type 127505 maps to the SK names the Tanks role already uses (`fuel`, `freshWater`, `wasteWater`, `blackWater`, ...).

**Mismatch is the normal case.** The server may call engines `port` and `starboard`, and Victron batteries often carry VE.Direct instances such as `279`. So config gets an explicit map:

```json
"sources": { "nmea2000": { "enabled": true,
  "ids": { "engine.0": "port", "engine.1": "starboard", "battery.1": "279" } } }
```

Keys are `<role group>.<n2k instance>`, values are the Signal K id. The decoder emits the mapped id. Unmapped instances use the default id.

**Duplicates are the danger.** `role_bind.c` discovers instances by walking every interned path under the prefix. If CAN interns `propulsion.0` while SK has `propulsion.port`, the Engine page grows a phantom third instance. Rule:

- **SK + CAN:** CAN never creates a role instance. It writes only to paths that already exist: discovered from SK, pinned in config, mapped, or restored from the cache below. Unmapped instances are counted and listed (serial `status`, later setup) but not shown.
- **CAN only:** CAN creates instances, mapped or default.

**Cold start with SK down.** Discovery finds nothing, so fallback would show nothing. Fix: an instance cache on the SD card, the same pattern as `anchor_record` (atomic write, `.bak`). It records the discovered ids per role when they change under live SK data. At boot they are interned as placeholders, so the CAN source has paths to fill and the roles find their instances.

Labels: `label_for` already turns `port` and `starboard` into PORT and STBD and upper-cases anything else. Default CAN ids `0`, `1` would be labelled "0", "1", so the map is also how a CAN-only boat gets sensible labels.

## What CAN can supply (first pass, **verify** every row)

| Role / data | PGNs | SK path (SI) | In CAN fallback |
|---|---|---|---|
| Engine RPM, boost | 127488 | `propulsion.<i>.revolutions` (Hz), `boostPressure` | live |
| Engine temps, oil, alternator, fuel rate, hours | 127489 | `temperature`, `oilPressure`, `oilTemperature`, `alternatorVoltage`, `fuel.rate`, `runTime` | live |
| Tank levels | 127505 | `tanks.<fluid>.<i>.currentLevel` (ratio), `capacity` | live |
| Battery V, A, temp | 127508 | `electrical.batteries.<i>.voltage`, `current`, `temperature` | live |
| Battery SOC, time remaining | 127506 | `...capacity.stateOfCharge`, `timeRemaining` | live if the device sends it |
| Solar, inverter, charger (Victron) | little standard coverage | `electrical.solar`, `inverters`, `chargers` | FAIL |
| Depth | 128267 | `environment.depth.belowTransducer` (+ offset) | live; keel and surface from the offset field |
| Wind | 130306 | `environment.wind.*Apparent` | live; true wind is computed locally (exists) |
| Speed, COG, SOG, heading, position, time | 128259, 129026, 127250, 129025/129029, 126992 | `navigation.*` | live; clock works |
| XTE, DTW, course | 129283, 129284 | `navigation.courseRhumbline.*` | live only if the plotter sends them |
| Autopilot | 127237 plus vendor-proprietary | `steering.autopilot.*` | FAIL in v1 |
| Tide | none | `environment.tide.*` (plugin) | FAIL |
| Anchor | none | `navigation.anchor.*` (plugin) | FAIL, with local alarm |

The table lives in one place (`n2k` path table) and doubles as the answer to "can CAN supply this path", which the setup and tests use.

## Degradation: widget by widget

Simple readouts, gauges, tank bars and Power rows already show the red cross and `<LABEL> FAIL` when a value is invalid or stale (`ui_gauge`, `ui_field_set_fault_text`), and `bd_invalidate_source` makes SK values invalid on disconnect. Those need no change except the audit below. The compound widgets need work:

| Widget | Today | Change |
|---|---|---|
| **Anchor page** | `AM_NO_DATA` mode when not live | Whole page FAIL when SK is not live: red cross, `ANCHOR FAIL`, and a line saying whether an anchor was armed. Role not offered in CAN-only (config load skips it with a logged warning; setup's ADD list omits it; no Glance tile). |
| **Anchor Glance tile** | `GT_NO_DATA` | FAIL state; ALARM while the watch-lost alert is up (see Anchor). |
| **Tide chart and tide line** (Depth page) | quietly falls back to the no-tide layout, by design ("a missing tide must not fault the role") | Keep that when the tide plugin has never sent anything and SK is live (no plugin installed). Show a FAIL flag in the tide region when tide has been seen this session or SK is configured but not live. Depth digits stay live from CAN. In CAN-only, the tide region is not drawn. |
| **Navigation** | per-field `XXX FAIL` text; true wind computed locally | Already correct for fields. Check that COURSE, XTE, DTW and AP HDG each flag independently and the compass does not freeze with stale course data. |
| **Power summary flow diagram** | nodes drop out when invalid; DC LOAD = IN − BATTERY | Check DC LOAD flags `LOAD FAIL` when an input node is FAIL rather than showing a wrong number (it does for solar today). Solar, shore and inverter nodes come from SK-only paths, so they FAIL in fallback. |
| **Glance tiles** | `GT_NO_DATA` per tile; history tiles record from boatdata | History keeps recording from whichever source writes, so trends continue through a switch. SK history backfill (12 h) is lost on a cold CAN-only start; tiles show a shorter trend. |

**Audit gate (phase 1):** extend `snapshot_smoke` with a `-fallback` render of every page: all SK-only paths invalidated, only CAN-suppliable paths live. A reviewer looks at each PNG. Any widget that shows a stale number, a blank hole or no flag is a bug.

## Alerts and the top strip

- Today the strip says `SK ALERTS OFFLINE` and `SK OFFLINE`. Change the alert text to **`ALARMS DISABLED`** whenever alerts are not coming from Signal K: CAN-only (steady, always) and SK + CAN while SK is down. Keep a separate source indicator that can show SK and CAN states (a second dot or `CAN` label); `ui_src_t` is SK-only today.
- `alerts_source_up(false)` already clears SK alerts. Local alerts (`alerts_upsert_local`) survive it; that is the mechanism the anchor alarm uses.
- Open wording question: while the anchor watch-lost alarm is up, `ALARMS DISABLED` is slightly untrue. The alarm banner is the answer to that, since it is on screen with sound. Probably leave it.

## Anchor

The user rule: Signal K owns the watch, so the display can only detect that the watch is gone, and it must alarm when an armed anchor loses its watch.

Already built (`components/roles/src/anchor_watch.c`):
- `ANCHOR WATCH LOST`: raised as a local alarm (audible, survives SK going down) when an anchor was seen set in this session and its data has been stale for 60 s (`app.c:203`).
- `ANCHOR WATCH NOT RESTORED`: after a reboot, the SD record says an anchor was armed and the server does not show it within 120 s.

So an SK drop with an armed anchor already alarms after about 60 s. Work needed:
1. Keep the SK-only meaning of `source_live` passed to `anchor_view_build`. When CAN arrives, nothing may pass "any source live" there, or the watch would not notice SK is gone. Add a test with CAN live, SK down.
2. Make the cause visible: `ANCHOR WATCH LOST: SK OFFLINE` when the SK connection is down, against the plugin going silent while SK is up. Different fix for the skipper.
3. Decide the delay (below). 60 s was chosen to ride out plugin hiccups; a dropped connection is a clearer signal than a quiet plugin.
4. Scenario in `docs/testing.md`: arm the anchor, stop the server, check the alarm and the FAIL page, restart, check recovery and that ACK stays local.

Possible later, explicitly not now: store the anchor position and radius in the SD record and run a circle-only local watch from CAN GPS. Polygon and sector zones would stay SK-only.

## Memory

Internal RAM is the constraint (`docs/hardware/boards.md`, Internal RAM). Estimates for CAN only (no Wi-Fi gateway source):

- TWAI driver, ISR and 32-frame queue: about 1 to 2 KB.
- RX task stack: about 3 to 4 KB; can be smaller with a measured high-water mark.
- Fast-packet reassembly (about 16 slots × 223 B): about 4 KB, `PLATFORM_EXT_BSS` (not touched by the ISR).
- Source table, instance cache and map: negligible; the map is config.

Total about 5 to 7 KB internal. Measured 2026-11-05 after the Anchor role: square4 71 KB free, lcd7 36 KB (lowest 28), lcd7b 31 KB (lowest 24). lcd7b would be at about 25 KB, lowest about 18 KB.

Gate: measure with `status` after each phase on all three boards. Floor: lcd7b lowest-since-boot at least 15 KB with CAN enabled and a full history backfill. If it falls short, move reassembly (already PSRAM) and then the task stack to PSRAM only if the task never runs with flash cache disabled, or share one task.

## Testing

- Host: PGN decoders from canboat sample frames; fast-packet reassembly with out-of-order, duplicate and lost frames; arbitration (SK fresh wins, SK silent hands over, SK replay with an old timestamp does not displace fresh CAN, per-path split); the map; the cache round trip; layering.
- Simulator: SocketCAN replay on `vcan0` of a `candump` log (record one on the boat or build from canboat samples). A `tools/n2k-inject` alongside `tools/sk-inject`, with a scenario for each mode.
- Hardware bench: no N2K bus is assumed. Wire two boards CANH to CANH and CANL to CANL with termination; a probe build on one transmits a log (the TWAI driver supports it; production stays listen-only). Check on lcd7 and lcd7b that the USB-off boot path works.
- `docs/testing.md` gets scenarios for: CAN with SK down (fallback screens, `ALARMS DISABLED`, anchor alarm), SK returning (SK takes over, no flicker), and CAN only.

## Phases

Superseded 2026-11-08 for phases 2 to 5: see `docs/specs/2026-11-08-n2k-library-design.md` (core and node, then decoders and simulator source, then TWAI and board hooks, then config). Phase 1 below is unchanged. The original list:

0. **Prerequisites: done 2026-11-05.** Source table, ranks, states and arbitration in `boatdata`, `bd_admit` in `sk_parse`, the app registers Signal K (rank 2) and publishes its state; tests in `test_boatdata` and `test_sk_parse`. The multi-source strip indicator moves to phase 1 with the `ALARMS DISABLED` text, since it has nothing to show until a CAN source exists.
1. **Degradation audit and fixes (can start now):** the `-fallback` snapshots, the Anchor FAIL page and tile, the Tide FAIL flag, the `ALARMS DISABLED` text, the anchor cause message. All testable in the simulator without CAN code.
2. **`n2k` decoder (after the capture):** fast-packet, the PGNs for Engine, Tanks, Batteries, Depth, Wind and Navigation basics; the path table; the `ids` map; host tests.
3. **Sim source:** SocketCAN source and `tools/n2k-inject`; the three modes in the simulator; the instance cache.
4. **Hardware:** TWAI listen-only on square4 first, then lcd7 and lcd7b with the EXIO5 boot path; memory measurements; bench loopback.
5. **Config:** `sources.nmea2000` in the schema and `tools/check_schema_roles.py`; CAN-only role filtering. Unmapped instances are listed on the serial `status` command only; on-device mapping is later.
6. **Later, on request:** a Wi-Fi gateway transport calling `n2k_feed_frame` or `n2k_feed_pgn`; more PGNs (autopilot, solar, environment); a local circle anchor watch.

Phase 1 is deliberately before any CAN code: it is the part with the most design risk, and it improves the SK-down experience today.

## Decisions (2026-11-05)

1. **Anchor loss delay: 60 s, unchanged.** It gives Signal K time to recover from a restart or a flipped switch. The watch-lost alarm keeps its current timing; only the cause text changes.
2. **Unmapped instances in SK + CAN: dropped and listed.** The expected setup is Signal K reading the CAN bus and turning every PGN into a path, so CAN fallback fills paths Signal K already has. The map and the cache exist for the cases where names differ.
3. **Instance map: config file only in v1.** On-device mapping is a later step, after the config-file version has been used on a real boat.
4. **Default ids are unverified, and the bench has no data yet.** The boat has few sensors today; the engine gateway and the Cerbo arrive in the week of 2026-11-09. Phases 0 and 1 need no real data and can start now. Nothing that depends on naming or PGN coverage (phase 2 onward) starts before the capture below.

## Capture (deferred)

The capture is no longer a gate (2026-11-08). When the engine gateway and Cerbo are connected, still record `candump -L` for a few minutes with the engine running and stopped, and Signal K's output for the same period (paths under `propulsion`, `tanks`, `electrical`, `environment`, with ids and units), plus any provider overrides. It verifies the default ids and PGN tables, and the `candump` log doubles as the replay input for the simulator and host tests. Open item: whether the Cerbo outputs NMEA 2000 at all or reaches Signal K only through `signalk-venus`; if the latter, the Power role's CAN fallback stays FAIL.

## Remaining open item

- **Power role in CAN-only:** accept FAIL for Victron solar, inverter and charger, or add the Victron N2K PGNs later. Settle it after the capture shows what the Cerbo puts on the bus.
