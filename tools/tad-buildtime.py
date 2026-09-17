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
  * **Airborne** (a construction aircraft). Gets TWO increments on the tick the
    0x09 is emitted, so an episode is two less than the number of increments,
    not one. VTOL_MobileBuild (0x413D80) calls the INBUILDSTANCE wait and
    ignores its answer, and the wait's side effect -- adding event bit 0x4 to
    the wake mask -- matches the 0x4 every COB `set` leaves pending, so the
    service loop 0x43B7C0 runs the lathe a second time before the tick ends.
    docs/TOTALA-EXE.md section 101 has the addresses and
    tools/exe/buildloop.py the replay. Over the Escalation corpus 60 of 68 such
    builds land on the model exactly and the other eight are late by whole
    seconds (the stall shape), none early. Scored cell by cell, like a factory,
    against that model.
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


def unit_namer(episodes):
    """name_at(demo, unit id, tick): the most recent build of that id to finish by then.

    Scoped in time because TA recycles unit ids; see cells(). A function of its
    own so that tools/tad-stalltime.py names builders exactly the way the cells
    here do rather than with a copy of the rule.
    """
    lives = collections.defaultdict(list)
    for e in episodes:
        name = e.get("unitName")
        if name is not None:
            lives[(e["demo"], e["unitId"])].append((e["finishTick"], name))
    for entries in lives.values():
        entries.sort()

    def name_at(demo, unit_id, tick):
        entries = lives.get((demo, unit_id))
        if not entries:
            return None
        best = None
        for finish, name in entries:
            if finish <= tick:
                best = name
        return best

    return name_at


def cells(episodes, units, min_builds):
    """(builder type, product type) -> the durations of every build of that pair.

    A builder's own type is known only where the builder was itself built during
    the recording, which is what makes builder-keyed rows possible at all: pair
    builderId back to the unitId of an earlier episode.

    That pairing has to be SCOPED IN TIME, because TA recycles unit ids heavily:
    in demo 14725, 2,622 of 4,093 distinct ids are reused by a later nanoframe
    and 2,550 of those by a different unit type. A map that keeps the first name
    an id ever held therefore looks most builders up under a stale name. So each
    id carries its whole list of builds and the name asked for is the most
    recent build that finished at or before the build under consideration
    started.

    Scoping does not move a single mode -- verified cell for cell -- because a
    stale name fails in one of two harmless ways: a different WorkerTime puts
    the duration outside the outlier cap and drops the build, and an identical
    one (every stock factory is p = 4) lands it in the wrong cell with the right
    duration, since only p and the product's BuildTime enter the arithmetic.
    What it buys is evidence: builds roughly double and four more pairs clear
    --min-builds.
    """
    name_at = unit_namer(episodes)

    grouped = collections.defaultdict(list)
    for e in episodes:
        builder = name_at(e["demo"], e["builderId"], e["startTick"])
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

        kind = builder_class(units[builder])
        # One increment lands on the 0x09's tick; a construction aircraft gets a
        # second one there too (docs/TOTALA-EXE.md section 101).
        predicted = ticks_to_build(build_time, p) - (2 if kind == "airborne" else 1)
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
                kind=kind,
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

    # The airborne class is scored cell by cell against its own model, two
    # increments on the creation tick. Its cells are thin -- a construction
    # aircraft builds few of any one thing -- so the pool is printed beside
    # them, per builder, as the wider view: offsets there should be 0, or late
    # by whole seconds where a stall held the job up, and never early.
    if airborne:
        print(f"\nthe {len(airborne)} pairs whose builder flies, against two increments on the 0x09's tick:\n")
        print(f"  {'builder':<10} {'product':<12} {'BuildTime':>9} {'p':>3} {'model':>6} {'mode':>6} {'n':>5} {'share':>6}")
        for r in sorted(airborne, key=lambda r: -r["n"]):
            flag = "" if r["mode"] == r["predicted"] else "   <-- disagrees"
            print(
                f"  {r['builder']:<10} {r['product']:<12} {r['build_time']:>9} {r['p']:>3} {r['predicted']:>6}"
                f" {r['mode']:>6} {r['n']:>5} {100 * r['share']:>5.0f}%{flag}"
            )
        pool = collections.Counter(d - r["predicted"] for r in airborne for d in r["durations"])
        builds = sum(pool.values())
        print(f"\n  pooled: {builds} builds, {pool.get(0, 0)} on the model, fastest {min(pool):+}")
        for builder in sorted({r["builder"] for r in airborne}):
            own = collections.Counter(
                d - r["predicted"] for r in airborne if r["builder"] == builder for d in r["durations"]
            )
            rates = sorted({r["p"] for r in airborne if r["builder"] == builder})
            late = ", ".join(f"{k:+}x{v}" for k, v in sorted(own.items()) if k != 0) or "none"
            print(
                f"    {builder:<10} p={','.join(str(x) for x in rates):<5} {sum(own.values()):>4} builds,"
                f" {own.get(0, 0):>4} on the model; off it: {late}"
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

    misses = [r for r in scored + airborne if r["mode"] != r["predicted"]]
    print()
    for r in misses:
        print(
            f"MISS {r['builder']} -> {r['product']} ({r['kind']}): BuildTime {r['build_time']}, p {r['p']},"
            f" model {r['predicted']}, corpus {r['mode']} over {r['n']} builds"
            f" ({100 * r['share']:.0f}% at the mode)"
        )

    if misses:
        print(f"\n{len(misses)} disagreement(s) with the model")
        return 1

    print(f"all {len(scored)} immobile and {len(airborne)} airborne pairs agree with the model")
    return 0


if __name__ == "__main__":
    sys.exit(main())
