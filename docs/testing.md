# Testing

## Host tests and snapshots

```sh
cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON
cmake --build build/sim
ctest --test-dir build/sim --output-on-failure
tools/check-layering.sh
```

`snapshot_smoke` renders every page at each size and palette into `build/sim/snap/`. Engine is rendered with fixture data twice: `-live` and `-fault`. The fault render has the port coolant sender failed. Power is rendered from a Victron fixture (`-live` on every page, and `-fault` on Summary with the solar current invalid). Engine page 1 is also rendered `-alarm` (port coolant alarm banner) and `-alarm-long` (a 96-character emergency message, wrapped to two lines and truncated, with all three buttons still on the banner); `alerts-list.png` shows the alert list open over two alerts. The Glance page is rendered `glance-p0-live.png` (every tile, with twelve hours of synthetic history behind the Power trend and the water change) and `glance-p0-fault.png` (the house charge, the port coolant and a water tank sender failed). Every snapshot shows the clock (02:14) and the live source dot.

## The Navigation page and its tile

An HSI-style compass, heading up: the DTK line with the cross-track shift, the COG triangle, the autopilot bug and badge, apparent and true wind outside the rim, set and drift. `tools/scenario.sh up`, then `tools/scenario.sh navigation-underway` (180 s). The boat's routine values include the heading, wind, speed through water and SOG; the overlay adds a plotter course and an engaged autopilot (`course` and `autopilot` groups). The server's true wind and the current are in the baseline too; with `signalk-derived-data` installed the plugin publishes the same paths and the two sources overlap. Compare with the snapshots in `build/sim/snap/` (`navigation-p0-<state>.png`, `glance-p0-live.png`).

| Check | Expect |
|---|---|
| Routine | Heading 047, rim marker for apparent wind 38° starboard (yellow, filled) and true wind 61° (orange, hollow); `APPARENT 38° STBD 14.2 kn`, `TRUE 61° STBD 10.0 kn`, `SOG 6.4 kn`, `SET 137° DRIFT 1.2 kn` in small grey type. Courses come only with the overlay |
| With the overlay (0-30 s) | The magenta DTK line through the centre on 070°, its arrowhead toward the rim, the middle segment shifted left of the others (the boat is right of track by 0.12 nm: one dot), the COG triangle on the inside of the rim at 070°, `COURSE 070°`, `XTE 0.12 nm R`, `DTW` in nm (dropped on the 480 board), the green `AP` badge and `AP HDG 052°` in cyan with the notched cyan bug on the outside of the rim at 052° |
| 30-50 s | No course: the line, arrowhead, dots and the course, XTE and DTW readouts go; the COG triangle stays |
| 70-90 s | Autopilot in standby: the `AP` badge, `AP HDG` box and the bug go |
| 110-130 s | The server stops publishing true wind. The display computes it (angle and speed from apparent wind and speed through water, which gives the same 61° and 10 kn). A boat with no speed through water has no True readout or marker; SOG is never used instead |
| From 150 s | The heading source stops: `HDG FAIL` over the heading box, the rose dimmed with no numerals, DTK line, COG triangle or bug; the wind markers and readouts stay (they are off the bow) |
| Night | One red for every nav colour. The shapes still tell the markers apart: filled arrow (apparent wind), hollow arrow (true wind), triangle inside the rim (COG), notched rectangle outside (autopilot) |
| Glance tile | `NAVIGATION`: true wind speed (apparent, with `APP`, when the server gives none), the change over the last hour, the largest minute sample, the 12 hour trend. Missing wind is `NO DATA` |

Unverified until the first bench run: the sign of `navigation.course.calcValues.crossTrackError` (one constant, `NAV_XTE_RIGHT_POSITIVE` in `nav_state.h`), that the chart plotter or the server's course provider publishes `bearingTrackTrue` on this boat, the autopilot's state strings and target paths (legacy `steering.autopilot` or the v2 API), the exact paths `signalk-derived-data` publishes for true wind and current, whether the notch on the autopilot bug is legible at 19 px, and whether 13 px type is readable on the 480 board. Magnetic variation is 0 in the baseline so the display can make a true heading from `headingMagnetic`.

## Against a real Signal K server

The test server is a local Signal K (2.35 at the time of writing) on `localhost:3000`.

1. Get a write token once:
   ```sh
   python3 -I tools/sk-inject/inject.py access
   ```
   Approve **boat-instruments test injector** with read/write in the admin UI (Security → Access Requests). The token goes to `tools/sk-inject/.token`, which is git-ignored.
