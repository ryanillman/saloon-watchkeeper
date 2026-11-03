# boat-instruments Milestone 5a Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (chosen: inline execution, one final review) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every panel configurable without pulling its SD card: a safe config foundation, serial commands, the Signal K access request, and on-device setup screens.

**Architecture:** `config` gains key-path edits, a lock, and a save that keeps unknown roles. `app` gains `app_apply_config` (UI task) and a request queue other tasks post to, plus a portable command dispatcher (`app_cmd.c`) fed by an ESP console REPL on the boards and a stdin thread in the sim. `source_sk` gains an access-request state machine. `ui` gains `ui_setup.c`, a screen stack drawn over the page area, fed by plain structs and callbacks from `app` (ui cannot include roles).

**Tech Stack:** C11, ESP-IDF 5.5 (esp_console, esp_wifi scan), LVGL 9 (lv_keyboard, lv_textarea, lv_list), cJSON, Unity host tests, SDL simulator.

**Spec:** `docs/specs/2026-11-03-milestone-5a-design.md`

## Global Constraints

- `roles/`, `ui/`, `alerts/` include no ESP-IDF headers; `tools/check-layering.sh` passes after every task.
- Every LVGL call and every change to the config in use happens on the UI task. Other tasks post requests (`app_post`).
- Large static buffers in our components carry `PLATFORM_EXT_BSS`; after each firmware task the boot line `RAM after start` on the 7B must show internal free ≥ 60 KB.
- Secrets (`wifi.password`, `sources.signalk.token`) are never printed unmasked: `set (N chars)`.
- No config change touches the SD card from the UI task; saves go through the existing config writer task.
- Each task ends with `ctest --test-dir build/sim --output-on-failure` and `tools/check-layering.sh` passing, the IDF build of `lcd7b` compiling, and one commit in the repo's style ending with the attribution trailer.

## Review Focus

1. **Console command during an alarm.** `config set` applies while a banner is up; expect the banner and alert list to survive a rebind/rebuild. Test in Task 2.
2. **`config set` of an invalid value** (`display.brightness abc`, `units.temperature K`). Expect a message and an untouched file, no reboot loop. Test in Task 1.
3. **Access request when the server is unreachable or answers 4xx/5xx.** Expect a clear state (`ERROR <code>`), retry only on the next request, existing token kept. Test in Task 4.
4. **Setup open when Signal K reconnects or discovery rebinds.** Expect setup to stay open and not be destroyed by `ui_shell_rebuild`. Test in Task 5.
5. **Keyboard on the 480×480 screen.** Expect the text field being edited to remain visible above the keyboard. Snapshot in Task 7.

---

### Task 1: Config — unknown roles kept, key-path edits, lock

**Files:** Modify `components/config/include/config.h`, `components/config/src/config.c`. Test `tests/test_config.c`.

**Interfaces (produces):**
```c
// cfg_role_t gains: int8_t src_index;   // entry in the file's roles array; -1 = new (added in setup)
bool cfg_set(const char *dir, const char *path, const char *text, cfg_t *out, cfg_report_t *err);
// Edits dir/config.json's tree at path ("sources.signalk.token"), keeping the existing key's type
// (string; number must parse; bool: true/false); a missing key is created as a string.
// Validates by loading the edited tree into *out; on any issue returns false, fills *err, and
// leaves the file and the cached tree untouched. On success writes the file (atomic save) and
// returns true with the new config in *out. Called only from the config writer task.
char *cfg_show(const char *dir);   // pretty JSON of the cached tree, secrets masked; caller frees
```
- `cfg_load`, `cfg_save`, `cfg_set`, `cfg_show` take one module lock (platform lock) around the cached tree.
- `write_roles`: each known role updates `roles[src_index]` (or appends a new object when -1); entries not loaded (unknown role names) are kept unchanged and placed after the known roles in their original order.

