// Setup > Power: pick the boat's banks (House, Start, Nav, ...), say which battery each one is, and say which bank each
// charging source feeds (spec 2026-11-12-power-configuration-design.md). Three sections on one scrolling list: Banks,
// Sources, Unassigned. Nothing is placed automatically. There is one bank of each kind and nothing is typed: a battery
// that is not in a bank is added to one by choosing which bank it is; a source that feeds nothing is added by choosing
// the bank it feeds. A bank's name is its kind. Choices are buttons that show their value and open
// one shared menu (ui_setup.c), never buttons that cycle. Edits go to the working copy of the config; SAVE (in
// ui_setup.c) asks setup_power_can_save() first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "ui_setup_priv.h"

static const char *const BANK_KINDS[] = {"house", "start", "nav", "windlass", "thruster", "genset", "other"};
static const char *const BANK_KIND_LABELS[] = {"HOUSE", "START", "NAV", "WINDLASS", "THRUSTER", "GENSET", "OTHER"};
static const char *const FLOW_KINDS[] = {"solar", "shore", "engine", "converter", "other"};
static const char *const FLOW_KIND_LABELS[] = {"SOLAR", "SHORE POWER", "ENGINE", "CONVERTER", "OTHER"};
#define N_BANK_KINDS 7
#define N_FLOW_KINDS 5
#define MENU_ROWS 16

PLATFORM_EXT_BSS static struct {
  ui_power_row_t seen[UI_POWER_ROWS];
  uint8_t n;
  char error[96];
  char pending[24];          // the battery a new bank is being made for
  uint8_t kindmap[MENU_ROWS];  // for the bank-kind menus: the kind behind each option
  lv_obj_t *error_label;
  uint32_t checked_ms;
  uint16_t shape;            // a signature of what was seen when the page was built
  char opt[MENU_ROWS][44];   // the options of the menu being opened
  const char *optp[MENU_ROWS];
  char cand[MENU_ROWS][24];  // for the battery menu: the ref behind each option
} pw;

enum { OP_NEW_BANK, OP_BANK_KIND, OP_BANK_BATT, OP_BANK_DEL, OP_FLOW_KIND, OP_FLOW_TO, OP_FLOW_FROM, OP_FLOW_OUT,
       OP_FLOW_DEL, OP_FEEDS, OP_ADD_TO, OP_ADD_BANK, OP_ONE_BANK };

static cfg_role_t *power_role(void) {
  cfg_t *w = setup_work();
  for (uint8_t i = 0; i < w->role_count; i++)
    if (!strcmp(w->roles[i].role, "power")) return &w->roles[i];
  return NULL;
}

// What an edit does to the notice about the file: the user has started over, so a rejected topology is replaced on SAVE.
static void touch(cfg_role_t *r) {
  r->power_rejected = false;
  r->power_old_keys = false;
  pw.error[0] = 0;
}

static void fail(const char *msg) { snprintf(pw.error, sizeof pw.error, "%s", msg); }

// ---- what was seen ----

static const ui_power_row_t *seen_ref(const char *ref) {
  if (!ref[0]) return NULL;
  for (uint8_t i = 0; i < pw.n; i++)
    if (!strcmp(pw.seen[i].ref, ref)) return &pw.seen[i];
  return NULL;
}

static bool same_device(const char *a, const char *b) { return a[0] && b[0] && !strncmp(a, b, 16); }

// Which of its device's outputs a row is, by instance order within its group.
static uint8_t ordinal_of(const ui_power_row_t *r) {
  uint8_t n = 0;
  int inst = atoi(r->ref + 17);
  for (uint8_t i = 0; i < pw.n; i++) {
    const ui_power_row_t *o = &pw.seen[i];
    if (o->fn == UI_PW_OUTPUT && o->group == r->group && o != r && same_device(o->ref, r->ref) && atoi(o->ref + 17) < inst) n++;
  }
  return n;
}

static uint8_t outputs_of(const char *device, uint8_t group) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < pw.n; i++)
    if (pw.seen[i].fn == UI_PW_OUTPUT && pw.seen[i].group == group && same_device(pw.seen[i].ref, device)) n++;
  return n;
}

