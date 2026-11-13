# The electrical resolver

How the display makes sense of DC and AC data from a boat it has never seen. Design and reasoning: `docs/superpowers/specs/2026-11-12-electrical-resolver-design.md` (local, not published); this page is what the code does.

## The problem it solves

Instance numbers on NMEA 2000 are unique per device, not per bus, and Signal K keeps only the latest value per path. On a real boat with a shunt, solar controllers, a charger and a GX device that mirrors the system battery, that meant: two devices both reporting "battery 0" overwrote each other, a solar controller's panel input appeared as a battery, and the mirrored bank appeared twice.

## What it does

Both feeds, the CAN bus and the Signal K stream, report readings to one resolver together with who sent them. The resolver keeps one **endpoint** per `(device, domain, instance)`, decides what each one is, collapses mirrors, and publishes the result into boatdata under `electrical.resolved.*`. The Power role binds to those paths.

- **Device identity** is the CAN NAME (from address claims, or Signal K's `sources` tree), else model plus serial, else a provisional key from the address. A device that re-claims another address keeps its endpoints and any config pins.
- **Function** is decided by the strongest evidence available: a path that already names it (signalk-venus style `electrical.solar.*`), the DC type in PGN 127506, which messages the device sends (converter status, AC, switch bank), the product table (`components/electrical/src/el_product_table.h`), and last a stable 8-60 V voltage. Evidence that disagrees at the same rank gives `unknown`, which is never published.
- **Charger outputs.** A device that reports converter status is converter-class: its "battery" DC endpoint is the charger's output, not a bank. Over Signal K, which has no DC type, only the lowest-numbered instance with current is taken as the output; other instances stay unknown unless the product table names them.
- **Mirrors.** Two battery endpoints from different devices merge when voltage (0.05 V), current (0.1 A) and state of charge (0.01) agree for 10 s and at least one measures current. The more authoritative device wins (a monitor from the product table, then one with current and SoC); the merge survives a short disagreement and a quiet winner.
- **Stale feeds.** When the CAN bus goes quiet or Signal K disconnects, that feed's published leaves become invalid. A leaf that did not change is not rewritten, so a quiet value never looks fresh.

## Published paths

`electrical.resolved.batteries.<id>.{voltage,current,capacity.stateOfCharge,capacity.timeRemaining,power}`,
`electrical.resolved.solar.<id>.{voltage,current,panelPower,chargingMode}`,
`electrical.resolved.chargers.<id>.{voltage,current,chargingMode}` (a converter publishes here too),
`electrical.resolved.alternators.<id>.{voltage,current,chargingMode}`,
`electrical.resolved.acin.<id>.current` (an AC input: its presence is what Power reads as shore).

Once an endpoint's device has a CAN NAME it also gets `electrical.resolved.<group>.<id>.ref`, `<NAME in lower-case hex>.<instance>`, which is how `config.json` names an endpoint (a bank's `battery`) or a device (a flow's `device`, the NAME alone) and survives a replug or an address change. An endpoint with no NAME yet has no `ref` and cannot be placed.
`<id>` is eight lowercase hex characters derived from the endpoint's stable key; use it in config pins (`battery.<id>`). The inverter, DC-DC, alternator and system groups still read raw signalk-venus paths.

## Names

Each published endpoint also gets `electrical.resolved.<group>.<id>.label`, a short name a person can read, which the Power role uses as the instance's label when `config.json` does not set one, and which Setup's Discovery list shows beside the id (never saved into config, so it keeps following the data).

- The name is the first word of the device's model that has no digit: `SmartShunt IP65 300A` is `SmartShunt`, `Phoenix Smart IP43 Charger 12|30` is `Phoenix`. A device with no model, or a model with no such word, is named by its function (`Battery`, `Solar`, `Charger`).
- It adds only what is needed to tell neighbours apart, among the endpoints of one function that would read the same: two devices of one model get the end of their NAME in hex (`BlueSolar f5`, `BlueSolar f6`: the end of Signal K's `can0.c046a0002cc001f5`); one device with several instances gets the instance (`SmartShunt 0`, `SmartShunt 1`); both give `BlueSolar f5.1`. Names are at most 15 characters.
- A name can change while the panel runs, when a second device of the same model appears; the pages follow. Set a label in config or on the Discovery screen to pin a name.
- The id still carries the identity for config pins (`battery.3b7be7d5`). To find a device behind an id without the screen, see the NAME tail in its name against Signal K's source list.

## Timing to expect

An endpoint with no DC type and no model (typical of a plain Signal K install) is believed after its voltage has stayed within 0.5 V for 10 s, and a mirror of it merges 10 s after that. Once believed, a battery stays one through load transients and is withdrawn only by a voltage no battery can have. With a DC type (CAN) or a known model it is classified at once.

## Limits

- **A Signal K server with source priorities hides the per-device data.** Signal K creates a priority group by itself when several sources report one path (it did for `electrical.batteries.0|1.voltage|current` on the bench once Victron devices shared them), and from then on the stream carries only the highest-priority source for those paths. The resolver then sees the shunt's `batteries.0.*` (which is right) but none of the solar controllers' or the charger's own values, so it cannot tell their output from their panel input, and the Summary's SOLAR, SHORE and LOAD nodes have nothing to compute from and say FAIL. That is the data being absent, not a display fault. `?sourcePolicy=all` on the WebSocket URL does deliver every source (checked against signalk-server on the bench), but it applies to the whole connection, so any other path with two sources would flap. For full per-device data connect the display to the bus (CAN or SK + CAN), or clear the server's priority group for those paths. The console command `data electrical.resolved` shows what the display actually holds.

- The display is listen-only on the bus, so it cannot ask a device for its product information; identity comes from address claims. The resolver and `source_can` support sending the request (`source_can_request_identity`) for firmwares that have a node.
- Product rows from configuration (`el_product_set_extra`) exist in the code but are not read from `config.json` yet.
- Victron MPPT yield and charge state live in proprietary PGN 61184 and are not decoded.
- Existing `config.json` pins such as `battery.0` do not match resolved ids and must be re-entered.
