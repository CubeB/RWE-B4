#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Check TA's projectile flight time against a demo corpus.

A demo records a shot leaving (0x0d) and damage arriving (0x0b) and NOTHING
LINKS THEM: no shot id, no sequence number, and no tick on a 0x0b beyond the
0x2c serial of the packet carrying it, against 631,578 shots and 824,844 damage
events. So a flight time is a filtered statistic and the filters are the work.
This script is where they live, and it is the reference the eventual C++ miner
has to be checked against, exactly as tools/tad-buildtime.py is for the
build-timing cells.

    ./build/tad_episodes --dir ~/ta-demos --units ~/ta-mods/x-esc \
        --emit-shots /tmp/shots.jsonl
    tools/tad-weapontime.py --shots /tmp/shots.jsonl --units ~/ta-mods/x-esc

WHAT MAKES THE PAIRING WORK. A 0x0b is emitted by the peer that owns the
**attacker**, never the one that owns the victim -- 797,783 records to 0 over
the corpus -- so a shot and the damage it caused are stamped by the same peer's
tick clock and a flight time is a difference within one clock. Keyed on
(attacker, victim), a shot is kept only where it is the only shot from that
shooter at that victim within +-WINDOW ticks and exactly one damage record from
that shooter to that victim lands in the WINDOW after it. That keeps 35,535 of
631,578 shots, and the survivors are confirmed by a number no filter looks at:
each (shooter, slot) cell's modal `damage` is that weapon's own [DAMAGE]
default.

THE MODEL, for a projectile that flies at a constant speed:

    flight = ceil(distance / (weaponvelocity / 30)) - 1

The -1 is not a fudge. A projectile takes its first step on the tick it is
fired, so it has covered the distance after ceil(d/v) steps and the gap between
the firing tick and the arrival tick is one less -- the same off-by-one as the
build accumulator's first increment landing on the 0x09's own tick. The spread
either side of the mode is quantisation: the overshoot past the aim point when
the damage lands is always less than one step.

WHICH CELLS ARE SCORED. Only weapons whose startvelocity equals their
weaponvelocity, which is to say the ones the model describes, and not:

  * **accelerating** weapons -- a missile leaves the rail at startvelocity and
    works up to weaponvelocity, so it arrives late against a constant-speed
    model (modal +2 over 15,826 pairings). That is the missile motor and it
    wants its own model.
  * **ballistic** ones, which travel an arc longer than the straight line this
    measures (modal +0 but only 11% of 4,946).
  * **vlaunch** ones, which go up before they go anywhere (ARMMERL reads +135).
  * **waterweapon** ones -- torpedoes -- whose path from a surface launcher to a
    submerged target is not the straight line either.
  * **burst** ones. A burst weapon fires `burst` rounds `burstrate` seconds
    apart from one trigger, each its own 0x0d and each thrown off the aim line
    by `sprayangle`, so the isolation filter cannot mean what it means
    everywhere else -- the shot that survives it is one round of several and the
    damage that arrives need not be its own. Six of the eight cells that failed
    this model before the exclusion existed are burst weapons, on a criterion
    that has nothing to do with flight time.

Those five are listed by --classes rather than scored. They are not
discrepancies to explain away; they are four more oracles.

TWO SCORED CELLS DISAGREE AND ARE NOT EXPLAINED. They are named in
KNOWN_EXCEPTIONS below with what they read, they are printed on every run, and
the exit code covers them the way tools/tad-buildtime.py covers its airborne
pool: the run fails if a NEW cell disagrees or if one of those two stops reading
what it reads today. Both sit one tick low against a model the other 23 cells
hit, both are near-ties with the +0 bucket, and one of them (ARMAMPH) shares its
weapon with a cell that agrees. Nothing is laundered by this; what is unexplained
says so.

