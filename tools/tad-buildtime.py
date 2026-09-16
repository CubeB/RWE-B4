#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Check TA's build-completion arithmetic against a demo corpus.

A builder contributes p = WorkerTime / 30 build units a tick, so the obvious
model of a job is ceil(BuildTime / p) ticks. That model is wrong, and the way it
is wrong is what this script exists to keep checkable.

What TA actually does is accumulate a **single-precision completion fraction**:

    x = (float)p / (float)BuildTime          computed once
    frac += x                                every tick the builder is lathing
    done when frac > 1.0f                    strictly greater

which is not the same number. Three things follow, and all three are visible in
the corpus:

  * The first increment lands on the tick the 0x09 is emitted, so an episode's
    finishTick - startTick is one less than the number of increments.
  * Where BuildTime is not a multiple of p the two models agree on the count.
  * Where it *is* a multiple they do not, and which way it falls depends on
    whether repeated float32 addition of x overshoots or lands exactly on 1.0f.
    That is the discriminating test: it gives a pattern of extra-tick / no-extra
    tick across those pairs that no integer model can produce, and the corpus
    reproduces it bit for bit. Doing the same sum in double precision gets 6 of
    15 right, which is what says the arithmetic is genuinely 32-bit.

Only unassisted builds mean anything here, so every cell is consumed as its
MODE -- assists shorten a build and missed micro-stalls lengthen it -- and
builders fall into three classes that have to be scored apart:

  * **Immobile** (a factory). Nothing to walk and nothing to deploy, so the
    model applies as written. This is the class an oracle should assert.
  * **Airborne** (a construction aircraft). Consistently finishes one tick
    SOONER than the model, which is an increment the model does not account
    for: over the Escalation corpus 58 of 66 such builds land on exactly -1 and
    none is faster, against 1 of 6,658 ground builds. Three builders at two
    different rates agree, so it is not a rate artifact. Scored at -1, and the
    exit code covers it, so that the regularity stops being invisible.
  * **Ground and mobile** (a construction vehicle or kbot). Pays its own COB
    deploy sequence before INBUILDSTANCE is set, which is real, is data rather
    than engine, and varies by mod, so there is no model to score it against.
    Listed by --overheads, never scored. See docs/TA-DEMOS.md.

    ./build/tad_episodes --file ~/ta-demos/14723.ted --units ~/ta-mods/x-esc \
        --emit-json /tmp/ep.json
    tools/tad-buildtime.py --episodes /tmp/ep.json --units ~/ta-mods/x-esc

The cell grouping and the completion model are both ported into
src/tad_episodes.cpp, which is what generates the checked-in fixture
(src/rwe/sim/tad_build_episodes.h). This script is the reference: `tad_episodes
--cells` prints the same table over the same episodes and the two must keep
agreeing, cell for cell. If they part company it is the port that is wrong.

Exits non-zero if any scored cell disagrees with the model, so this is a check
and not a listing. It ALSO exits non-zero when there is nothing to score, which
reads like a failure and is not one: the single ProTA demo in the corpus has
only one immobile-builder pair with enough builds, so it needs --min-builds 3
and at the default says "nothing to score". Read the message, not just the
status. Neither demos nor mod files are in the repository; both arguments are
paths.
"""

import argparse
import collections
import json
import os
import re
import struct
import sys


def f32(x):
    """The nearest float32, as TA's FPU would leave it in a 4-byte slot."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def ticks_to_build(build_time, p, limit=400000):
    """How many increments TA needs, by replaying its accumulator."""
    x = f32(f32(p) / f32(build_time))
    frac = f32(0.0)
    n = 0
    while frac <= f32(1.0) and n < limit:
        frac = f32(frac + x)
        n += 1
    return n


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


def is_mobile(unit):
    return (unit.get("maxvelocity") or "0").strip() not in ("0", "0.0", "")


def flies(unit):
    return (unit.get("canfly") or "0").strip() not in ("0", "")


def builder_class(unit):
    """Which of the three scoring classes this builder belongs to."""
    if not is_mobile(unit):
        return "immobile"
    return "airborne" if flies(unit) else "ground"


