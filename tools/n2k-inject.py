#!/usr/bin/env python3
"""Put instrument data on a SocketCAN bus as NMEA 2000, for the simulator (sim --can <ifname>).

    tools/n2k-inject.py vcan0 --rate 1 basic     # repeat the scenario every second
    tools/n2k-inject.py can0 --once basic        # once, on the real bus
    tools/n2k-inject.py --dump basic             # print candump -L lines, touch no bus

The byte layouts are the same ones components/n2k/src/n2k_decode.c reads, both from memory and unverified
against canboat. This does not claim an address (source 0xF1), so use it on a bench bus, not on a boat.
Stdlib only.
"""
import argparse, socket, struct, sys, time

SRC, PRIO = 0xF1, 3


def can_id(pgn):
    dp, pf, ps = (pgn >> 16) & 1, (pgn >> 8) & 0xFF, pgn & 0xFF
    assert pf >= 240, "only PDU2 (broadcast) PGNs here"
    return (PRIO << 26) | (dp << 24) | (pf << 16) | (ps << 8) | SRC


def u16(v): return struct.pack("<H", v)
def i16(v): return struct.pack("<h", v)
def u32(v): return struct.pack("<I", v)


def depth(m, offset_m): return 128267, bytes([1]) + u32(round(m * 100)) + i16(round(offset_m * 1000)) + bytes([10])
def wind(ms, rad, ref=2): return 130306, bytes([1]) + u16(round(ms * 100)) + u16(round(rad * 10000)) + bytes([ref, 0xFF, 0xFF])
def battery(inst, v, a, k): return 127508, bytes([inst]) + u16(round(v * 100)) + i16(round(a * 10)) + u16(round(k * 100)) + bytes([0xFF])
def engine(inst, rpm): return 127488, bytes([inst]) + u16(round(rpm * 4)) + u16(0xFFFF) + bytes([0, 0xFF, 0xFF])
def tank(inst, fluid, ratio, litres):
    return 127505, bytes([(fluid << 4) | inst]) + i16(round(ratio * 100 / 0.004)) + u32(round(litres * 10)) + bytes([0xFF])


SCENARIOS = {
    "basic": [
        depth(12.3, 0.4),
        wind(8.0, 0.6981),                  # 8 m/s, 40 degrees to starboard
        battery(1, 12.6, -8.5, 298.15),
        engine(0, 2400),
        tank(1, 1, 0.62, 200),              # fresh water 1: 62 % of 200 L
        tank(0, 0, 0.35, 300),              # fuel 0: 35 % of 300 L
    ],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("interface", nargs="?", default="vcan0")
    ap.add_argument("scenario", choices=sorted(SCENARIOS), nargs="?", default="basic")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--rate", type=float, default=1.0, help="seconds between repeats (default 1)")
    ap.add_argument("--dump", action="store_true", help="print candump -L lines instead of sending")
    a = ap.parse_args()
    frames = SCENARIOS[a.scenario]
    if a.dump:
        for pgn, data in frames:
            print(f"(0.000000) can0 {can_id(pgn):08X}#{data.hex().upper()}")
        return
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.bind((a.interface,))
    try:
        while True:
            for pgn, data in frames:
                s.send(struct.pack("=IB3x8s", can_id(pgn) | socket.CAN_EFF_FLAG, len(data), data.ljust(8, b"\0")))
                print(f"sent pgn {pgn}: {data.hex()}", file=sys.stderr)
            if a.once:
                return
            time.sleep(a.rate)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
