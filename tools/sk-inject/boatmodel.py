"""The baseline boat: routine values for every Signal K path the firmware covers.

Pure logic, no I/O beyond loading baseline.json, so it is unit-tested without a server.
"""
import copy
import datetime
import json
import math
import zlib
from pathlib import Path
from typing import Any


class Baseline:
    def __init__(self, doc: dict):
        self.rows: list[dict] = doc["rows"]
        self.profiles: dict[str, dict] = doc["profiles"]
        self.optional: set[str] = set(doc.get("optionalGroups", []))

    def paths(self) -> set[str]:
        return {r["path"] for r in self.rows}

    def groups(self) -> set[str]:
        return {r["group"] for r in self.rows}

    def default_on(self, profile: str) -> set[str]:
        """Groups that are on for a profile: every non-optional group minus the profile's off list."""
        off = set(self.profiles[profile].get("groupsOff", []))
        return (self.groups() - self.optional) - off

    def values(self, t_ms: float, profile: str, groups_on: set[str],
               overrides: dict[str, Any]) -> dict[str, Any]:
        """Every path's value at t_ms. A caller override replaces the final value (None = null)."""
        prof = self.profiles[profile].get("overrides", {})
        out: dict[str, Any] = {}
        for r in self.rows:
            path, group = r["path"], r["group"]
            if path in overrides:
                out[path] = overrides[path]
                continue
            if group in self.optional and group not in groups_on:
                continue
            if group not in groups_on and "off" in r:
                out[path] = r["off"]
                continue
            nominal = prof.get(path, r["nominal"])
            out[path] = _resolve(nominal, r, path, t_ms)
        return out


def _resolve(nominal: Any, row: dict, path: str, t_ms: float) -> Any:
    if nominal == "$now":
        return datetime.datetime.now(datetime.timezone.utc).isoformat(
            timespec="milliseconds").replace("+00:00", "Z")
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
    return Baseline(json.loads(Path(path).read_text()))