2. Start the boat. It runs in the background and sends routine values for every path the displays cover (underway: both engines running, house bank charging, six tanks part full; `up --profile anchor` for engines off, `up --with three-bank` for the three-bank paths, `up --with venus-system` to add the Cerbo's `electrical.venus.dcPower`, which the Summary then uses for LOAD):
   ```sh
   tools/scenario.sh up          # also: down, status, log
   ```
   Stopping the boat leaves the last values on the server (Signal K keeps them), so a fault or alarm in progress stays; start the boat again to put routine values back.
3. Play an overlay on top of it (`tools/scenario.sh` with no name lists them; a unique prefix is enough):
   ```sh
   tools/scenario.sh two-engines
   tools/scenario.sh coolant-null
   ```
   Overlay events are logged by the boat (`tools/scenario.sh log`). Several overlays can run at once.

   **Alarms come from Signal K, not from the tool.** An alarm step looks at the zones on the path, creates a default set if there are none (listed by `tools/scenario.sh zones`, left in place), then drives the value across a zone boundary; the server raises, escalates and clears the notification. Where the server already has zones on the path (the house battery charge, fuel level, black water level), those are used and never edited, and the alarm text is theirs. A path whose zones lack the requested state is reported and left alone. Alarms that a plugin owns (the anchor) are not zone alarms: the overlay sets the plugin up with `post`/`put` steps and moves the boat with `move` steps, and the plugin raises the alarm. A `post` or `put` that the server refuses (a 401 from the anchoralarm plugin means the token lacks the permission that plugin wants) is logged and the overlay carries on. One alarm by hand: `tools/scenario.sh alarm propulsion.port.temperature alarm [hold-seconds]`, and `tools/scenario.sh clear <path>`. The loop is checked by `tools/sk-inject/live-check.sh`.
4. Watch it:
   ```sh
   ./build/sim/sim --board sim800                                 # window
   ./build/sim/sim --capture out.png --after 15 [--page 1]        # headless, saves the screen
   ./build/sim/sk_probe localhost propulsion.port.revolutions     # raw values
   ```

### Expected

| Check | Result |
|---|---|
| No engine data on the server | `SK LIVE`, every Engine field flagged (`RPM FAIL`, `COOLANT FAIL`, ...) |
| `two-engines` | Both engines discovered, PORT and STBD columns, values moving |
| `coolant-null` after 10 s | Port `COOLANT FAIL` only. Starboard coolant, oil, volts, and both RPMs stay live |
| `rpm-pickup-fails` after 5 s | `PORT RPM FAIL` only; oil pressure says the engine is running, so everything else stays live |
| `key-off` after 5 s | Starboard dim `OFF` (no crosses), hours still shown; port live |
| `controller-dropout` after about 8 s | Every port field flagged (silent after running); starboard live |
| `tanks` after 20 s | Six tanks, strips side by side, fresh water draining slowly. `FRESH 2` shows `LEVEL FAIL`; the others stay live. Only `FUEL` has a volume line (110 L) |
| `victron-power` | Power Summary is a flow diagram: SOLAR, SHORE and ENGINE nodes (only those bound; SHORE also when the inverter reports shore power) feed a bus dot, which feeds the battery and the DC LOAD; the battery icon fills to the SOC, which is the `BATTERY` readout (from `capacity.stateOfCharge`). Arrows carry chevrons that slide from source to destination, faster and heavier for larger flows, labelled `161 W to battery` (wide) or `161 W` (square). A flow of 0 W is a dim static stub. DC LOAD = IN - BATTERY. After 30 s the battery discharges: the `from battery` arrow animates and `to battery` goes to a 0 W stub; Batteries shows `4:00` instead of `CHARGING`. After 45 s solar current is null: `SOLAR FAIL` and `LOAD FAIL`, those arrows disappear, the other nodes, BATTERY and SOC stay live, the Charging solar row flags `AMPS FAIL` and `WATTS FAIL`. At 60 s solar current returns and the battery is at 5 %, which is inside the house battery's alarm zone on the server, so Signal K raises its low-charge alarm; at 80 s the battery is back at 95 %. Check the arrows move smoothly on the boards (they redraw 16 steps per cycle). The paths are the signalk-venus defaults and may change after the Cerbo capture |
| `three-bank` | Needs a power role with `banks` and `links` (below). Summary is the house view: SOLAR is solar 288, SHORE is the charger's main output, ENGINE is the start-to-house converter (dcdc 1), and DC LOAD is house IN, minus the house battery, minus what the nav converter (dcdc 2) draws; the Cerbo's `dcPower` is not used. At 20 s: BATTERY 82 %, IN about 255 W, 161 W into the house battery, 58 W to nav, so DC LOAD is about 35 W. After 30 s the nav bank discharges and the nav converter draws more from house (about 94 W in) while the house battery's own current falls to 4 A. The nav draw never counts as house load. After 50 s the charger current is null: `SHORE FAIL` and `LOAD FAIL`, the other nodes and BATTERY stay live. Back at 70 s. ENGINE reads `START > HOUSE` with a battery-and-arrow icon filled to START's SoC (96 %), and its watts are dcdc 1's output (109 W). Wide: a NAV battery bottom right (filled to 90 %) reads NAV LOAD, with an arrow from the house battery labelled `59 W to nav`. Square: a text line `NAV 90 %  40 W` under the diagram. Page 2 is the Banks page: START, HOUSE, NAV with an arrow labelled with each converter's watts between them, each bank's sources under it (`FLOAT CHARGER`, `ALTERNATOR 0 W`; `CHARGER 82 W`, `SOLAR 48 W`). Batteries lists HOUSE, START, NAV with their sources; Charging groups its rows by destination (`TO HOUSE`, `START > HOUSE`, `TO START`, `HOUSE > NAV`). After 50 s the charger current is null: `SHORE FAIL`, `LOAD FAIL` on the Summary, `CHARGER FAIL` in HOUSE's source line on the Banks page; NAV stays live. Snapshots: `power-p<n>-banks-live` and `-banks-fault` in every size and palette (the fault render also fails the nav shunt's current: `NAV FAIL`). Every path is unverified until a Cerbo capture |
| `alarm-coolant` at 10 s | The port coolant is driven up through its zones (the first run creates default zones: alert from 95 °C, alarm from 100 °C), so the banner arrives about 2 s after the event, raised by the server. Red banner under the strip, flashing 1 Hz: `Port coolant high`, ACK, SILENCE, and GO (GO opens Engine page 1, the detail page). The port coolant value turns red. Buzzer on boards that have one (Square 4.0). The notification appears in `GET /signalk/v2/api/notifications` |
| `alarm-fuel-low` | The boat's routine fuel level is nominal, so any previous fuel alarm has cleared; at 8 s the level is driven through the fuel zones (alert, then alarm) and a red banner appears (text from the server's fuel zones, or `Fuel low` from the defaults), with GO. Six tanks: the extra fresh-water tank puts fuel on the second square page. GO opens that page (FUEL and GREY), not the first tanks page (BLACK and the three FRESH tanks). A wide screen has one tanks page, so GO opens that. The fuel strip is low and red. The alarm stays up through the end of the 80 s run |
| `alarm-coolant`, tap ACK | The banner goes away and a bell with `1` appears in the top strip, left of the status text; the server shows the notification acknowledged. Tap the bell for the list, where the alarm stays with its ACK hidden. Tap SILENCE first and the buzzer stops, the server shows silenced. If the ACK did not reach the server the banner stays, steady, with `ACK NOT SENT` |
| `alarm-coolant` at 40 s | Banner (or bell, if acknowledged) disappears and the coolant value returns to white |
| `warn-tank-low` at 10 s | The first fresh water tank is driven to 10 % (default zones: warn below 15 %). No banner. Amber `1` marker left of the status text; tap it for the list (`WARN`, `Fresh water low`, age, ACK). The fresh water tank value is amber |
| Server stopped with an alarm showing | Banner gone, `ALARMS DISABLED` in the strip, `SK OFFLINE` status, buzzer silent |
| Server stopped | `SK OFFLINE`, every field flagged within about 2 s |
| Server restarted | `SK LIVE`, fields clear on the next delta |

### Multi-bank power config

For `three-bank`, give the simulator a `config.json` whose power role is:

```json
{ "role": "power",
  "banks": [ {"id": "279", "name": "HOUSE", "kind": "house"},
             {"id": "280", "name": "START", "kind": "start"},
             {"id": "281", "name": "NAV",   "kind": "nav"} ],
  "links": [ {"src": "charger:276", "output": 0, "to": "house"},
             {"src": "charger:276", "output": 1, "to": "start"},
             {"src": "alternator:0", "to": "start"},
             {"src": "solar:288", "to": "house"},
             {"src": "dcdc:1", "from": "start", "to": "house"},
             {"src": "dcdc:2", "from": "house", "to": "nav"} ] }
```

Run `./build/sim/sim --board sim800 --config <dir> --capture out.png --after 20`. A topology that does not validate is reported on the setup screen and the role runs single-bank. Design: `docs/specs/2026-11-04-multi-bank-power-design.md`.

### Milestone 4 live results

Simulator against Signal K 2.35.1 on `localhost:3000`, 2026-11-03, `sim800`, 800x480, day palette. Acknowledge and silence from the panel need `"token"` under `sources.signalk` in the sim's `config.json` (the injector's token works); without it the banner shows `ACK NOT SENT`. The headless sim has no pointer, so ACK, SILENCE and the alert marker were clicked by a scratch, uncommitted hook that sent `LV_EVENT_CLICKED` to `ui_alerts_part(...)`. The server was not stopped: "server stopped" rows used a throwaway local TCP proxy (sim pointed at it, proxy killed, and for the restart row started again).