static const ui_power_row_t *output_of(const cfg_flow_t *f) {
  uint8_t group = !strcmp(f->kind, "engine") ? 1 : 0;
  for (int pass = 0; pass < 2; pass++, group = (uint8_t)!group)   // the other group too, as the role binds (power_topo.c)
    for (uint8_t i = 0; i < pw.n; i++) {
      const ui_power_row_t *r = &pw.seen[i];
      if (r->fn == UI_PW_OUTPUT && r->group == group && same_device(r->ref, f->device) && ordinal_of(r) == f->output) return r;
    }
  return NULL;
}

static bool battery_placed(const cfg_role_t *r, const char *ref, int except) {
  for (uint8_t i = 0; i < r->bank_count; i++)
    if ((int)i != except && !strcmp(r->bank[i].battery, ref)) return true;
  return false;
}

static int bank_index(const cfg_role_t *r, const char *id) {
  for (uint8_t i = 0; i < r->bank_count; i++)
    if (!strcmp(r->bank[i].id, id)) return i;
  return -1;
}

static void upper(const char *in, char *out, size_t n) {
  size_t i = 0;
  for (; in[i] && i + 1 < n; i++) out[i] = (char)(in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i]);
  out[i] = 0;
}

static void value_text(const ui_power_row_t *r, char *out, size_t n) {
  if (!r->value_ok) snprintf(out, n, "--");
  else if (r->fn == UI_PW_BATTERY) snprintf(out, n, "%.2f V", r->value);
  else snprintf(out, n, "%.0f W", r->value);
}

// "SmartShunt 13.10 V", with the input told apart ("input 2") when the same name is seen more than once.
static void battery_text(const ui_power_row_t *s, char *out, size_t n) {
  uint8_t same = 0;
  for (uint8_t i = 0; i < pw.n; i++) same += pw.seen[i].fn == UI_PW_BATTERY && !strcmp(pw.seen[i].label, s->label);
  char v[16];
  value_text(s, v, sizeof v);
  if (same > 1 && s->ref[0]) snprintf(out, n, "%s input %d  %s", s->label, atoi(s->ref + 17) + 1, v);
  else snprintf(out, n, "%s  %s", s->label, v);
}

// A short id for a bank from its name: lower-case letters and digits, made unique by a trailing number.
static void make_id(const cfg_role_t *r, const char *name, char out[12]) {
  char base[12];
  size_t k = 0;
  bool dash = false;
  for (const char *c = name; *c && k < 11; c++) {
    char ch = *c >= 'A' && *c <= 'Z' ? (char)(*c + 32) : *c;
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) { base[k++] = ch; dash = false; }
    else if (!dash && k) { base[k++] = '-'; dash = true; }
  }
  while (k && base[k - 1] == '-') k--;
  if (!k) { strcpy(base, "bank"); k = 4; }
  base[k] = 0;
  snprintf(out, 12, "%.11s", base);
  for (int n = 2; bank_index(r, out) >= 0 && n < 100; n++) snprintf(out, 12, "%.*s%d", n < 10 ? 10 : 9, base, n);
}

// ---- menus ----

static void bank_names(const cfg_role_t *r, uint8_t *n) {
  *n = 0;
  for (uint8_t i = 0; i < r->bank_count && *n < MENU_ROWS; i++) {
    upper(r->bank[i].name[0] ? r->bank[i].name : r->bank[i].kind, pw.opt[*n], sizeof pw.opt[0]);
    (*n)++;
  }
}