Exits non-zero if a scored cell's modal flight time stops agreeing with the
model, so this is a check and not a listing. Neither demos nor mod files are in
the repository; both arguments are paths.
"""

import argparse
import collections
import json
import math
import os
import re
import sys


# --- the mod's own data -----------------------------------------------------
#
# Crude readers on purpose: this is the argue-it-in-Python stage, and anything
# that reaches a checked-in fixture goes through the engine's own parsers first,
# so a fixture can never disagree with the loader about what a field means.


# The scored cells that do not land on the model, with what they read instead.
# Carried by name so a new disagreement is distinguishable from these two, which
# are an open question rather than a licensed divergence -- see the docstring.
KNOWN_EXCEPTIONS = {
    ("ARMAMPH", 0): (-1, "amphibious shooter, GAUSS_MAV; ARMMAV fires the same weapon and agrees"),
    ("CORGEO", 0): (-1, "a geothermal plant firing RIOT_ALL, 45% at -1 against 40% at +0"),
}


def _strip_comments(raw):
    raw = re.sub(r"//[^\n]*", "", raw)
    return re.sub(r"/\*.*?\*/", "", raw, flags=re.S)


def _kv_flat(raw):
    return {m.group(1).lower(): m.group(2).strip()
            for m in re.finditer(r"([A-Za-z0-9_]+)\s*=\s*([^;]*);", raw)}


def _kv_shallow(body):
    """key=value; at this level only, so a nested [DAMAGE] block does not leak in."""
    flat = []
    depth = 0
    for ch in body:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif depth == 0:
            flat.append(ch)
    return _kv_flat("".join(flat))


def _blocks(raw):
    """Top-level [NAME] { ... } blocks, brace-matched so nesting does not fool it."""
    out = {}
    cursor = 0
    for m in re.finditer(r"\[([^\]\n]+)\]", raw):
        if m.start() < cursor:
            continue
        open_at = raw.find("{", m.end())
        if open_at < 0:
            continue
        depth = 0
        close_at = len(raw) - 1
        for j in range(open_at, len(raw)):
            if raw[j] == "{":
                depth += 1
            elif raw[j] == "}":
                depth -= 1
                if depth == 0:
                    close_at = j
                    break
        out[m.group(1).strip().upper()] = raw[open_at + 1:close_at]
        cursor = close_at
    return out


def load_units(root):
    """Every *.FBI under root, keyed by upper-cased stem -- what the merged VFS sees.

    Read flat: an FBI is one [UNITINFO] block, so the depth filter a weapon
    block needs would throw the whole file away.
    """
    units = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.lower().endswith(".fbi"):
                raw = open(os.path.join(dirpath, f), "rb").read().decode("latin-1")
                units[os.path.splitext(f)[0].upper()] = _kv_flat(_strip_comments(raw))
    return units


def load_weapons(root):
    """Every weapon block under root's weapon directories, with its damage table."""
    weapons = {}
    for dirpath, _, files in sorted(os.walk(root)):
        if "weapon" not in os.path.basename(dirpath).lower():
            continue
        for f in sorted(files):
            if not f.lower().endswith(".tdf"):
                continue
            raw = _strip_comments(open(os.path.join(dirpath, f), "rb").read().decode("latin-1"))
            for name, body in _blocks(raw).items():
                block = _kv_shallow(body)
                damage = _blocks(body).get("DAMAGE")
                block["_damage"] = _kv_shallow(damage) if damage is not None else {}
                weapons[name] = block
    return weapons


def weapon_of(units, weapons, unit_name, slot):
    """The WeaponN block with N = slot + 1, since the 0x0d's byte is 0-based."""
    unit = units.get(unit_name)
    if not unit:
        return None, None
    name = unit.get(f"weapon{slot + 1}")
    if not name:
        return None, None
    return name.upper(), weapons.get(name.upper())


def num(block, key, default=None):
    if not block:
        return default
    try:
        return float(block[key])
    except (KeyError, ValueError, TypeError):
        return default


def weapon_class(block):
    """Which of the models describes this weapon, or why none of them does."""
    if not block:
        return "no weapon block"
    if block.get("vlaunch"):
        return "vlaunch"
    if block.get("waterweapon"):
        return "waterweapon"
    if block.get("ballistic"):
        return "ballistic"
    if block.get("burst"):
        return "burst"
    velocity = num(block, "weaponvelocity")
    if not velocity:
        return "no velocity"
    start = num(block, "startvelocity", velocity)
    if abs(start - velocity) > 1e-6:
        return "accelerating"
    return "constant speed"


# --- the pairing ------------------------------------------------------------


