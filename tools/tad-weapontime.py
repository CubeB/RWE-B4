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
This script is where they live, and it is the reference the C++ miner has to be
checked against, exactly as tools/tad-buildtime.py is for the build-timing
cells.

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

TWO MODELS, ONE PER SCORED CLASS.

For a projectile that flies at a constant speed:

    flight = ceil(distance / (weaponvelocity / 30)) - 1

The -1 is not a fudge. A projectile takes its first step on the tick it is
fired, so it has covered the distance after ceil(d/v) steps and the gap between
the firing tick and the arrival tick is one less -- the same off-by-one as the
build accumulator's first increment landing on the 0x09's own tick. The spread
either side of the mode is quantisation: the overshoot past the aim point when
the damage lands is always less than one step.

For a self-propelled one -- a missile -- the speed is not a constant and the
flight has to be replayed a tick at a time: leave the barrel at `startvelocity`,
gain `weaponacceleration` up to `weaponvelocity` while the motor runs, coast
after it stops, and take the first step on the firing tick as before. That
replay is a PORT OF RWE'S OWN createProjectileFromWeapon and
updateSelfPropelledProjectile, which are themselves a decoded reading of
0x49C980 and 0x49B9AE, rather than a guess from the TDF field names.

The missile class also needs a filter the constant-speed one does not, and the
reason is the same thing that makes the pairing work at all: a 0x0d records
WHERE THE SHOT WAS AIMED. A victim that moves while the round is in the air is
not where the distance says it is. Bucketing every pairing by how far the victim
could have gone -- its FBI `maxvelocity` times the observed flight -- puts both
classes on one monotone curve (--drift prints it), and the classes differ only
in where their mass sits on it: a laser crossing 200 units in six ticks barely
notices, a missile spending thirty ticks getting there does. So a missile cell
is scored over the pairings whose victim could not have outrun ONE STEP of the
projectile, and the constant-speed cells, whose pairings are nearly all at the
still end of that curve already, keep every victim. The bound is the
projectile's own step because the quantity is quantised in steps; it is not
tuned, and the two cells it leaves off the model are named below rather than
filtered away by tightening it.

WHICH CELLS ARE SCORED. The two classes above, and not:

  * **ballistic** ones, which travel an arc longer than the straight line this
    measures (modal +0 but only 11% of 4,946).
  * **vlaunch** ones, which go up before they go anywhere (ARMMERL reads +138).
  * **cruise** ones, which climb to a fixed altitude, cross the aim point and
    come down on it (0x49B455). ROCKET_HRK is the only one over this corpus and
    reading the velocities alone had it in the constant-speed table, at the
    worst share in it.
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

FOUR SCORED CELLS DISAGREE AND ARE NOT EXPLAINED. They are named in
KNOWN_EXCEPTIONS below with what they read, they are printed on every run, and
the exit code covers them the way tools/tad-buildtime.py covers its airborne
pool: the run fails if a NEW cell disagrees or if one of those four stops
reading what it reads today. All four sit one tick low against a model the other
33 cells hit and all four are near-ties with the +0 bucket, and two of them are
firing a weapon that another cell fires and lands on the model with -- ARMAMPH
against ARMMAV, and ARMFIG against CORVENG, which is the same airframe at the
same speed. Nothing is laundered by this; what is unexplained says so.

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
    ("ARMFIG", 0): (-1, "a fighter firing MISSILE_VTOL; CORVENG is the same airframe at the"
                        " same speed with the same weapon and lands on the model"),
    ("CORVAMP", 0): (-1, "a fighter firing MISSILE_VTOL_GF, the thinnest scored cell at 44"
                         " pairings and the only one under 150"),
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