// The options of one choice, in pw.opt/pw.optp; the title and the index to highlight. Returns how many.
static uint8_t build_menu(const cfg_role_t *r, uint8_t op, uint8_t i, const char **title, uint8_t *sel) {
  uint8_t n = 0;
  *sel = 255;
  switch (op) {
    case OP_NEW_BANK:
    case OP_BANK_KIND:   // one bank of each kind: only the kinds no other bank has
      *title = op == OP_NEW_BANK ? "Which bank is it?" : "What kind of bank?";
      for (uint8_t k = 0; k < N_BANK_KINDS; k++) {
        bool taken = false;
        for (uint8_t j = 0; j < r->bank_count; j++)
          taken |= !strcmp(r->bank[j].kind, BANK_KINDS[k]) && !(op == OP_BANK_KIND && j == i);
        if (taken) continue;
        if (op == OP_BANK_KIND && i < r->bank_count && !strcmp(r->bank[i].kind, BANK_KINDS[k])) *sel = n;
        pw.kindmap[n] = k;
        snprintf(pw.opt[n], sizeof pw.opt[0], "%s", BANK_KIND_LABELS[k]);
        n++;
      }
      break;
    case OP_BANK_BATT:
      *title = "Which battery?";
      for (uint8_t k = 0; k < pw.n && n < MENU_ROWS; k++) {   // seen, named, and not in another bank
        const ui_power_row_t *s = &pw.seen[k];
        if (s->fn != UI_PW_BATTERY || !s->ref[0] || (i < r->bank_count && battery_placed(r, s->ref, i))) continue;
        if (i < r->bank_count && !strcmp(s->ref, r->bank[i].battery)) *sel = n;
        snprintf(pw.cand[n], sizeof pw.cand[0], "%s", s->ref);
        battery_text(s, pw.opt[n], sizeof pw.opt[0]);
        n++;
      }
      break;
    case OP_FLOW_KIND:
      *title = "What is it?";
      for (n = 0; n < N_FLOW_KINDS; n++) {
        snprintf(pw.opt[n], sizeof pw.opt[0], "%s", FLOW_KIND_LABELS[n]);
        if (i < r->flow_count && !strcmp(r->flow[i].kind, FLOW_KINDS[n])) *sel = n;
      }
      break;
    case OP_FLOW_TO:
    case OP_FEEDS:
      *title = op == OP_FEEDS ? "Add to which bank?" : "Which bank does it feed?";
      bank_names(r, &n);
      if (op == OP_FLOW_TO && i < r->flow_count) *sel = (uint8_t)bank_index(r, r->flow[i].to);
      break;
    case OP_FLOW_FROM:
      *title = "Which bank does it draw from?";
      bank_names(r, &n);
      if (i < r->flow_count) *sel = (uint8_t)bank_index(r, r->flow[i].from);
      break;
    case OP_FLOW_OUT: {
      *title = "Which output of the device?";
      const ui_power_row_t *o = i < r->flow_count ? output_of(&r->flow[i]) : NULL;
      uint8_t cnt = o ? outputs_of(r->flow[i].device, o->group) : 0;
      for (n = 0; n < cnt && n < MENU_ROWS; n++) snprintf(pw.opt[n], sizeof pw.opt[0], "OUTPUT %u", n + 1);
      if (i < r->flow_count) *sel = r->flow[i].output;
      break;
    }
  }
  for (uint8_t k = 0; k < n; k++) pw.optp[k] = pw.opt[k];
  return n;
}

// ---- actions ----

static void new_bank(cfg_role_t *r, uint8_t kind, const char *battery) {
  if (r->bank_count >= CFG_MAX_BANKS) { fail("Too many banks."); return; }
  cfg_bank_t *k = &r->bank[r->bank_count];
  memset(k, 0, sizeof *k);
  snprintf(k->name, sizeof k->name, "%s", BANK_KIND_LABELS[kind]);   // the name is the kind
  make_id(r, k->name, k->id);
  snprintf(k->kind, sizeof k->kind, "%s", BANK_KINDS[kind]);
  snprintf(k->battery, sizeof k->battery, "%s", battery);
  r->bank_count++;
}

static void new_flow(cfg_role_t *r, const ui_power_row_t *row, const char *to_id) {
  if (r->flow_count >= CFG_MAX_FLOWS) { fail("Too many sources."); return; }
  cfg_flow_t *f = &r->flow[r->flow_count];
  memset(f, 0, sizeof *f);
  snprintf(f->device, sizeof f->device, "%.16s", row->ref);
  snprintf(f->kind, sizeof f->kind, "%s", row->proposed[0] ? row->proposed : "other");
  f->output = ordinal_of(row);
  snprintf(f->to, sizeof f->to, "%s", to_id);
  r->flow_count++;
}

