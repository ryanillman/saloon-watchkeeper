# Milestone 5a: setup, serial commands, Signal K access

Status: draft for review, 2026-11-03. Extends `2026-11-02-boat-instruments-design.md` and `2026-11-03-milestone-4-design.md`; where they differ, this document wins for milestone 5a.

## Scope

Milestone 5 is split in three. 5a (this document) makes every panel configurable without pulling its SD card. 5b is the Anchor role. 5c is SD card updates (`update.bin`). The v1 release follows 5c.

5a covers:

- A config foundation that setup, serial commands, and the access request share.
- Serial commands on the USB console.
- The Signal K device access request.
- On-device setup screens.

Out of scope: reassigning a single slot to another Signal K path from the screen (it stays a `bind` edit in `config.json` or `config set`), the Anchor role, SD updates, and the milestone 4 minor findings not named below.

## Config foundation

### Unknown roles survive a save

Today `cfg_save` deletes role entries the firmware did not load and matches the rest by position, so a role after a skipped entry can be rewritten with another role's settings.

- Each loaded role records the index of the `roles` array entry it came from.
- On save, each known role updates its own original entry, matched by that index, not by position.
- Entries the firmware skipped are kept unchanged, after the known roles, in their original order.

### Edit by key path

`bool cfg_set(const char *dir, const char *path, const char *text, cfg_report_t *err)`, for example `sources.signalk.token` or `wifi.ssid`.

- Edits the JSON tree. An existing key keeps its type: a string stays a string, a number must parse as a number, a bool accepts `true` or `false`. A missing key is created as a string.
- Checks the result by loading it into a temporary `cfg_t`. A value that load would reject is refused with the same message the boot screen shows, and the file is not touched.
- Saves through the config writer task.

### Locking

The config module keeps a cached JSON tree. The UI task, the config writer task, and the console task all reach it in 5a, so every access to the tree takes one lock.

### Apply without a reboot

`app_apply_config(const cfg_t *next)` runs on the UI task, compares with the config in use, and does only what changed:

| Change | Action |
|---|---|
| Palette, brightness, units, buzzer | Applied at once |
| Signal K host, port, or token | Signal K client reconnects |
| Roles, instances, excludes, labels, start role | Rebind and rebuild pages |
| Wi-Fi SSID or password | Wi-Fi restarts |

Setup SAVE, `config set`, and `config reload` all use it.

### Requests from other tasks

Console commands run on their own task. They post a request to the UI task and wait for its reply. No LVGL call and no change to the config in use happens off the UI task.

## Serial commands

- On the boards, ESP-IDF's console REPL runs on the build's console port (USB Serial/JTAG on the 4" and 7"; the UART port on the 7B unless native USB is used), with line editing and history.
- Each command passes its arguments to a portable dispatcher, `app_cmd_run(int argc, char **argv, void (*out)(const char *line))`. The dispatcher is host-tested. The simulator reads the same commands from its terminal.
- Log lines can interleave with typing. The prompt is reprinted.

| Command | Does |
|---|---|
| `help` | Lists the commands |
| `status` | Firmware version, board, IP and Wi-Fi RSSI, Signal K state and host, token set (yes/no), access request state, internal and PSRAM free, SD mounted, config origin (main, backup, defaults), active alerts |
| `config show` | The config as JSON, with `wifi.password` and `sources.signalk.token` shown as `set (N chars)` |
| `config set <path> <value>` | Edits, checks, saves, and applies. Prints what was applied (`saved; applied: reconnect signalk`) or the validation message |
| `config reload` | Reads `config.json` again and applies it |
| `sk request-access` | Starts a Signal K access request and prints its progress |
| `reboot` | Restarts |

Values set over the console travel in plain text over USB. They are masked whenever printed.

## Signal K access request

- Runs on the Signal K task.
- `POST /signalk/v1/access/requests` with:
  - `clientId`: a UUID derived from the chip's MAC address, stable across requests, so a repeated request does not add another entry.
  - `description`: `boat-instruments <board> <last two MAC bytes>`, for example `boat-instruments lcd7b 2a:10`.
  - `permissions`: `readwrite`.
