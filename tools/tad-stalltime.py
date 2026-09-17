#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Check TA's once-a-second resource settle against a demo corpus.

TA does not refuse a consumer because the stockpile is empty. A builder asks
every tick; once a second the player's whole asking is settled against one
fraction per resource, and whatever a consumer was granted and not paid for is
carried as DEBT on that consumer (docs/TOTALA-EXE.md section 23). The only gate
between settles is the debt: a unit that owes anything is refused outright, and
nothing it does can clear the debt before the next settle. So a stall costs a
builder whole seconds and never part of one.

The corpus samples a player's stockpile (0x28) once every 120 ticks, so a
single settle's fractions cannot be read off it. What CAN be read is the shape
the debt rule leaves on a factory's build timings, and the 0x28 samples are the
independent witness that a stall happened. This script measures three things:

  1. THE PHASE. Every 0x28 is sent from the settle pass, one settle in four, so
     its tick says where the settles fall. They fall on multiples of 30 of the
     demo clock for every player of every game, within the few ticks of lag a
     sender's clock carries -- not staggered per player. TotalA.exe initialises
     every player's next-settle tick in one loop at the same game tick
     (0x464990 -> 0x464700), which is why. Scored: every sender.

  2. THE QUANTUM. A factory build on a cell whose mode tad-buildtime.py already
     explains, and which is late against that model, is late by an exact
     multiple of 30 far more often than chance allows; and a build with a
     zero-stock sample inside it is almost never on time. Printed, not scored:
     assists shorten builds by amounts nothing in the stream records, so the
     population is contaminated in a way no filter can undo.

  3. THE EPISODES, which are scored. A factory finishes one product during a
     second whose settle a 0x28 shows stalled, and starts its next product
     before the following settle. It was granted resources in the stalled
     second, so it is in debt, so the new job is refused from its first tick
     until a settle pays the debt off -- which can only happen on a settle.
     Prediction: the new job is late by exactly (30 - start % 30) plus a whole
     number of further stalled seconds. Nothing about that residue is read
     from the build it predicts: it comes from the start tick, the settle
     cadence and a stall the 0x28 stream saw.

The filters are the work, as ever. Only immobile builders, whose (builder,
product) cell agrees with tad-buildtime.py's float32 model at its mode. The
builder's builds either side must not be early against the model either,
because an early build means something was assisting it and an assist moves a
duration by an amount nothing records. A demo whose own unit table disagrees
with --units is dropped, as the build cells drop it.

    ./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc --all \\
        --emit-json /tmp/ep.json --emit-resources /tmp/res.json
    tools/tad-stalltime.py --episodes /tmp/ep.json --resources /tmp/res.json \\
        --units ~/ta-mods/x-esc

--episodes MUST be an --all dump: the builds this is about are exactly the ones
tad_episodes rejects as "owner stalled", and the build cells are recomputed here
from the clean subset so they are the cells tad-buildtime.py scores.

The completion model and the builder naming are imported from
tools/tad-buildtime.py, not copied. The pass is ported into src/tad_episodes.cpp
as --stall-episodes and --emit-stall-cpp, which writes
src/rwe/sim/tad_stall_episodes.h; this script is the reference and the two must
agree episode for episode.