// The simplest boat: one battery becomes the house bank and every proposed source feeds it. Does nothing with more than
// one battery seen (the person has to say which is which), and leaves converters out (they need two banks).
static void one_bank(cfg_role_t *r) {
  uint8_t batteries = 0, pick = 0;
  for (uint8_t k = 0; k < pw.n; k++)
    if (pw.seen[k].fn == UI_PW_BATTERY) { batteries++; pick = k; }
  if (batteries != 1) {
    snprintf(pw.error, sizeof pw.error, "ONE BANK needs exactly one battery; %u are seen.", batteries);
    return;
  }
  if (!pw.seen[pick].ref[0]) { fail("The battery is still being identified."); return; }
  new_bank(r, 0, pw.seen[pick].ref);   // 0 is HOUSE
  if (r->bank_count == 0) return;
  const char *house = r->bank[r->bank_count - 1].id;
  for (uint8_t f = 0; f < r->flow_count; f++)   // a source left without a bank (the only bank was deleted by an older build) gets this one
    if (!r->flow[f].to[0] && strcmp(r->flow[f].kind, "converter")) snprintf(r->flow[f].to, sizeof r->flow[f].to, "%s", house);
  for (uint8_t k = 0; k < pw.n && r->flow_count < CFG_MAX_FLOWS; k++) {
    const ui_power_row_t *o = &pw.seen[k];
    if (o->fn != UI_PW_OUTPUT || !o->ref[0] || !strcmp(o->proposed, "converter")) continue;
    bool have = false;
    for (uint8_t f = 0; f < r->flow_count; f++) have |= output_of(&r->flow[f]) == o;
    if (!have) new_flow(r, o, house);
  }
}

static void first_other_bank(const cfg_role_t *r, const char *than, char out[12]) {
  out[0] = 0;
  for (uint8_t k = 0; k < r->bank_count; k++)
    if (strcmp(r->bank[k].id, than)) { snprintf(out, 12, "%s", r->bank[k].id); return; }
}

// A choice was made in the menu.
static void picked(uint32_t sel, uintptr_t v) {
  uint8_t op = (uint8_t)(v >> 8), i = (uint8_t)(v & 255), sel0;
  const char *title;
  cfg_role_t *r = power_role();
  if (!r) return;
  touch(r);
  uint8_t n = build_menu(r, op, i, &title, &sel0);   // the same options the person saw
  if (sel >= n) return;
  switch (op) {
    case OP_NEW_BANK:
      new_bank(r, pw.kindmap[sel], pw.pending);
      break;
    case OP_BANK_KIND:
      if (i >= r->bank_count) break;
    {
      char old[12], fresh[12];
      snprintf(old, sizeof old, "%s", r->bank[i].id);
      snprintf(r->bank[i].kind, sizeof r->bank[i].kind, "%s", BANK_KINDS[pw.kindmap[sel]]);
      snprintf(r->bank[i].name, sizeof r->bank[i].name, "%s", BANK_KIND_LABELS[pw.kindmap[sel]]);
      r->bank[i].id[0] = 0;                       // not in its own way while a new id is made
      make_id(r, r->bank[i].name, fresh);
      snprintf(r->bank[i].id, sizeof r->bank[i].id, "%s", fresh);
      for (uint8_t k = 0; k < r->flow_count; k++) {   // sources name banks by id: they follow
        if (!strcmp(r->flow[k].to, old)) snprintf(r->flow[k].to, sizeof r->flow[k].to, "%s", fresh);
        if (!strcmp(r->flow[k].from, old)) snprintf(r->flow[k].from, sizeof r->flow[k].from, "%s", fresh);
      }
      break;
    }
    case OP_BANK_BATT:
      if (i < r->bank_count) snprintf(r->bank[i].battery, sizeof r->bank[i].battery, "%s", pw.cand[sel]);
      break;
    case OP_FLOW_KIND:
      if (i >= r->flow_count) break;
      snprintf(r->flow[i].kind, sizeof r->flow[i].kind, "%s", FLOW_KINDS[sel]);
      if (!strcmp(r->flow[i].kind, "converter")) {
        if (!r->flow[i].from[0] || !strcmp(r->flow[i].from, r->flow[i].to)) first_other_bank(r, r->flow[i].to, r->flow[i].from);
      } else {
        r->flow[i].from[0] = 0;
      }
      break;
    case OP_FLOW_TO:
      if (i >= r->flow_count || sel >= r->bank_count) break;
      snprintf(r->flow[i].to, sizeof r->flow[i].to, "%s", r->bank[sel].id);
      if (!strcmp(r->flow[i].kind, "converter") && !strcmp(r->flow[i].from, r->flow[i].to)) first_other_bank(r, r->flow[i].to, r->flow[i].from);
      break;
    case OP_FLOW_FROM:
      if (i >= r->flow_count || sel >= r->bank_count) break;
      if (!strcmp(r->bank[sel].id, r->flow[i].to)) { fail("A converter draws from one bank and feeds another."); break; }
      snprintf(r->flow[i].from, sizeof r->flow[i].from, "%s", r->bank[sel].id);
      break;
    case OP_FLOW_OUT:
      if (i < r->flow_count) r->flow[i].output = (uint8_t)sel;
      break;
    case OP_FEEDS:   // i is the seen row; sel the bank it feeds
      if (i < pw.n && sel < r->bank_count) new_flow(r, &pw.seen[i], r->bank[sel].id);
      break;
  }
  setup_rebuild();
}