| Row | Result |
|---|---|
| No engine data | PASS: `SK LIVE`, every Engine field flagged |
| `two-engines` | PASS: PORT and STBD columns, values moving |
| `coolant-null` | PASS: port `COOLANT FAIL` only |
| `rpm-pickup-fails` | PASS: `PORT RPM FAIL` only |
| `key-off` | PASS: starboard `OFF` without crosses, hours 1221.0 h shown, port live |
| `controller-dropout` | PASS: every port field flagged, starboard live |
| `tanks` | PASS: five tanks, `FRESH 2` `LEVEL FAIL`, only `FUEL` has a volume line (110 L) |
| `victron-power` | PASS on the old readout-and-strip Summary (the diagram has only been checked in the simulator snapshots), re-run after the final-review fixes (SoC and time remaining on the `capacity.` paths, `DC LOAD` label). Summary at 20 s: IN 658 W, BATTERY 161 W, DC LOAD 497 W, SOC 82 %, `SHORE`; Batteries at 35 s: −15.0 A, −201 W, SOC 82 %, `4:00`; Summary at 50 s: `IN FAIL`, `LOAD FAIL`, BATTERY −201 W and SOC 82 % live. The Charging page was not re-captured |
| `alarm-coolant` at 10 s | PASS: red banner flashing, `Port coolant high`, ACK, SILENCE, GO; port coolant value red; notification on the server |
| `alarm-coolant`, ACK | PASS: banner steady red; server `acknowledged: true`, `method: []` |
| `alarm-coolant`, SILENCE | PASS: server `silenced: true`, `sound` dropped from `method` |
| `alarm-coolant` at 40 s | PASS: banner gone, coolant white |
| `warn-tank-low` | PASS: no banner, amber `1` marker, list shows `WARN`, `Fresh water low`, age, ACK; tank value amber |
| Server stopped with an alarm | PASS: banner gone, `ALARMS DISABLED`, `SK OFFLINE` (buzzer is hardware only) |
| Server stopped | PASS: `SK OFFLINE`, every field flagged within 2 s |
| Server restarted | PASS: `SK LIVE`, fields live again |

**Power discovery bug (fixed):** `wanted()` in `components/source_sk/src/sk_parse.c` used to compare only the top-level segment with multi-segment prefixes, so no Power instance was discovered. It now matches on segment boundaries and descends into ancestors of a prefix. Re-run against the server: IN 668 W, BATTERY 161 W, LOAD 507 W, SOC 82 %, `SHORE` marker, no FAIL, with the committed code.

Notes for repeat runs: kill the injector only after its clear event, or write `null` to the notification paths first. The server keeps a notification's acknowledged state when the same alarm is rewritten, and answers a second acknowledge with HTTP 400 `Alarm already acknowledged!`. `GET /signalk/v2/api/notifications` also keeps stale entries for cleared paths.

## The Glance page, the clock and history

Play `tools/scenario.sh glance-mixed` (or `two-engines` for the clock), then check on each board. Compare with the snapshots in `build/sim/snap/`.