- The panel polls the returned `href` every 5 s. States: PENDING, APPROVED, DENIED, EXPIRED.
  - APPROVED: the token is saved with `cfg_set` and applied, so Signal K reconnects with it.
  - DENIED: shown, nothing saved.
  - EXPIRED: no answer after 30 minutes; polling stops.
- A request starts only from `sk request-access` or the REQUEST ACCESS button in setup. Nothing starts on its own.
- An existing token stays in use until a new request is approved.
- The state shows in `status`, the console log, and the Signal K setup screen.

## Setup screens

### Frame

- The quick panel gets a SETUP button. Setup opens over the page area, below the top strip. The alarm banner stays above it.
- Each screen has BACK at the top left. Screens that edit something also have SAVE, which applies through `app_apply_config`. BACK with unsaved changes asks "Discard changes?".
- Setup content scrolls vertically. Swipes do not change role while setup is open.
- Touch targets are at least 48 px. Text is 20 pt on wide boards and 14 to 20 pt on square boards. Setup uses the current palette.
- Setup closes on CLOSE, when an alarm's GO is tapped, or after 5 minutes without a touch. Unsaved changes are discarded.
- Without an SD card, SAVE applies for the session and setup shows "No SD card. Changes last until restart."

### Screens

1. **Menu.** Wi-Fi, Signal K, Roles, Discovery, Display, About, CLOSE.
2. **Wi-Fi.** Current network, state, and IP. SCAN lists networks with signal strength, plus "Other..." for a hidden network. Choosing one asks for the password on LVGL's keyboard (bottom half of the screen on square boards). SAVE restarts Wi-Fi.
3. **Signal K.** Server: Auto (mDNS) or a manual host and port. Connection state, token set (yes/no), and REQUEST ACCESS with its live state.
4. **Roles.** Configured roles in swipe order, each with up, down, and remove. ADD offers implemented roles not yet present. Start role. Roles the firmware does not know are not shown and are kept.
5. **Discovery.** RUN reads the Signal K tree again. Per role, the instances found, each with a keep box and an editable label. A pinned instance that is gone shows as MISSING. CONFIRM writes the kept instances as pinned and the unticked ones as excluded. AUTO clears the pins so the role discovers on each boot.
6. **Display.** Units as segmented buttons (°C/°F; bar/psi/kPa; L/gal; kn, m/s, km/h; m/ft/nm), palette, brightness, and buzzer on/off on boards that have one.
7. **About.** Firmware version, board, IP, SD state, config origin, internal and PSRAM free.

In the simulator, a Wi-Fi scan returns a fixed list.

## Testing

- **Host tests:**
  - Unknown roles at positions 1 and 4 survive a palette change, and the file is otherwise unchanged except for the palette.
  - `cfg_set` on strings, numbers, and bools. A rejected value leaves the file untouched.
  - `app_apply_config` does only what changed.
  - Every command's output, including masking.
  - The access request state machine against a fake transport: pending then approved saves the token; denied; expired after 30 minutes.
  - Setup: BACK with unsaved changes; discovery CONFIRM writes pins and excludes; role reorder.
- **Snapshots:** every setup screen, square and wide, day and night.
- **Live, against the local Signal K server:**
  - An access request from the simulator, approved in the admin UI.
  - The token set with `config set` on the simulator's console.
  - ACK reaching the server afterwards.
- **Bench, all three panels:**
  - The token set with `config set` over USB; ACK then reaches the server.
  - An access request on one panel, approved in the admin UI.
  - Every setup screen opened; a Wi-Fi change and a Display change saved and applied without a reboot.

## Order

1. Config: unknown roles kept, `cfg_set`, locking.
2. `app_apply_config` and requests from other tasks.
3. Command dispatcher, simulator console, ESP console REPL.
4. Signal K access request, from the command first.
5. Setup frame, menu, About, Display.
6. Roles and Discovery screens.
7. Wi-Fi and Signal K screens, with the keyboard and Wi-Fi scan.
8. Live and bench pass.

Work is done inline, with one review of the whole branch at the end.

## Done when

Every panel can be configured from its own screen or over USB without pulling its SD card, ACK reaches the server from all three panels, and the host tests, snapshots, and bench checks pass.
