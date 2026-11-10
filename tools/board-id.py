#!/usr/bin/env python3
"""Print the board id the firmware on a serial port reports, e.g. "square4".

Resets the chip with the RTS line and reads the boot log, where the firmware prints "main: board <id> <w>x<h>".
Exit 1 with a message on stderr if no such line arrives (no firmware, or one that is not this project's).
Needs pyserial (it comes with the ESP-IDF environment: . ~/esp/esp-idf/export.sh).
"""
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("board-id: pyserial is missing; run: . ~/esp/esp-idf/export.sh")

BOOT_LINE = re.compile(rb"main: board ([a-z0-9_]+) \d+x\d+")


def board_of(port, wait_s=10.0):
    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.dtr = False
        s.rts = True    # EN low
        time.sleep(0.1)
        s.rts = False
        buf = b""
        end = time.monotonic() + wait_s
        while time.monotonic() < end:
            buf += s.read(1024)
            m = BOOT_LINE.search(buf)
            if m:
                return m.group(1).decode()
    return None


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: board-id.py <port>")
    try:
        found = board_of(sys.argv[1])
    except (serial.SerialException, OSError) as e:
        sys.exit(f"board-id: {sys.argv[1]}: {e}")
    if not found:
        sys.exit(f"board-id: {sys.argv[1]}: no board id in the boot log (is this project's firmware on it?)")
    print(found)
