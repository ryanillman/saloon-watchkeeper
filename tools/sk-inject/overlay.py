"""Overlays: timed operations layered over the baseline boat. Pure logic, no I/O.

An overlay never names a notifications.* path: alarms come from the server reacting to zones.
"""
from dataclasses import dataclass
from typing import Any, Optional

STATES = ("alert", "warn", "alarm", "emergency")


class OverlayError(ValueError):
    pass


@dataclass(frozen=True)
class Set:
    path: str
    value: Any


@dataclass(frozen=True)
class Ramp:
    path: str
    start: Optional[float]   # None = from the path's current value
    to: float
    over_ms: float


@dataclass(frozen=True)
class Silent:
    path: str


@dataclass(frozen=True)
class GroupOff:
    group: str


@dataclass(frozen=True)
class GroupOn:
    group: str


@dataclass(frozen=True)
class Alarm:
    path: str
    state: str
    hold_ms: Optional[float]   # None = until a clear or the end of the overlay


@dataclass(frozen=True)
class Clear:
    path: str


class Overlay:
    def __init__(self, description, duration_ms, profile, with_groups, events):
        self.description = description
        self.duration_ms = duration_ms
        self.profile = profile
        self.with_groups = with_groups
        self._events = sorted(events, key=lambda e: e[0])   # stable: file order within a time

    def step(self, t_ms):
        """Actions that have come due by t_ms, each returned exactly once."""
        due = []
        while self._events and self._events[0][0] <= t_ms:
            due.append(self._events.pop(0)[1])
        return due


def _path(ev, baseline):
    p = ev.get("path")
    if not isinstance(p, str):
        raise OverlayError(f"{ev.get('op')}: needs a path")
    if p.startswith("notifications."):
        raise OverlayError(f"{p}: overlays never write notifications.*; use an alarm step "
                           "and let the server's zones raise it")
    if p not in baseline.paths():
        raise OverlayError(f"{p}: not in baseline.json")
    return p


def _group(ev, baseline):
    g = ev.get("group")
    if g not in baseline.groups():
        raise OverlayError(f"{ev.get('op')}: unknown group {g!r}")
    return g


def _action(ev, baseline):
    op = ev.get("op")
    if op == "set":
        return Set(_path(ev, baseline), ev.get("value"))
    if op == "null":
        return Set(_path(ev, baseline), None)
    if op == "silent":
        return Silent(_path(ev, baseline))
    if op == "ramp":
        if "to" not in ev or "overMs" not in ev:
            raise OverlayError("ramp: needs to and overMs")
        return Ramp(_path(ev, baseline), ev.get("from"), ev["to"], ev["overMs"])
    if op == "group-off":
        return GroupOff(_group(ev, baseline))
    if op == "group-on":
        return GroupOn(_group(ev, baseline))
    if op == "alarm":
        if ev.get("state") not in STATES:
            raise OverlayError(f"alarm: state must be one of {', '.join(STATES)}")
        return Alarm(_path(ev, baseline), ev["state"], ev.get("holdMs"))
    if op == "clear":
        return Clear(_path(ev, baseline))
    raise OverlayError(f"unknown op {op!r}")


def parse_overlay(doc, baseline):
    profile = doc.get("profile")
    if profile is not None and profile not in baseline.profiles:
        raise OverlayError(f"unknown profile {profile!r}")
    with_groups = set(doc.get("with", []))
    for g in with_groups:
        if g not in baseline.groups():
            raise OverlayError(f"with: unknown group {g!r}")
    events = [(ev["atMs"], _action(ev, baseline)) for ev in doc.get("after", [])]
    return Overlay(doc.get("description", ""), doc.get("durationMs", 60000), profile,
                   with_groups, events)