static void open_menu(uint8_t op, uint8_t i) {
  uint8_t sel;
  const char *title = "";
  cfg_role_t *r = power_role();
  if (!r) return;
  uint8_t n = build_menu(r, op, i, &title, &sel);
  if (n == 0) {
    fail(op == OP_BANK_BATT ? "No battery is seen." : op == OP_NEW_BANK ? "Every kind of bank is already added." : "Nothing to choose from.");
    setup_rebuild();
    return;
  }
  setup_menu(title, pw.optp, n, sel, picked, (uintptr_t)((op << 8) | i));
}

static void on_choice(lv_event_t *e) {
  uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  open_menu((uint8_t)(v >> 8), (uint8_t)(v & 255));
}

static void on_btn(lv_event_t *e) {
  uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  uint8_t op = (uint8_t)(v >> 8), i = (uint8_t)(v & 255);
  cfg_role_t *r = power_role();
  if (!r) return;
  touch(r);
  switch (op) {
    case OP_ADD_TO:   // a seen battery that is in no bank: ask which bank it is
      if (i < pw.n && pw.seen[i].ref[0]) {
        snprintf(pw.pending, sizeof pw.pending, "%s", pw.seen[i].ref);
        open_menu(OP_NEW_BANK, 0);
      }
      return;
    case OP_ADD_BANK: {
      const ui_power_row_t *pick = NULL;
      for (uint8_t k = 0; k < pw.n && !pick; k++)
        if (pw.seen[k].fn == UI_PW_BATTERY && pw.seen[k].ref[0] && !battery_placed(r, pw.seen[k].ref, -1)) pick = &pw.seen[k];
      if (pick) {   // it takes the first battery that is in no bank; its battery button changes that
        snprintf(pw.pending, sizeof pw.pending, "%s", pick->ref);
        open_menu(OP_NEW_BANK, 0);
        return;
      }
      fail("No unplaced battery is seen.");
      break;
    }
    case OP_BANK_DEL:
      if (i >= r->bank_count) break;
      for (uint8_t k = r->flow_count; k-- > 0;) {   // what fed or fed from it goes back to Unassigned: a source never has no bank
        if (strcmp(r->flow[k].to, r->bank[i].id) && strcmp(r->flow[k].from, r->bank[i].id)) continue;
        memmove(&r->flow[k], &r->flow[k + 1], (size_t)(r->flow_count - k - 1) * sizeof r->flow[0]);
        r->flow_count--;
      }
      memmove(&r->bank[i], &r->bank[i + 1], (size_t)(r->bank_count - i - 1) * sizeof r->bank[0]);
      r->bank_count--;
      break;
    case OP_FLOW_DEL:
      if (i >= r->flow_count) break;
      memmove(&r->flow[i], &r->flow[i + 1], (size_t)(r->flow_count - i - 1) * sizeof r->flow[0]);
      r->flow_count--;
      break;
    case OP_ONE_BANK:
      if (r->bank_count == 0) one_bank(r);
      break;
  }
  setup_rebuild();
}

