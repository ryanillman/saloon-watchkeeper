"""tools/config.schema.json must accept exactly the roles the firmware knows (KNOWN_ROLES in config.c)."""
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
src = (root / "components/config/src/config.c").read_text()
known = re.search(r"KNOWN_ROLES\[\]\s*=\s*\{([^}]*)\}", src).group(1)
firmware = re.findall(r'"([^"]+)"', known)
schema = json.loads((root / "tools/config.schema.json").read_text())
listed = schema["$defs"]["roleName"]["enum"] if "$defs" in schema else schema["definitions"]["roleName"]["enum"]
if sorted(firmware) != sorted(listed):
    sys.exit(f"config.schema.json roleName {sorted(listed)} differs from KNOWN_ROLES {sorted(firmware)}")