- [ ] **Step 1: Failing tests:** `test_unknown_roles_survive_save` (file with roles `engine, wind, tanks, anchor, foo` where `wind`, `foo` are unknown and `tanks` has `"exclude":["x.0"]`; load, change palette, save → reloaded file has all five entries, `tanks` still has its exclude, `engine` and `tanks` unchanged otherwise, unknown entries byte-identical as JSON); `test_reordered_roles_keep_their_settings` (swap engine/tanks in cfg_t, save → each keeps its own keys); `test_cfg_set_string` (`sources.signalk.token` → file has it, `*out` has it); `test_cfg_set_number_and_bool` (`display.brightness 40`, `buzzer.enabled true`); `test_cfg_set_rejects_bad_number` (`display.brightness abc` → false, file bytes unchanged); `test_cfg_set_rejects_invalid_value` (`units.temperature K` → false with the load's message); `test_cfg_set_creates_missing_string_key`; `test_cfg_show_masks_secrets` (output contains `set (` for token and password, not the values).
- [ ] **Step 2: Run, expect FAIL.** `cmake --build build/sim && ./build/sim/tests/test_config`
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run all tests and layering. Expected PASS.**
- [ ] **Step 5: Commit** "Keep unknown roles on save and edit config by key path"

### Task 2: Apply config on the UI task; requests from other tasks

**Files:** Modify `components/app/include/app.h`, `components/app/src/app.c`, `components/net/include/net.h`, `components/net/src/net_esp.c`; create `components/net/src/net_linux.c` (stub), add net to the sim build. Test `tests/test_app_apply.c` (new; `bi_test(test_app_apply app ...)` with the libraries test_app_palette uses).

**Interfaces (produces):**
```c
// app.h
typedef enum { APPLY_NONE = 0, APPLY_DISPLAY = 1, APPLY_SIGNALK = 2, APPLY_ROLES = 4, APPLY_WIFI = 8 } app_apply_t;
unsigned app_apply_config(const cfg_t *next);        // UI task only; returns the APPLY_ bits it acted on
typedef void (*app_req_fn)(void *arg);
bool app_post(app_req_fn fn, void *arg, uint32_t wait_ms);   // runs fn on the UI task at the next tick; waits for it
const cfg_t *app_config(void);                       // config in use (UI task)
const char *app_config_dir(void);                    // "" when no SD
// net.h
void net_wifi_restart(const cfg_wifi_t *wifi);
bool net_wifi_info(char ip[16], int8_t *rssi);       // false when not connected
typedef struct { char ssid[33]; int8_t rssi; bool secure; } net_ap_t;
int net_wifi_scan(net_ap_t *out, int max);           // blocking, ≤ 4 s; sim returns 3 fixed networks
```
- Apply rules (spec table): display keys → palette/brightness/units/buzzer applied (units change rebuilds pages); `sources.signalk.*` → `sk_client_init` with new cfg + restart connection (add `void sk_client_reconfigure(const cfg_sk_t *cfg)` that closes the socket so the loop reconnects with the new host/token); roles/instances/excludes/labels/start_role → `app_bind` + rebind subscriptions + `ui_shell_rebuild` (alerts UI is on the screen layer and survives); wifi → `net_wifi_restart`.
- `app_post` uses a small queue drained at the start of `app_tick`; `wait_ms` 0 = fire and forget. On the sim, tests call `app_tick` directly.

- [ ] **Step 1: Failing tests:** `test_apply_palette_only_display`; `test_apply_token_reconnects_signalk` (returns APPLY_SIGNALK, sk_client reconfigure called — use a test hook counter); `test_apply_exclude_rebinds`; `test_apply_identical_is_none`; `test_post_runs_on_tick`; `test_apply_keeps_alarm_banner` (Review Focus 1: raise an alarm, apply a roles change, banner still visible).
- [ ] **Step 2: Run, expect FAIL.** **Step 3: Implement.** **Step 4: PASS + layering + lcd7b IDF build.**
- [ ] **Step 5: Commit** "Apply config changes without a reboot"

### Task 3: Command dispatcher and consoles

**Files:** Create `components/app/include/app_cmd.h`, `components/app/src/app_cmd.c`, `main/console.c` (ESP REPL), add `platform_reboot()` and `platform_mem(size_t *internal, size_t *psram)` to `components/platform` (Linux: exit / zeros). Modify `main/main.c`, `main/CMakeLists.txt` (REQUIRES console), `sim/sim_main.c` (stdin thread when `--console`), `app.h` (`void app_set_info(const char *board, const char *version)`), `boards/lcd7b/sdkconfig.defaults` (console: `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` — the 7B's connected port is its UART; existing lcd7b build dirs need `idf.py fullclean`). Test `tests/test_app_cmd.c` (new).

**Interfaces (produces):**
```c
typedef void (*app_out_fn)(const char *line);
int app_cmd_run(int argc, char **argv, app_out_fn out);   // 0 ok, 1 usage/error; safe from any task (posts to UI task)
```
- Commands and output exactly as the spec table: `help`, `status`, `config show`, `config set <path> <value>` (value may contain spaces if quoted; esp_console splits quotes), `config reload`, `sk request-access` (Task 4 fills it; until then prints `not available yet`), `reboot`.
- `config set` flow: console task → config writer task runs `cfg_set` (file I/O off the UI task) → on success `app_post(apply, next, 2000)` → prints `saved; applied: <list>` where list names the APPLY_ bits (`display`, `reconnect signalk`, `rebind roles`, `restart wifi`) or `nothing to apply`.
- `status` lines (one per line, `key: value`): `firmware`, `board`, `ip`, `rssi`, `signalk` (state + host:port), `token` (`set`/`not set`), `access`, `ram` (`internal N KB, psram N KB`), `sd` (`mounted`/`none`), `config` (`main`/`backup`/`defaults`), `alerts` (count, worst state).
- ESP: `esp_console_new_repl_usb_serial_jtag` when `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`, else `esp_console_new_repl_uart`; prompt `bi> `; one `esp_console_cmd_register` per top-level command (`help` is esp_console's own plus ours listing ours), each forwarding argc/argv. REPL task stack 6144, in PSRAM is not allowed for console I/O — keep internal; check the RAM line.

- [ ] **Step 1: Failing tests:** `test_help_lists_commands`; `test_status_has_all_keys`; `test_config_show_masks`; `test_config_set_token_applies_signalk` (output `saved; applied: reconnect signalk`); `test_config_set_invalid_reports` (Review Focus 2); `test_unknown_command_usage`.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS + layering + IDF builds for all three boards.**
- [ ] **Step 5: Sim check:** `./build/sim/sim --console` then type `status`, `config show`, `config set display.palette night` → palette changes.
- [ ] **Step 6: Commit** "Add serial commands for status and config"

### Task 4: Signal K access request

**Files:** Create `components/source_sk/include/sk_access.h`, `components/source_sk/src/sk_access.c`. Modify `sk_transport.h` + both transports (generic request with response body), `sk_client.c` (runs the state machine in its loop), `app_cmd.c` (`sk request-access`, `status` access line). Test `tests/test_sk_access.c` (new; fake transport like test_sk_client).

**Interfaces (produces):**
```c
// sk_transport.h
bool sk_http_request(const char *method, const char *url, const char *token, const char *body,
                     int *status, char **resp, size_t *resp_len);   // caller frees *resp
// sk_access.h
typedef enum { SKA_IDLE, SKA_PENDING, SKA_APPROVED, SKA_DENIED, SKA_EXPIRED, SKA_ERROR } ska_state_t;
void sk_access_start(void);                                  // any task; request runs on the SK task
ska_state_t sk_access_state(int *http_error);                // http_error set for SKA_ERROR
void sk_access_set_identity(const char *client_id, const char *description);
void sk_access_on_token(void (*fn)(const char *token));      // app saves it via cfg_set + apply
void sk_access_poll(int64_t now_ms);                          // SK task: sends/polls as due
void sk_access_client_id_from_mac(const uint8_t mac[6], char out[37]);   // stable v4-shaped UUID
```
- POST `/signalk/v1/access/requests` `{"clientId","description","permissions":"readwrite"}`; expect 202 with `href` (or 400 "already requested" → poll the same id's href if returned). Poll `href` every 5000 ms; `state: COMPLETED` + `accessRequest.permission: APPROVED` + `accessRequest.token` → APPROVED, call on_token; `DENIED` → DENIED; 30 min without completion → EXPIRED. Non-2xx or no connection → ERROR with code, polling stops (Review Focus 3).
- Polling runs whether or not the WebSocket is live (needs only HTTP); `sk_client` calls `sk_access_poll` from its loop and from its backoff sleep.
- Live behaviour of 2.35.1 is checked first (Step 1) and the exact response shapes recorded in the spec's access section.

- [ ] **Step 1: Probe the local server** with a scratch script: POST a request, GET its href, approve in the admin UI, GET again; record the JSON shapes (state names, token field, 400 on repeat) in the spec. Deny one and record that too.
- [ ] **Step 2: Failing tests** matching the recorded shapes: `test_request_then_approved_saves_token`; `test_denied`; `test_expired_after_30_min`; `test_http_error_stops`; `test_client_id_stable_for_mac`; `test_repeat_request_reuses_href`.
- [ ] **Step 3: FAIL. Step 4: Implement. Step 5: PASS + IDF builds.**
- [ ] **Step 6: Live, sim:** `sk request-access` in the sim console, approve in the admin UI → `status` shows `token: set`, an injected alarm ACKs (`status.acknowledged` true on the server).
- [ ] **Step 7: Commit** "Request Signal K access from the panel"

### Task 5: Setup frame, menu, About, Display

**Files:** Create `components/ui/include/ui_setup.h`, `components/ui/src/ui_setup.c`, `components/app/src/app_setup.c` (glue). Modify `ui_quick.c/.h` (SETUP button, `UI_QUICK_SETUP` part), `app.c`, `sim/sim_main.c` (snapshots), `tests/check_snapshots.cmake`. Test `tests/test_ui_setup.c` (new).

**Interfaces (produces):**
```c
typedef enum { SETUP_MENU, SETUP_WIFI, SETUP_SIGNALK, SETUP_ROLES, SETUP_DISCOVERY, SETUP_DISPLAY, SETUP_ABOUT } ui_setup_screen_t;
typedef struct {
  void (*save)(const cfg_t *edited);          // app: hands to the writer + apply
  void (*about)(char *out, size_t n);         // app: multi-line About text
  bool has_buzzer, has_sd;
  // later tasks add: wifi_scan, wifi_state, sk_state, request_access, discovery_list, run_discovery
} ui_setup_cb_t;
void ui_setup_open(lv_obj_t *screen, const cfg_t *current, size_class_t sc, const ui_setup_cb_t *cb);
void ui_setup_close(void);
bool ui_setup_is_open(void);
void ui_setup_show(ui_setup_screen_t s);       // tests and the menu
void ui_setup_tick(int64_t now_ms);            // inactivity timeout (300000 ms), live state labels
lv_obj_t *ui_setup_part(const char *name);     // tests: "back","save","close","discard_yes", ...
```
- Frame per spec: below the strip, above pages, below the alarm banner (banner, marker and list keep their z-order; GO closes setup). Content scrolls vertically; shell swipes ignored while open. Working copy of `cfg_t` held in PSRAM (`PLATFORM_EXT_BSS`). Dirty flag → BACK asks "Discard changes?". No SD → note shown, SAVE still applies.
- Survives `ui_shell_rebuild` (Review Focus 4): setup lives on the screen layer, not in content.
- Display screen: segmented buttons for each unit, palette (DAY/DUSK/NIGHT/AUTO), brightness slider (or ON), buzzer switch only when `has_buzzer`.

- [ ] **Step 1: Failing tests:** `test_setup_opens_from_quick_panel`; `test_back_with_changes_asks`; `test_display_save_calls_save_with_units`; `test_inactivity_closes`; `test_go_closes_setup`; `test_setup_survives_shell_rebuild`; `test_buzzer_hidden_without_buzzer`.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS; snapshots `setup-menu`, `setup-display`, `setup-about` at sim480 and sim800, day and night, added to check_snapshots; view them.**
- [ ] **Step 5: Commit** "Add the setup screens: menu, display, about"

### Task 6: Roles and Discovery screens

**Files:** Modify `ui_setup.c/.h`, `app_setup.c`, `components/roles/src/role_bind.c` (a function that lists what a fresh bind finds, without pins), sim snapshots. Tests in `tests/test_ui_setup.c`, `tests/test_role_bind.c`.

**Interfaces (produces):**
```c
// ui_setup.h additions
typedef struct { char id[24]; char label[12]; bool found, pinned, excluded; } ui_disc_inst_t;
typedef struct { char role[12]; char title[16]; ui_disc_inst_t inst[16]; uint8_t count; } ui_disc_role_t;
// ui_setup_cb_t gains:
uint8_t (*discovery_list)(ui_disc_role_t *out, uint8_t max);   // app: from role_discover_all
void (*run_discovery)(void);                                   // app: asks sk_client to rediscover now
const char *const *implemented_roles;                          // NULL-terminated, for ADD
// role.h
uint8_t role_discover(const role_def_t *def, const cfg_role_t *cfg, char ids[][24], uint8_t max);  // ids found, ignoring pins and excludes; multi-group ids prefixed
```
- Roles screen edits order (up/down), remove, ADD (implemented roles not present), start role. Unknown roles never shown (kept by Task 1).
- Discovery: per role, union of found and pinned ids; MISSING = pinned and not found; keep box (unticked → exclude); label edit opens the keyboard (Task 7 provides the keyboard component; until then labels are read-only and the test marks it pending). CONFIRM writes `inst` (kept, with labels) and `exclude`; AUTO clears `inst` and `exclude` for that role. RUN calls `run_discovery` and refreshes when `sk_client_discovery_count` changes.
- Add `void sk_client_rediscover_now(void)`.

- [ ] **Step 1: Failing tests:** `test_role_discover_ignores_pins`; `test_roles_reorder_and_save`; `test_add_offers_missing_roles_only`; `test_discovery_confirm_writes_pins_and_excludes`; `test_discovery_missing_shown`; `test_auto_clears_pins`.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS; snapshots `setup-roles`, `setup-discovery` (fixture with engine and power), sim480/sim800; view.**
- [ ] **Step 5: Commit** "Add the roles and discovery setup screens"

### Task 7: Wi-Fi and Signal K screens, keyboard

**Files:** Modify `ui_setup.c/.h`, `app_setup.c`, sim snapshots. Tests in `tests/test_ui_setup.c`.

**Interfaces (produces):** `ui_setup_cb_t` gains
```c
int (*wifi_scan)(net_ap_t *out, int max);       // app → net_wifi_scan, run off the UI task via a worker; result posted back
void (*wifi_state)(char *line, size_t n);        // "connected illmannet 192.168.0.40 -61 dBm" / "connecting" / "off"
void (*sk_state)(char *line, size_t n);          // "live 192.168.0.108:3000" ...
void (*request_access)(void);
void (*access_state)(char *line, size_t n);      // "pending" / "approved" / "denied" / "expired" / "error 401" / "idle"
```
- `net_ap_t` comes from net.h; ui may not include net.h if net pulls ESP headers — define `ui_ap_t` in ui_setup.h with the same fields and convert in app_setup.c.
- One shared keyboard helper in ui_setup.c: textarea + `lv_keyboard`, field scrolled above the keyboard (Review Focus 5); password mode for Wi-Fi.
- Wi-Fi: SCAN runs on a worker (blocking up to 4 s; a "Scanning…" label meanwhile), list sorted by RSSI, "Other…" for SSID entry; choosing asks for the password; SAVE → wifi keys → APPLY_WIFI.
- Signal K: Auto/Manual segmented; manual host (keyboard) and port (numeric keyboard); live state line; token set yes/no; REQUEST ACCESS → `sk_access_start`; access state line refreshed by `ui_setup_tick`.
- Discovery label edit (Task 6) switches to the keyboard helper.

- [ ] **Step 1: Failing tests:** `test_wifi_scan_lists_sorted`; `test_wifi_choose_then_password_saves_wifi_keys`; `test_sk_manual_host_saves`; `test_request_access_button_calls_cb`; `test_keyboard_keeps_field_visible_480` (field's y2 < keyboard's y1); `test_discovery_label_edit`.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS; snapshots `setup-wifi`, `setup-wifi-keyboard`, `setup-signalk` at sim480/sim800; view (Review Focus 5).**
- [ ] **Step 5: Commit** "Add the Wi-Fi and Signal K setup screens"

### Task 8: Live and bench pass

**Files:** Modify `docs/testing.md` (console commands, access request, setup checks), `docs/hardware/boards.md` (console port per board, RAM line after 5a).

- [ ] **Step 1: Sim live:** access request approved in the admin UI; `config set` token; ACK reaches the server; every setup screen opened.
- [ ] **Step 2: Bench, all three panels** (ports: square4 `/dev/ttyACM0`, lcd7 `/dev/ttyACM1` USB-JTAG; lcd7b `/dev/ttyACM2` UART, the 7B's primary console after Task 3): `config set sources.signalk.token <token>` over USB → ACK reaches the server; one panel's access request approved; Wi-Fi and Display changes saved and applied without a reboot; `RAM after start` internal free ≥ 60 KB on each.
- [ ] **Step 3: Final review** of the branch (one reviewer, most capable model), fix wave, then merge per the user.
- [ ] **Step 4: Commit** "Record the milestone 5a bench pass"

**Milestone 5a done when:** the spec's "Done when" holds on all three panels.
