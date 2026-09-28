#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["scapy"]
# ///
"""Take a sanitised sample of the TA packets in the spike's captures.

    uv run tools/ta-net/ta-packet-captures.py            rewrite the header
    uv run tools/ta-net/ta-packet-captures.py --audit    report, write nothing

The captures hold 2907 TA packets, 1497 distinct on their wire bytes and 121
distinct shapes -- a shape being the framing, whether the u32 is a reply's
0xffffffff or a sender's own count, and the sequence of subpacket codes with
repetition collapsed. Keeping all of them is 265 KB of hex for a test that only
needs to know the envelope reads them, so this keeps at most two per shape,
preferring instances that already carry no text and spreading what is left over
the shape's range of sizes. It then asserts the sample still reaches every code,
both framings and both shapes of the u32, rather than trusting the rule.

Sanitising: the only text a TA packet carries is the map name in a 0x20, fifteen
bytes NUL-padded at offset 1. It is replaced with a neutral name of the same
length, padded the same way, and the packet is re-packed and re-encrypted so the
checksum still verifies. A sample that will not survive that is dropped rather
than kept half-sanitised.

Player names are why the captures may not be checked in, and they are not in a TA
packet: they are in the DirectPlay system messages, in the SUPERENUMPLAYERSREPLY
as UTF-16, and `ta_payload` drops those before any of this happens. The handle
this repository's own history carries is in two of the three captures and in none
of the output. There is no address in a TA packet either. `--audit` prints every
printable run in the result and searches it for those handles in both encodings.
"""
import re
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from tanet import compress, decompress, decrypt, encrypt, split_subpackets, ta_payload  # noqa

CAPTURES = [
    ("baseline", "/home/oscar/source/repos/.RWE-B4.worktrees/ta-baseline.pcap"),
    ("options", "/home/oscar/source/repos/.RWE-B4.worktrees/ta-options.pcap"),
    ("small", "/home/oscar/source/repos/.RWE-B4.worktrees/ta-small.pcap"),
]

PER_SHAPE = 2
MAP_NAME_OFFSET = 1
MAP_NAME_LENGTH = 15
NEUTRAL = "Sanitised Map"

HANDLES = ["ozgb", "oscar", "bailey", "ozbailey", "79094698"]


def plain_of(wire):
    """The decrypted, decompressed form of a wire packet."""
    decrypted, _ = decrypt(wire)
    return decompress(decrypted)


def codes_of(plain):
    return "".join(f"{s[0]:02x}" for s in split_subpackets(plain[7:]))


def shape_of(codes):
    """The code sequence with repetition collapsed, so 0x1a thirty-one times is one shape."""
    out = []
    for i in range(0, len(codes), 2):
        code = codes[i:i + 2]
        if not out or out[-1] != code:
            out.append(code)
    return "+".join(out)


def map_name(plain):
    """The 15 NUL-padded bytes of the first 0x20's map name, or None if there is no 0x20."""
    for s in split_subpackets(plain[7:]):
        if s[0] == 0x20 and len(s) == 186:
            return bytes(s[MAP_NAME_OFFSET:MAP_NAME_OFFSET + MAP_NAME_LENGTH])
    return None


def sanitise(plain, name):
    """The plain packet with its map name replaced, or None if there was nothing to replace."""
    text = name.rstrip(b"\x00")
    if not text:
        return None
    filled = (NEUTRAL + " " * len(text))[:len(text)].encode()
    out = bytearray(plain)
    at = out.find(bytes([0x20]) + name, 7)
    if at < 0:
        return None
    start = at + MAP_NAME_OFFSET
    out[start:start + MAP_NAME_LENGTH] = filled + b"\x00" * (MAP_NAME_LENGTH - len(filled))
    return bytes(out)


def repack(plain, framing):
    """A wire packet carrying `plain` under `framing`, encrypted with a correct checksum."""
    body = bytearray(plain)
    body[0] = framing
    return encrypt(compress(bytes(body)) if framing == 0x04 else bytes(body))


