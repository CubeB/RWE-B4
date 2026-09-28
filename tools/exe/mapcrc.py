#!/usr/bin/env python3
"""mapcrc.py -- reproduce TotalA.exe's map checksum and unit-type id.

Both come out of one routine, `0x4B6BA0` in TotalA.exe (GOG v3.1, md5
8e74a1dffa1f5988624c52048f5b20cd). It is not a CRC: it walks the input with
four one-byte accumulators and combines them big-endian as `A D B C`:

    A ^= (index_lo + byte)
    B ^= byte
    C += byte
    D += (index_lo ^ byte)
    result = (A << 24) | (D << 16) | (B << 8) | C

The map checksum is that routine over two places, XORed:

    tnt  = hash(.tnt[0:0x40])
         ^ hash(.tnt[mapAttributesOffset : + width*height*4])
         ^ hash(.tnt[featuresOffset     : + numberOfFeatures*132])
    ota  = hash(.ota GlobalHeader body minus its last two bytes)
    map  = tnt ^ ota

`0x4373A0` builds `tnt`, and the OTA reader stores `ota` against the
`[GlobalHeader]` TDF block at block+0x25 (`0x4C4183`); the status builder writes
their XOR to bytes 170-173 of the host's `0x20`. A joining TA compares that to
its own at `0x448F2C` and shows "does not have this map" when they differ.

A unit type's content-derived id -- the `a` field of a `0x1A` sub-type-2 record --
is the same routine over the raw bytes of its FBI file, and nothing else.

Docs: docs/TOTALA-EXE-DATA.md section 117, docs/TA-DEMOS.md D7.

Usage:
    uv run --no-project python tools/exe/mapcrc.py map <tnt> <ota>
    uv run --no-project python tools/exe/mapcrc.py unit <fbi> [<fbi>...]
    uv run --no-project python tools/exe/mapcrc.py selftest <dir-with-maps>

`selftest` expects Canal Crossing and Great Divide under <dir>, as the HPI
extraction produces, and checks the two recorded host status values.
"""
import os
import sys

# tools/exe holds a probe named dis.py, which shadows the standard library
# module by that name; on Python 3.14 argparse reaches it through inspect. Drop
# this script's own directory from the path before importing argparse.
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != _HERE]

import argparse
import struct
from pathlib import Path

# The two values read out of a host's 0x20 in the captures.
ORACLES = {
    "Canal Crossing": 0x6EBE0529,
    "Great Divide": 0xE4D8389A,
}


def ta_checksum(data):
    a = b = c = d = 0
    for i, byte in enumerate(data):
        index = i & 0xFF
        a = (a ^ ((index + byte) & 0xFF)) & 0xFF
        b = (b ^ byte) & 0xFF
        c = (c + byte) & 0xFF
        d = (d + (index ^ byte)) & 0xFF
    return (a << 24) | (d << 16) | (b << 8) | c


def tnt_checksum(tnt):
    if len(tnt) < 0x40 or struct.unpack_from("<I", tnt, 0)[0] != 0x2000:
        raise ValueError("not a TNT file")
    width, height = struct.unpack_from("<II", tnt, 4)
    attributes_offset = struct.unpack_from("<I", tnt, 0x10)[0]
    features_offset = struct.unpack_from("<I", tnt, 0x20)[0]
    number_of_features = struct.unpack_from("<I", tnt, 0x1C)[0]
    attributes = tnt[attributes_offset:attributes_offset + width * height * 4]
    features = tnt[features_offset:features_offset + number_of_features * 132]
    if len(attributes) != width * height * 4 or len(features) != number_of_features * 132:
        raise ValueError("a TNT block runs past the end of the file")
    return ta_checksum(tnt[0:0x40]) ^ ta_checksum(attributes) ^ ta_checksum(features)


def ota_checksum(ota):
    """The [GlobalHeader] block's own hash, as the TDF reader stores it."""
    name = b"GlobalHeader"
    start = 0
    while True:
        start = ota.lower().find(b"[globalheader]", start)
        if start < 0:
            raise ValueError("no [GlobalHeader] block")
        brace = start + len(name) + 2
        while brace < len(ota) and ota[brace:brace + 1] in b" \t\r\n":
            brace += 1
        if ota[brace:brace + 1] != b"{":
            raise ValueError("[GlobalHeader] is not followed by '{'")
        body_start = brace + 1
        depth = 0
        close = None
        for i in range(body_start, len(ota)):
            if ota[i:i + 1] == b"{":
                depth += 1
            elif ota[i:i + 1] == b"}":
                if depth == 0:
                    close = i
                    break
                depth -= 1
        if close is None:
            raise ValueError("[GlobalHeader]'s brace never closes")
        body_length = close - body_start
        # 0x4C4183 hashes [bodyStart, close - 2).
        return ta_checksum(ota[body_start:body_start + max(body_length - 2, 0)])


def map_checksum(tnt, ota):
    return tnt_checksum(tnt) ^ ota_checksum(ota)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["map", "unit", "selftest"])
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()

    if args.command == "map":
        if len(args.paths) != 2:
            ap.error("map wants a .tnt and a .ota")
        tnt = Path(args.paths[0]).read_bytes()
        ota = Path(args.paths[1]).read_bytes()
        print("0x%08x" % map_checksum(tnt, ota))
        return 0

    if args.command == "unit":
        for path in args.paths:
            print("0x%08x  %s" % (ta_checksum(Path(path).read_bytes()), path))
        return 0

    # selftest
    if len(args.paths) != 1:
        ap.error("selftest wants one directory holding the extracted maps")
    root = Path(args.paths[0])
    failed = False
    for name, expected in ORACLES.items():
        try:
            got = map_checksum((root / (name + ".tnt")).read_bytes(), (root / (name + ".ota")).read_bytes())
        except (OSError, ValueError) as e:
            print("FAIL %-14s %s" % (name, e))
            failed = True
            continue
        ok = got == expected
        failed = failed or not ok
        print("%s %-14s 0x%08x %s 0x%08x" % ("ok  " if ok else "FAIL", name, got, "==" if ok else "!=", expected))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