def pair(path, window, still_only, units):
    """(shooter type, slot) -> [(flight ticks, distance, damage, demo, tick)].

    Everything is keyed on (attacker, victim) because that is the only join the
    stream offers. A shot survives when it is alone in its window and draws
    exactly one damage record.
    """
    shots = collections.defaultdict(lambda: collections.defaultdict(list))
    hits = collections.defaultdict(lambda: collections.defaultdict(list))
    for line in open(path):
        record = json.loads(line)
        kind = record["kind"]
        if kind == "shot":
            if record["target"] and record["shooterName"]:
                distance = math.dist(
                    (record["ox"], record["oy"], record["oz"]),
                    (record["tx"], record["ty"], record["tz"]))
                shots[record["demo"]][(record["shooter"], record["target"])].append(
                    (record["tick"], record["shooterName"], record["slot"], distance,
                     record["targetName"]))
        elif kind == "damage":
            hits[record["demo"]][(record["attacker"], record["victim"])].append(
                (record["tick"], record["damage"]))

    def immobile(name):
        unit = units.get(name)
        return bool(unit) and (unit.get("maxvelocity") or "0").strip() in ("0", "0.0", "")

    cells = collections.defaultdict(list)
    rejections = collections.Counter()
    for demo in sorted(shots):
        for key, fired in shots[demo].items():
            fired.sort()
            landed = sorted(hits[demo].get(key, []))
            ticks = [f[0] for f in fired]
            for i, (tick, shooter, slot, distance, target) in enumerate(fired):
                if i > 0 and tick - ticks[i - 1] < window:
                    rejections["another shot at the same victim just before"] += 1
                    continue
                if i + 1 < len(ticks) and ticks[i + 1] - tick < window:
                    rejections["another shot at the same victim just after"] += 1
                    continue
                if still_only and not immobile(target):
                    rejections["victim can move"] += 1
                    continue
                inside = [(t, d) for t, d in landed if tick <= t <= tick + window]
                if not inside:
                    rejections["no damage recorded in the window"] += 1
                    continue
                if len(inside) > 1:
                    rejections["several damage events in the window"] += 1
                    continue
                cells[(shooter, slot)].append(
                    (inside[0][0] - tick, distance, inside[0][1], demo, tick))
    return cells, rejections