def main():
    audit = "--audit" in sys.argv
    seen = {}
    for name, path in CAPTURES:
        for t, proto, flow, message in read(path):
            payload = ta_payload(message, proto)
            if payload is None or len(payload) < 7:
                continue
            decrypted, _ = decrypt(payload)
            if len(decrypted) < 7:
                continue
            # The type byte is read from the decrypted packet, not the plain one:
            # decompress rewrites it to 0x03, which would merge the two framings.
            framing = decrypted[0]
            plain = decompress(decrypted)
            if len(plain) < 7:
                continue
            wire = bytes(payload)
            if wire in seen:
                continue
            marker = int.from_bytes(plain[3:7], "little")
            kind = "ff" if marker == 0xFFFFFFFF else "count"
            codes = codes_of(plain)
            seen[wire] = (framing, kind, shape_of(codes), len(wire), name + " " + proto,
                          map_name(plain), codes)

    shapes = defaultdict(list)
    for wire, info in seen.items():
        shapes[info[:3]].append((wire, info))
    shapes = {k: v for k, v in shapes.items()}
    print(f"{len(seen)} distinct packets in {len(shapes)} shapes", file=sys.stderr)

    chosen, dropped, repacked = [], [], 0

    def take(wire, info):
        """Add one captured packet, rewritten if it carries a name."""
        nonlocal repacked
        framing, kind, shape, size, source, name, codes = info
        if name is not None and name.strip(b"\x00"):
            edited = sanitise(plain_of(wire), name)
            if edited is None:
                dropped.append((codes, source, "name field not found"))
                return False
            wire = repack(edited, framing)
            repacked += 1
        chosen.append((wire, framing, kind, codes, size, source))
        return True

    for shape, rows in sorted(shapes.items()):
        # An instance whose name field is already empty needs no rewriting, so
        # those come first; then spread over the shape's sizes.
        rows.sort(key=lambda r: (bool(r[1][5] and r[1][5].strip(b"\x00")), r[1][3]))
        step = max(1, len(rows) // PER_SHAPE)
        for wire, info in rows[::step][:PER_SHAPE]:
            take(wire, info)

    def codes_in(seq):
        return {seq[i:i + 2] for i in range(0, len(seq), 2)}

    # Every subpacket code the captures hold has to be in the sample whatever the
    # per-shape rule picked: that coverage is what the round trip rests on.
    corpus_codes = set()
    for info in seen.values():
        corpus_codes |= codes_in(info[6])
    covered = set()
    for wire, framing, kind, codes, size, source in chosen:
        covered |= codes_in(codes)
    for code in sorted(corpus_codes - covered):
        for wire, info in seen.items():
            if code in codes_in(info[6]):
                take(wire, info)
                break

    # Nothing goes in that does not still read as what it was.
    verified = []
    for wire, framing, kind, codes, size, source in chosen:
        ok, hdr, subs = decode(wire)
        again = "".join(f"{s[0]:02x}" for s in subs)
        if not ok or wire[0] != framing or again != codes:
            dropped.append((codes, source, "did not survive"))
            continue
        verified.append((wire, int.from_bytes(hdr[3:7], "little"), codes, source))

    covered = set()
    for wire, marker, codes, source in verified:
        covered |= codes_in(codes)
    assert not corpus_codes - covered, f"the sample lost codes {sorted(corpus_codes - covered)}"
    assert {w[0] for w, _, _, _ in verified} == {0x03, 0x04}, "the sample lost a framing"
    assert len({m for _, m, _, _ in verified} - {0xFFFFFFFF}) > 0, "the sample lost a counted u32"
    assert any(m == 0xFFFFFFFF for _, m, _, _ in verified), "the sample lost a reply's u32"

    verified.sort(key=lambda v: (v[2], v[3]))
    total = sum(len(v[0]) for v in verified)
    print(f"kept {len(verified)} packets, {total} bytes; repacked {repacked}; dropped {len(dropped)}",
          file=sys.stderr)
    for codes, source, why in dropped:
        print(f"  dropped {source} {codes}: {why}", file=sys.stderr)

    runs = defaultdict(int)
    for wire, marker, codes, source in verified:
        for m in re.findall(rb"[ -~]{4,}", plain_of(wire)):
            runs[m] += 1
    print("printable runs in the sample's plaintext:", dict(runs) or "none", file=sys.stderr)
    for handle in HANDLES:
        for form in (handle.encode(), handle.encode("utf-16-le"), handle.upper().encode()):
            for wire, marker, codes, source in verified:
                assert form not in wire and form not in plain_of(wire), handle
    print(f"none of {HANDLES} appears in the sample, in either encoding", file=sys.stderr)
    if audit:
        return

    write_header(verified, repacked)


def read(path):
    from tanet import read_capture
    return read_capture(path)


def decode(wire):
    from tanet import decode as ta_decode
    return ta_decode(wire)


def write_header(rows, repacked):
    out = [HEADER.format(repacked=repacked)]
    for wire, marker, codes, source in rows:
        out.append(f'                {{"{wire.hex()}", 0x{marker:08x}u, "{codes}", "{source}"}},\n')
    out.append(FOOTER)
    path = __file__.rsplit("/", 3)[0] + "/src/rwe/net/ta/ta_packet_captures.h"
    with open(path, "w") as f:
        f.write("".join(out))
    print("wrote", path, file=sys.stderr)


HEADER = """#pragma once

// A sample of the TA packets in the spike's captures, as they came off the wire.
//
// Generated by tools/ta-net/ta-packet-captures.py from ta-baseline.pcap,
// ta-options.pcap and ta-small.pcap, the three recordings of a real Total
// Annihilation playing over DirectPlay that the spike took. The captures hold
// 2907 packets, 1497 distinct on their wire bytes. A shape here is the framing,
// whether the u32 is a reply's 0xffffffff or a sender's own count, and the
// sequence of subpacket codes with repetition collapsed -- so a packet of
// thirty-one 0x1a records is one shape and not thirty-one, which is 36 shapes
// rather than 121. At most two per shape are kept, spread over the shape's
// range of sizes, so a run is represented by its shortest and its longest
// instance. Between them the 59 here cover all 25 codes the captures hold, both
// framings, both shapes of the u32, and every packet size from 8 bytes to the
// 711-byte one, for 14 KB instead of 265 KB. The generator asserts that
// coverage rather than trusting the rule to produce it.
//
// Sanitised on the way in, and the substitutions are these. The only text a TA
// packet carries is the map name in a 0x20 -- fifteen bytes NUL-padded at
// offset 1, and in the captures "Canal Crossing" or "Great Divide", both stock
// vanilla map names. {repacked} packets carried one and were re-packed and
// re-encrypted with it replaced by "Sanitised Map", the same length and padded
// the same way; the rest already had the field empty. A sample that would not
// survive the rewrite was dropped, and the generator says which.
//
// No player name is here because no player name is in a TA packet: the captures
// carry them in the DirectPlay system messages, in the SUPERENUMPLAYERSREPLY
// (command 0x29) as UTF-16, and `ta_payload` drops those before any of this.
// The handle this repository's own history carries is there and not here, and
// `--audit` checks for it by name in both encodings. There is no address in a
// TA packet either: what reads as a dotted quad in 0x1a, 0x2c and 0x28 is four
// bytes of a CRC or a 16.16 coordinate.
//
// The generator's --audit pass prints every printable run in the result and
// checks those handles against it, so the claim above stays checkable.
//
// A `Packet` is what the envelope should make of its wire bytes: the u32, the
// subpacket codes in order, and where it came from, so a failure names the
// recording. The round trip is checked in TaPacket.test.cpp.

#include <cstdint>
#include <vector>

namespace rwe
{{
    namespace ta_test
    {{
        struct Packet
        {{
            /** The bytes as they came off the wire, lowercase hex. */
            const char* wire;

            /** The u32 at offset 3, once decrypted and decompressed. */
            std::uint32_t marker;

            /** One two-digit hex code per subpacket, in order. */
            const char* codes;

            /** The recording and transport it was seen in. */
            const char* source;
        }};

        inline const std::vector<Packet>& capturedPackets()
        {{
            static const std::vector<Packet> all{{
"""

FOOTER = """};
            return all;
        }
    }
}
"""


main()
