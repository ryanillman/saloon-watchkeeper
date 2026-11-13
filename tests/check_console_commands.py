#!/usr/bin/env python3
"""Every command app_cmd.c lists in its help is registered with the ESP console in main/console.c.

The console task forwards a registered command to app_cmd_run; one that is not registered is "Unrecognized command" on
the board even though the host tests (which call app_cmd_run directly) pass.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cmd_src = (ROOT / "components/app/src/app_cmd.c").read_text()
console_src = (ROOT / "main/console.c").read_text()

help_body = re.search(r"static int help\(app_out_fn out\) \{(.*?)\n\}", cmd_src, re.S).group(1)
help_cmds = {m.group(1) for m in re.finditer(r'out\("([a-z][a-z-]*)[ "]', help_body)}
registered = set(re.findall(r'"([a-z]+)"', re.search(r"names\[\] = \{(.*?)\};", console_src, re.S).group(1)))

body = re.search(r"names\[\] = \{(.*?)\};", console_src, re.S).group(1)
hints_m = re.search(r"hints\[\] = \{(.*?)\};", console_src, re.S)
n_names = len(re.findall(r'"[a-z]+"', body))
n_hints = len(re.findall(r'"(?:[^"\\]|\\.)*"', hints_m.group(1))) if hints_m else 0
if n_names != n_hints:
    sys.exit(f"main/console.c has {n_names} command names but {n_hints} hints: the loop would read past the hints array")

missing = sorted(help_cmds - registered)
if missing:
    sys.exit(f"in the help but not registered in main/console.c: {', '.join(missing)}")
print(f"ok: {len(help_cmds)} commands registered")
