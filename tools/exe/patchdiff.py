#!/usr/bin/env python3
"""Compare two same-size TotalA.exe builds and ask whether a routine was patched.

TA: Escalation 10.2 and ProTA 4.8 both ship a `TotalA.exe`. ProTA's is
byte-identical to the GOG one; Escalation's is the same size with 4,425 bytes
changed in place, which is why docs/TA-PATCHES.md quarantines the twelve
Escalation demos for some subjects and clears them for others. That mapping --
"is routine X inside a patched run?" -- was done by hand, and this is the
re-runnable version of it, kept for the same reason the rest of tools/exe is.

The question it answers is deliberately narrow. Given a routine's address range
it reports the patched runs inside it and the nearest patched byte either side,
so a finding can say "confirmed unchanged by reading the bytes" rather than
"I did not find anything".

    tools/exe/patchdiff.py --base ~/ta-mods/prota/TotalA.exe \\
                           --other ~/ta-mods/esc-data/TotalA.exe --summary
    tools/exe/patchdiff.py ... --range 0x41BA60:0x41BCD0      # the build pipeline
    tools/exe/patchdiff.py ... --near 0x402A09,0x403E43       # the 512-byte halo test

**The halo test is the weaker one and it is easy to over-read.** 512 bytes
crosses function boundaries freely, so a hit near a routine is not a hit in it.
`--bounds` guesses the enclosing function from `ret`-plus-padding and a standard
prologue, which is a heuristic and says so; use it to tighten a range, then
re-run `--range` and trust that.

Section mapping is the GOG binary's, from docs/TOTALA-EXE.md; `pe.py` prints it
for any other build and this refuses to run if the two files differ in size,
since an in-place patch is the only case it is meant for.
"""

import argparse
import sys

# VA -> file offset, GOG TotalA.exe (1,178,624 bytes). docs/TOTALA-EXE.md.
SECTIONS = [
    (".text", 0x401000, 0x400, 0xFA92A),
    (".rdata", 0x4FC000, 0xFAE00, 0x468C),
    (".data", 0x501000, 0xFF600, 0x10A00),
    (".tls", 0x52C000, 0x110000, 0x14),
    (".rsrc", 0x52D000, 0x110200, 0xA58),
]


def section_of_offset(off):
    for name, va, file_off, size in SECTIONS:
        if file_off <= off < file_off + size:
            return name, va, file_off
    return None, None, None


def to_va(off):
    name, va, file_off = section_of_offset(off)
    return None if name is None else va + (off - file_off)


def to_offset(va):
    for name, sec_va, file_off, size in SECTIONS:
        if sec_va <= va < sec_va + size:
            return file_off + (va - sec_va)
    return None


def diff_runs(base, other):
    """Maximal runs of differing bytes, as (file offset, length)."""
    runs = []
    i = 0
    n = len(base)
    while i < n:
        if base[i] != other[i]:
            j = i
            while j < n and base[j] != other[j]:
                j += 1
            runs.append((i, j - i))
            i = j
        else:
            i += 1
    return runs


