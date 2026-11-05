"""The two kinds of request the displays make of a Signal K server besides streaming: a POST to a
plugin path (drop or raise an anchor) and a PUT of a value to a spec path (the anchor radius).

Returns (status, body text). An HTTP error status is returned, not raised; an unreachable server
raises OSError.
"""
import json
import urllib.error
import urllib.request


def _send(method, server, token, path, payload):
    req = urllib.request.Request(
        f"http://{server}{path}", json.dumps(payload).encode(), method=method,
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        try:
            return e.code, e.read().decode(errors="replace")
        finally:
            e.close()


def post_json(server, token, path, body):
    """POST body (an object; None sends {}) to a server path such as /plugins/anchoralarm/dropAnchor."""
    return _send("POST", server, token, path, body or {})


def put_value(server, token, path, value, source=None):
    """PUT {"value": value} to a Signal K path given dotted, relative to vessels.self.

    source names the publisher when several publish the path; the server answers 400 without it.
    """
    body = {"value": value}
    if source is not None:
        body["source"] = source
    return _send("PUT", server, token,
                 f"/signalk/v1/api/vessels/self/{path.replace('.', '/')}", body)