def flag(block, key):
    """A TDF boolean, which is absent or `0` for false and anything else true."""
    return bool(block) and (block.get(key) or "0").strip() not in ("0", "")


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
    # `cruise` and `twophase` are read only on the self-propelled path, and
    # each replaces the flight with a different shape: a cruise missile ignores
    # what it was fired at, climbs to a fixed altitude and flies over the aim
    # point before coming down (0x49B455), and a two-phase one turns over and
    # restarts its motor. Neither is the straight line every model here
    # measures, so they are classes of their own rather than cells that happen
    # to read badly. ROCKET_HRK is the one that matters over this corpus: it is
    # `selfprop` with `cruise`, its start speed equals its cap, and reading the
    # velocities alone put it in the constant-speed table at the worst share in
    # it.
    if flag(block, "selfprop") and flag(block, "cruise"):
        return "cruise"
    if flag(block, "selfprop") and flag(block, "twophase"):
        return "two phase"
    # Whether the round leaves the barrel at the speed it will keep, asked the
    # way createProjectileFromWeapon asks it: a `startvelocity` of zero is full
    # speed for a weapon with no motor and a standstill for one with a motor,
    # so the field alone does not answer it.
    if abs(launch_speed(block) - velocity / 30.0) > 1e-6:
        return "accelerating"
    return "constant speed"


# --- the two models ---------------------------------------------------------
#
# A constant-speed round is ceil(d / v) - 1 and there is nothing else to it. A
# self-propelled one needs a replay, and the replay here is a PORT OF RWE'S OWN
# createProjectileFromWeapon and updateSelfPropelledProjectile, which are in
# turn a decoded reading of 0x49C980 and 0x49B9AE. It is not a guess at the
# shape from the TDF field names, which is what it was when the class was only
# being scouted: where the two differed the engine's reading won, exactly as
# tools/tad-buildtime.py takes its completion arithmetic from the binary rather
# than from what the field is called.


def launch_speed(block):
    """What the projectile leaves the barrel at, in world units a tick.

    `startvelocity = 0` means two different things and the original
    distinguishes them in this order: with no motor it means "off the rail at
    full speed", and with one it means "from a standstill".
    """
    velocity = num(block, "weaponvelocity", 0.0) or 0.0
    start = num(block, "startvelocity", 0.0) or 0.0
    accel = num(block, "weaponacceleration", 0.0) or 0.0
    if start != 0.0:
        return start / 30.0
    return velocity / 30.0 if accel == 0.0 else 0.0


def burn_ticks(block):
    """How long the motor runs before the missile coasts.

    `range / weaponvelocity` -- the ticks it would need to fly its whole range
    at the cap -- unless the weapon says `noautorange`, when it is
    `weapontimer`. Running out is not death: without `burnblow` the missile
    carries on at whatever speed it had reached, which is why one fired at the
    far edge of its range arrives slower than one fired up close.

    THE BURN CHANGES PAIRINGS BUT NO CELL'S MODE. A missile fired far enough
    does run its motor out and coast -- a MISSILE_GF_HEAVY's stops at tick 15,
    and one of the checked-in episodes arrives on step 17 -- but not often
    enough to move a mode: adding this term left every cell's mode where it was
    and moved no share by more than three points. So it is in the model on the
    strength of being the engine's arithmetic rather than on the strength of
    what it does to this corpus, which is the right way round.
    """
    velocity = num(block, "weaponvelocity", 0.0) or 0.0
    no_auto_range = (block.get("noautorange") or "0").strip() not in ("0", "")
    if velocity != 0.0 and not no_auto_range:
        return int((num(block, "range", 0.0) or 0.0) / (velocity / 30.0))
    return int((num(block, "weapontimer", 0.0) or 0.0) * 30.0)


def motor_flight(distance, block, limit=4000):
    """Flight ticks for a self-propelled projectile, flown as RWE flies one.

    One tick is: gain `weaponacceleration` up to the cap if the motor is still
    running, then move. The projectile takes its first step on the tick it is
    fired -- the same off-by-one the constant-speed model's -1 carries -- so the
    answer is one less than the number of steps it takes to cover the distance.
    """
    speed = launch_speed(block)
    cap = (num(block, "weaponvelocity", 0.0) or 0.0) / 30.0
    accel = (num(block, "weaponacceleration", 0.0) or 0.0) / 900.0
    burn = burn_ticks(block)
    travelled = 0.0
    ticks = 0
    while travelled < distance and ticks < limit:
        ticks += 1
        if ticks <= burn:
            speed = min(cap, speed + accel)
        travelled += speed
    return ticks - 1


def flight_model(kind, distance, block):
    """What the model for this class predicts, in ticks."""
    if kind == "accelerating":
        return motor_flight(distance, block)
    return math.ceil(distance / (num(block, "weaponvelocity") / 30.0)) - 1


