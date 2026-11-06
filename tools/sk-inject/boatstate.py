"""What the boat is doing right now: baseline plus running overlays. Pure logic, driven by tick(now_ms).

The daemon (boat.py) calls tick() each interval and sends the returned values to Signal K. Alarm
actions come back to the daemon because they need the server (zones); it then calls drive_to().
"""
import math
from dataclasses import dataclass
from typing import Any, Callable, Optional

from overlay import Alarm, Clear, GroupOff, GroupOn, Move, Notify, Post, Put, Ramp, Set, Silent

M_PER_DEG = 111320.0   # metres per degree of latitude (close enough for a test boat)


MANUAL = "manual"   # owner of overrides made from the command line: released only by hold or clear


@dataclass
class _Override:
    owner: str
    fn: Callable[[float], Any]
    release_at: Optional[float] = None


@dataclass
class _Running:
    owner: str
    name: str
    overlay: Any
    start_ms: float
    touched_groups: bool


def _ramp(t0, start, to, over_ms):
    def fn(t):
        if not isinstance(start, (int, float)) or isinstance(start, bool):
            return to
        frac = 1.0 if over_ms <= 0 else min(1.0, max(0.0, (t - t0) / over_ms))
        return start + (to - start) * frac
    return fn


def _offset_m(pos, nominal):
    """(north, east) metres of pos from nominal; (0, 0) when pos is not a position (a null, say)."""
    if not (isinstance(pos, dict) and "latitude" in pos and "longitude" in pos):
        return 0.0, 0.0
    lat0 = nominal["latitude"]
    return ((pos["latitude"] - lat0) * M_PER_DEG,
            (pos["longitude"] - nominal["longitude"]) * M_PER_DEG * math.cos(math.radians(lat0)))


def _move(t0, nominal, start, north_m, east_m, over_ms):
    lat0, lon0 = nominal["latitude"], nominal["longitude"]
    cos = math.cos(math.radians(lat0))

    def fn(t):
        frac = 1.0 if over_ms <= 0 else min(1.0, max(0.0, (t - t0) / over_ms))
        n = start[0] + (north_m - start[0]) * frac
        e = start[1] + (east_m - start[1]) * frac
        return {"latitude": lat0 + n / M_PER_DEG, "longitude": lon0 + e / (M_PER_DEG * cos)}
    return fn


class BoatState:
    def __init__(self, baseline, profile="underway", with_groups=()):
        self.baseline = baseline
        self.base_profile = profile
        self.base_groups = baseline.default_on(profile) | set(with_groups)
        self.profile = profile
        self.groups_on = set(self.base_groups)
        self._overrides: dict[str, _Override] = {}
        self._silent: dict[str, str] = {}
        self._running: list[_Running] = []
        self._n = 0

    # -- queries ----------------------------------------------------------------------
    def current(self, path, now_ms):
        """The value the boat would send for path right now (before silencing)."""
        o = self._overrides.get(path)
        if o is not None:
            return o.fn(now_ms)
        return self.baseline.values(now_ms, self.profile, self.groups_on, {}).get(path)

    def status(self, now_ms):
        return {
            "profile": self.profile,
            "groups": sorted(self.groups_on),
            "overlays": [{"name": r.name, "elapsedMs": now_ms - r.start_ms,
                          "durationMs": r.overlay.duration_ms} for r in self._running],
            "overrides": sorted(self._overrides),
            "silent": sorted(self._silent),
        }

    # -- control ----------------------------------------------------------------------
    def start_overlay(self, name, overlay, now_ms):
        self._n += 1
        owner = f"{name}#{self._n}"
        touched = bool(overlay.profile or overlay.with_groups)
        if touched:
            self.profile = overlay.profile or self.base_profile
            self.groups_on = self.baseline.default_on(self.profile) | overlay.with_groups
        self._running.append(_Running(owner, name, overlay, now_ms, touched))
        return owner

    def drive_to(self, owner, path, target, now_ms, ramp_ms, hold_ms):
        """Ramp path to target, hold, then fall back to the baseline (hold_ms None = until cleared).

        False, and nothing changed, if owner is an overlay that has already ended: an alarm step
        takes seconds (zones are read and written first) and nothing would ever release a late drive.
        """
        if owner != MANUAL and not any(r.owner == owner for r in self._running):
            return False
        start = self.current(path, now_ms)
        release = None if hold_ms is None else now_ms + ramp_ms + hold_ms
        self._overrides[path] = _Override(owner, _ramp(now_ms, start, target, ramp_ms), release)
        return True

    def clear(self, path):
        self._overrides.pop(path, None)

    # -- the clock --------------------------------------------------------------------
    def tick(self, now_ms):
        """Advance to now_ms. Returns (values to send, [(owner, Alarm)] for the daemon, log lines)."""
        alarms, msgs = [], []
        for r in list(self._running):
            t = now_ms - r.start_ms
            for a in r.overlay.step(t):
                msgs.append(f"{t / 1000:5.1f}s  {r.name}: {a}")
                if isinstance(a, (Alarm, Post, Put, Notify)):   # need the server: handed to the daemon
                    alarms.append((r.owner, a))
                else:
                    self._apply(r, a, now_ms)
            if t >= r.overlay.duration_ms:
                self._end(r)
                msgs.append(f"{r.name} done")
        for path, o in list(self._overrides.items()):
            if o.release_at is not None and now_ms >= o.release_at:
                del self._overrides[path]
        overrides = {p: o.fn(now_ms) for p, o in self._overrides.items()}
        values = self.baseline.values(now_ms, self.profile, self.groups_on, overrides)
        for p in self._silent:
            values.pop(p, None)
        return values, alarms, msgs

    def _apply(self, r, a, now_ms):
        if isinstance(a, Set):
            self._overrides[a.path] = _Override(r.owner, lambda t, v=a.value: v)
        elif isinstance(a, Ramp):
            start = a.start if a.start is not None else self.current(a.path, now_ms)
            self._overrides[a.path] = _Override(r.owner, _ramp(now_ms, start, a.to, a.over_ms))
        elif isinstance(a, Move):
            nominal = self.baseline.values(now_ms, self.profile, self.groups_on, {})[a.path]
            start = _offset_m(self.current(a.path, now_ms), nominal)
            self._overrides[a.path] = _Override(
                r.owner, _move(now_ms, nominal, start, a.north_m, a.east_m, a.over_ms))
        elif isinstance(a, Silent):
            self._silent[a.path] = r.owner
        elif isinstance(a, GroupOff):
            self.groups_on.discard(a.group)
            r.touched_groups = True
        elif isinstance(a, GroupOn):
            self.groups_on.add(a.group)
            r.touched_groups = True
        elif isinstance(a, Clear):
            self.clear(a.path)

    def _end(self, r):
        self._running.remove(r)
        for path in [p for p, o in self._overrides.items() if o.owner == r.owner]:
            del self._overrides[path]
        for path in [p for p, owner in self._silent.items() if owner == r.owner]:
            del self._silent[path]
        if r.touched_groups:
            self.profile = self.base_profile
            self.groups_on = set(self.base_groups)
