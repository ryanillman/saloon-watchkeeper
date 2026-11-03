# Milestone 4: v1 roles, alerts, palettes

Status: draft for review, 2026-11-03. Extends `2026-11-02-boat-instruments-design.md`; where the two differ, this document wins for milestone 4.

## Scope

- Role instance groups, so one role can read several kinds of device.
- Tanks role.
- Power role with Summary, Batteries, and Charging pages, built for a Victron system (Cerbo GX with signalk-venus: battery monitor, MPPTs, MultiPlus or Quattro, DC-DC or alternator charger, Victron smart charger).
- Alerts mirrored from Signal K `notifications.*`, with acknowledge and silence sent back.
- Palette and brightness from the top strip, with auto palette from Signal K.

Out of scope: on-device setup, the Signal K access request, and discovery confirmation (milestone 5). The Signal K token comes from `config.json`, written by hand or with the injector.

## Verification first

Three facts decide details below. Each is checked before the code that depends on it, and the result is written back into this document.

1. **Cerbo capture.** `tools/sk-capture` saves `vessels/self` from the boat's Signal K server to a fixture. Slot paths, instance ids, and injector scenarios are taken from it, not from memory. It also shows whether signalk-venus publishes a DC system power path.
2. **Fixed-period subscriptions.** Whether Signal K 2.35 resends unchanged values to a subscription with `policy: "fixed"`. The Power max ages depend on it.
3. **Notification acknowledge and silence.** Whether 2.35 offers a notifications API for acknowledge and silence, or accepts a v1 PUT to a notification path without a plugin to handle it.

## Role instance groups

A role declares one or more groups instead of one instance prefix:

```c
typedef struct {
  const char *name;          // "battery"; prefixes config ids in multi-group roles
  const char *prefix;        // "propulsion", "tanks", "electrical.solar"
  uint8_t id_depth;          // 1: "port"; 2: "freshWater.0"
  uint8_t max_inst;          // Engine 2, Tanks 8, Power groups 4
  const role_slot_t *slots;
  uint8_t slot_count;
} role_group_t;
```

- `role_def_t` replaces `instance_prefix`, `slots`, and `slot_count` with `groups` and `group_count`. `role_binding_t` holds one bound set per group: instance ids and labels, slot ids, and ranges.
- `role_bind` applies the existing rules per group: pinned instances from config, else discovered instances sorted, else one placeholder with every slot unbound.
- Config `instances`, `exclude`, and `bind` keys use ids relative to the role. A single-group role uses the bare id (`port`, `freshWater.0`). A multi-group role prefixes the group name (`battery.279`, `solar.288`).
- Discovery limits itself to the prefixes the configured roles' groups declare.
- Engine becomes one group (`propulsion`, depth 1, max 2) with no change in behavior. Existing `test_role_bind` cases and Engine snapshots pass unchanged.

## Tanks

- One group: `tanks`, depth 2, max 8. Slots: `currentLevel` (ratio, strip 0–100 %), `currentVolume`, `capacity`. Volume is shown when present, else level × capacity when capacity is present, else omitted.
- One page: every tank as a vertical strip side by side, labelled with type and name (`FRESH 1`, `FUEL`, `BLACK`), percent large and volume small. Strip colour by fluid type: fresh water blue, fuel amber, black water brown, grey water grey, anything else the palette's accent.
- Wide boards show up to 8 tanks on one page. Square boards show 4 per page and add pages as needed.
- `meta.zones` draw as bands, as on Engine. No max age: a steady level is healthy.

## Power

### Groups

Paths are the expected signalk-venus layout and are confirmed against the Cerbo capture before the slot tables are written.

| Group | Prefix | Slots |
|---|---|---|
| battery | `electrical.batteries` | voltage, current, power (fallback voltage × current), stateOfCharge, timeRemaining |
| solar | `electrical.solar` | voltage, current, panelPower, chargingMode |
| charger | `electrical.chargers` | voltage, current, chargingMode. The MultiPlus charger and the smart charger appear here |
| inverter | `electrical.inverters` | AC input state (shore connected), inverterMode |
| dcdc | `electrical.dcdc` or `electrical.alternators`, per the capture | voltage, current, chargingMode |

`chargingMode` and `inverterMode` are text samples drawn as readouts.

### Pages

1. **Summary.** `IN → BATTERY → LOAD` across the page on wide boards, 2×2 on square, with a large SoC strip.
   - **IN:** sum of positive DC-side charge power (voltage × current on the battery side) over solar, charger, and dcdc instances. A charger reporting negative current (a MultiPlus while inverting) adds to load, not to IN. DC-side power is used so IN and BATTERY are measured at the same place; panel watts are shown on Charging.
   - **BATTERY:** net power of the primary bank, signed, positive when charging.
   - **DC LOAD:** the Cerbo's DC system power when signalk-venus publishes it, else IN − BATTERY. Labelled DC LOAD because AC loads fed straight from shore through the MultiPlus never reach the DC side.
   - **SoC:** the primary bank's state of charge.
2. **Batteries.** One row per bank: voltage, current, power, SoC strip, time remaining.
3. **Charging.** One row per solar, charger, and dcdc instance: power, current, mode; panel watts for solar. A shore-connected marker from the inverter group.

