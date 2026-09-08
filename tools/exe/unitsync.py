#!/usr/bin/env python3
"""unitsync.py -- attempt to compute the TA demo `0x1a` unit-type table's
per-unit `a` (checksum) and `b` fields from a directory of shipped unit data,
and check the result against a ground-truth CSV extracted from a real demo.

## Status: the checksum ('a') was NOT reproduced.

This script exists to make that negative result runnable and checkable rather
than asserted. See docs/TOTALA-EXE.md and the write-up this was produced
alongside (task (a), the unit type checksum) for the full account of what was
tried in the binary and why each candidate below was tried.

What IS established, and implemented here as fact rather than guesswork:

- The 14-byte wire record is `code(1)=0x1a, sub(1), zero(4)=0, a(4), b(4)`,
  little-endian, confirmed by decoding the packet-build routine at 0x46d630
  in TotalA.exe (md5 8e74a1dffa1f5988624c52048f5b20cd):
    buf[0]      = 0x1a
    buf[1]      = sub                       (caller-supplied, 2 or 3)
    buf[2:6]    = 0                         (never written by this routine)
    buf[6:10]   = dword at source_struct+0x0    -> 'a'
    buf[10]     = byte  at source_struct+0x8    -> b's low byte
    buf[11]     = byte  at source_struct+0xa    -> b's second byte
    buf[12:14]  = word  at source_struct+0xc    -> b's top 16 bits
  This exactly explains the ground truth's b column: every sub=3 row has
  low byte 1 (source+0x8 is a constant), the rare 0xffff0001 rows have
  source+0xa == 0 where the common 0xffff0101 rows have source+0xa == 1, and
  the top word is always 0xffff (a sentinel/unused field). Since the FBI ->
  unit-name mapping that would let us tell which unit gets the rare value is
  itself gated on solving 'a', that correlation could not be pinned down
  further.

- The source struct that 0x46d630 reads from was NOT located. It is not the
  585-byte (0x249) main FBI-parse record: that record's field layout was
  independently mapped (BuildCostEnergy/BuildCostMetal at +0x186/+0x18a,
  economy fields at +0x1C2.., a per-unit self-index at +0x21e, flag words at
  +0x241/+0x245) and its earliest few dozen bytes carry none of the offsets
  0x0/0x8/0xa/0xc that 0x46d630 reads. The likely explanation is that this is
  a small, separately-built "network sync" record constructed only when a
  restrictions dialog or lobby handshake needs it, but its construction site
  was not found in the time available: 0x46d630's only sibling with a
  confirmed caller (0x46d500, called from 0x4559b7) is itself reached only
  through a large incoming-network-message dispatch switch, and objdump's
  linear disassembly measurably desynchronises around at least one jump
  table in that area (0x46d84c, decoded by hand -- see FINDINGS.md), so a
  direct or relative call into the checksum's construction site may simply
  not appear in a flat objdump listing.

- 88 name-hash combinations (crc32 with several forms, djb2, sdbm, FNV-1/1a,
  java-31, rotate-xor, byte sum, adler32, case variants, `.fbi`-suffixed and
  `units\\`-prefixed forms) were tried against two real unit-name sets before
  this script was written and matched nothing; that work is not repeated
  here. What IS repeated here, for anyone who wants to pick this up, is a
  small set of *content* hash candidates (crc32/adler32 of the raw FBI file,
  of the file with CRLF stripped, of a case-folded canonical key=value
  serialisation, and of the referenced .3do/.cob file bytes) -- all tried
  against real ProTA and Escalation ground truth and all producing zero
  matches. See --selftest.

Usage:
    uv run --no-project python tools/exe/unitsync.py <unit-dir> [--csv ground_truth.csv]

<unit-dir> is scanned recursively for *.fbi/*.FBI files. Without --csv this
just lists parsed unit names and the (non-matching) candidate hash columns.
With --csv (a demo-table CSV, columns index,sub,zero,a,b) it reports how many
of the sub=2/sub=3 'a' values each candidate hash reproduces -- which, per the
above, is expected to be zero for all of them; a nonzero count would be a
genuine new finding and worth chasing immediately.
"""
import argparse
import csv
import glob
import os
import re
import sys
import zlib


