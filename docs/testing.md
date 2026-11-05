# Testing

## Host tests and snapshots

```sh
cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON
cmake --build build/sim
ctest --test-dir build/sim --output-on-failure
tools/check-layering.sh
```

`snapshot_smoke` renders every page at each size and palette into `build/sim/snap/`. Engine is rendered with fixture data twice: `-live` and `-fault`. The fault render has the port coolant sender failed. Power is rendered from a Victron fixture (`-live` on every page, and `-fault` on Summary with the solar current invalid). Engine page 1 is also rendered `-alarm` (port coolant alarm banner) and `-alarm-long` (a 96-character emergency message, wrapped to two lines and truncated, with all three buttons still on the banner); `alerts-list.png` shows the alert list open over two alerts. The Glance page is rendered `glance-p0-live.png` (all three tiles, with twelve hours of synthetic history behind the Power trend and the water change) and `glance-p0-fault.png` (the house charge, the port coolant and a water tank sender failed). Every snapshot shows the clock (02:14) and the live source dot.

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
| Server stopped with an alarm showing | Banner gone, `SK ALERTS OFFLINE` in the strip, `SK OFFLINE` status, buzzer silent |
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
| Server stopped with an alarm | PASS: banner gone, `SK ALERTS OFFLINE`, `SK OFFLINE` (buzzer is hardware only) |
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
