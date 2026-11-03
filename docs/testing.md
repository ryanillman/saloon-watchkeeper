# Testing

## Host tests and snapshots

```sh
cmake -S sim -B build/sim -G Ninja -DBUILD_TESTING=ON
cmake --build build/sim
ctest --test-dir build/sim --output-on-failure
tools/check-layering.sh
```

`snapshot_smoke` renders every page at each size and palette into `build/sim/snap/`. Engine is rendered with fixture data twice: `-live` and `-fault`. The fault render has the port coolant sender failed. Power is rendered from a Victron fixture (`-live` on every page, and `-fault` on Summary with the solar current invalid). Engine page 1 is also rendered `-alarm` (port coolant alarm banner) and `-alarm-long` (a 96-character emergency message, wrapped to two lines and truncated, with all three buttons still on the banner); `alerts-list.png` shows the alert list open over two alerts.

## Against a real Signal K server

The test server is a local Signal K (2.35 at the time of writing) on `localhost:3000`.

1. Get a write token once:
   ```sh
   python3 -I tools/sk-inject/inject.py access
   ```
   Approve **boat-instruments test injector** with read/write in the admin UI (Security → Access Requests). The token goes to `tools/sk-inject/.token`, which is git-ignored.
2. Play a scenario:
   ```sh
   python3 -I tools/sk-inject/inject.py tools/sk-inject/scenarios/two-engines.json
   python3 -I tools/sk-inject/inject.py tools/sk-inject/scenarios/coolant-null.json
   ```
3. Watch it:
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
| `tanks` after 20 s | Five tanks, strips side by side, fresh water draining slowly. `FRESH 2` shows `LEVEL FAIL`; the others stay live. Only `FUEL` has a volume line (110 L) |
| `victron-power` | Power Summary: IN grows with the solar ramp, `SHORE` marker, BATTERY and SOC live, LOAD = IN - BATTERY. After 30 s the battery discharges and Batteries shows `4:00` instead of `CHARGING`. After 45 s solar current is null: `IN FAIL` and `LOAD FAIL`, BATTERY and SOC stay live, the Charging solar row flags `AMPS FAIL` and `WATTS FAIL`. The paths are the signalk-venus defaults and may change after the Cerbo capture |
| `alarm-coolant` at 10 s | Red banner under the strip, flashing 1 Hz: `Port coolant high`, ACK, SILENCE, and GO (GO opens Engine page 1). The port coolant value turns red. Buzzer on boards that have one (Square 4.0). The notification appears in `GET /signalk/v2/api/notifications` |
| `alarm-coolant`, tap ACK | Flashing stops, the banner stays red; the server shows the notification acknowledged. Tap SILENCE first and the buzzer stops, the server shows silenced |
| `alarm-coolant` at 40 s | Banner disappears and the coolant value returns to white |
| `warn-tank-low` at 10 s | No banner. Amber `1` marker left of the status text; tap it for the list (`WARN`, `Fresh water low`, age, ACK). The fresh water tank value is amber |
| Server stopped with an alarm showing | Banner gone, `SK ALERTS OFFLINE` in the strip, `SK OFFLINE` status, buzzer silent |
| Server stopped | `SK OFFLINE`, every field flagged within about 2 s |
| Server restarted | `SK LIVE`, fields clear on the next delta |

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
| `victron-power` | PASS, but only with a scratch fix (see below). Summary IN 658 W, BATTERY 161 W, LOAD 497 W, SOC 82 %, `SHORE`; Batteries `CHARGING` then `4:00` after 30 s; after 45 s `IN FAIL`, `LOAD FAIL`, BATTERY and SOC live, Charging solar `WATTS FAIL` and `AMPS FAIL` |
| `alarm-coolant` at 10 s | PASS: red banner flashing, `Port coolant high`, ACK, SILENCE, GO; port coolant value red; notification on the server |
| `alarm-coolant`, ACK | PASS: banner steady red; server `acknowledged: true`, `method: []` |
| `alarm-coolant`, SILENCE | PASS: server `silenced: true`, `sound` dropped from `method` |
| `alarm-coolant` at 40 s | PASS: banner gone, coolant white |
| `warn-tank-low` | PASS: no banner, amber `1` marker, list shows `WARN`, `Fresh water low`, age, ACK; tank value amber |
| Server stopped with an alarm | PASS: banner gone, `SK ALERTS OFFLINE`, `SK OFFLINE` (buzzer is hardware only) |
| Server stopped | PASS: `SK OFFLINE`, every field flagged within 2 s |
| Server restarted | PASS: `SK LIVE`, fields live again |

**Power discovery bug (open):** with the committed code every Power field is flagged against the real server. `wanted()` in `components/source_sk/src/sk_parse.c` compares the top-level segment (`electrical`) with two-segment prefixes (`electrical.batteries`) and skips the whole group, so instances (`279`, `288`, ...) are never discovered. The Power rows above passed with a scratch change that also accepts a top-level segment that is the start of a prefix.

Notes for repeat runs: kill the injector only after its clear event, or write `null` to the notification paths first. The server keeps a notification's acknowledged state when the same alarm is rewritten, and answers a second acknowledge with HTTP 400 `Alarm already acknowledged!`. `GET /signalk/v2/api/notifications` also keeps stale entries for cleared paths.

## Capturing a boat's server

Power reads the signalk-venus default paths. To compare them with a real Cerbo, run this on any computer that can reach the boat's Signal K server:

```sh
python3 -I tools/sk-capture.py <boat-server>[:port] capture.json [--token-file token.txt]
```

It saves `GET /signalk/v1/api/vessels/self` as pretty JSON (port 3000 by default) and exits non-zero with a message on a connection or HTTP error. Use `--token-file` only if the server demands a token. Compare the `electrical` tree in `capture.json` (batteries, solar, chargers, inverters, dcdc, alternators, venus) with the slot paths in `components/roles/src/role_power.c` and the Power Groups table in the milestone 4 design, and correct the slots, `tools/sk-inject/scenarios/victron-power.json` and the table where they differ.

## Repeating the Signal K checks

Milestone 4 recorded two server behaviours in `docs/specs/2026-11-03-milestone-4-design.md` (Verification first). Repeat them after a Signal K upgrade, or against the boat's server.

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
