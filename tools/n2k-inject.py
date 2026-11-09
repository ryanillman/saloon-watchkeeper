#!/usr/bin/env python3
"""Put instrument data on a SocketCAN bus as NMEA 2000, for the simulator (sim --can <ifname>).

    tools/n2k-inject.py vcan0 --rate 1 basic     # repeat the scenario every second
    tools/n2k-inject.py can0 --once basic        # once, on the real bus
    tools/n2k-inject.py vcan0 --dump full        # print candump -L lines, touch no bus (scenarios: basic, full)

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


def engine_dynamic(inst, oil_pa, oil_k, coolant_k, alt_v, lph, hours_s):
    return 127489, (bytes([inst]) + u16(round(oil_pa / 100)) + u16(round(oil_k * 10)) + u16(round(coolant_k * 100))
                    + i16(round(alt_v * 100)) + i16(round(lph * 10)) + u32(round(hours_s)) + bytes([0xFF] * 11))
def dc_status(inst, soc_pct, minutes):
    return 127506, bytes([1, inst, 0, soc_pct, 100]) + u16(minutes) + bytes([0xFF] * 4)
def heading(rad, variation_rad): return 127250, bytes([1]) + u16(round(rad * 10000)) + i16(0x7FFF) + i16(round(variation_rad * 10000)) + bytes([0])
def cog_sog(cog_rad, sog_ms): return 129026, bytes([1, 0]) + u16(round(cog_rad * 10000)) + u16(round(sog_ms * 100)) + bytes([0xFF, 0xFF])
def stw(ms): return 128259, bytes([1]) + u16(round(ms * 100)) + u16(0xFFFF) + bytes([0xFF, 0xFF, 0xFF])
def position(lat, lon): return 129025, struct.pack("<ii", round(lat * 1e7), round(lon * 1e7))
def system_time(days, seconds): return 126992, bytes([1, 0]) + u16(days) + u32(seconds * 10000)


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
SCENARIOS["full"] = SCENARIOS["basic"] + [
    engine_dynamic(0, 320000, 353.2, 350.0, 13.8, 12.5, 3600),   # oil 3.2 bar, coolant 350 K, 12.5 L/h, 1 hour
    dc_status(1, 82, 600),
    heading(1.5708, 0.05),
    cog_sog(3.1416, 5.14),
    stw(4.9),
    position(41.5402833, -70.25),
    system_time(20000, 43200),                                   # 2024-10-04 12:00:00 UTC
]

FAST = {127489, 127506, 129029}   # multi-frame PGNs (the ones n2k's fast-packet table lists)
_seq = 0


def to_frames(pgn, data):
    """One (id, 8 bytes) per CAN frame: single frames as they are, fast-packet PGNs split with a sequence counter."""
    global _seq
    cid = can_id(pgn)
    if pgn not in FAST:
        return [(cid, data.ljust(8, b"\xff"))]
    seq, _seq = _seq, (_seq + 1) & 7
    out = [(cid, (bytes([seq << 5, len(data)]) + data[:6]).ljust(8, b"\xff"))]
    for n, off in enumerate(range(6, len(data), 7), start=1):
        out.append((cid, (bytes([seq << 5 | n]) + data[off:off + 7]).ljust(8, b"\xff")))
    return out


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
            for cid, fr in to_frames(pgn, data):
                print(f"(0.000000) can0 {cid:08X}#{fr.hex().upper()}")
        return
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.bind((a.interface,))
    try:
        while True:
            for pgn, data in frames:
                for cid, fr in to_frames(pgn, data):
                    s.send(struct.pack("=IB3x8s", cid | socket.CAN_EFF_FLAG, len(fr), fr))
                print(f"sent pgn {pgn}: {data.hex()}", file=sys.stderr)
            if a.once:
                return
            time.sleep(a.rate)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
