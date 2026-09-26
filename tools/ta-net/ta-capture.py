#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["scapy"]
# ///
"""Read a packet capture of Total Annihilation playing over DirectPlay.

    tools/ta-net/ta-capture.py <capture.pcap>              flows, the DirectPlay sequence, subpacket counts
    tools/ta-net/ta-capture.py <capture.pcap> --timeline   first and last time each subpacket code was sent, per side
    tools/ta-net/ta-capture.py <capture.pcap> --settings   every change to a player's options byte and team
    tools/ta-net/ta-capture.py <capture.pcap> --units      decoded 0x2c: waypoints and full-state records
    tools/ta-net/ta-capture.py <capture.pcap> --check      exits non-zero unless every TA packet verifies and
                                                           every ground 0x2c re-encodes byte for byte

Record one with, for example:
    sudo tcpdump -i any -w game.pcap 'port 47624 or portrange 2300-2400'
Start it before anyone joins if the capture is to drive fakehost.py. See docs/TA-NETWORK.md.
"""
import argparse, struct, sys
from collections import Counter, defaultdict

from tanet import (CMDS, decode, decode_2c, dp_command, encode_2c, is_dp, read_capture, subpacket_size,
                   ta_payload)


def sender_ids(ms):
    """Player id -> "host"/"joiner", from who sent the ENUMSESSIONSREPLY. None if the capture began after the join."""
    host_flow = next((f for t, p, f, m in ms if p == "TCP" and is_dp(m) and dp_command(m) == 0x01), None)
    names = {}
    for t, p, f, m in ms:
        if is_dp(m) or len(m) <= 8:
            continue
        pid = struct.unpack_from("<I", m, 20 if p == "TCP" else 0)[0]
        if p == "TCP" and pid not in names and host_flow is not None:
            names[pid] = "host" if f == host_flow else "joiner"
    return names


def side(names, p, m):
    pid = struct.unpack_from("<I", m, 20 if p == "TCP" else 0)[0]
    return names.get(pid, f"{pid:#x}")


def summary(ms, names):
    flows = defaultdict(lambda: [0, 0.0, 0.0])
    for t, p, f, m in ms:
        v = flows[(p, f)]; v[0] += 1; v[1] = v[1] or t; v[2] = t
    print("flows:")
    for (p, f), (n, a, b) in sorted(flows.items(), key=lambda kv: kv[1][1]):
        print(f"  {p} {f:13} {n:6} messages  {a:7.2f}-{b:7.2f}s")
    print("DirectPlay system messages:")
    for t, p, f, m in ms:
        if is_dp(m):
            print(f"  {t:7.2f} {p} {f:13} {CMDS.get(dp_command(m), hex(dp_command(m)))}")
    counts, bad, unsized = Counter(), 0, Counter()
    for t, p, f, m in ms:
        pl = ta_payload(m, p)
        if pl is None:
            continue
        ok, hdr, subs = decode(pl)
        bad += not ok
        for s in subs:
            counts[(p, s[0])] += 1
            if subpacket_size(s) != len(s):
                unsized[s[0]] += 1
    print(f"TA packets: {bad} checksum failures, {sum(unsized.values())} unsized subpackets")
    for (p, c), n in sorted(counts.items()):
        print(f"  {p} 0x{c:02x}: {n}")
    return bad == 0 and not unsized


def timeline(ms, names):
    span = {}
    for t, p, f, m in ms:
        pl = ta_payload(m, p)
        if pl is None:
            continue
        for s in decode(pl)[2]:
            k = (side(names, p, m), p, s[0])
            a, b, n = span.get(k, (t, t, 0))
            span[k] = (a, t, n + 1)
    for (who, p, c), (a, b, n) in sorted(span.items(), key=lambda kv: kv[1][0]):
        print(f"{a:7.2f}-{b:7.2f}  {who:8} {p} 0x{c:02x} x{n}")


def settings(ms, names):
    seen = {}
    for t, p, f, m in ms:
        pl = ta_payload(m, p)
        if pl is None:
            continue
        who = side(names, p, m)
        for s in decode(pl)[2]:
            if s[0] == 0x20 and seen.get((who, "options")) != s[157]:
                seen[(who, "options")] = s[157]; print(f"{t:7.2f} {who:8} options {s[157]:#04x}")
            elif s[0] == 0x24 and seen.get((s[1:5], "team")) != s[5]:
                seen[(s[1:5], "team")] = s[5]
                print(f"{t:7.2f} {who:8} team: player {struct.unpack_from('<I', s, 1)[0]:#x} -> {s[5]}")
            elif s[0] == 0x08:
                print(f"{t:7.2f} {who:8} loading started")


def units(ms, names, max_units):
    for t, p, f, m in ms:
        if p != "UDP" or ta_payload(m, p) is None:
            continue
        for s in decode(ta_payload(m, p))[2]:
            if s[0] != 0x2C:
                continue
            tick, entries, full = decode_2c(s, max_units)
            for e in entries:
                print(f"{t:7.2f} {side(names, p, m):8} tick {tick:6} slot {e['slot']:3} type {e['type']:3} waypoints {e['waypoints']}")
            if full:
                pos = tuple(round(c / 65536, 1) for c in full["pos"]) if "pos" in full else full.get("attached")
                print(f"{t:7.2f} {side(names, p, m):8} tick {tick:6} slot {full['slot']:3} type {full['type']:3} "
                      f"health {full['health']} pos {pos} heading {full.get('rot_yzx', [0])[0] * 360 / 65536:.0f}")


def check(ms, max_units):
    ok_all = summary(ms, {})
    n = same = 0
    for t, p, f, m in ms:
        if p != "UDP" or ta_payload(m, p) is None:
            continue
        for s in decode(ta_payload(m, p))[2]:
            if s[0] != 0x2C:
                continue
            tick, entries, full = decode_2c(s, max_units)
            if full and "attached" in full:
                continue
            n += 1; same += encode_2c(tick, entries, full) == s
    print(f"0x2c: {same} of {n} re-encode byte for byte")
    return ok_all and same == n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--timeline", action="store_true")
    ap.add_argument("--settings", action="store_true")
    ap.add_argument("--units", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--max-units", type=int, default=250, help="the game's unit limit, which sets the full-state cycle")
    a = ap.parse_args()
    ms = read_capture(a.capture)
    names = sender_ids(ms)
    if a.check:
        sys.exit(0 if check(ms, a.max_units) else 1)
    if a.timeline:
        timeline(ms, names)
    elif a.settings:
        settings(ms, names)
    elif a.units:
        units(ms, names, a.max_units)
    else:
        summary(ms, names)


if __name__ == "__main__":
    main()
