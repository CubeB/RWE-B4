#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Check TA's storage capacity against a demo.

A player's metal and energy capacity is a plain sum over what that player has
FINISHED: the lobby's base, which is what the lobby gave the commander, plus
each finished unit's own MetalStorage / EnergyStorage. A nanoframe standing at
the time contributes nothing. That is the falsifiable half: crediting frames
breaks most samples with one in flight. docs/TA-DEMOS.md ("The economy
oracle") has the measurement.

Every 0x28 a player sends reports both capacities, once every 120 ticks. This
script walks each player's samples in order and predicts both from the builds
the demo paired. A player is scored from its first sample to the first one the
sum stops explaining.

The base is read off the player's first sample, which is the only way to learn
the lobby's setting. So a player who finished anything by then, meaning a
recording joined in progress, has no base and is not scored.

Where the sum stops explaining, the walk ends, as it does in the C++ miner.
Whether that is a finding or not depends on the shape:

  * **Lost.** The capacity is below the prediction and never comes back up to
    it. A storage unit died or was captured, and nothing in the build stream
    records either. That is a counted rejection, not a failure.
  * **Moved.** The capacity is above the prediction, or it dips below and the
    next sample is explained again. Neither has an explanation in the model:
    no unfinished frame, death or capture comes back the next sample, and only
    a finished unit adds storage. These are the scored failures.

Rejections are counted in classes and printed: a demo recorded on another data
set, a watcher, a recording that joined in progress, a build of a type the
unit table cannot name, and a storage loss.

    ./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc --all \\
        --emit-json /tmp/ep.json --emit-resources /tmp/res.json
    tools/tad-storagecapacity.py --episodes /tmp/ep.json --resources /tmp/res.json \\
        --units ~/ta-mods/x-esc

--episodes must be an --all dump. A build rejected as a DURATION still holds its
storage, which is why the C++ miner counts rejected builds too.

This is the reference for the storage oracle. `tad_episodes --emit-cpp` mines
the same model, with the same base, the same finish-tick rule, float32 sums in
the same order and the same stopping point, into the checked-in fixture
src/rwe/sim/tad_economy_episodes.h. The two must agree: --list prints every
sample at which a capacity changed, which is the set the miner chooses
episodes from.

Exits non-zero when a scored sample moves, and also when there is nothing to
score, so read the message and not just the status. Neither demos nor mod
files are in the repository; the arguments are paths.
"""

import argparse
import collections
import json
import os
import re
import struct
import sys


def f32(x):
    """The nearest float32, which is what TA and the C++ miner sum in."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def parse_fbi(path):
    """Key=value; pairs, comments stripped. The same reading tad-buildtime.py makes."""
    raw = re.sub(r"//[^\n]*", "", open(path, "rb").read().decode("latin-1"))
    return {m.group(1).lower(): m.group(2).strip() for m in re.finditer(r"([A-Za-z0-9_]+)\s*=\s*([^;]*);", raw)}


def load_units(root):
    """Every *.FBI under root, keyed by upper-cased stem: what the merged VFS sees."""
    units = {}
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            if f.lower().endswith(".fbi"):
                units[os.path.splitext(f)[0].upper()] = parse_fbi(os.path.join(dirpath, f))
    return units


def storage_of(unit):
    """(metal, energy) storage a finished unit grants, as float32."""

    def number(key):
        try:
            return f32(float(unit.get(key) or 0))
        except ValueError:
            return f32(0.0)

    return number("metalstorage"), number("energystorage")


def score_player(samples, completions, units):
    """Walk one player's samples; return (explained samples, changes, end, failure).

    samples: [(tick, metal capacity, energy capacity)] in the order sent.
    completions: every paired build of this player, sorted (finishTick, unitId).
    end is None if every sample was explained, else a rejection class or a
    failure kind; failure carries the numbers for a moved sample.
    """
    first_tick = samples[0][0]
    if any(c["finishTick"] <= first_tick for c in completions):
        return 0, [], "joined in progress", None

    # Every build has to be nameable, whenever it finished: the miner asks this
    # of the whole list at every sample, so one unnamed build anywhere in a
    # player's history keeps the player out altogether.
    if any(c.get("unitName") is None or c["unitName"].upper() not in units for c in completions):
        return 0, [], "unnamed unit type", None

    base_metal, base_energy = samples[0][1], samples[0][2]

    def predict(tick):
        # Summed in float32, in completion order, as the miner sums.
        metal, energy = base_metal, base_energy
        for c in completions:
            if c["finishTick"] <= tick:
                add_metal, add_energy = storage_of(units[c["unitName"].upper()])
                metal = f32(metal + add_metal)
                energy = f32(energy + add_energy)
        return metal, energy

    explained = 0
    changes = []
    last = None

    for i, (tick, metal, energy) in enumerate(samples):
        predicted = predict(tick)
        if (metal, energy) == predicted:
            explained += 1
            if (metal, energy) != last:
                changes.append((tick, metal, energy))
                last = (metal, energy)
            continue

        failure = (tick, predicted, (metal, energy))
        if metal > predicted[0] or energy > predicted[1]:
            return explained, changes, "moved: above the prediction", failure

        # Below. A loss stays below; a dip that the next sample explains again
        # is not something any unit's death or capture can do.
        if i + 1 < len(samples):
            next_tick, next_metal, next_energy = samples[i + 1]
            if (next_metal, next_energy) == predict(next_tick):
                return explained, changes, "moved: a dip the next sample undoes", failure

        return explained, changes, "storage lost", None

    return explained, changes, None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--episodes", required=True, help="tad_episodes --all --emit-json output")
    ap.add_argument("--resources", required=True, help="tad_episodes --emit-resources output")
    ap.add_argument("--units", required=True, help="directory of the data set's unit files, recursed")
    ap.add_argument("--list", action="store_true", help="print every sample at which a capacity changed")
    args = ap.parse_args()

    units = load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")
    episodes = json.load(open(args.episodes))
    resources = json.load(open(args.resources))

    rejections = collections.Counter()
    failures = []
    scored_players = 0
    scored_samples = 0

    by_block = collections.defaultdict(list)
    for e in episodes:
        by_block[(e["demo"], e["ownerBlock"])].append(e)
    for builds in by_block.values():
        builds.sort(key=lambda e: (e["finishTick"], e["unitId"]))

    for d in resources:
        demo = d["demo"]
        # A demo recorded on another data set names its units out of the wrong
        # load order, so it is dropped whole, as the build cells drop it.
        if "unitTypes" in d and d["unitTypes"] != len(units):
            rejections["other data set"] += 1
            continue

        blocks = {int(k): v for k, v in d["senderBlocks"].items()}
        per_sender = collections.defaultdict(list)
        for r in d["records"]:
            per_sender[r["sender"]].append((r["tick"], r["metalStorage"], r["energyStorage"]))

        for sender in sorted(per_sender):
            samples = per_sender[sender]
            if sender not in blocks or not samples:
                continue
            if samples[0][1] == 0.0 and samples[0][2] == 0.0:
                rejections["watcher"] += 1
                continue

            block = blocks[sender]
            explained, changes, end, failure = score_player(samples, by_block[(demo, block)], units)
            if end in ("joined in progress", "unnamed unit type"):
                rejections[end] += 1
                continue

            scored_players += 1
            scored_samples += explained
            if end is not None and failure is None:
                rejections[end] += 1
            if failure is not None:
                failures.append((demo, block, end, failure))

            print(f"{demo} block {block}: {explained} of {len(samples)} samples explained"
                  + (f", then {end}" if end else ""))
            if args.list:
                for tick, metal, energy in changes:
                    print(f"    tick {tick}: metal {metal:g}, energy {energy:g}")

    print()
    print(f"scored {scored_samples} samples from {scored_players} players")
    if rejections:
        print("rejections: " + ", ".join(f"{k} x{v}" for k, v in sorted(rejections.items())))

    if failures:
        print()
        print(f"{len(failures)} player(s) with a sample the model cannot explain:")
        for demo, block, kind, (tick, predicted, observed) in failures:
            print(f"  {demo} block {block} tick {tick}: predicted metal {predicted[0]:g} energy {predicted[1]:g}, "
                  f"reported metal {observed[0]:g} energy {observed[1]:g} ({kind})")
        sys.exit(1)

    if scored_samples == 0:
        print("nothing to score")
        sys.exit(1)


if __name__ == "__main__":
    main()