#define OP(op, i) ((void *)(uintptr_t)(((op) << 8) | (i)))

static lv_obj_t *button(lv_obj_t *row, const char *label, uint8_t op, uint8_t i, const char *part_prefix, int32_t w) {
  lv_obj_t *b = setup_button(row, label, on_btn, OP(op, i));
  if (w) lv_obj_set_width(b, w);
  if (part_prefix) {
    char name[56];
    snprintf(name, sizeof name, "%s_%u", part_prefix, i);
    setup_part(name, b);
  }
  return b;
}

// A button that shows its value and opens the menu of the other choices.
static lv_obj_t *choice(lv_obj_t *row, const char *value, uint8_t op, uint8_t i, const char *part_name, int32_t w) {
  char text[64];
  snprintf(text, sizeof text, "%s " LV_SYMBOL_DOWN, value);
  lv_obj_t *b = setup_button(row, text, on_choice, OP(op, i));
  if (w) lv_obj_set_width(b, w);
  setup_part(part_name, b);
  return b;
}

// ---- the page ----

static uint16_t shape_of(const ui_power_row_t *rows, uint8_t n) {
  uint16_t h = n;
  for (uint8_t i = 0; i < n; i++) {
    h = (uint16_t)(h * 31 + (rows[i].ref[0] ? 7 : 3) + rows[i].fn);
    for (const char *c = rows[i].ref; *c; c++) h = (uint16_t)(h * 31 + (uint8_t)*c);
  }
  return h;
}

static void read_seen(void) {
  const ui_setup_cb_t *cb = setup_cb();
  pw.n = cb->power_rows ? cb->power_rows(pw.seen, UI_POWER_ROWS) : 0;
  pw.shape = shape_of(pw.seen, pw.n);
}

static void bank_label(const cfg_role_t *r, int bi, char *out, size_t n) {
  if (bi >= 0) upper(r->bank[bi].name[0] ? r->bank[bi].name : r->bank[bi].kind, out, n);
  else snprintf(out, n, "CHOOSE BANK");
}