def travelled_by(kind, flight, block):
    """How far the model says the projectile had flown when the damage landed.

    The arrival tick is the flight time plus the step taken on the firing tick.
    """
    if kind != "accelerating":
        return (flight + 1) * (num(block, "weaponvelocity") / 30.0)
    speed = launch_speed(block)
    cap = (num(block, "weaponvelocity", 0.0) or 0.0) / 30.0
    accel = (num(block, "weaponacceleration", 0.0) or 0.0) / 900.0
    burn = burn_ticks(block)
    travelled = 0.0
    for tick in range(1, flight + 2):
        if tick <= burn:
            speed = min(cap, speed + accel)
        travelled += speed
    return travelled


# --- the drift bound, which is what makes the second class scoreable ---------
#
# A 0x0d records WHERE THE SHOT WAS AIMED, and the distance every model here
# measures is the distance to that point. A victim that moves while the round is
# in the air is somewhere else when it arrives, so the measurement is wrong by
# however far it went -- and that error is not a property of the weapon but of
# the pair. Bucketing every pairing in the corpus by how far the victim COULD
# have gone (its own FBI `maxvelocity` times the observed flight) sorts both
# scored classes onto one monotone curve:
#
#     drift (world units)   constant speed      accelerating
#     0 (immobile victim)    65% (n=848)         54% (n=567)
#     0-8                    69% (n=2129)        58% (n=173)
#     8-16                   62% (n=2601)        51% (n=836)
#     16-32                  60% (n=3709)        42% (n=4064)
#     32-64                  43% (n=1666)        27% (n=4121)
#     64-128                 33% (n=203)         17% (n=667)
#     128+                   22% (n=203)          9% (n=5398)
#
# (share of pairings landing on the model's tick; --drift reprints it.)
#
# The two classes sit on the SAME curve and differ only in where their mass
# lies: two thirds of the constant-speed pairings drift less than 32 units,
# and two thirds of the accelerating ones drift more than 32. That is the whole
# of the difference between a class that can be scored over every victim and one
# that cannot -- a laser crossing 200 units in six ticks barely notices that its
# target moved, and a missile spending thirty ticks getting there does.
#
# So the accelerating class is scored over the pairings where the victim could
# not have outrun ONE STEP of the projectile. The bound is the projectile's own
# step rather than a constant because the thing being measured is quantised in
# steps: a drift of less than a step cannot move the arrival tick by more than
# one, and a drift of several can move it by several.
#
# THE BOUND IS NOT TUNED. Half a step would put 10 of 10 accelerating cells on
# the model and make both exceptions below disappear, which is exactly why it is
# not the bound: a filter chosen for the disagreements it removes is not
# evidence. One step is the statement the mechanism makes, and the two cells it
# leaves off the model are named rather than filtered away.


def drift_of(units, victim, flight):
    """How far the victim could have travelled while the shot was in the air.

    None where the victim could not be named -- 160 pairings over the corpus,
    where the id's build was not seen. That is not the same as a drift of zero
    and must not be scored as though it were, so the bound rejects it.
    """
    unit = units.get(victim)
    if not unit:
        return None
    try:
        return float(unit.get("maxvelocity", 0) or 0) * flight
    except ValueError:
        return None


def within_drift_bound(units, block, observation):
    flight, _distance, _damage, _demo, _tick, victim = observation
    drift = drift_of(units, victim, flight)
    return drift is not None and drift < num(block, "weaponvelocity") / 30.0


def scoreable(kind, units, block, observations):
    """The pairings a class may be scored over: all of them, or the still ones."""
    if kind != "accelerating":
        return observations
    return [o for o in observations if within_drift_bound(units, block, o)]

# --- the pairing ------------------------------------------------------------