1. **Tiles match the alerts.** Each tile's edge and word agree with the alert list: an alarm on `propulsion.port.temperature` turns the Engine tile to ALARM; a warn zone on a tank turns Tanks to WARN. A tap on a tile opens that role's first page; the swipe back returns.
2. **Missing data is never calm.** At 2 minutes `glance-mixed` sends `null` for the house charge: the Power tile shows NO DATA, a red cross and no number. Stop the injector and the Signal K server: tiles that read it show NO DATA, the source dot becomes a ring with a cross and `SK OFFLINE` appears.
3. **The clock.** It shows the Signal K time in the configured zone (Display setup, Time zone, in half-hour steps). It is not drawn until a `navigation.datetime` arrives, and it keeps running after Signal K drops, until 10 minutes have passed with the source down, when it disappears.
4. **History after a reboot.** With a Signal K history provider recording the paths, reboot the board and open Glance: the Power trend is full width at once (axis `-12 h`) and the Tanks tile shows `WATER -n% in 6 h`. Without a provider the trend starts empty and the axis shows only the time since boot. `GET /signalk/v2/api/history/_providers` lists the providers; the bench server (2.35.1) listed one, `kip`, and returned empty `data` for the simulator's paths, so confirm on the boat that the provider records `electrical.batteries.*.capacity.stateOfCharge` and `tanks.*.currentLevel`.
5. **Small board.** On the 480x480 board the second context line and the axis labels are dropped. Judge from across the saloon whether the 14 px context line is readable; if not, drop it there too.
6. **Night.** The night palette is red only: OK is the dim red, a warning or alarm the bright red, and the state word always says which.

## The Anchor page, its tile and the watch

Needs an anchor plugin on the server (`hoekens-anchor-alarm` or `signalk-anchoralarm-plugin`; set `anchor.plugin` in `config.json` to match, default `hoekens-anchor-alarm`), a position (a GPS, or `tools/sk-inject` publishing `navigation.position`), and the display's token with write access. `tools/scenario.sh anchor-holding` plays a boat at anchor without a plugin. Compare with the snapshots in `build/sim/snap/` (`anchor-p0-<state>.png`, `glance-p0-anchor-<state>.png`).

