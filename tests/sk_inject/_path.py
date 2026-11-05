import sys
from pathlib import Path

SKDIR = Path(__file__).resolve().parents[2] / "tools" / "sk-inject"
if str(SKDIR) not in sys.path:
    sys.path.insert(0, str(SKDIR))
