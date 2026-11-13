"""The baseline boat: routine values for every Signal K path the firmware covers.

Pure logic, no I/O beyond loading baseline.json, so it is unit-tested without a server.
"""
import copy
import datetime
import json
import math
import zlib
from pathlib import Path
from typing import Any, NamedTuple

TIDE_HIGH_M = 3.2
TIDE_LOW_M = 0.4
TIDE_PERIOD_S = 12 * 3600 + 25 * 60
TIDE_T0 = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)   # a reference high water


class TideState(NamedTuple):
    height_m: float
    next_high: datetime.datetime
    next_low: datetime.datetime


def tide_state(now: datetime.datetime) -> TideState:
    """The bench tide at wall-clock `now`: a cosine between TIDE_HIGH_M and TIDE_LOW_M, so a tides plugin's
    next high and low really move with time (a constant offset from now would draw the same graph forever)."""
    elapsed = (now - TIDE_T0).total_seconds()
    mid, amp = (TIDE_HIGH_M + TIDE_LOW_M) / 2, (TIDE_HIGH_M - TIDE_LOW_M) / 2
    height = mid + amp * math.cos(2 * math.pi * elapsed / TIDE_PERIOD_S)
    half = TIDE_PERIOD_S / 2

    def next_after(offset_s: float) -> datetime.datetime:
        k = math.floor((elapsed - offset_s) / TIDE_PERIOD_S) + 1
        return TIDE_T0 + datetime.timedelta(seconds=offset_s + k * TIDE_PERIOD_S)

    return TideState(height, next_after(0), next_after(half))


def build_delta(values: dict[str, Any], real_updates: list[dict], label: str = "sk-inject") -> dict:
    """The delta one tick sends: the baseline values as one update, then each real device's own update, so the
    server stamps every value with the device it came from."""
    updates = [{"source": {"label": label}, "values": [{"path": p, "value": v} for p, v in values.items()]}]
    return {"context": "vessels.self", "updates": updates + copy.deepcopy(real_updates)}


class Baseline:
    def __init__(self, doc: dict, real: list[dict] | None = None):
        self.real: list[dict] = real or []
        self.rows: list[dict] = doc["rows"]
        self.profiles: dict[str, dict] = doc["profiles"]
        self.optional: set[str] = set(doc.get("optionalGroups", []))

    def paths(self) -> set[str]:
        return {r["path"] for r in self.rows}

    def groups(self) -> set[str]:
        return {r["group"] for r in self.rows}

    def real_enabled(self, profile: str) -> bool:
        """A profile can send a captured boat's electrical data (real-electrical.json) instead of the synthetic one."""
        return bool(self.profiles[profile].get("realElectrical"))

    def real_updates(self, profile: str) -> list[dict]:
        return copy.deepcopy(self.real) if self.real_enabled(profile) else []

    def default_on(self, profile: str) -> set[str]:
        """Groups that are on for a profile: every non-optional group minus the profile's off list."""
        off = set(self.profiles[profile].get("groupsOff", []))
        return (self.groups() - self.optional) - off

    def values(self, t_ms: float, profile: str, groups_on: set[str],
               overrides: dict[str, Any]) -> dict[str, Any]:
        """Every path's value at t_ms. A caller override replaces the final value (None = null)."""
        prof = self.profiles[profile].get("overrides", {})
        silent = set(self.profiles[profile].get("silentGroups", []))
        out: dict[str, Any] = {}
        for r in self.rows:
            path, group = r["path"], r["group"]
            if path in overrides:
                out[path] = overrides[path]
                continue
            if group in silent:   # sent nothing. Not null: a path that exists with a null value is a failed sender
                continue
            if group in self.optional and group not in groups_on:
                continue
            if group not in groups_on and "off" in r:
                out[path] = r["off"]
                continue
            nominal = prof.get(path, r["nominal"])
            out[path] = _resolve(nominal, r, path, t_ms)
        return out


def _iso(when: datetime.datetime) -> str:
    return when.isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _resolve(nominal: Any, row: dict, path: str, t_ms: float) -> Any:
    if nominal == "$now":
        return _iso(datetime.datetime.now(datetime.timezone.utc))
    if isinstance(nominal, str) and nominal.startswith("$tide:"):
        t = tide_state(datetime.datetime.now(datetime.timezone.utc))
        return {"height": t.height_m, "high": _iso(t.next_high), "low": _iso(t.next_low)}[nominal[6:]]
    if isinstance(nominal, str) and nominal.startswith("$now+") and nominal.endswith("m"):
        # "$now+152m": that many minutes ahead (a tide plugin's next high or low), always in the future
        return _iso(datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(minutes=float(nominal[5:-1])))
    if isinstance(nominal, (dict, list)):
        return copy.deepcopy(nominal)   # an object value (a position): never share the table's copy
    if not isinstance(nominal, (int, float)) or isinstance(nominal, bool):
        return nominal
    v = nominal + row.get("drift", 0) * (t_ms / 60000.0)
    wobble = row.get("wobble", 0)
    if wobble:
        phase = (zlib.crc32(path.encode()) % 628) / 100.0
        v += wobble * math.sin(t_ms / 1500.0 + phase)
    return v


def load_baseline(path) -> Baseline:
    path = Path(path)
    doc = json.loads(path.read_text())
    real = None
    if doc.get("realElectrical"):   # a file next to baseline.json, made by mkreal.py
        real = json.loads((path.parent / doc["realElectrical"]).read_text())["updates"]
    return Baseline(doc, real)