def pair(path, window, still_only, units):
    """(shooter type, slot) -> [(flight, distance, damage, demo, tick, victim type)].

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
                    (inside[0][0] - tick, distance, inside[0][1], demo, tick, target))
    return cells, rejections


# --- --drift: the evidence behind the bound ---------------------------------


def report_drift(cells, units, weapons, min_n):
    """Both scored classes bucketed by how far the victim could have moved.

    This is the measurement the accelerating class's victim filter rests on, and
    it is printed rather than described because a number in a comment goes stale
    and a number a flag reprints does not. What to look at: the two columns fall
    together down the same axis, and the accelerating column starts lower only
    because a missile's flight is long enough for the drift to matter at every
    distance.
    """
    print("\n--drift: the share of pairings landing on the model's tick, by how far")
    print("the victim could have travelled while the shot was in the air.\n")
    print(f"  {'drift (world units)':<22}{'constant speed':>18}{'accelerating':>18}")

    edges = [(0.0, 1e-9), (1e-9, 8.0), (8.0, 16.0), (16.0, 32.0),
             (32.0, 64.0), (64.0, 128.0), (128.0, float("inf"))]
    buckets = {kind: [collections.Counter() for _ in edges]
               for kind in ("constant speed", "accelerating")}
    for (shooter, slot), observations in cells.items():
        if len(observations) < min_n:
            continue
        _name, block = weapon_of(units, weapons, shooter, slot)
        if not block:
            continue
        kind = weapon_class(block)
        if kind not in buckets:
            continue
        for observation in observations:
            flight, distance, _damage, _demo, _tick, victim = observation
            drift = drift_of(units, victim, flight)
            if drift is None:
                continue
            for index, (low, high) in enumerate(edges):
                if low <= drift < high:
                    buckets[kind][index][flight - flight_model(kind, distance, block)] += 1
                    break

    for index, (low, high) in enumerate(edges):
        label = ("0 (immobile victim)" if high <= 1e-9
                 else f"{low if low > 1 else 0:g}-{high:g}" if high != float("inf")
                 else f"{low:g}+")
        columns = []
        for kind in ("constant speed", "accelerating"):
            counts = buckets[kind][index]
            total = sum(counts.values())
            columns.append(f"{100 * counts[0] / total:3.0f}% (n={total})" if total else "-")
        print(f"  {label:<22}{columns[0]:>18}{columns[1]:>18}")

    print("\n  the bound the accelerating class is scored under is one step of the")
    print("  projectile: a victim that could have outrun it is not measuring a flight.")


def score(cells, units, weapons, min_n):
    """Every cell with enough pairings, scored by whichever model its class has.

    A cell is scored over the pairings its class allows -- all of them for a
    constant-speed weapon, the ones inside the drift bound for a self-propelled
    one -- and `min_n` applies to what survives that, so a cell whose pairings
    were nearly all against aircraft drops out rather than being scored thin.
    """
    rows = []
    for (shooter, slot), observations in cells.items():
        name, block = weapon_of(units, weapons, shooter, slot)
        velocity = num(block, "weaponvelocity")
        if not velocity:
            continue
        kind = weapon_class(block)
        subset = scoreable(kind, units, block, observations)
        if len(subset) < min_n:
            continue
        per_tick = velocity / 30.0
        errors = collections.Counter(
            flight - flight_model(kind, distance, block)
            for flight, distance, _damage, _demo, _tick, _victim in subset)
        mode, at_mode = errors.most_common(1)[0]
        damage, damage_at = collections.Counter(
            d for _f, _dist, d, _demo, _t, _v in subset).most_common(1)[0]
        # The overshoot past the aim point when the damage lands, which is what
        # says the spread either side of the mode is quantisation: it is always
        # less than one step.
        overshoot = sorted(distance - travelled_by(kind, flight, block)
                           for flight, distance, _d, _demo, _t, _victim in subset)
        rows.append(dict(
            shooter=shooter, slot=slot, weapon=name or "-",
            kind=kind, velocity=velocity, per_tick=per_tick,
            n=len(subset), dropped=len(observations) - len(subset),
            mode=mode, share=at_mode / len(subset),
            damage=damage, damage_share=damage_at / len(subset),
            declared_damage=num(block["_damage"], "default") if block else None,
            overshoot=overshoot[len(overshoot) // 2]))
    return rows


SCORED_CLASSES = ("constant speed", "accelerating")


def print_table(rows):
    print(f"  {'shooter':<13} {'sl':>2} {'weapon':<22} {'v/30':>6} {'n':>5} {'delta':>6}"
          f" {'share':>6} {'over':>6} {'damage':>7} {'decl':>6}")
    # (-n, shooter, slot), which is the order the port prints in too: two cells
    # with the same count would otherwise sort by whichever dict filled first and
    # a diff against --weapon-cells would show a phantom difference.
    for r in sorted(rows, key=lambda r: (-r["n"], r["shooter"], r["slot"])):
        flag = "" if r["mode"] == 0 else "   <-- disagrees"
        declared = int(r["declared_damage"]) if r["declared_damage"] else 0
        print(f"  {r['shooter']:<13} {r['slot']:>2} {r['weapon']:<22} {r['per_tick']:>6.1f}"
              f" {r['n']:>5} {r['mode']:>+6} {100 * r['share']:>5.0f}% {r['overshoot']:>6.1f}"
              f" {r['damage']:>7} {declared:>6}{flag}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shots", required=True, help="tad_episodes --emit-shots output")
    ap.add_argument("--units", required=True, help="the data set's unit files, recursed")
    ap.add_argument("--window", type=int, default=300, help="isolation window in ticks (default 300)")
    ap.add_argument("--min-n", type=int, default=30, help="pairings a cell needs to be scored (default 30)")
    ap.add_argument("--still-victim", action="store_true",
                    help="keep only victims that cannot move: the drift bound at its limit,"
                         " applied to every class at once")
    ap.add_argument("--drift", action="store_true",
                    help="print the evidence behind the accelerating class's victim bound")
    ap.add_argument("--classes", action="store_true",
                    help="also list the classes neither model describes")
    args = ap.parse_args()

    units = load_units(args.units)
    if not units:
        sys.exit(f"no *.FBI files under {args.units}")
    weapons = load_weapons(args.units)
    if not weapons:
        sys.exit(f"no weapon *.tdf files under {args.units}")

    cells, rejections = pair(args.shots, args.window, args.still_victim, units)
    rows = score(cells, units, weapons, args.min_n)
    scored = [r for r in rows if r["kind"] in SCORED_CLASSES]
    if not scored:
        sys.exit(f"no scoreable cells met --min-n {args.min_n}; nothing to score")

    paired = sum(len(v) for v in cells.values())
    print(f"{paired} shots paired, {len(rows)} cells with {args.min_n}+ scoreable pairings")

    for kind in SCORED_CLASSES:
        group = [r for r in scored if r["kind"] == kind]
        if not group:
            continue
        print()
        if kind == "constant speed":
            print(f"the {len(group)} cells whose weapon flies at a constant speed,"
                  f" ceil(d / v) - 1, over every victim"
                  + (", victims that cannot move only" if args.still_victim else "") + "\n")
        else:
            dropped = sum(r["dropped"] for r in group)
            print(f"the {len(group)} cells whose weapon has a motor, flown as RWE flies"
                  f" one, over the victims\nthat could not outrun a step of it"
                  f" ({dropped} pairings dropped by that bound)\n")
        print_table(group)

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

    if args.drift:
        report_drift(cells, units, weapons, args.min_n)

    if args.classes:
        print("\nthe classes neither model describes, listed and never scored:")
        by_kind = collections.defaultdict(list)
        for r in rows:
            if r["kind"] not in SCORED_CLASSES:
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
            model = (f"a motor replay from {launch_speed(weapons.get(r['weapon'], {})) :.1f}"
                     f" up to {r['per_tick']:.1f} a tick" if r["kind"] == "accelerating"
                     else f"ceil(d / {r['per_tick']:.1f}) - 1")
            print(f"MISS {r['shooter']} slot {r['slot']} ({r['weapon']}): model says"
                  f" {model}, corpus is {r['mode']:+} off it"
                  f" over {r['n']} pairings ({100 * r['share']:.0f}% at the mode)")
        else:
            print(f"MOVED {r['shooter']} slot {r['slot']} ({r['weapon']}): was {known[0]:+},"
                  f" now {r['mode']:+} over {r['n']} pairings")

    if failures:
        print(f"\n{failures} disagreement(s) with the model")
        return 1

    known_here = sum(1 for r in scored if (r["shooter"], r["slot"]) in KNOWN_EXCEPTIONS)
    print(f"all {len(scored) - known_here} of {len(scored)} scored cells agree with their model"
          + (f", and the {known_here} known exception(s) still read what they read" if known_here else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