def cells(episodes, units, min_builds):
    """(builder type, product type) -> the durations of every build of that pair.

    A builder's own type is known only where the builder was itself built during
    the recording, which is what makes builder-keyed rows possible at all: pair
    builderId back to the unitId of an earlier episode.
    """
    born = {}
    for e in sorted(episodes, key=lambda e: (e["demo"], e["finishTick"])):
        born.setdefault((e["demo"], e["unitId"]), e.get("unitName"))

    grouped = collections.defaultdict(list)
    for e in episodes:
        builder = born.get((e["demo"], e["builderId"]))
        product = e.get("unitName")
        if builder in units and product in units:
            grouped[(builder, product)].append(e["durationTicks"])

    out = []
    for (builder, product), durations in grouped.items():
        worker_time = units[builder].get("workertime")
        build_time = units[product].get("buildtime")
        if not worker_time or not build_time:
            continue
        worker_time, build_time = int(worker_time), int(build_time)
        p = worker_time // 30
        if p <= 0 or build_time <= 0:
            continue

        predicted = ticks_to_build(build_time, p) - 1
        # Assisted builds are the bulk of a competitive game and only ever
        # shorten; a cap either side keeps them and the badly stalled ones from
        # dragging the mode off the unassisted build.
        kept = [d for d in durations if -20 <= d - predicted <= 120]
        if len(kept) < min_builds:
            continue
        mode, at_mode = collections.Counter(kept).most_common(1)[0]
        out.append(
            dict(
                builder=builder,
                product=product,
                build_time=build_time,
                worker_time=worker_time,
                p=p,
                predicted=predicted,
                mode=mode,
                n=len(kept),
                share=at_mode / len(kept),
                fastest=min(kept),
                floor=build_time // p,
                ceil=-(-build_time // p),
                durations=kept,
                kind=builder_class(units[builder]),
            )
        )
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--episodes", required=True, help="tad_episodes --emit-json output")
    ap.add_argument("--units", required=True, help="directory of the data set's unit files, recursed")
    ap.add_argument("--min-builds", type=int, default=5, help="builds a pair needs to be scored (default 5)")
    ap.add_argument("--overheads", action="store_true", help="also list the mobile builders and what they cost")
    args = ap.parse_args()

    units = load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")

    episodes = json.load(open(args.episodes))
    rows = cells(episodes, units, args.min_builds)
    scored = [r for r in rows if r["kind"] == "immobile"]
    airborne = [r for r in rows if r["kind"] == "airborne"]
    if not scored:
        sys.exit("no immobile-builder pairs met --min-builds; nothing to score")

    demos = sorted({e["demo"] for e in episodes})
    print(f"{len(episodes)} episodes over {len(demos)} demo(s), {len(rows)} pairs with {args.min_builds}+ builds")
    print(f"scoring the {len(scored)} whose builder is immobile\n")

    for name, predict in (
        ("ceil(BuildTime / p)", lambda r: r["ceil"]),
        ("floor(BuildTime / p)", lambda r: r["floor"]),
        ("float32 completion fraction", lambda r: r["predicted"]),
    ):
        hits = sum(1 for r in scored if r["mode"] == predict(r))
        print(f"  {name:<30} {hits:>3}/{len(scored)} pairs exact")

    divisible = [r for r in scored if r["build_time"] % r["p"] == 0]
    if divisible:
        print(f"\nthe {len(divisible)} pairs where BuildTime is a multiple of p, which is")
        print("where an integer model and the float32 one part company:\n")
        print(f"  {'builder':<10} {'product':<12} {'BuildTime':>9} {'p':>3} {'BT/p':>6} {'extra':>6} {'mode':>6} {'n':>5} {'share':>6}")
        for r in sorted(divisible, key=lambda r: -r["n"]):
            extra = (r["predicted"] + 1) - r["floor"]
            flag = "" if r["mode"] == r["predicted"] else "   <-- disagrees"
            print(
                f"  {r['builder']:<10} {r['product']:<12} {r['build_time']:>9} {r['p']:>3} {r['floor']:>6}"
                f" {extra:>+6} {r['mode']:>6} {r['n']:>5} {100 * r['share']:>5.0f}%{flag}"
            )

    # The airborne class is pooled rather than scored cell by cell: a
    # construction aircraft builds few of any one thing, so its cells are thin
    # and the mode of the pool is the trustworthy statistic.
    air_mode = None
    if airborne:
        pool = collections.Counter(d - r["predicted"] for r in airborne for d in r["durations"])
        air_mode, air_at = pool.most_common(1)[0]
        builds = sum(pool.values())
        print(f"\nairborne builders, pooled over {len(airborne)} pairs and {builds} builds:")
        print(f"  modal offset from the model {air_mode:+}, on {air_at} of {builds} builds, fastest {min(pool):+}")
        for builder in sorted({r["builder"] for r in airborne}):
            own = collections.Counter(
                d - r["predicted"] for r in airborne if r["builder"] == builder for d in r["durations"]
            )
            rates = sorted({r["p"] for r in airborne if r["builder"] == builder})
            print(
                f"    {builder:<10} p={','.join(str(x) for x in rates):<5} {sum(own.values()):>4} builds,"
                f" {own.get(air_mode, 0):>4} at {air_mode:+}, fastest {min(own):+}"
            )

    if args.overheads:
        print("\nground mobile builders, which pay their own COB deploy before lathing:")
        print("a hard floor well above zero is a deploy sequence; a floor at zero with a")
        print("long tail is a builder that sometimes had to reposition. Never scored.\n")
        print(f"  {'builder':<10} {'product':<12} {'n':>5} {'mode d':>7} {'fastest':>8} {'share':>6}")
        for r in sorted((r for r in rows if r["kind"] == "ground"), key=lambda r: -r["n"]):
            print(
                f"  {r['builder']:<10} {r['product']:<12} {r['n']:>5} {r['mode'] - r['predicted']:>+7}"
                f" {r['fastest'] - r['predicted']:>+8} {100 * r['share']:>5.0f}%"
            )

    misses = [r for r in scored if r["mode"] != r["predicted"]]
    print()
    for r in misses:
        print(
            f"MISS {r['builder']} -> {r['product']}: BuildTime {r['build_time']}, p {r['p']},"
            f" model {r['predicted']}, corpus {r['mode']} over {r['n']} builds"
            f" ({100 * r['share']:.0f}% at the mode)"
        )
    if airborne and air_mode != -1:
        print(f"MISS airborne builders pool at {air_mode:+} against the model, not -1")

    failed = len(misses) + (1 if airborne and air_mode != -1 else 0)
    if failed:
        print(f"\n{failed} disagreement(s) with the model")
        return 1

    print(f"all {len(scored)} scored pairs agree with the model", end="")
    print(", and the airborne pool sits at -1 as it should" if airborne else "")
    return 0


if __name__ == "__main__":
    sys.exit(main())
