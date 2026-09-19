#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Score candidate unit load orders against a demo corpus.

A demo's 0x09 carries its unit type as a load-order index (docs/TA-DEMOS.md,
"0x09"). This is the validator that settled which order that is, kept so the
positive result stays checkable the way tools/exe/unitsync.py keeps the
checksum negative checkable -- a claim about an ordering should be re-runnable
against real data, not taken on trust.

The invariant needs no absolute calibration:

    for one builder, buildDurationTicks / BuildTime is the same for every type
    it builds, because the only other term is that builder's own WorkerTime

so a candidate is scored by the mean coefficient of variation of that ratio
across builders that built two or more types. Zero is a perfect mapping.

    ./build/tad_episodes --file ~/ta-demos/14724.ted --emit-json /tmp/ep.json
    tools/tad-loadorder.py --episodes /tmp/ep.json --units ~/ta-mods/x-prota/units

The answer, for the record, is offset 1 -- sorted names numbered from one --
which scores 0.171 on ProTA 4.8 and 0.359 on TA: Escalation 10.2 against 0.494
and 0.927 for the same sort numbered from zero, with every other offset in
-8..+8 worse than 0.5 and 0.83 respectively.

Neither demos nor mod files are in the repository; both arguments are paths.
"""

import argparse
import collections
import json
import math
import os
import re
import statistics
import sys


def parse_fbi(path):
    """UnitName=value; pairs, comments stripped. Enough for BuildTime/WorkerTime."""
    raw = re.sub(r"//[^\n]*", "", open(path, "rb").read().decode("latin-1"))
    return {m.group(1).lower(): m.group(2).strip() for m in re.finditer(r"([A-Za-z0-9_]+)\s*=\s*([^;]*);", raw)}


def load_units(root):
    """Every *.FBI under root, keyed by upper-cased stem -- what the merged VFS sees."""
    units = {}
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            if f.lower().endswith(".fbi"):
                units[os.path.splitext(f)[0].upper()] = parse_fbi(os.path.join(dirpath, f))
    return units


def modal_cells(episodes):
    """(demo, builder, type) -> modal duration. The mode is the unassisted build."""
    cell = collections.defaultdict(list)
    for e in episodes:
        cell[(e["demo"], e["builderId"], e["typeIndex"])].append(e["durationTicks"])
    return {k: collections.Counter(v).most_common(1)[0][0] for k, v in cell.items()}


def score(cells, order, units, offset):
    """Mean CV of duration/BuildTime per builder. Lower is better; 0 is perfect."""
    index = {i + offset: n for i, n in enumerate(order)}
    builders = collections.defaultdict(dict)
    for (demo, builder, type_), duration in cells.items():
        builders[(demo, builder)][type_] = duration

    cvs = []
    for types in builders.values():
        ratios = []
        for type_, duration in types.items():
            name = index.get(type_)
            if name is None:
                continue
            build_time = units[name].get("buildtime")
            if build_time and int(build_time) > 0:
                ratios.append(duration / int(build_time))
        if len(ratios) >= 2:
            mean = statistics.mean(ratios)
            if mean > 0:
                cvs.append(statistics.pstdev(ratios) / mean)
    return (statistics.mean(cvs) if cvs else math.nan), len(cvs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--episodes", required=True, help="tad_episodes --emit-json output")
    ap.add_argument("--units", required=True, help="directory of the data set's unit files, recursed")
    ap.add_argument("--demo", action="append", help="restrict to these demo file names; repeatable")
    ap.add_argument("--offsets", default="-8:8", help="offset range to scan, lo:hi (default -8:8)")
    args = ap.parse_args()

    units = load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")

    episodes = json.load(open(args.episodes))
    if args.demo:
        wanted = set(args.demo)
        episodes = [e for e in episodes if e["demo"] in wanted]
    if not episodes:
        sys.exit("no episodes selected")

    order = sorted(units)
    cells = modal_cells(episodes)
    demos = sorted({e["demo"] for e in episodes})
    print(f"{len(units)} unit types, {len(episodes)} episodes over {len(demos)} demo(s)")

    lo, hi = (int(x) for x in args.offsets.split(":"))
    results = []
    for offset in range(lo, hi + 1):
        cv, n = score(cells, order, units, offset)
        results.append((cv, offset, n))
        print(f"  offset {offset:>3}: mean CV {cv:>7.4f}  ({n} builders with 2+ types)")

    best_cv, best_offset, _ = min(results)
    runner_up = min(cv for cv, offset, _ in results if offset != best_offset)
    print(f"\nbest offset {best_offset} at {best_cv:.4f}; next best {runner_up:.4f}")
    if best_offset != 1:
        print("NOTE: docs/TA-DEMOS.md says the answer is offset 1. It is not here.")


if __name__ == "__main__":
    main()