| Check | Expect |
|---|---|
| No anchor, plugin up | `NOT SET`, grey; the button reads DROP and the limit shows 30 m. Tap `+` twice: 40 m, amber, nothing sent. Tap DROP: `dropAnchor` is sent with radius 40; within seconds the plugin publishes the anchor and the page goes to `HOLDING`, green, with the ring and the boat |
| Radius | With an anchor set, `+`: the limit turns amber at once and 1.5 s after the last tap the plugin shows the new value and the colour returns to normal. Hoeken: a `setZone` with the radius; anchoralarm: a PUT of `maxRadius`. A polygon or sector zone (Hoeken): `ZONE: POLYGON`, `-` and `+` disabled, Drop and Raise still work |
| Raise | Press and hold RAISE: a bar fills the button over 1.5 s and the raise is sent when it fills. Letting go early cancels. The page returns to `NOT SET` |
| Dragging | `anchor-holding` (Hoeken; `anchor-holding-anchoralarm` for the other plugin), which sets the anchor up the way the DROP and radius buttons do (POST `dropAnchor`, PUT `maxRadius`), swings the boat 28-30 m out, then from 40 s moves it to about 52 m, past the 45 m limit; the plugin raises the alarm itself from the position (the tool writes no notification), observed about 6-10 s later as the boat crosses 45 m, and clears it a few seconds after the boat returns at 70 s. The radius PUT names its source (`anchoralarm`), because with both plugins enabled the server refuses a PUT that names none. The overlay clears the other plugin's anchor first. Engines are off (`anchor` profile): Hoeken with `enableEngineCheck` drops its watch if an engine is turning. Expect `DRAGGING`: `DRAGGING`, red ring, the banner and buzzer; ACK moves it to the bell. The boat dot stays on the plot, the scale growing to keep it in view. At 70 s the alarm clears |
| Track | After a few minutes the plot shows the swing since the anchor was set. With a plugin that republishes the anchor position continuously the set time may read as just now and the track will be short: note which plugin does |
| Wrong plugin or no access | `NO PLUGIN` (404) when the configured plugin is not installed; `NO ACCESS` for a token without write; `OFFLINE` with the server down. Shown on the button for 5 s. A plugin that answers 200 but publishes nothing: `NOT CONFIRMED` after 10 s |
| Missing data | Stop the plugin: after 90 s `NO DATA`, grey, controls dimmed; after 60 s more the buzzer and `ANCHOR WATCH LOST` on the banner. The alarm clears when data returns |
| Restart | Restart the Signal K server with an anchor set (both plugins restore it): the page shows `NO DATA` while it is down, then `HOLDING`. `ANCHOR WATCH LOST` appears only if it stays down past 60 s |
| Power cut | Power-cycle the display with an anchor set: `RESTORING` after boot, then `HOLDING` once the plugin shows the anchor. With the anchor raised on the server first (plugin's own page, display off): `ANCHOR WATCH NOT RESTORED` as soon as the server answers, ACK silences it, the record stays; hold RAISE to clear it. With the server off for over 120 s the same alarm |
| No SD card | `NO SD` on the page; the boot check is off, `ANCHOR WATCH LOST` still works |
| Glance tile | The tile follows the page: green `HOLDING` with the plan view, amber `WARN`, red `DRAGGING`, `NO DATA` and `RESTORING` never green. On the 480 board the second context line is dropped |

Unverified until the first bench run: that each plugin republishes `currentRadius` often enough (the stale limit is 90 s), what Hoeken publishes with no anchor set (if nothing, `NOT SET` waits for the first drop), what the display sees for a moment after a server restart, and the plugins' reply to a drop with no GPS position.

## The Depth page and its tile

Depth is not a default role: add `{ "role": "depth" }` to `config.json` (it is in `docs/config.example.json`). `tools/scenario.sh depth-sounder` and `alarm-shallow` play on a boat whose routine values include a sounder (`environment.depth.belowTransducer`, `belowKeel`, `belowSurface`) and a tide plugin (`environment.tide.heightNow`, `heightHigh`, `heightLow`, `timeHigh`, `timeLow`; the bench tide runs a real 12 h 25 m cycle between 0.4 m and 3.2 m, so the next high and low move with the clock and the tide graph changes over hours; the times are always ahead of now). The default zones alarm under 1 m and warn under 1.5 m. The page shows the depth in big digits (never more than `99.9` or `999`), then the chart: 6 h of depth and 6 h of the tide's forecast of it, on a scale in the display unit. The forecast starts from the measured tide height, so in 10 ft of water with the tide at +3 ft and a high of +10 ft the line climbs to 17 ft on the scale at high water. The shallow zones are drawn on the same scale. If the forecast line enters a warn or alarm zone of the depth shown, the whole line (page and tile) is drawn in a lighter warn (amber) or alarm (red) colour and an otherwise `OK` tile goes amber `LOW TIDE`, never red: this is advisory, from the device's own tide curve, and only Signal K raises alerts and alarms. A Signal K state on the tile always keeps its own word (`low-tide-warn` and `low-tide-alarm` on the page, `glance-p0-low-tide`). The HIGH and LOW rows with their times stay at the foot. The digit fonts are generated (`tools/gen-digits-font.sh`). Compare with the snapshots in `build/sim/snap/` (`depth-p0-<live|tide-partial|surface|fault|no-tide>.png`, and the Depth tile in every `glance-p0-*.png`).

| Check | Expect |
|---|---|
| Naming | The page and the tile always say what the number is measured from: `BELOW TRANSDUCER` (preferred), else `BELOW KEEL`, else `BELOW SURFACE`. A reading older than 10 s is skipped, so the name changes with the number and never lies |
| `depth-sounder` at 10 s | The transducer reading is `null`: the number changes to the keel depth and the line above it to `BELOW KEEL`. The tile's first context line follows |
| `depth-sounder` at 25 s | Keel and surface fail: the page shows a red cross and `NO DEPTH`; the tile is amber `NO DATA`, no number |
| Tide | `RISING 1.8 m` (or `FALLING`) on the page with the next high and low and their times in the display's time zone; on the tile (wide boards) `RISING 1.8 m to HW 14:32`. The direction is whichever of the next high or low comes first, by the server's clock. With no clock, or only past times (a stalled plugin), there is no direction, only `TIDE 1.8 m`. A height older than 30 minutes is dropped. A boat with no tide plugin shows no tide block at all |
| `alarm-shallow` at 10 s | The depth is driven under 1 m: Signal K raises `Shallow water`; banner, buzzer where fitted, the depth value turns red and the tile goes `ALARM`. ACK as for any alarm. At 40 s it clears. The tide never raises an alarm |
| 480 board | The tile keeps only the first context line, so it shows the reference (`BELOW KEEL`) and the tide is on the page only |

## Capturing a boat's server

Power reads the signalk-venus default paths. To compare them with a real Cerbo, run this on any computer that can reach the boat's Signal K server:

```sh
python3 -I tools/sk-capture.py <boat-server>[:port] capture.json [--token-file token.txt]
```

It saves `GET /signalk/v1/api/vessels/self` as pretty JSON (port 3000 by default) and exits non-zero with a message on a connection or HTTP error. Use `--token-file` only if the server demands a token. Compare the `electrical` tree in `capture.json` (batteries, solar, chargers, inverters, dcdc, alternators, venus) with the slot paths in `components/roles/src/role_power.c` and the Power Groups table in the milestone 4 design, and correct the slots, `tools/sk-inject/scenarios/victron-power.json` and the table where they differ.

## Repeating the Signal K checks

Milestone 4 recorded two server behaviours in the milestone 4 design notes (kept locally, not in the repo). Repeat them after a Signal K upgrade, or against the boat's server.

1. **Fixed-period resend.** Write `electrical.batteries.279.voltage` once with the injector's token. Open `ws://localhost:3000/signalk/v1/stream?subscribe=none`, send `{"context":"vessels.self","subscribe":[{"path":"electrical.batteries.279.voltage","policy":"fixed","period":2000}]}`, and log message times for 20 s with no further writes. Only the first delta arriving means unchanged values are not resent.
2. **Notification acknowledge and silence.** Inject `notifications.propulsion.port.temperature` as `{"state":"alarm","method":["visual","sound"],"message":"Port coolant high"}`. Read its `id` from `GET /signalk/v2/api/notifications`, then `POST /signalk/v2/api/notifications/{id}/silence` and `.../acknowledge` with `Authorization: Bearer <token>`. The token is in `tools/sk-inject/.token` (get one with `inject.py access`). Expect HTTP 200 (no body needed), and a second WebSocket client subscribed to `notifications.*` sees `method` lose `"sound"` and then become `[]`, with `status.silenced` and `status.acknowledged` set.

   Negative checks: `PUT /signalk/v1/api/vessels/self/notifications/propulsion/port/temperature` with a `value` body returns HTTP 404 and changes nothing; a POST with a valid but unknown UUID returns HTTP 400 `Alarm not found!`; without the `Authorization` header, HTTP 401. Also confirm the WebSocket delta's value includes `id` and `status`.

Keep probe scripts outside the repo.

## On a board

Put a `config.json` (see `docs/config.example.json`) at the root of the SD card, then flash:

```sh
idf.py -D BOARD=lcd7 -B build/lcd7 -p /dev/ttyACM1 flash monitor
```

Run the same scenarios. The board must be on a network that can reach the Signal K server. `"host": "auto"` finds it by mDNS.

### Display reset (bench, all three boards)

The RGB scan occasionally loses sync: the top status bar shows at the bottom and the picture is corrupt until it recovers on its own. The quick panel (tap the top strip) has RESET SCREEN (RESET on the 480 board), which restarts the panel scan and redraws. Not yet run on a board.

| Check | Expect |
|---|---|
| Wait for a corrupted frame, open the quick panel, tap RESET SCREEN | The panel closes, the picture is whole again within a second |
| RESET SCREEN on a healthy screen | A brief redraw, no lasting flicker, touch still works |
| Set `display.reset_after_swipe` to `true` (`config set`), swipe between pages repeatedly | One restart about 400 ms after you stop swiping (not one per swipe); note any visible flicker. If it flickers, leave the flag off |
| Corruption still happens and RESET SCREEN does not fix it | The restart hypothesis is wrong; try a lower PCLK (`docs/hardware/boards.md`) |

## Emergency: MOB, fire, flooding, SOS

Add `{ "role": "navigation" }` to `config.json`. The emergency script is Navigation page 2 (swipe up from the compass). Contract with the signal controller: `docs/signal-contract.md`. Run `tools/scenario.sh emergency-mob`, `emergency-mob-nopos` and `emergency-fire` on top of the routine boat; they send the notification as a delta (the `notify` op), the exception to "alarms are never written as notifications" because these paths have no zone. Every button, CANCEL included, needs a one second hold: a tap sends nothing, and the button fills while it is held.

| Scenario | Each display should show |
|---|---|
| `emergency-mob` at 10 s | The alarm banner and buzzer; the SOS button (red) replaces the role name on every page. The page does not jump by itself: GO on the banner opens the Navigation compass with the dashed MOB arrow, `MOB 046° 4.3 nm` (illustrative) and `MARKED n s AGO`, and the present position stays in the lower right. At 60 s everything clears |
| `emergency-mob-nopos` at 10 s | The banner; the MOB readout flagged `NO POSITION`, `MOB POSITION UNKNOWN` under it, no arrow. The display never uses its own position |
| `emergency-fire` at 10 s | The banner; the display jumps to the script page by itself, once (swipe away and it stays away), reading the fire call (`We have a fire on board. Over.`). In the quick panel (tap the top strip) the FIRE button is filled. At 30 s flooding as well: the script keeps showing FIRE (one declared nature). At 60 s both clear |
| SOS, from any page, while a MOB is declared | One tap goes to the script page. It still shows with Signal K stopped |
| Tap the top strip, then hold MOB for 1 s in the EMERGENCY row at the bottom of the quick panel (Signal K up, Navigation role configured) | The panel closes. A MOB goes to the Navigation compass (the target to steer to), FIRE and SINKING to the script page: the same pages the alert's GO button opens on the other displays.  The MOB is declared at once on this display (compass target, SOS). `SENDING...` then `SENT`; every other display raises it. Check on the server what arrived: `GET /signalk/v2/api/notifications` shows `notifications.mob.<uuid>` (state `emergency`) with `value.data.position` and `value.data.time` from the moment of the press, and the waypoint `MOB hh:mm:ss` |
| Release before 1 s | Nothing is declared or sent |
| Hold MOB with the server stopped (or the token read-only) | The display declares it locally, shows `NOT SENT - TELL HELM` and the buzzer; with a read-only token `NO WRITE ACCESS`. Nothing is retried. Pressing again is a new event with a new position |
| Signal K stopped | The EMERGENCY row (MOB, FIRE, SINKING, CANCEL) is not in the quick panel at all. The script page still shows the call for any emergency already declared (and `No emergency declared...` otherwise) |
| Hold CANCEL (quick panel, shown while anything is declared, including flooding raised by another display) | `PUT /notifications/<uuid>` with `{"state":"normal"}`: the emergency clears on this display and on the others once the server has it (the notification stays in the server's list as `normal`; a MOB must not be raised a second time). If the cancel cannot be sent (no id yet, offline, refused) it stays declared and says `CANCEL NOT SENT` |
| The script page (Navigation page 2) with an emergency declared | Two numbered steps for someone who has never used a VHF (`press and hold the red DISTRESS button for 3 seconds`, `then speak into the microphone`), then the call, one thing to say per line, in the largest type that fits (36 pt on the 800 board, 28 pt on the 480): `MAYDAY MAYDAY MAYDAY` / `This is <name> <name> <name>` / `Position four eight degrees three two decimal four two minutes north` / `one two three degrees one zero decimal six zero minutes west` / `We have a man overboard. Over.` There are no buttons and no numeric footer on this page (the numbers are on the compass page). The position is the boat's live fix, spoken in degrees and hundredths of a minute (about 60 ft). No fix reads `(NO GPS FIX - read the position from the chartplotter)`, an unknown name `(BOAT NAME, said three times)`. Nothing declared: `No emergency declared. Tap the top strip, then hold MOB, FIRE or SINKING.` and no steps. Read it aloud once: is each line comfortable in one breath? |
| Hold FIRE (or SINKING) in the quick panel. There is no FLOOD or OTHER button; flooding or another nature raised by a different display still shows on the script page and can be cancelled | Raised through `POST /notifications` with `path` `fire` etc. (there is no per-nature endpoint on the server): `SENT`, `notifications.fire.<uuid>` on the server, the other displays jump to their script page |
| A MOB raised by the server's own endpoint (`POST /signalk/v2/api/notifications/mob`, the helm's button) | Every display declares it with the server's position (`value.position`); CANCEL on a display clears it by its id |
| No GPS fix at the press | The MOB is declared with no position: `NO POSITION`, no waypoint, no arrow |
| Restart a display during a MOB | On reconnect it adopts the MOB from the server's notification, with the position from its payload |

Also check on a board, 45 s after boot with the Navigation role: internal free memory with `status` (the script page and SOS add a few KB of LVGL objects while the page is shown) against the figures in `docs/hardware/boards.md`. On the server: write permission for the token (a read-only token shows `NO WRITE ACCESS`). The endpoints and bodies were verified against signalk-server 2.35.1 on 2026-11-06 (`docs/signal-contract.md`); recheck after a server upgrade. The vessel name comes from Signal K's `name` unless `vessel.name` is set.

## CAN bench (NMEA 2000 probe)

What it checks: the `n2k`, `n2k_node` and `n2k_bus` libraries and the `can_twai` driver on real hardware, before any display code uses them. Each board runs a probe image (`CONFIG_BI_DIAG_CAN_PROBE`, in place of the normal app) that claims an address, sends a 28-byte alert (PGN 126983, rolling counter in byte 0) every 5 s, logs every message it hears (at most 20 lines per second), and sends an ISO Request for product information to the whole bus every 20 s (logged as `sent ISO request ...`, with each reply as `rx pgn=126996 ... len=134`), and prints a `can:` status line and a RAM line every 10 s. Two or three boards are enough; each one ACKs the others. A single board alone on a bus gets no ACK (its `failed` count climbs) and proves nothing.

**Build** (`. ~/esp/esp-idf/export.sh` first):

```sh
for b in square4 lcd7 lcd7b; do idf.py -D BOARD=$b -D BI_EXTRA=diag/can-probe.defaults -B build/$b-canprobe build; done
```

**Wire** the boards CANH to CANH and CANL to CANL as a daisy chain with a 120 Ω resistor across CANH and CANL at each physical end of the chain (the 7B has a DIP-switch terminator; use it at one end). Boards powered from the same PC over USB already share a ground; otherwise add a GND wire. Pins are in `docs/hardware/boards.md`.

**Flash and watch.** The 7" boards switch the CAN transceiver in through EXIO5, which takes the native USB pair away: from the moment the probe starts, the console is the UART Type-C port (UART switch on UART1; the 7B's is the CH340 on `/dev/ttyACM2`). So flash and monitor those two through the UART port, and flash square4 over its USB:

```sh
idf.py -B build/square4-canprobe -p /dev/ttyACM0 flash monitor
idf.py -B build/lcd7b-canprobe   -p /dev/ttyACM2 flash monitor
idf.py -B build/lcd7-canprobe    -p <the lcd7 CH343 port> flash monitor
```

`tools/deploy.sh` matches the native-USB serial, so it cannot see lcd7 while the probe is on it. To put a normal image back on lcd7, flash it through the UART port too; if a 7" board is unreachable over native USB, power-cycle it (the CH422G keeps EXIO5 high across a plain reset) or use the UART port.

### Expected

- Within about a second of power-up each board logs `claiming address 0x..`. Addresses differ between boards (the preferred address is `0x40 + (MAC low byte & 0x3F)`); if two boards pick the same one, the lower NAME keeps it and the other moves to 128 or above.
- `can: state=2` on every board (node states: 0 idle, 1 claiming, 2 ready, 3 cannot claim).
- Every 5 s each board logs the others' alert: `rx pgn=126983 src=<their address> len=28 b0=<counter>`, with `b0` counting up by one per alert. Claims (`pgn=60928`) appear at start.
- `rx` and `tx` climb; `refused`, `drop`, `failed` and `bus_off` stay 0. `txerr` and `rxerr` stay near 0.
- No-ACK behaviour: with only two boards on the bus, unplug one. The survivor's alerts get no ACK, so its `failed` count climbs and `txerr` rises to about 128 (error-passive) and stays there; ACK errors do not push a node on to bus-off. On replug the traffic resumes without a reboot, `failed` stops climbing, and the other board's alerts are heard again. With three boards the bus stays healthy when one is unplugged.
- Optional, forces the bus-off path: with two or more boards running, briefly short CANH to CANL at the wires (a few seconds; the TJA1051 tolerates a bus short, but only do this on the bench). A transmitting board's `txerr` runs past 255 and `bus_off` becomes 1; after the short is removed the board recovers by itself, `state` returns to 2 after a new claim (about 250 ms), `node_dropped` shows what was queued, and alerts resume. This matters because the controller must come back without a reboot.
- The `RAM:` line is recorded per board (internal free, lowest, largest block). The normal app is not running, so this is the cost of the CAN stack plus the board bring-up only; the real memory gate (`docs/hardware/boards.md`, Internal RAM) is rechecked when the display uses it.

Record results in `docs/hardware/boards.md` under each board's CAN row.

### Bench notes (2026-11-08)

- A SocketCAN adapter such as the gs_usb ("Geschwister Schneider") one shows up as `can0`, not a device file. Bring it up with `sudo ip link set can0 up type can bitrate 250000` in a real terminal (the `!` prefix has no tty for the sudo password). This adapter does not support `restart-ms`, so after a bus-off, `down` then `up` is the only reset. `candump` comes from the `can-utils` package; `-e` (error frames) cannot be combined with `-L`.
- Terminate only the two physical ends of the chain (120 Ω each); leave the boards' own DIP terminators off.
- Judge the adapter by its state (`ip -d link show can0`), not the berr-counter value, which the driver leaves stale.
- Let the boards talk to each other for the node tests (the probe's own request sender); the adapter is for listening, and a few single requests (`cansend can0 18EA78F0#14F001` asks the node at 0x78 for product info).

## Simulator on a CAN bus

The simulator can read NMEA 2000 from a SocketCAN interface, listen-only (it has no node and never transmits): `build/sim/sim --can <ifname>`. The decoders and default Signal K ids are from memory and **unverified** until a real capture exists; this slice covers depth (128267), apparent wind (130306), battery (127508), engine rapid (127488) and tank level (127505).

- `--can <if>`: the CAN source fills only paths Signal K has already created (the SK + CAN mode). Signal K, when live, wins on every path it writes.
- `--can <if> --can-only`: the CAN source creates its own paths (CAN only mode). Use this with no Signal K server.

Virtual bus, no hardware (needs root once):

```sh
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
tools/n2k-inject.py vcan0 --rate 1 basic          # one terminal
build/sim/sim --can vcan0 --can-only               # another
```

Real bus: bring the adapter up first (`sudo ip link set can0 up type can bitrate 250000`, see "CAN bench") and run `build/sim/sim --can can0 --can-only`. The simulator logs `can: <if> listening` on stderr; the boat instruments' probe images send only alert PGNs, so with those alone it stays CONNECTING.

The `basic` scenario shows depth 12.3 m (surface offset 0.4 m), apparent wind 8 m/s at 40° to starboard, battery 1 at 12.6 V and -8.5 A, engine 0 at 2400 rpm, fresh water 1 at 62 % and fuel 0 at 35 %. Which pages show them depends on the config's roles and ids: the engine appears under the id `0`, the battery under `1` and the tanks under their instances unless the config names them (the instance map is a later step). Stop the injector and the source goes OFFLINE after 5 s and its values turn to FAIL.

`tests/test_can_socketcan` loops a frame through `vcan0` when it exists and skips itself when it does not.

### Live check on the bench bus (2026-11-08)

A board can act as the sensor so the real bus carries instrument data without Signal K: build the probe with the sensors added (`idf.py -D BOARD=square4 -D "BI_EXTRA=diag/can-probe.defaults;diag/can-probe-sensors.defaults" -B build/square4-canprobe-sensors build`) and flash it. It sends the `basic` scenario once a second (depth wandering 12.3 to 13.2 m so updates are visible). Then, with the adapter up:

```sh
build/sim/sim --board sim800 --config <dir with a config.json whose signalk port is unused> --can can0 --can-only --capture out.png --after 7
```

The config must keep the simulator off the local Signal K test server (for example `"signalk": { "host": "127.0.0.1", "port": 3999 }`): Signal K is rank 2 and wins wherever it has a fresh value, so with it running the screen shows its data, not the bus's. Result on the bench: Glance shows Engine 2400 RPM, Power 12.6 V and -8.5 A, Tanks W 62 % and F 35 %, Navigation 16 kn apparent, Depth 40.4 ft. Engine and Power still say NO DATA because this slice carries no coolant, oil or state of charge.

### The display on the bus (hardware)

Needs two boards on the bus: one runs the sensors probe (`idf.py -D BOARD=<b> -D "BI_EXTRA=diag/can-probe.defaults;diag/can-probe-sensors.defaults" -B build/<b>-canprobe-sensors build`), the other the normal firmware with the bus enabled. Enable it over the display board's console (the 7" boards: the UART port; `tools/deploy.sh` flashes the normal image):

```
config set sources.nmea2000.enabled true
config set sources.nmea2000.mode only
reboot
status
```

`status` must show `can: live rx=<climbing> values=<climbing> unmapped=0 bus_off=0` and, in `only` mode, `signalk: offline` (the client is not started). Mode `fallback` keeps Signal K first and fills only paths it has, so with a server up most instance values count as `unmapped` until `sources.nmea2000.ids` maps them. The `ids` map has dots in its keys (`engine.0`), so `config set` cannot write it: edit `config.json` on the SD card (see `docs/config.example.json`). Enabling or disabling the bus needs a reboot. On the 7" boards the bus takes native USB away.

### The instance cache

In `fallback` mode the display writes `instances.json` (and keeps `instances.bak.json`) to the SD card: the engine, battery and tank leaves Signal K has delivered a valid value for. A path is added within 5 s of Signal K delivering it; a path is dropped only after Signal K has been live for a minute without it. At boot, with the bus enabled in `fallback` mode, the cached paths are created empty (the boot log says `can: restored <n> instance paths from the cache`), so the roles find their instances and the bus has places to write even when the server is down. Check it: let the display run with Signal K live for a minute, change `sources.signalk.port` to an unused port with `config set`, `reboot`, and read the boot log. Delete `instances.json` and `instances.bak.json` to make it forget the boat. It is not used in `only` mode or with the bus off.

## The strip's sources and ALARMS DISABLED

The right of the top strip shows the data sources in use. Signal K is a circle; the NMEA 2000 bus, once `sources.nmea2000.enabled`, is a rounded square to its left. Each is a filled shape when live, a hollow ring while connecting (amber) and a ring with a cross when offline (red), so the state reads without colour. In bus-only mode (`mode: only`) the Signal K dot is hidden. The status text names the sources that are not live, using the worse state: `SK OFFLINE`, `SK CONNECTING`, `CAN OFFLINE`, `CAN CONNECTING`, `SK + CAN OFFLINE`. When the strip is crowded (the alert markers, a long title, many page dots) the status text steps aside and the dots carry the state. `ALARMS DISABLED` shows whenever alarms are not coming from Signal K: it is down, or the bus is the only source (then it is steady). On the square board it replaces the status text.

Check in the simulator (`build/sim/sim --board sim480 --config <dir> --can can0 [--can-only] --capture out.png --after 6`): Signal K live with the bus not yet live shows the hollow bus square (and `CAN CONNECTING` where there is room); a config whose Signal K port is unused shows `ALARMS DISABLED` and the crossed circle; `--can-only` shows no Signal K dot and a steady `ALARMS DISABLED`.
