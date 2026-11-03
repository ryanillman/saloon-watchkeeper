#!/usr/bin/env python3
"""Saves a Signal K server's model of this vessel to a JSON file.

  sk-capture.py <host[:port]> <out.json> [--token-file F]

Does GET /signalk/v1/api/vessels/self and writes it pretty-printed. The port defaults to 3000.
Use --token-file when the server needs a read token (a file holding just the token).
Standard library only.
"""
import argparse
import json
import sys
import urllib.error
import urllib.request


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("server", help="host or host:port of the Signal K server (port defaults to 3000)")
    ap.add_argument("out", help="JSON file to write")
    ap.add_argument("--token-file", help="file holding a Signal K token, sent as a Bearer header")
    args = ap.parse_args()

    server = args.server if ":" in args.server else args.server + ":3000"
    url = f"http://{server}/signalk/v1/api/vessels/self"
    headers = {"Accept": "application/json"}
    if args.token_file:
        try:
            with open(args.token_file) as f:
                headers["Authorization"] = "Bearer " + f.read().strip()
        except OSError as e:
            sys.exit(f"sk-capture: cannot read token file: {e}")

    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=15) as resp:
            body = resp.read()
    except urllib.error.HTTPError as e:
        sys.exit(f"sk-capture: {url} answered HTTP {e.code} {e.reason}"
                 + (" (a token is needed: --token-file)" if e.code in (401, 403) else ""))
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f"sk-capture: cannot reach {url}: {getattr(e, 'reason', e)}")

    try:
        data = json.loads(body)
    except ValueError as e:
        sys.exit(f"sk-capture: {url} did not return JSON: {e}")

    try:
        with open(args.out, "w") as f:
            json.dump(data, f, indent=2, sort_keys=True)
            f.write("\n")
    except OSError as e:
        sys.exit(f"sk-capture: cannot write {args.out}: {e}")
    print(f"sk-capture: wrote {args.out} ({len(body)} bytes from {server})")


if __name__ == "__main__":
    main()