def guess_bounds(data, va, window=0x800):
    """The enclosing function, guessed from a ret plus padding. Heuristic.

    Walks back for `c3` / `c2 imm16` followed by `90` or `cc` filler, and
    forward for the next one. Wrong wherever a routine has no padding after it
    or an embedded jump table, so the caller is told it is a guess.
    """
    off = to_offset(va)
    if off is None:
        return None, None

    def is_ret(k):
        if data[k] == 0xC3:
            return 1
        if data[k] == 0xC2:
            return 3
        return 0

    start = None
    for k in range(off, max(off - window, 1) - 1, -1):
        size = is_ret(k)
        if size and k + size < len(data) and data[k + size] in (0x90, 0xCC):
            j = k + size
            while j < len(data) and data[j] in (0x90, 0xCC):
                j += 1
            start = j
            break

    end = None
    for k in range(off, min(off + window, len(data) - 3)):
        size = is_ret(k)
        if size and k + size < len(data) and data[k + size] in (0x90, 0xCC):
            end = k + size
            break

    return (to_va(start) if start else None), (to_va(end) if end else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="the unpatched binary (the GOG TotalA.exe)")
    ap.add_argument("--other", required=True, help="the patched binary to compare against it")
    ap.add_argument("--summary", action="store_true", help="run and byte counts per section")
    ap.add_argument("--range", action="append", default=[], metavar="LO:HI", help="report patched runs in this VA range; repeatable")
    ap.add_argument("--near", default="", metavar="VA,VA", help="512-byte halo test around each VA (the weaker test)")
    ap.add_argument("--halo", type=int, default=512, help="halo width for --near (default 512)")
    ap.add_argument("--bounds", default="", metavar="VA,VA", help="guess the function enclosing each VA")
    args = ap.parse_args()

    base = open(args.base, "rb").read()
    other = open(args.other, "rb").read()
    if len(base) != len(other):
        sys.exit(f"sizes differ ({len(base)} vs {len(other)}); this tool is only for in-place patches")
    if base == other:
        print("the two binaries are identical")
        return 0

    runs = diff_runs(base, other)
    total = sum(n for _, n in runs)
    print(f"{len(runs)} runs, {total} bytes changed")

    if args.summary:
        for name, _, file_off, size in SECTIONS:
            here = [(o, n) for o, n in runs if file_off <= o < file_off + size]
            if here:
                print(f"  {name:<7} {len(here):>4} runs / {sum(n for _, n in here):>5} bytes")
        header = [(o, n) for o, n in runs if section_of_offset(o)[0] is None]
        if header:
            print(f"  {'(PE)':<7} {len(header):>4} runs / {sum(n for _, n in header):>5} bytes")

    for spec in args.range:
        lo, hi = (int(x, 0) for x in spec.split(":"))
        print(f"\nrange 0x{lo:X}..0x{hi:X}")
        inside = [(o, n) for o, n in runs if to_va(o) is not None and lo <= to_va(o) < hi]
        if inside:
            print(f"  PATCHED: {len(inside)} run(s), {sum(n for _, n in inside)} bytes")
            for o, n in inside:
                print(f"    0x{to_va(o):X} +{n}  {base[o:o+n].hex()} -> {other[o:o+n].hex()}")
        else:
            print("  clean: no patched byte inside this range")
        vas = sorted(v for v in (to_va(o) for o, _ in runs) if v is not None)
        below = max((v for v in vas if v < lo), default=None)
        above = min((v for v in vas if v >= hi), default=None)
        if below is not None:
            print(f"  nearest patched byte below: 0x{below:X} ({lo - below} bytes away)")
        if above is not None:
            print(f"  nearest patched byte above: 0x{above:X} ({above - hi} bytes away)")

    if args.near:
        print(f"\nhalo test, +-{args.halo} bytes (weaker: a halo crosses function boundaries)")
        for text in args.near.split(","):
            va = int(text.strip(), 0)
            near = [(to_va(o), n) for o, n in runs if to_va(o) is not None and abs(to_va(o) - va) < args.halo]
            if near:
                hits = ", ".join(f"0x{v:X}+{n}" for v, n in near)
                print(f"  0x{va:X}: {len(near)} run(s) within {args.halo}: {hits}")
            else:
                print(f"  0x{va:X}: nothing within {args.halo} bytes")

    if args.bounds:
        print("\nenclosing function, guessed from ret-plus-padding (heuristic, verify it)")
        for text in args.bounds.split(","):
            va = int(text.strip(), 0)
            start, end = guess_bounds(base, va)
            s = f"0x{start:X}" if start else "?"
            e = f"0x{end:X}" if end else "?"
            print(f"  0x{va:X} lies in {s}..{e}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
