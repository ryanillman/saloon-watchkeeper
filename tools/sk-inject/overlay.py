"""Overlays: timed operations layered over the baseline boat. Pure logic, no I/O.

An overlay never names a notifications.* path: alarms come from the server reacting to zones. The one exception is the
notify op, for the emergency natures (mob, fire, flooding, sinking, emergency): they have no zone to cross, so the boat
sends the notification itself, as a plugin would.
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


@dataclass(frozen=True)
class Post:
    """POST to a server plugin path, e.g. /plugins/anchoralarm/dropAnchor (what the displays' buttons do)."""
    path: str
    body: Optional[dict]


@dataclass(frozen=True)
class Put:
    """A Signal K PUT of a value to a spec path, e.g. navigation.anchor.maxRadius."""
    path: str
    value: Any
    source: Optional[str] = None   # needed when more than one source publishes the path (two anchor plugins)


@dataclass(frozen=True)
class Move:
    """Ramp a position path to an offset in metres from its baseline position (the anchor point)."""
    path: str
    north_m: float
    east_m: float
    over_ms: float


NATURES = {"mob": "MAN OVERBOARD", "fire": "FIRE", "flooding": "FLOODING", "sinking": "SINKING", "emergency": "EMERGENCY"}


@dataclass(frozen=True)
class Notify:
    """An emergency notification sent straight to Signal K (notifications.<nature>). state is "emergency" or "normal"."""
    nature: str
    state: str
    message: Optional[str]
    position: Optional[tuple]   # (latitude, longitude); only a MOB carries one


def notify_delta(n: Notify) -> dict:
    """The delta the boat sends for a Notify."""
    if n.state == "normal":
        value: dict = {"state": "normal"}
    else:
        value = {"state": "emergency", "method": ["visual", "sound"], "message": n.message or NATURES[n.nature]}
        if n.nature == "mob" and n.position:
            value["data"] = {"position": {"latitude": n.position[0], "longitude": n.position[1]}}
    return {"context": "vessels.self",
            "updates": [{"source": {"label": "sk-inject"}, "values": [{"path": f"notifications.{n.nature}", "value": value}]}]}


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
    if op == "notify":
        nature = ev.get("nature")
        if nature not in NATURES:
            raise OverlayError(f"notify: nature must be one of {', '.join(NATURES)}")
        if ev.get("state") not in ("emergency", "normal"):
            raise OverlayError("notify: state must be emergency or normal")
        pos = ev.get("position")
        if pos is not None:
            ok = (isinstance(pos, list) and len(pos) == 2 and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in pos)
                  and -90 <= pos[0] <= 90 and -180 <= pos[1] <= 180)
            if not ok:
                raise OverlayError("notify: position must be [latitude, longitude]")
            pos = (float(pos[0]), float(pos[1]))
        msg = ev.get("message")
        if msg is not None and not isinstance(msg, str):
            raise OverlayError("notify: message must be a string")
        return Notify(nature, ev["state"], msg, pos)
    if op == "post":
        p = ev.get("path")
        if not isinstance(p, str) or not p.startswith("/plugins/") or ".." in p:
            raise OverlayError("post: path must be a plugin path such as /plugins/anchoralarm/dropAnchor")
        body = ev.get("body")
        if body is not None and not isinstance(body, dict):
            raise OverlayError("post: body must be an object")
        return Post(p, body)
    if op == "put":
        p = ev.get("path")
        if not isinstance(p, str) or not p:
            raise OverlayError("put: needs a path")
        if p.startswith("notifications."):
            raise OverlayError(f"{p}: overlays never write notifications.*")
        if "value" not in ev:
            raise OverlayError("put: needs a value")
        src = ev.get("source")
        if src is not None and not isinstance(src, str):
            raise OverlayError("put: source must be a string")
        return Put(p, ev["value"], src)
    if op == "move":
        p = _path(ev, baseline)
        row = next(r for r in baseline.rows if r["path"] == p)
        nominal = row["nominal"]
        if not (isinstance(nominal, dict) and "latitude" in nominal and "longitude" in nominal):
            raise OverlayError(f"move: {p} is not a position path")
        if not all(k in ev for k in ("northM", "eastM", "overMs")):
            raise OverlayError("move: needs northM, eastM and overMs")
        return Move(p, float(ev["northM"]), float(ev["eastM"]), ev["overMs"])
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
