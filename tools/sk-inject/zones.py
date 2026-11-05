"""Signal K zones: pick a value that lands in a zone, and default zone sets for paths with none.

Mirrors signalk-server dist/zones.js: a value is in the first zone with lower <= value < upper
(missing bounds are open); in no zone, or null, it is "normal".
"""
import asyncio
import copy
import fnmatch
import json
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
CREATED_FILE = HERE / "zones-created.json"
_DEFAULTS = json.loads((HERE / "zones-default.json").read_text())


class NoSuchState(Exception):
    def __init__(self, states):
        super().__init__(f"no zone with that state; zones have: {', '.join(states) or 'none'}")
        self.states = states


def _in(zone, value):
    return zone.get("lower", float("-inf")) <= value < zone.get("upper", float("inf"))


def zone_index(zones, value):
    """Index of the first zone containing value, or -1 (the server then reports normal)."""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return -1
    for i, z in enumerate(zones):
        if _in(z, value):
            return i
    return -1


def _candidates(zone):
    lo, hi = zone.get("lower"), zone.get("upper")
    if lo is None and hi is None:
        return [0.0]
    if lo is None or hi is None:
        bound = lo if lo is not None else hi
        span = max(1.0, abs(bound) * 0.1)
        return [bound + span] if lo is not None else [bound - span]
    return [lo + f * (hi - lo) for f in (0.5, 0.25, 0.75, 0.0)]


def pick_target(zones, state):
    """A finite value that the server will place in the first zone with this state."""
    matches = [i for i, z in enumerate(zones) if z.get("state") == state]
    if not matches:
        raise NoSuchState([z.get("state") for z in zones])
    for i in matches:
        for v in _candidates(zones[i]):
            if zone_index(zones, v) == i:
                return v
    raise NoSuchState([z.get("state") for z in zones if z.get("state") != state])


def default_zones(path):
    """Signal K meta (zones plus per-state methods) for a path with none, or None if no kind fits."""
    for kind in _DEFAULTS["kinds"]:
        if fnmatch.fnmatchcase(path, kind["pattern"]):
            meta = copy.deepcopy(_DEFAULTS["methods"])
            meta["zones"] = copy.deepcopy(kind["zones"])
            return meta
    return None


# ---- Signal K server I/O ----------------------------------------------------------------

def _get_json(server, url_path):
    """GET http://server/url_path as JSON; None on 404. Other failures raise (OSError subclasses)."""
    try:
        with urllib.request.urlopen(f"http://{server}{url_path}", timeout=3) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        e.close()
        if e.code == 404:
            return None
        raise


def _slash(path):
    return path.replace(".", "/")


def read_zones(server, path):
    """The zones on a path's meta, or None if it has none (including an empty list or no meta)."""
    meta = _get_json(server, f"/signalk/v1/api/vessels/self/{_slash(path)}/meta")
    zs = meta.get("zones") if isinstance(meta, dict) else None
    return zs or None


def meta_delta(path, meta):
    """A Signal K delta that sets a path's meta; the server starts watching its zones."""
    return {"context": "vessels.self",
            "updates": [{"source": {"label": "sk-inject"}, "meta": [{"path": path, "value": meta}]}]}


async def raise_check(server, path, state, timeout_s, poll_s=0.5):
    """True once the server's notification for path has this state, False after timeout_s."""
    url = f"/signalk/v1/api/vessels/self/notifications/{_slash(path)}"
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout_s
    while True:
        try:
            doc = await asyncio.to_thread(_get_json, server, url)
        except OSError:
            doc = None
        if isinstance(doc, dict) and (doc.get("value") or {}).get("state") == state:
            return True
        if loop.time() >= deadline:
            return False
        await asyncio.sleep(poll_s)


def created_paths(file=CREATED_FILE):
    try:
        return json.loads(Path(file).read_text())
    except FileNotFoundError:
        return []


def record_created(path, file=CREATED_FILE):
    paths = created_paths(file)
    if path not in paths:
        paths.append(path)
        Path(file).write_text(json.dumps(paths, indent=2) + "\n")