### Rules (`power_calc.c`, unit-tested)

- **Primary bank:** the first battery instance with a stateOfCharge path, unless config sets `"primary": "<id>"` on the power role.
- **Strict totals:** if any bound contributor to IN is invalid, IN shows `IN FAIL` and DC LOAD shows `LOAD FAIL`. If the primary bank is invalid, BATTERY, DC LOAD, and SoC flag. A valid 0 (an MPPT at night) is a reading, not a fault.
- **Time remaining:** signalk-venus sends `null` while a bank is not discharging. On a valid bank with current ≥ 0, the field reads `CHARGING`. It flags only when the bank itself is invalid.
- **Max age:** if check 2 confirms fixed-period resends, Power paths are subscribed with `policy: "fixed"` and a 15 s max age, so a device that stops reporting flags. If not, Power slots have no max age, and only source loss and `null` flag a field, as for tanks.
- **Known limit:** with several monitored banks, BATTERY covers the primary bank only. Charge into another bank (a start battery) counts as load.

## Alerts over Signal K

### `alerts/` component

- A list of up to 16 `alert_t` (as in the main spec), keyed by notification path. Sources write under a lock; the UI reads a snapshot each tick.
- API: upsert, clear by id, source down, snapshot, request acknowledge, request silence, and lookup by data path for field colouring.
- No ESP-IDF headers. Host-tested.

### Signal K

- The client subscribes to `notifications.*` alongside the bound paths. A delta carrying `{state, method, message}` updates an entry. A `null` value or `normal` state clears it.
- On disconnect the list is cleared and the top strip shows `SK ALERTS OFFLINE` until the connection is back. The server resends current notifications on subscribe, which rebuilds the list. An empty list is never shown as all clear while alerts cannot be received.
- Acknowledge and silence are queued by the UI and sent by the Signal K task with the configured token, using the method check 3 finds. Silence falls back to a v1 PUT of the notification with `sound` removed from `method`.
- The local state changes at once: flashing and buzzer stop. If the request fails, the banner shows `ACK NOT SENT`.

### On the glass

- **Banner** for `alarm` and `emergency`: full-width red, flashing until acknowledged, with ACK and SILENCE, and GO when the path maps to a configured role: `propulsion.*` to Engine, `electrical.*` to Power, `tanks.*` to Tanks, `navigation.anchor.*` to Anchor. Several active alarms show the most severe first with a `+N` count. The display never changes pages on its own.
- **Top strip:** amber marker with a count for `alert` and `warn`. Tapping it opens the alert list: state, message, age, and ACK per row.
- **Field colour:** a page refreshing a slot checks for an active notification on that slot's path and draws the readout in the alert colour, in addition to zone bands.
- **Buzzer:** through the existing `board->buzzer` hook (`square4` only; a no-op in the sim). Off unless `buzzer.enabled`. Sounds only for unacknowledged `alarm` and `emergency` whose `method` includes `sound`: slow pulse for alarm, fast for emergency. A non-blocking pattern advanced on the UI tick.

## Palette and brightness

- Tapping the palette area of the top strip opens a quick panel: DAY, DUSK, NIGHT, AUTO, and a brightness slider.
- **AUTO** follows Signal K `environment.mode`: `dawn` and `dusk` give dusk, `night` gives night, anything else gives day. Day when the path is absent or Signal K is offline. The client subscribes to `environment.mode` while the palette is auto.
- Choosing DAY, DUSK, or NIGHT stores that palette and stops following Signal K until AUTO is chosen again.
- Brightness calls `board->backlight(percent)`. On `lcd7`, which is on/off only, the slider is replaced by a fixed `ON` note.
- Palette and brightness are saved with the existing atomic config save, 2 s after the last change. Without an SD card, changes last for the session.

## Testing

- **Host tests:** `role_bind` per group (Engine unchanged, Tanks at depth 2, Power with several groups, prefixed excludes); `power_calc` (sums, negative charger current as load, strict failure, `CHARGING`, primary bank choice); alert transitions (raise, escalate, clear, re-raise after acknowledge, source down); notification parsing including `null` and missing fields; `environment.mode` to palette.
- **Snapshots:** Tanks; Power Summary, Batteries, and Charging; each live and faulted; an alarm banner over Engine; the alert list. Every size and palette.
- **Live:** the Cerbo fixture replayed by the injector, plus scenarios `victron-power`, `tanks`, `alarm-coolant` (raise, then clear), and `warn-tank-low`. An ACK from the panel is visible on the server.
- **Bench:** each board runs the scenarios. Banner and buzzer are checked on `square4`.

## Order

1. Verification: Cerbo capture, fixed-period resend, notification acknowledge and silence. Results recorded here.
2. Role groups, with Engine unchanged.
3. Tanks.
4. Power: `power_calc` and three pages.
5. Alerts: component, Signal K wiring, banner and list, field colour, buzzer.
6. Palette and brightness panel, auto palette.
7. Live and bench pass.

## Done when

Tanks, Power, and alert scenarios pass in the simulator against Signal K; an ACK from the panel shows on the server; auto palette follows `environment.mode`; and the live checks pass on each board.