void setup_build_power(void) {
  const ui_colors_t *c = ui_theme();
  lv_obj_t *body = setup_body();
  cfg_role_t *r = power_role();
  const bool wide = setup_wide();
  read_seen();
  pw.checked_ms = lv_tick_get();
  if (!r) {
    lv_obj_t *l = setup_text(body, "Add the Power role on the Roles screen first.", c->dim);
    lv_obj_set_width(l, lv_pct(100));
    return;
  }
  if (r->power_old_keys || r->power_rejected) {
    lv_obj_t *n = setup_text(body, r->power_old_keys
                                       ? "The old-format banks and links in config.json are not read. Set them up here; SAVE replaces them."
                                       : "The banks and flows in config.json are invalid and were not loaded. SAVE replaces them.",
                             c->warn);
    lv_obj_set_width(n, lv_pct(100));
    setup_part("power_notice", n);
  }
  pw.error_label = setup_text(body, pw.error, c->warn);
  lv_obj_set_width(pw.error_label, lv_pct(100));
  setup_part("power_error", pw.error_label);

  // Banks: which bank it is, then which battery
  setup_text(body, "BANKS", c->accent);
  for (uint8_t i = 0; i < r->bank_count; i++) {
    const cfg_bank_t *k = &r->bank[i];
    lv_obj_t *row = setup_row(body);
    char reading[56] = "", pn[32], kind[16];
    const ui_power_row_t *s = seen_ref(k->battery);
    if (s) battery_text(s, reading, sizeof reading);
    int kn = 0;
    for (int q = 0; q < N_BANK_KINDS; q++)
      if (!strcmp(k->kind, BANK_KINDS[q])) kn = q;
    upper(k->name[0] ? k->name : BANK_KIND_LABELS[kn], kind, sizeof kind);   // the name the source menus show
    snprintf(pn, sizeof pn, "bank_kind_%u", i);
    lv_obj_t *kb = choice(row, kind, OP_BANK_KIND, i, pn, wide ? 190 : lv_pct(78));   // the bank is its kind: leftmost
    (void)kb;
    if (!wide) button(row, LV_SYMBOL_CLOSE, OP_BANK_DEL, i, "bank_del", 0);   // narrow: the kind and remove first
    if (!s) {   // its battery is not seen now
      lv_obj_t *m = setup_text(row, "MISSING", c->warn);
      snprintf(pn, sizeof pn, "missing_bank_%u", i);
      setup_part(pn, m);
    }
    const char *t = "";
    uint8_t sel;
    if (build_menu(r, OP_BANK_BATT, i, &t, &sel)) {   // the battery's name and reading, and a menu of the others
      snprintf(pn, sizeof pn, "bank_batt_%u", i);
      choice(row, s ? reading : "CHOOSE BATTERY", OP_BANK_BATT, i, pn, 0);
    } else if (s) {
      setup_text(row, reading, c->dim);
    }
    if (wide) button(row, LV_SYMBOL_CLOSE, OP_BANK_DEL, i, "bank_del", 0);
  }
  setup_part("add_bank", setup_button(body, "ADD BANK", on_btn, OP(OP_ADD_BANK, 0)));

  // Sources: each says what it is and which bank it feeds
  setup_text(body, "SOURCES", c->accent);
  for (uint8_t i = 0; i < r->flow_count; i++) {
    const cfg_flow_t *f = &r->flow[i];
    lv_obj_t *row = setup_row(body);
    const ui_power_row_t *o = output_of(f);
    char line[64], v[16], pn[32], label[24];
    if (o) {
      value_text(o, v, sizeof v);
      if (outputs_of(f->device, o->group) > 1) snprintf(line, sizeof line, "%s output %u  %s", o->label, f->output + 1, v);
      else snprintf(line, sizeof line, "%s  %s", o->label, v);
      lv_obj_t *t = setup_text(row, line, c->fg);
      lv_obj_set_width(t, wide ? 260 : lv_pct(100));
    } else {
      lv_obj_t *t = setup_text(row, "MISSING", c->warn);
      if (!wide) lv_obj_set_width(t, lv_pct(100));
      snprintf(pn, sizeof pn, "missing_flow_%u", i);
      setup_part(pn, t);
    }
    int kn = 0;
    for (int k = 0; k < N_FLOW_KINDS; k++)
      if (!strcmp(f->kind, FLOW_KINDS[k])) kn = k;
    snprintf(pn, sizeof pn, "flow_kind_%u", i);
    choice(row, FLOW_KIND_LABELS[kn], OP_FLOW_KIND, i, pn, 0);
    bool conv = !strcmp(f->kind, "converter");
    if (conv) {
      bank_label(r, bank_index(r, f->from), label, sizeof label);
      setup_text(row, "FROM", c->dim);
      snprintf(pn, sizeof pn, "flow_from_%u", i);
      choice(row, label, OP_FLOW_FROM, i, pn, 0);
    }
    bank_label(r, bank_index(r, f->to), label, sizeof label);
    setup_text(row, conv ? "TO" : "FEEDS", c->dim);
    snprintf(pn, sizeof pn, "flow_to_%u", i);
    choice(row, label, OP_FLOW_TO, i, pn, 0);
    if (o && outputs_of(f->device, o->group) > 1) {
      snprintf(label, sizeof label, "OUTPUT %u", f->output + 1);
      snprintf(pn, sizeof pn, "flow_out_%u", i);
      choice(row, label, OP_FLOW_OUT, i, pn, 0);
    }
    button(row, LV_SYMBOL_CLOSE, OP_FLOW_DEL, i, "flow_del", 0);
  }

  // Unassigned: seen, and in no bank and feeding none
  uint8_t listed = 0, identifying = 0;
  for (uint8_t k = 0; k < pw.n; k++) {
    const ui_power_row_t *s = &pw.seen[k];
    bool placed = false;
    if (s->fn == UI_PW_BATTERY) {
      placed = s->ref[0] && battery_placed(r, s->ref, -1);
    } else {
      for (uint8_t f = 0; f < r->flow_count && s->ref[0]; f++)
        if (output_of(&r->flow[f]) == s) placed = true;
    }
    if (placed) continue;
    if (!listed++) {
      setup_text(body, "UNASSIGNED", c->accent);
      if (r->bank_count == 0) setup_part("one_bank", setup_button(body, "ONE BANK", on_btn, OP(OP_ONE_BANK, 0)));
    }
    lv_obj_t *row = setup_row(body);
    char line[72], v[16], pn[56];
    value_text(s, v, sizeof v);
    if (!s->ref[0]) {   // the device has not said who it is yet; it can be added a few seconds from now
      identifying++;
      snprintf(line, sizeof line, "%s  %s  Identifying...", s->label, v);
      lv_obj_t *t = setup_text(row, line, c->dim);
      lv_obj_set_width(t, lv_pct(100));
      snprintf(pn, sizeof pn, "wait_%u", k);
      setup_part(pn, t);
      continue;
    }
    if (s->fn == UI_PW_BATTERY) battery_text(s, line, sizeof line);
    else snprintf(line, sizeof line, "%s  %s", s->label, v);
    lv_obj_t *t = setup_text(row, line, c->fg);
    lv_obj_set_width(t, wide ? 330 : lv_pct(100));
    if (s->fn == UI_PW_BATTERY) {
      snprintf(pn, sizeof pn, "addto_%s", s->ref);
      setup_part(pn, setup_button(row, "ADD TO BANK", on_btn, OP(OP_ADD_TO, k)));
    } else if (r->bank_count) {
      snprintf(pn, sizeof pn, "feeds_%s", s->ref);
      choice(row, "ADD TO BANK", OP_FEEDS, k, pn, 0);
    } else {
      setup_text(row, "Add a bank first", c->dim);
    }
  }
  if (identifying) {
    lv_obj_t *n = setup_text(body, "A device appears here as soon as it has identified itself, a few seconds after it is seen.", c->dim);
    lv_obj_set_width(n, lv_pct(100));
    setup_part("identifying_note", n);
  }
}

// SAVE: the edited topology must stand on its own, or the file would hold something that loads as "not set up".
bool setup_power_can_save(void) {
  cfg_role_t *r = power_role();
  if (!r) return true;
  char why[96];
  if (cfg_power_check(r, why, sizeof why)) {
    pw.error[0] = 0;
    return true;
  }
  fail(why);
  if (pw.error_label && lv_obj_is_valid(pw.error_label)) lv_label_set_text(pw.error_label, pw.error);
  return false;
}

// A device identifies itself, appears or goes: the rows change. Values are only as fresh as the last build.
void setup_power_tick(void) {
  uint32_t now = lv_tick_get();
  if (now - pw.checked_ms < 2000) return;
  pw.checked_ms = now;
  if (ui_setup_part("kb") || ui_setup_part("menu")) return;   // typing or choosing: leave the screen alone
  PLATFORM_EXT_BSS static ui_power_row_t fresh[UI_POWER_ROWS];   // 2 KB: not for the UI task's stack
  const ui_setup_cb_t *cb = setup_cb();
  uint8_t n = cb->power_rows ? cb->power_rows(fresh, UI_POWER_ROWS) : 0;
  if (shape_of(fresh, n) != pw.shape) setup_rebuild();
}