Exits non-zero if the phase check fails for any sender or any scored episode
misses its predicted residue, and also when there is nothing to score. Neither
demos nor mod files are in the repository; the arguments are paths.
"""

import argparse
import bisect
import collections
import importlib.util
import json
import os
import sys

sys.dont_write_bytecode = True  # importing tad-buildtime.py must not leave a __pycache__ in tools/

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("tad_buildtime", os.path.join(_here, "tad-buildtime.py"))
bt = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bt)

# Rejections that say a duration does not measure the nanolathe at all. "owner
# stalled" is deliberately not one of them: that is the subject here.
SPOILT = {"frame took damage", "builder took damage", "speed change", "builder in another owner block", "no elapsed ticks"}

SETTLE = 30

# How far a 0x28's tick may sit from the settle it reports. A sender stamps it
# with the last 0x2c serial it sent, which runs a few ticks behind; the corpus
# puts 99.7% of samples within 6 and none of the residue between 7 and 23 is
# more than a scattering.
SAMPLE_LAG = 6

# The share of one sender's samples that must sit that close to a settle.
PHASE_SHARE = 0.95

# The scored builds that do not land on their residue, keyed (demo, builder id,
# start tick), with how many ticks SHORT of it each falls. All four are short and
# none is long, and a debt can only lengthen a job while an assist can only
# shorten one, so they read as assisted builds the neighbour guard did not catch:
# a helper that joined this job and neither of the two either side of it. That
# is a reading and not a proof -- nothing in the stream records an assist -- so
# they are named here rather than filtered out, and they never become episodes.
KNOWN_EXCEPTIONS = {
    ("14725.ted", 2008, 56516): 19,  # ARMLAB -> ARMJETH
    ("14727.ted", 8056, 25783): 2,  # CORALAB -> CORPYRO
    ("14728.ted", 6064, 26139): 18,  # ARMAVP -> ARMLATNK
    ("14730.ted", 7653, 81332): 4,  # ARMAP -> ARMPEEP
}


def nearest_settle(tick):
    return ((tick + SETTLE // 2) // SETTLE) * SETTLE


def model_duration(units, product, p):
    build_time = int(units[product].get("buildtime") or 0)
    if build_time <= 0 or p <= 0:
        return None
    return bt.ticks_to_build(build_time, p) - 1


def load(args):
    units = bt.load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")
    episodes = json.load(open(args.episodes))
    resources = json.load(open(args.resources))

    wrong = {d["demo"] for d in resources if "unitTypes" in d and d["unitTypes"] != len(units)}
    episodes = [e for e in episodes if e["demo"] not in wrong]
    resources = [d for d in resources if d["demo"] not in wrong]
    return units, episodes, resources, wrong


def samples_by_block(resources):
    """(demo, owner block) -> [(tick, metal stored, energy stored)], watchers dropped."""
    out = {}
    for d in resources:
        blocks = {int(k): v for k, v in d["senderBlocks"].items()}
        per = collections.defaultdict(list)
        for r in d["records"]:
            if r["metalStorage"] == 0.0 and r["energyStorage"] == 0.0:
                continue
            if r["sender"] in blocks:
                per[blocks[r["sender"]]].append((r["tick"], r["metalStored"], r["energyStored"]))
        for block, rows in per.items():
            rows.sort()
            out[(d["demo"], block)] = rows
    return out


def phase(resources):
    rows = []
    for d in resources:
        per = collections.defaultdict(list)
        for r in d["records"]:
            if r["metalStorage"] == 0.0 and r["energyStorage"] == 0.0:
                continue
            per[r["sender"]].append(r["tick"])
        for sender, ticks in sorted(per.items()):
            near = sum(1 for t in ticks if abs(t - nearest_settle(t)) <= SAMPLE_LAG)
            rows.append((d["demo"], sender, len(ticks), near))
    return rows


def factory_builds(units, episodes, agreeing):
    """Every paired build by an immobile builder, named, with its lateness where modelled."""
    name_at = bt.unit_namer(episodes)
    out = []
    for e in episodes:
        builder = name_at(e["demo"], e["builderId"], e["startTick"])
        product = e.get("unitName")
        if builder not in units or product not in units:
            continue
        if bt.builder_class(units[builder]) != "immobile":
            continue
        p = int(units[builder].get("workertime") or 0) // 30
        model = model_duration(units, product, p)
        if model is None:
            continue
        out.append(
            dict(
                demo=e["demo"],
                block=e["ownerBlock"],
                builderId=e["builderId"],
                builder=builder,
                product=product,
                start=e["startTick"],
                finish=e["finishTick"],
                duration=e["durationTicks"],
                model=model,
                late=e["durationTicks"] - model,
                spoilt=bool(set(e.get("rejections", [])) & SPOILT),
                scored_cell=(builder, product) in agreeing,
            )
        )
    return out


def zero_near(rows, settle):
    """The samples reporting this settle, and whether any of them read an empty store."""
    ticks = [r[0] for r in rows]
    i = bisect.bisect_left(ticks, settle - SAMPLE_LAG)
    near = [r for r in rows[i:] if r[0] <= settle + SAMPLE_LAG]
    return near, [r for r in near if r[1] == 0.0 or r[2] == 0.0]


def stall_episodes(builds, samples):
    """The refused-into-the-next-job episodes, and a tally of why the rest were not."""
    by_builder = collections.defaultdict(list)
    for b in builds:
        by_builder[(b["demo"], b["builderId"])].append(b)

    tally = collections.Counter()
    scored = []
    for (demo, _), runs in sorted(by_builder.items()):
        runs.sort(key=lambda b: (b["start"], b["finish"]))
        for i in range(1, len(runs)):
            prev, cur = runs[i - 1], runs[i]
            nxt = runs[i + 1] if i + 1 < len(runs) else None

            start = cur["start"]
            if start % SETTLE == 0:
                continue
            settle = start - start % SETTLE
            # The previous job was lathing in the second this settle closes.
            if not (settle - SETTLE <= prev["finish"] <= settle - 1):
                continue
            # A different unit under a recycled id is not the same factory.
            if prev["builder"] != cur["builder"]:
                continue
            near, zeros = zero_near(samples.get((demo, cur["block"]), []), settle)
            if not near:
                continue

            # The CONTROL: the same pattern where the sample says the settle was
            # NOT a stall. Everything else about it -- a job finished just before
            # a settle and the next started just after -- is identical, so if the
            # residue came from the selection rather than from the debt, it would
            # show up here too.
            if not zeros:
                if not cur["spoilt"] and cur["scored_cell"] and prev["late"] >= 0 and cur["late"] >= 0 and (
                    nxt is None or nxt["late"] >= 0
                ):
                    residue = SETTLE - start % SETTLE
                    control = cur["late"] >= residue and (cur["late"] - residue) % SETTLE == 0
                    tally["control: settle not stalled, residue " + ("predicted" if control else "not predicted")] += 1
                continue

            tally["candidates"] += 1
            if cur["spoilt"]:
                tally["rejected: damage or speed change"] += 1
                continue
            if not cur["scored_cell"]:
                tally["rejected: cell not explained by the build model"] += 1
                continue
            if prev["late"] < 0 or (nxt is not None and nxt["late"] < 0):
                tally["rejected: a neighbouring build was assisted"] += 1
                continue
            if cur["late"] < 0:
                tally["rejected: assisted"] += 1
                continue

            residue = SETTLE - start % SETTLE
            hit = cur["late"] >= residue and (cur["late"] - residue) % SETTLE == 0
            scored.append(
                dict(
                    cur,
                    previousProduct=prev["product"],
                    previousStart=prev["start"],
                    previousFinish=prev["finish"],
                    settle=settle,
                    stalledResource="metal" if zeros[0][1] == 0.0 else "energy",
                    sampleTick=zeros[0][0],
                    residue=residue,
                    furtherStalls=(cur["late"] - residue) // SETTLE if hit else None,
                    hit=hit,
                )
            )
    return scored, tally


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--episodes", required=True, help="tad_episodes --all --emit-json output")
    ap.add_argument("--resources", required=True, help="tad_episodes --emit-resources output")
    ap.add_argument("--units", required=True, help="directory of the data set's unit files, recursed")
    ap.add_argument("--min-builds", type=int, default=5, help="builds a cell needs, as tad-buildtime.py (default 5)")
    ap.add_argument("--list", action="store_true", help="print every scored episode")
    args = ap.parse_args()

    units, episodes, resources, wrong = load(args)
    for demo in sorted(wrong):
        print(f"excluding {demo}: its unit table disagrees with --units")

    failures = 0

    # 1. The phase.
    rows = phase(resources)
    total = sum(r[2] for r in rows)
    near = sum(r[3] for r in rows)
    worst = min(rows, key=lambda r: r[3] / r[2])
    print(f"\n1. settle phase: {near} of {total} resource samples, from {len(rows)} senders,")
    print(f"   sit within {SAMPLE_LAG} ticks of a multiple of {SETTLE} of the demo clock")
    print(f"   least aligned sender: {worst[0]} sender {worst[1]}, {worst[3]} of {worst[2]}")
    for demo, sender, n, k in rows:
        if k / n < PHASE_SHARE:
            print(f"MISS phase: {demo} sender {sender}: only {k} of {n} samples near a settle")
            failures += 1

    # 2. The quantum, printed.
    clean = [e for e in episodes if not e.get("rejections")]
    agreeing = {
        (c["builder"], c["product"])
        for c in bt.cells(clean, units, args.min_builds)
        if c["kind"] == "immobile" and c["mode"] == c["predicted"]
    }
    builds = factory_builds(units, episodes, agreeing)
    samples = samples_by_block(resources)

    shape = collections.Counter()
    inside = collections.Counter()
    for b in builds:
        if b["spoilt"] or not b["scored_cell"]:
            continue
        late = b["late"]
        kind = "early" if late < 0 else "on the model" if late == 0 else "late by 30k" if late % SETTLE == 0 else "late otherwise"
        shape[kind] += 1
        rows_ = samples.get((b["demo"], b["block"]), [])
        stalled = False
        for t, m, en in rows_:
            s = nearest_settle(t)
            if b["start"] <= s - SETTLE and s + SETTLE <= b["finish"] and (m == 0.0 or en == 0.0):
                stalled = True
                break
        inside[(kind, stalled)] += 1
    print(f"\n2. factory builds on the {len(agreeing)} cells the build model explains:")
    for kind in ("on the model", "late by 30k", "late otherwise", "early"):
        print(
            f"   {kind:<15} {shape[kind]:>6}   with an empty store sampled inside: {inside[(kind, True)]:>5}"
        )
    late = shape["late by 30k"] + shape["late otherwise"]
    if late:
        print(f"   {shape['late by 30k']} of {late} late builds are late by whole seconds, where chance gives 1 in {SETTLE}")

    # 3. The episodes, scored.
    scored, tally = stall_episodes(builds, samples)
    print("\n3. a factory refused into its next job by a stall the 0x28 stream saw:")
    for key in sorted(tally):
        print(f"   {key:<50} {tally[key]:>5}")
    hits = [e for e in scored if e["hit"]]
    print(f"   scored {len(scored)}, residue predicted exactly in {len(hits)}")
    residues = collections.Counter(e["residue"] for e in hits)
    print(f"   {len(residues)} distinct residues, {len({(e['demo'], e['block']) for e in hits})} players, "
          f"{len({e['demo'] for e in hits})} demos")

    if args.list:
        for e in scored:
            print(
                f"   {e['demo']} block {e['block']} {e['builder']} {e['previousProduct']}->{e['product']}"
                f" finish {e['previousFinish']} settle {e['settle']} ({e['stalledResource']} empty at {e['sampleTick']})"
                f" start {e['start']} late {e['late']} residue {e['residue']}"
                + ("" if e["hit"] else "   <-- MISS")
            )

    # A miss is new unless KNOWN_EXCEPTIONS names that build AND it still falls
    # short by what it fell short by when it was named. A named build that starts
    # landing on its residue is reported too, so the list cannot go stale.
    seen = set()
    for e in scored:
        key = (e["demo"], e["builderId"], e["start"])
        shortfall = (e["residue"] - e["late"]) % SETTLE
        known = KNOWN_EXCEPTIONS.get(key)
        if known is not None:
            seen.add(key)
            if not e["hit"] and shortfall == known:
                print(
                    f"KNOWN {e['demo']} {e['builder']} -> {e['product']} at {e['start']}:"
                    f" {shortfall} ticks short of its residue, which only an assist can make"
                )
                continue
            print(
                f"MOVED {e['demo']} {e['builder']} -> {e['product']} at {e['start']}: was {known} short,"
                f" now late {e['late']} against residue {e['residue']}"
            )
            failures += 1
            continue
        if not e["hit"]:
            print(
                f"MISS {e['demo']} {e['builder']} {e['builderId']} -> {e['product']}: start {e['start']}, late {e['late']},"
                f" predicted {e['residue']} + 30k ({shortfall} short)"
            )
            failures += 1
    for key in sorted(set(KNOWN_EXCEPTIONS) - seen):
        print(f"MOVED {key}: named as a known exception but no longer scored")
        failures += 1

    if not scored:
        print("nothing to score")
        return 1
    if failures:
        print(f"\n{failures} disagreement(s)")
        return 1
    print(
        f"\nevery sender settles on the common phase; {len(hits)} of {len(scored)} scored builds land on their"
        f" residue and the {len(scored) - len(hits)} known exception(s) still read what they read"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