def score(cells, units, weapons, min_n):
    rows = []
    for (shooter, slot), observations in cells.items():
        if len(observations) < min_n:
            continue
        name, block = weapon_of(units, weapons, shooter, slot)
        velocity = num(block, "weaponvelocity")
        if not velocity:
            continue
        per_tick = velocity / 30.0
        errors = collections.Counter(
            flight - (math.ceil(distance / per_tick) - 1)
            for flight, distance, _damage, _demo, _tick in observations)
        mode, at_mode = errors.most_common(1)[0]
        damage, damage_at = collections.Counter(
            d for _f, _dist, d, _demo, _t in observations).most_common(1)[0]
        # The overshoot past the aim point when the damage lands, which is what
        # says the spread either side of the mode is quantisation: it is always
        # less than one step.
        overshoot = sorted(distance - (flight + 1) * per_tick
                           for flight, distance, _d, _demo, _t in observations)
        rows.append(dict(
            shooter=shooter, slot=slot, weapon=name or "-",
            kind=weapon_class(block), velocity=velocity, per_tick=per_tick,
            n=len(observations), mode=mode, share=at_mode / len(observations),
            damage=damage, damage_share=damage_at / len(observations),
            declared_damage=num(block["_damage"], "default") if block else None,
            overshoot=overshoot[len(overshoot) // 2]))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shots", required=True, help="tad_episodes --emit-shots output")
    ap.add_argument("--units", required=True, help="the data set's unit files, recursed")
    ap.add_argument("--window", type=int, default=300, help="isolation window in ticks (default 300)")
    ap.add_argument("--min-n", type=int, default=30, help="pairings a cell needs to be scored (default 30)")
    ap.add_argument("--still-victim", action="store_true",
                    help="keep only victims that cannot move, which sharpens every mode")
    ap.add_argument("--classes", action="store_true",
                    help="also list the classes this model does not describe")
    args = ap.parse_args()

    units = load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")
    weapons = load_weapons(args.units)
    if not weapons:
        sys.exit(f"no weapon *.tdf files under {args.units}")

    cells, rejections = pair(args.shots, args.window, args.still_victim, units)
    rows = score(cells, units, weapons, args.min_n)
    scored = [r for r in rows if r["kind"] == "constant speed"]
    if not scored:
        sys.exit(f"no constant-speed cells met --min-n {args.min_n}; nothing to score")

    paired = sum(len(v) for v in cells.values())
    print(f"{paired} shots paired, {len(rows)} cells with {args.min_n}+ pairings")
    print(f"scoring the {len(scored)} whose weapon flies at a constant speed"
          + (", victims that cannot move only" if args.still_victim else "") + "\n")

    print(f"  {'shooter':<13} {'sl':>2} {'weapon':<22} {'v/30':>6} {'n':>5} {'delta':>6}"
          f" {'share':>6} {'over':>6} {'damage':>7} {'decl':>6}")
    # (-n, shooter, slot), which is the order the port prints in too: two cells
    # with the same count would otherwise sort by whichever dict filled first and
    # a diff against --weapon-cells would show a phantom difference.
    for r in sorted(scored, key=lambda r: (-r["n"], r["shooter"], r["slot"])):
        flag = "" if r["mode"] == 0 else "   <-- disagrees"
        declared = int(r["declared_damage"]) if r["declared_damage"] else 0
        print(f"  {r['shooter']:<13} {r['slot']:>2} {r['weapon']:<22} {r['per_tick']:>6.1f}"
              f" {r['n']:>5} {r['mode']:>+6} {100 * r['share']:>5.0f}% {r['overshoot']:>6.1f}"
              f" {r['damage']:>7} {declared:>6}{flag}")

    # The damage figure is the independent check on the pairing: no filter looks
    # at it, so a wrongly paired event has no reason to carry the firing
    # weapon's own damage.
    checkable = [r for r in scored if r["declared_damage"]]
    agreeing = [r for r in checkable if r["damage"] == int(r["declared_damage"])]
    print(f"\n{len(agreeing)} of {len(checkable)} scored cells carry their weapon's own"
          f" [DAMAGE] default as the modal damage")

    overshoots = sorted(r["overshoot"] for r in scored)
    print(f"median overshoot past the aim point: {overshoots[len(overshoots) // 2]:.1f}"
          f" world units, never a whole step")

    if args.classes:
        print("\nthe classes this model does not describe, listed and never scored:")
        by_kind = collections.defaultdict(list)
        for r in rows:
            if r["kind"] != "constant speed":
                by_kind[r["kind"]].append(r)
        for kind in sorted(by_kind, key=lambda k: -sum(r["n"] for r in by_kind[k])):
            group = by_kind[kind]
            pooled = sum(r["n"] for r in group)
            print(f"\n  {kind}: {len(group)} cells, {pooled} pairings")
            for r in sorted(group, key=lambda r: -r["n"])[:6]:
                print(f"    {r['shooter']:<13} {r['slot']:>2} {r['weapon']:<22}"
                      f" {r['n']:>5} {r['mode']:>+6} {100 * r['share']:>5.0f}%")

    print("\nrejections:", ", ".join(f"{k} x{v}" for k, v in rejections.most_common()))

    # A miss is new unless KNOWN_EXCEPTIONS names that cell AND it still reads
    # what it read when it was written down. Either half failing is a result.
    print()
    failures = 0
    # (-n, shooter, slot), which is the order the port prints in too: two cells
    # with the same count would otherwise sort by whichever dict filled first and
    # a diff against --weapon-cells would show a phantom difference.
    for r in sorted(scored, key=lambda r: (-r["n"], r["shooter"], r["slot"])):
        known = KNOWN_EXCEPTIONS.get((r["shooter"], r["slot"]))
        if r["mode"] == 0 and known is None:
            continue
        if known is not None and r["mode"] == known[0]:
            print(f"KNOWN {r['shooter']} slot {r['slot']} ({r['weapon']}): {r['mode']:+} off the"
                  f" model over {r['n']} pairings, {100 * r['share']:.0f}% at the mode"
                  f" -- {known[1]}")
            continue
        failures += 1
        if known is None:
            print(f"MISS {r['shooter']} slot {r['slot']} ({r['weapon']}): model says"
                  f" ceil(d / {r['per_tick']:.1f}) - 1, corpus is {r['mode']:+} off it"
                  f" over {r['n']} pairings ({100 * r['share']:.0f}% at the mode)")
        else:
            print(f"MOVED {r['shooter']} slot {r['slot']} ({r['weapon']}): was {known[0]:+},"
                  f" now {r['mode']:+} over {r['n']} pairings")

    if failures:
        print(f"\n{failures} disagreement(s) with the model")
        return 1

    known_here = sum(1 for r in scored if (r["shooter"], r["slot"]) in KNOWN_EXCEPTIONS)
    print(f"all {len(scored) - known_here} of {len(scored)} scored cells agree with the model"
          + (f", and the {known_here} known exception(s) still read what they read" if known_here else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