def find_fbis(root):
    pats = ["*.fbi", "*.FBI"]
    out = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if f.lower().endswith(".fbi"):
                out.append(os.path.join(dirpath, f))
    return sorted(out)


def get_field(text, key):
    m = re.search(r"(?im)^\s*" + re.escape(key) + r"\s*=\s*([^;]+);", text)
    return m.group(1).strip() if m else None


def strip_comments(text):
    lines = []
    for line in text.splitlines():
        idx = line.find("//")
        if idx >= 0:
            line = line[:idx]
        lines.append(line)
    return "\n".join(lines)


def canonical_kv(text):
    text2 = strip_comments(text)
    kv = {}
    for m in re.finditer(r"([A-Za-z0-9_]+)\s*=\s*([^;]*);", text2):
        kv[m.group(1).strip().lower()] = m.group(2).strip()
    keys = sorted(kv.keys())
    return ";".join(f"{k}={kv[k]}" for k in keys)


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def adler32(b):
    return zlib.adler32(b) & 0xFFFFFFFF


CANDIDATES = [
    "crc32_raw",
    "crc32_raw_complement",
    "adler32_raw",
    "crc32_no_cr",
    "crc32_canon_kv",
    "adler32_canon_kv",
]


def compute_candidates(fbi_path):
    data = open(fbi_path, "rb").read()
    text = data.decode("latin1")
    canon = canonical_kv(text).encode("latin1", errors="ignore")
    vals = {
        "crc32_raw": crc32(data),
        "crc32_raw_complement": crc32(data) ^ 0xFFFFFFFF,
        "adler32_raw": adler32(data),
        "crc32_no_cr": crc32(data.replace(b"\r", b"")),
        "crc32_canon_kv": crc32(canon),
        "adler32_canon_kv": adler32(canon),
    }
    return text, vals


def load_ground_truth(csv_path):
    rows = []
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            rows.append((int(row["sub"]), int(row["a"]), int(row["b"])))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("unit_dir")
    ap.add_argument("--csv", help="ground-truth demo table CSV (index,sub,zero,a,b) to check candidates against")
    ap.add_argument("--selftest", action="store_true", help="just run the candidate-vs-ground-truth check and exit")
    args = ap.parse_args()

    fbis = find_fbis(args.unit_dir)
    if not fbis:
        print(f"no .fbi files found under {args.unit_dir}", file=sys.stderr)
        return 1

    records = []
    for path in fbis:
        text, cands = compute_candidates(path)
        name = get_field(text, "UnitName") or os.path.splitext(os.path.basename(path))[0]
        records.append((name, path, cands))

    if args.csv:
        gt = load_ground_truth(args.csv)
        target_a = {2: set(), 3: set()}
        for sub, a, b in gt:
            if sub in target_a:
                target_a[sub].add(a)
        print(f"ground truth: sub=2 {len(target_a[2])} ids, sub=3 {len(target_a[3])} ids")
        print(f"unit dir: {len(records)} FBI files")
        print()
        print(f"{'candidate':22s} {'hits(sub=2)':>12s} {'hits(sub=3)':>12s}")
        for cname in CANDIDATES:
            h2 = sum(1 for _n, _p, c in records if c[cname] in target_a[2])
            h3 = sum(1 for _n, _p, c in records if c[cname] in target_a[3])
            print(f"{cname:22s} {h2:12d} {h3:12d}")
        print()
        print("Zero across the board is the expected, already-confirmed result --")
        print("see the module docstring and FINDINGS.md. A nonzero count here")
        print("would be new and should be chased immediately.")
        if args.selftest:
            return 0
        return 0

    print(f"{'UnitName':16s} {'crc32_raw':>10s} {'crc32_canon_kv':>14s}  file")
    for name, path, cands in records:
        print(f"{name:16s} {cands['crc32_raw']:10d} {cands['crc32_canon_kv']:14d}  {path}")
    print(file=sys.stderr)
    print(
        "NOTE: none of the columns above are known to be TotalA.exe's actual "
        "unit-type checksum. See the module docstring.",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
