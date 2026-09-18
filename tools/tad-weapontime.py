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

WHERE THE ROUND STOPS: ON THE VICTIM'S FOOTPRINT, NOT AT THE AIM POINT.

A projectile does not detonate on reaching the point it was aimed at. It
detonates on the first tick its move puts it in a map square the victim
occupies (0x49B090, called straight after the move in 0x49B720), and a unit
occupies its footprint -- FootprintX by FootprintZ squares of sixteen world
units, stamped at the unit's position with the left edge rounded to the nearest
square, which is RWE's computeFootprintRegion. So:

    flight = the first step k at which the round, flown along the line from
             where it was fired to where it was aimed, is inside the victim's
             footprint squares

and the shot-to-damage interval is k itself. At drift zero that takes 792 of
810 constant-speed pairings (98%), at every footprint from 2 to 8, where the
aim-point model it replaced took 65% and fell to nothing on large buildings.

THE RETIRED -1. The model this replaced was `ceil(d / v) - 1`, and the -1 was
explained as a projectile taking its first step on the firing tick. It was the
footprint: a round stops about a footprint's half-width short of the aim point,
which for the common cases is about one step, so the aim-point model sat a tick
high on a small target and more on a big one -- which is exactly the residual
that model carried, a second bucket at -1 growing with the victim's footprint.

WHICH TICK THE FIRST STEP LANDS ON: THE FIRING TICK, and the interval read here
is a tick wider than the flight for a reason that belongs to the demo rather
than to the engine. TotalA.exe creates the round inside the unit pass and the
projectile pass that follows in the same tick latches the array length once and
walks it (0x49D77E, 0x49B740), so the round moves immediately and its damage
lands on T + k - 1. But an event is stamped with the last 0x2c before it in its
sender's stream, and the 0x2c is queued at the end of that player's unit
sub-pass (0x48B003): a 0x0d goes in before it and a 0x0b after it, so the shot
reads a tick early and the difference of the two is k. Over the corpus, 121,624
of the 122,637 runs between consecutive 0x2c records that carry both codes put
every 0x0b before every 0x0d, which is only possible if the damage belongs to
the previous tick. NOTHING HERE CHANGES: `flight` stays k and the scoring is
untouched. What it settles is what a consumer may conclude -- a round fired on
tick T detonates on T + k - 1 -- and docs/TA-DEMOS.md, "Which tick a round
first moves on", has the decode, the Escalation patch check and the stream
measurement.

HOW FAST THE ROUND FLIES, ONE MODEL PER SCORED CLASS. A constant-speed round
covers weaponvelocity / 30 a tick. A self-propelled one -- a missile -- is
replayed a tick at a time: leave the barrel at `startvelocity`, gain
`weaponacceleration` up to `weaponvelocity` while the motor runs, coast after it
stops. That replay is a PORT OF RWE'S OWN createProjectileFromWeapon and
updateSelfPropelledProjectile, which are themselves a decoded reading of
0x49C980 and 0x49B9AE, rather than a guess from the TDF field names. A BALLISTIC
one -- a shell -- is launched at the flat root of the firing solution 0x49A890
solves (RWE's computeFiringAngles, `pitches->second`) and then stepped
HORIZONTALLY at weaponvelocity / 30 * cos(pitch): the ballistic branch of
updateProjectiles touches only `velocity.y`, so the round's horizontal motion is
constant for ever and the arc's fall never enters the flight time. The three
classes differ only in the distance each step covers; the stopping rule is the
same.

WHY THE BALLISTIC CLASS NEEDS A SECOND BOUND AND THE OTHERS DO NOT. The original
perturbs every turret shot before it spawns (0x49D6D7): heading and pitch each
take `rand(accuracy) - accuracy/2`, widened by however hurt the shooter is. For
a round that flies level a pitch error costs tan(pitch)*d of its speed, under
one percent. For a shell it is a RANGE error of 2*cot(2*pitch)*d, because the
range goes as sin(2*pitch) -- ten percent of the flight at
CANNON_ART_MEDIUM's `accuracy=750`, which is six or seven ticks -- and nothing
in a demo records the draw. So a ballistic pairing is scored only where
replaying the whole arc at the corners of the weapon's own cone gives the same
answer. That is the bound, it has nothing in it to tune, and --cone prints the
split it makes: 42% of the pairings it keeps land on the model against 17% of
the ones it rejects. It is a LOWER bound on the jitter, because the health term
opens the cone for a damaged shooter and hit points are not in the stream, and
that is most of what is left between the 42% here and the 98% the constant-speed
class reaches at drift zero.

WHAT THAT LEAVES. Two of the seventeen ballistic cells -- CORMORT and ARMBULL --
keep enough pairings to be scored, and both land on the model. The other fifteen
are printed with what took them rather than dropped: the artillery cells lose
nearly everything to the cone, and the short-ranged tank cannons, whose cone is
zero at full health, lose it to the drift bound instead, because a shell
spending forty ticks in the air gives even a slow victim time to leave.

The missile class also needs a filter the constant-speed one does not, and the
reason is the same thing that makes the pairing work at all: a 0x0d records
WHERE THE SHOT WAS AIMED. A victim that moves while the round is in the air is
not where the distance says it is. Bucketing every pairing by how far the victim
could have gone -- its FBI `maxvelocity` times the observed flight -- puts both
classes on one falling curve (--drift prints it), and the classes differ only
in where their mass sits on it: a laser crossing 200 units in six ticks barely
notices, a missile spending thirty ticks getting there does. So a missile cell
is scored over the pairings whose victim could not have outrun ONE STEP of the
projectile, and the constant-speed cells, whose pairings are nearly all at the
still end of that curve already, keep every victim that can be named -- a
footprint needs a name, so a victim whose build was never seen is not scored in
either class. The bound is the projectile's own step because the quantity is
quantised in steps, and it is not tuned: it was fixed before the footprint model
existed and the model was scored under it unchanged.

WHICH CELLS ARE SCORED. The three classes above, and not:

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
    that has nothing to do with flight time. `burst` is now asked BEFORE
    `ballistic`, which it has to be once the ballistic class is scored:
    CANNON_FIDO is both, and it sat in the ballistic table reading +5 while it
    was really six shells from one trigger thrown off the aim line by a 1536
    `sprayangle`.

Those four are listed by --classes rather than scored. They are not
discrepancies to explain away; they are four more oracles. (A fifth name appears
there, "ballistic selfprop": TA dispatches on `selfprop` first at 0x49B9C2, so a
weapon carrying both flags is flown by the motor off a ballistic launch angle,
which is neither model. ROCKET_HEAVY is the only one in this data set and
nothing in the corpus fires it.)

NO SCORED CELL DISAGREES. Under the aim-point model four did -- ARMAMPH and
CORGEO among the constant-speed cells, ARMFIG and CORVAMP among the missiles,
each one tick low -- and all four land on the footprint model. Two of them are
explained outright. ARMAMPH goes from 45% at -1 to 71% at +0, and it read low
where ARMMAV firing the same GAUSS_MAV did not because its victims are bigger
(a mean footprint side of 3.0 against 2.3). CORVAMP goes from 41% at -1 to 66%
at +0. The other two land as near-ties still -- CORGEO 47% against 38% at -1,
ARMFIG 41% against 35% at +1 -- and ARMFIG still sits twenty points under
CORVENG (61%), the same airframe with the same weapon. Both fighters fire from
about 130 units up, which is where the half of the collision test this model
leaves out -- the round must also be below the victim's model top -- would
bite; but CORVENG fires from the same height, so that half alone does not
separate ARMFIG from it. KNOWN_EXCEPTIONS is kept, empty, so that the next
disagreement is named rather than tolerated: the run fails if a NEW cell
disagrees or if a named one stops reading what it read.

THE DUMP MUST CARRY EXACT COORDINATES. This model floors positions onto
sixteen-unit squares, so a coordinate rounded to three decimals can land on the
wrong side of a boundary and move a whole pairing. --emit-shots writes each
16.16 coordinate in its shortest exact decimal form for that reason; against a
three-decimal dump CORVAMP read 52% here and 66% in the port.

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


# The scored cells that do not land on the model, with what they read instead:
# {(shooter, slot): (mode, why)}. Carried by name so a new disagreement is
# distinguishable from a known one, which is an open question rather than a
# licensed divergence -- see the docstring. Empty since the footprint model: the
# four the aim-point model left here (ARMAMPH, CORGEO, ARMFIG, CORVAMP, each -1)
# all land on it.
KNOWN_EXCEPTIONS = {}


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
    # `burst` is asked BEFORE `ballistic`, and it has to be. A burst weapon's
    # isolation filter cannot mean what it means elsewhere whatever shape its
    # rounds fly, so a weapon that is both has to fall on the excluded side.
    # CANNON_FIDO is the one in this data set -- `burst=6` at `burstrate=0.001`
    # with a 1536 `sprayangle`, six shells from one trigger thrown off the aim
    # line -- and it sat in the ballistic table until the ballistic class became
    # scored, where it was merely unscored for the wrong reason.
    if block.get("burst"):
        return "burst"
    if block.get("ballistic"):
        # TA dispatches a round's flight on `selfprop` first (0x49B9C2), so a
        # weapon carrying both flags is flown by the motor and not by the arc --
        # its launch angle is still the ballistic solver's, which is a fourth
        # shape and not either model. ROCKET_HEAVY is the only one here and
        # nothing in the corpus fires it, so it is named rather than modelled.
        if flag(block, "selfprop"):
            return "ballistic selfprop"
        return "ballistic"
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


# --- the model: how fast the round flies, and where it stops -----------------
#
# Two classes differ only in the speed each step covers. A constant-speed round
# covers weaponvelocity / 30 every step. A self-propelled one needs a replay, and
# the replay here is a PORT OF RWE'S OWN createProjectileFromWeapon and
# updateSelfPropelledProjectile, which are in turn a decoded reading of 0x49C980
# and 0x49B9AE. It is not a guess at the shape from the TDF field names, which is
# what it was when the class was only being scouted: where the two differed the
# engine's reading won, exactly as tools/tad-buildtime.py takes its completion
# arithmetic from the binary rather than from what the field is called.
#
# Both classes stop the same way: on the first step that puts the round in a
# map square the victim's footprint covers (0x49B090, straight after the move).


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
    does run its motor out and coast -- a MISSILE_GF_HEAVY's stops at tick 15 --
    but not often enough to move a mode: adding this term left every cell's
    mode where it was and moved no share by more than three points. So it is in
    the model on the strength of being the engine's arithmetic rather than on
    the strength of what it does to this corpus, which is the right way round.
    """
    velocity = num(block, "weaponvelocity", 0.0) or 0.0
    no_auto_range = (block.get("noautorange") or "0").strip() not in ("0", "")
    if velocity != 0.0 and not no_auto_range:
        return int((num(block, "range", 0.0) or 0.0) / (velocity / 30.0))
    return int((num(block, "weapontimer", 0.0) or 0.0) * 30.0)


# The per-tick gravity every ballistic round in the game falls under, which is
# the map's own `gravity` (112 in nearly everything shipped) over 30 squared:
# 0x49BD10 takes it off velocity.y each tick, and the ballistic branch of RWE's
# updateProjectiles is the same line. The smoke emitter reads the same word and
# lifts a puff by four times it, which is what pins the scale (TOTALA-EXE.md §7).
GRAVITY = 112.0 / (30.0 * 30.0)

# One unit of TA's 16-bit angle, in radians.
ANGLE_UNIT = 2.0 * math.pi / 65536.0


def ballistic_pitch(block, origin, target):
    """The angle a ballistic gun elevates to, or None where it cannot reach.

    THE FLAT ROOT, ALWAYS. 0x49A890 solves the standard ballistic quadratic --
    the same discriminant RWE's computeFiringAngles forms, arrived at by a
    different factoring -- and picks between the two roots at 0x49AA11 against
    `minbarrelangle` as a floor and pi/4 as a ceiling. The high root exceeds 45
    degrees for every target inside the gun's range and equals it only at the
    range itself, so the ceiling rejects it every time and the original always
    fires the flat one. That is RWE's `pitches->second`, and TOTALA-EXE.md,
    "The ballistic firing solution", is the reading.

    No solution means the weapon does not fire at all (`cmp ax,0x8000` at
    0x49D61B), so a pairing whose geometry has none is not a shot this model can
    account for.
    """
    speed = (num(block, "weaponvelocity", 0.0) or 0.0) / 30.0
    flat = math.dist((origin[0], origin[2]), (target[0], target[2]))
    if speed <= 0.0 or flat <= 0.0:
        return None
    rise = target[1] - origin[1]
    inner = (GRAVITY * flat * flat) + (2.0 * speed * speed * rise)
    discriminant = (speed ** 4) - (GRAVITY * inner)
    if discriminant < 0.0:
        return None
    return math.atan(((speed * speed) - math.sqrt(discriminant)) / (GRAVITY * flat))


def steps(kind, block, pitch=None):
    """The distance each successive step covers, forever.

    One tick of a self-propelled round is: gain `weaponacceleration` up to the
    cap if the motor is still running, then move.

    A BALLISTIC ROUND'S STEP IS CONSTANT, and that is the whole of what gravity
    does to a flight time. createProjectileFromWeapon launches it at
    `direction * weaponvelocity` with `direction` rebuilt from the solved
    heading and pitch, and the ballistic branch of updateProjectiles touches
    only `velocity.y` afterwards -- so the round's HORIZONTAL motion is
    `weaponvelocity / 30 * cos(pitch)` every tick for ever, and the stop is a
    horizontal test. The arc is longer than the straight line the other two
    classes fly, which is exactly the cosine, and the falling half never enters
    the answer. (The wind is the one term left out: 0x49BD10 adds the map's
    vector to a ballistic round's position every tick, a demo does not record
    it, and at the 3000 `maxwindspeed` of a typical map it is 0.09 world units
    a tick -- six units over a Crusader's whole flight.)
    """
    if kind == "ballistic":
        per_tick = (num(block, "weaponvelocity") / 30.0) * math.cos(pitch)
        while True:
            yield per_tick
    if kind != "accelerating":
        per_tick = num(block, "weaponvelocity") / 30.0
        while True:
            yield per_tick
    speed = launch_speed(block)
    cap = (num(block, "weaponvelocity", 0.0) or 0.0) / 30.0
    accel = (num(block, "weaponacceleration", 0.0) or 0.0) / 900.0
    burn = burn_ticks(block)
    tick = 0
    while True:
        tick += 1
        if tick <= burn:
            speed = min(cap, speed + accel)
        yield speed


def footprint_of(units, victim):
    """The victim's (FootprintX, FootprintZ) in map squares, or None if unnamed.

    Absent keys read as zero, which is what the engine's own FBI parser gives
    them; a unit with no footprint occupies no square and nothing hits it.
    """
    unit = units.get(victim)
    if not unit:
        return None
    try:
        return (int(float(unit.get("footprintx", 0) or 0)),
                int(float(unit.get("footprintz", 0) or 0)))
    except ValueError:
        return None


def footprint_squares(target, footprint):
    """The (x0, z0) map square the victim's footprint starts at.

    Stamped at the aim point with the left edge rounded to the NEAREST square,
    which is RWE's computeFootprintRegion. The aim point is taken as the
    victim's own position: for a victim that cannot move, the footprint's edge
    computed from it lands on a square boundary, to a tenth of a world unit on
    both axes, in 904 of the 1,313 still pairings the scored cells hold. Over the
    constant-speed class at drift zero, rounding the edge puts 98% of pairings on
    the model, against 89% for flooring it, 84% for ceiling it and 95% for
    flooring the centre and counting half the footprint either side -- and the
    gap widens as the victim moves (88% against 66%, 67% and 74% for drift under
    eight units). (Measured on the pairing alone, before the named-victim
    filter, so the denominators are this function's and not the table's.)
    """
    fx, fz = footprint
    return (math.floor((target[0] - fx * 8.0) / 16.0 + 0.5),
            math.floor((target[2] - fz * 8.0) / 16.0 + 0.5))


def span_of(kind, origin, target):
    """The distance the class's own step is measured along.

    A straight-flying round covers its step along the line in space, so its span
    is the three-dimensional distance. A ballistic one is stepped horizontally,
    because that is the only part of its velocity that never changes, so its
    span is the horizontal distance -- and the two agree wherever the shot is
    level.
    """
    if kind == "ballistic":
        return math.dist((origin[0], origin[2]), (target[0], target[2]))
    return math.dist(origin, target)


def flight_model(kind, origin, target, footprint, block, pitch=None):
    """The step on which the round first stands in one of the victim's squares.

    That step number IS the flight time: the shot-to-damage interval is the
    number of moves it took. None if the round passes the footprint without
    ever being in it -- a small footprint can be stepped over by a fast round --
    which the scoring counts as a disagreement rather than dropping.
    """
    fx, fz = footprint
    x0, z0 = footprint_squares(target, footprint)
    span = span_of(kind, origin, target)
    ux, uz = ((target[0] - origin[0]) / span, (target[2] - origin[2]) / span) \
        if span > 0 else (0.0, 0.0)
    if kind == "ballistic" and pitch is None:
        return None
    # Past this the line has left any square the footprint could cover.
    give_up = span + 16.0 * (fx + fz + 2)
    travelled = 0.0
    for k, step in enumerate(steps(kind, block, pitch), 1):
        travelled += step
        sx = math.floor((origin[0] + ux * travelled) / 16.0)
        sz = math.floor((origin[2] + uz * travelled) / 16.0)
        if x0 <= sx < x0 + fx and z0 <= sz < z0 + fz:
            return k
        if travelled > give_up or k >= 4000:
            return None


def aim_point_flight(kind, distance, block, pitch=None):
    """THE RETIRED MODEL: the step on which the round reaches the aim point, - 1.

    Kept only so --footprint can print what it scored beside what replaced it.
    Its -1 was read as a first step on the firing tick; it was the footprint.
    """
    travelled = 0.0
    for k, step in enumerate(steps(kind, block, pitch), 1):
        travelled += step
        if travelled >= distance or k >= 4000:
            return k - 1


def travelled_by(kind, flight, block, pitch=None):
    """How far the model says the projectile had flown when the damage landed."""
    travelled = 0.0
    for k, step in zip(range(flight), steps(kind, block, pitch)):
        travelled += step
    return travelled


# --- the drift bound, which is what makes the second class scoreable ---------
#
# A 0x0d records WHERE THE SHOT WAS AIMED, and the model stamps the victim's
# footprint at that point. A victim that moves while the round is in the air is
# somewhere else when it arrives, so the measurement is wrong by however far it
# went -- and that error is not a property of the weapon but of the pair.
# Bucketing every pairing in the corpus by how far the victim COULD have gone
# (its own FBI `maxvelocity` times the observed flight) sorts both scored classes
# onto one falling curve:
#
#     drift (world units)   constant speed      accelerating
#     0 (immobile victim)    98% (n=810)         78% (n=515)
#     0-8                    88% (n=2091)        71% (n=173)
#     8-16                   77% (n=2523)        56% (n=836)
#     16-32                  61% (n=3415)        43% (n=4064)
#     32-64                  39% (n=1383)        26% (n=4121)
#     64-128                 47% (n=194)         18% (n=667)
#     128+                   21% (n=198)          9% (n=5398)
#
# (share of pairings landing on the model's tick; --drift reprints it. Under the
# retired aim-point model the still row read 65% and 56%: the footprint is what
# lifted the top of the curve, and drift is what is left below it.)
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
# THE BOUND IS NOT TUNED. It was set at one step under the aim-point model, when
# half a step would have made that model's two missile exceptions disappear --
# which is exactly why it was not the bound -- and the footprint model was
# scored under it unchanged. A filter chosen for the disagreements it removes is
# not evidence, and one chosen before the model it is applied to cannot be.


def drift_of(units, victim, flight):
    """How far the victim could have travelled while the shot was in the air.

    None where the victim could not be named -- the id's build was not seen. That is not the same as a drift of zero
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
    drift = drift_of(units, observation.victim, observation.flight)
    return drift is not None and drift < num(block, "weaponvelocity") / 30.0


# --- the aim cone, which is what the ballistic class needs and the others do not
#
# The original perturbs every turret shot before it spawns the round
# (0x49D6D7): `heading += rand(acc) - acc/2` and `pitch += rand(acc) - acc/2`,
# where `acc` is the weapon's `accuracy` in sixteen-bit angle units, widened by
# however hurt the shooter is and narrowed by its kills. `sprayangle` is a
# second draw on the heading alone. Both are read by the turret fire handler
# and nothing else (0x49E010), and every ballistic weapon in this data set but
# three is `turret=1`.
#
# WHY THAT MATTERS HERE AND NOWHERE ELSE. For a round that flies in a straight
# line, a pitch error of d changes the horizontal speed by tan(pitch)*d -- under
# one percent for anything the other two classes fire, because they are aimed
# almost level. For a ballistic round it is a RANGE error of 2*cot(2*pitch)*d,
# because the range goes as sin(2*pitch): at CANNON_ART_MEDIUM's `accuracy=750`
# and an 18-degree elevation that is ten percent of the flight, six or seven
# ticks on a sixty-five-tick shell. So the same jitter that the constant-speed
# class can ignore is the dominant term here, and it is not recorded anywhere in
# a demo.
#
# So a ballistic pairing is scored only where the jitter CANNOT MOVE THE ANSWER:
# replay the round at the corners of its own cone and keep the pairing only if
# every corner gives the same step. The bound is the weapon's own declared
# `accuracy` run through the original's own formula -- there is nothing in it to
# tune -- and it separates: over the drift-bounded ballistic pairings the ones
# it keeps land on the model 41% of the time against 16% for the ones it
# rejects. `--cone` prints that table.
#
# IT IS A LOWER BOUND ON THE JITTER, not the whole of it. The health term opens
# the cone for a damaged shooter and a demo does not carry hit points, so a
# pairing this bound keeps may still have been fired through a wider cone than
# the weapon asked for. That is most of what is left between the 41% here and
# the 98% the constant-speed class reaches at drift zero.


def aim_cone(block):
    """(pitch half-width, heading half-width) in radians, at full health.

    The health term cancels exactly at full health and veterancy only narrows,
    so the weapon's own `accuracy` is the floor of the cone and the width either
    side of the aim is half of it. `sprayangle` is drawn on the heading only --
    changeDirectionByRandomAngle rotates in XZ -- and adds to that half.
    """
    accuracy = num(block, "accuracy", 0.0) or 0.0
    spray = num(block, "sprayangle", 0.0) or 0.0
    return (accuracy / 2.0) * ANGLE_UNIT, ((accuracy + spray) / 2.0) * ANGLE_UNIT


def ballistic_arc(origin, target, footprint, block, pitch, heading_error):
    """The step the round stops on when flown as a whole arc, jitter included.

    The same horizontal stop as flight_model, with two things flight_model does
    not need: the heading may be off the aim line, and the round is followed in
    y as well, so that a shell the jitter sends into the ground SHORT of the
    victim is reported as not having been stopped by the footprint at all. That
    second clause is why the bound rejects ARMVULC, whose shells land thirty to
    fifty units below their aim point and reach their victim through the blast
    rather than by arriving.
    """
    fx, fz = footprint
    x0, z0 = footprint_squares(target, footprint)
    flat = math.dist((origin[0], origin[2]), (target[0], target[2]))
    if flat <= 0.0:
        return None
    speed = num(block, "weaponvelocity") / 30.0
    bearing = math.atan2(target[0] - origin[0], target[2] - origin[2]) + heading_error
    ux, uz = math.sin(bearing), math.cos(bearing)
    horizontal = speed * math.cos(pitch)
    rise = speed * math.sin(pitch)
    x, y, z = origin
    give_up = flat + 16.0 * (fx + fz + 2)
    for k in range(1, 4000):
        # updateProjectiles' ballistic branch, then the move: gravity onto the
        # velocity, and the position after it.
        rise -= GRAVITY
        x += ux * horizontal
        y += rise
        z += uz * horizontal
        if x0 <= math.floor(x / 16.0) < x0 + fx and z0 <= math.floor(z / 16.0) < z0 + fz:
            return k
        # Coming down past the height the victim stands at, before reaching it:
        # the ground took the round and the footprint did not.
        if rise < 0.0 and y < target[1]:
            return None
        if math.dist((origin[0], origin[2]), (x, z)) > give_up:
            return None
    return None


def within_aim_cone_bound(units, block, observation):
    """Whether the weapon's own aim jitter could move this pairing's answer."""
    footprint = footprint_of(units, observation.victim)
    pitch = ballistic_pitch(block, observation.origin, observation.target)
    if footprint is None or pitch is None:
        return False
    answer = flight_model("ballistic", observation.origin, observation.target,
                          footprint, block, pitch)
    if answer is None:
        return False
    dpitch, dheading = aim_cone(block)
    for pitch_error in (-dpitch, 0.0, dpitch):
        for heading_error in (-dheading, 0.0, dheading):
            arc = ballistic_arc(observation.origin, observation.target, footprint,
                                block, pitch + pitch_error, heading_error)
            if arc != answer:
                return False
    return True


def scoreable(kind, units, block, observations):
    """The pairings a class may be scored over.

    Every pairing needs its victim named, because the model stops the round on
    the victim's footprint. A constant-speed class keeps every such victim; a
    self-propelled one keeps the ones inside the drift bound; a ballistic one
    keeps the ones inside the drift bound AND the aim cone, because its flights
    are three to ten times longer and because a pitch error is a range error.
    """
    named = [o for o in observations if footprint_of(units, o.victim) is not None]
    if kind not in ("accelerating", "ballistic"):
        return named
    inside = [o for o in named if within_drift_bound(units, block, o)]
    if kind == "accelerating":
        return inside
    return [o for o in inside if within_aim_cone_bound(units, block, o)]


def predict(kind, units, block, observation):
    """What the model says this pairing's flight time is, or None if it misses."""
    pitch = ballistic_pitch(block, observation.origin, observation.target) \
        if kind == "ballistic" else None
    return flight_model(kind, observation.origin, observation.target,
                        footprint_of(units, observation.victim), block, pitch)


def delta(kind, units, block, observation):
    """flight - model, or None where the model has the round miss the footprint."""
    predicted = predict(kind, units, block, observation)
    return None if predicted is None else observation.flight - predicted


def modal(counter):
    """The most common non-None key and its count; None never wins a mode."""
    for key, count in counter.most_common():
        if key is not None:
            return key, count
    return None, 0

# --- the pairing ------------------------------------------------------------


Observation = collections.namedtuple(
    "Observation", "flight distance damage demo tick victim origin target")


def pair(path, window, still_only, units):
    """(shooter type, slot) -> [Observation].

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
                origin = (record["ox"], record["oy"], record["oz"])
                aimed = (record["tx"], record["ty"], record["tz"])
                shots[record["demo"]][(record["shooter"], record["target"])].append(
                    (record["tick"], record["shooterName"], record["slot"],
                     math.dist(origin, aimed), record["targetName"], origin, aimed))
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
            for i, (tick, shooter, slot, distance, target, origin, aimed) in enumerate(fired):
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
                cells[(shooter, slot)].append(Observation(
                    inside[0][0] - tick, distance, inside[0][1], demo, tick, target,
                    origin, aimed))
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
            drift = drift_of(units, observation.victim, observation.flight)
            if drift is None or footprint_of(units, observation.victim) is None:
                continue
            for index, (low, high) in enumerate(edges):
                if low <= drift < high:
                    buckets[kind][index][delta(kind, units, block, observation)] += 1
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


def report_unscored_ballistic(cells, units, weapons, min_n):
    """Every ballistic cell the bounds left too thin to score, and what took it.

    A ballistic cell that cannot be scored is a result and not an omission, so
    it is printed rather than silently dropped -- the same discipline as the
    MISS lines below, one step earlier. The column that matters is `cone`: the
    two artillery cells with more pairings than anything else in the corpus,
    ARMMART and CORMART, lose nearly all of theirs to it, because
    CANNON_ART_MEDIUM's `accuracy=750` is a ten-percent range error on a
    sixty-five-tick shell.
    """
    rows = []
    for (shooter, slot), observations in cells.items():
        name, block = weapon_of(units, weapons, shooter, slot)
        if not block or weapon_class(block) != "ballistic" or not num(block, "weaponvelocity"):
            continue
        named = [o for o in observations if footprint_of(units, o.victim) is not None]
        drifted = [o for o in named if within_drift_bound(units, block, o)]
        kept = [o for o in drifted if within_aim_cone_bound(units, block, o)]
        if len(kept) >= min_n:
            continue
        if len(named) < min_n:
            continue
        rows.append((len(named), shooter, slot, name or "-",
                     len(observations) - len(named), len(named) - len(drifted),
                     len(drifted) - len(kept), len(kept)))
    if not rows:
        return
    print(f"\nthe {len(rows)} ballistic cell(s) with {min_n}+ pairings that the two bounds left too")
    print("thin to score, and what took them:\n")
    print(f"  {'shooter':<13} {'sl':>2} {'weapon':<22} {'named':>6} {'unnamed':>8}"
          f" {'drift':>7} {'cone':>6} {'left':>6}")
    for r in sorted(rows, key=lambda r: -r[0]):
        print(f"  {r[1]:<13} {r[2]:>2} {r[3]:<22} {r[0]:>6} {r[4]:>8} {r[5]:>7} {r[6]:>6} {r[7]:>6}")


def report_cone(cells, units, weapons):
    """The evidence behind the ballistic class's second bound.

    Every drift-bounded ballistic pairing, split by whether the weapon's own aim
    cone could move the model's answer. It is printed rather than described
    because a number in a comment goes stale. What to look at: the two rows are
    the same model over the same corpus and differ only in whether the jitter
    can reach them, and the split is better than two to one -- which is the
    argument that the spread this class carries is the original's own aim error
    and not the model being wrong.
    """
    print("\n--cone: the drift-bounded ballistic pairings, split by whether the weapon's")
    print("own aim jitter (accuracy, and sprayangle on the heading) could move the")
    print("model's answer.\n")
    rows = {True: collections.Counter(), False: collections.Counter()}
    by_accuracy = collections.defaultdict(lambda: {True: [0, 0], False: [0, 0]})
    for (shooter, slot), observations in cells.items():
        _name, block = weapon_of(units, weapons, shooter, slot)
        if not block or weapon_class(block) != "ballistic":
            continue
        accuracy = int(num(block, "accuracy", 0.0) or 0.0)
        for o in observations:
            if footprint_of(units, o.victim) is None or not within_drift_bound(units, block, o):
                continue
            error = delta("ballistic", units, block, o)
            if error is None:
                continue
            stable = within_aim_cone_bound(units, block, o)
            rows[stable][error] += 1
            bucket = by_accuracy[accuracy][stable]
            bucket[0] += 1
            bucket[1] += error == 0

    for stable, label in ((True, "the aim cone cannot move it"), (False, "it can")):
        counts = rows[stable]
        total = sum(counts.values())
        if not total:
            continue
        top = " ".join(f"{k:+d}:{v}" for k, v in counts.most_common(6))
        print(f"  {label:<28} n={total:<6} at +0: {100 * counts[0] / total:3.0f}%   {top}")

    print(f"\n  {'weapon accuracy':<18}{'kept':>8}{'at +0':>8}{'rejected':>10}{'at +0':>8}")
    for accuracy in sorted(by_accuracy):
        kept, rejected = by_accuracy[accuracy][True], by_accuracy[accuracy][False]
        print(f"  {accuracy:<18}{kept[0]:>8}"
              f"{(f'{100 * kept[1] / kept[0]:.0f}%' if kept[0] else '-'):>8}"
              f"{rejected[0]:>10}"
              f"{(f'{100 * rejected[1] / rejected[0]:.0f}%' if rejected[0] else '-'):>8}")
    print("\n  a weapon with no `accuracy` has a zero cone at full health, so nothing")
    print("  but its `sprayangle` can reject one of its pairings.")


def report_footprint(cells, units, weapons, min_n):
    """Still victims by footprint: the retired aim-point model against this one.

    The measurement the footprint model rests on. Only victims that cannot move,
    so nothing but the geometry is being scored, and only cells that would be
    scored. What to look at: the aim-point column falls as the footprint grows,
    because a bigger target stops the round further short of where it was
    aimed, and the footprint column does not.
    """
    print("\n--footprint: victims that cannot move, by the larger side of their footprint --")
    print("the share landing on the retired aim-point model, and on this one.\n")
    print(f"  {'':<22}{'aim point':>10}{'(at -1)':>9}{'footprint':>11}")
    table = collections.defaultdict(lambda: [0, 0, 0, 0])
    for (shooter, slot), observations in cells.items():
        _name, block = weapon_of(units, weapons, shooter, slot)
        if not block or not num(block, "weaponvelocity"):
            continue
        kind = weapon_class(block)
        if kind not in SCORED_CLASSES or len(scoreable(kind, units, block, observations)) < min_n:
            continue
        for o in observations:
            footprint = footprint_of(units, o.victim)
            if footprint is None or drift_of(units, o.victim, o.flight) != 0:
                continue
            pitch = ballistic_pitch(block, o.origin, o.target) if kind == "ballistic" else None
            if kind == "ballistic" and pitch is None:
                continue
            row = table[(kind, max(footprint))]
            row[0] += 1
            aim = o.flight - aim_point_flight(kind, span_of(kind, o.origin, o.target), block, pitch)
            row[1] += aim == 0
            row[2] += aim == -1
            row[3] += delta(kind, units, block, o) == 0
    for kind in SCORED_CLASSES:
        if not any(k == kind for k, _ in table):
            continue
        print(f"  {kind}")
        totals = [0, 0, 0, 0]
        for (k, side), row in sorted(table.items()):
            if k != kind:
                continue
            totals = [a + b for a, b in zip(totals, row)]
            label = f"footprint {side} (n={row[0]})"
            print(f"    {label:<20}{100 * row[1] / row[0]:>9.0f}%{100 * row[2] / row[0]:>8.0f}%"
                  f"{100 * row[3] / row[0]:>10.0f}%")
        if totals[0]:
            label = f"all (n={totals[0]})"
            print(f"    {label:<20}{100 * totals[1] / totals[0]:>9.0f}%"
                  f"{100 * totals[2] / totals[0]:>8.0f}%{100 * totals[3] / totals[0]:>10.0f}%")


def score(cells, units, weapons, min_n):
    """Every cell with enough pairings, scored by whichever model its class has.

    A cell is scored over the pairings its class allows -- all of them for a
    constant-speed weapon, the ones inside the drift bound for a self-propelled
    one, and those inside the drift bound and the aim cone for a ballistic one --
    and `min_n` applies to what survives that, so a cell whose pairings were
    nearly all against aircraft, or nearly all fired through a cone wide enough
    to move the answer, drops out rather than being scored thin.
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
        errors = collections.Counter(delta(kind, units, block, o) for o in subset)
        mode, at_mode = modal(errors)
        if mode is None:
            continue
        damage, damage_at = collections.Counter(o.damage for o in subset).most_common(1)[0]
        # How far short of the aim point the round was when the damage landed:
        # about the footprint's half-width, which is the whole of what the
        # retired aim-point model's -1 had been absorbing. Measured along the
        # class's own span, so a ballistic shell's is a horizontal distance.
        short = sorted(
            span_of(kind, o.origin, o.target)
            - travelled_by(kind, o.flight, block,
                           ballistic_pitch(block, o.origin, o.target)
                           if kind == "ballistic" else None)
            for o in subset)
        rows.append(dict(
            shooter=shooter, slot=slot, weapon=name or "-",
            kind=kind, velocity=velocity, per_tick=per_tick,
            n=len(subset), dropped=len(observations) - len(subset),
            mode=mode, share=at_mode / len(subset),
            damage=damage, damage_share=damage_at / len(subset),
            declared_damage=num(block["_damage"], "default") if block else None,
            misses=errors[None], short=short[len(short) // 2]))
    return rows


SCORED_CLASSES = ("constant speed", "accelerating", "ballistic")


def print_table(rows):
    print(f"  {'shooter':<13} {'sl':>2} {'weapon':<22} {'v/30':>6} {'n':>5} {'delta':>6}"
          f" {'share':>6} {'short':>6} {'damage':>7} {'decl':>6}")
    # (-n, shooter, slot), which is the order the port prints in too: two cells
    # with the same count would otherwise sort by whichever dict filled first and
    # a diff against --weapon-cells would show a phantom difference.
    for r in sorted(rows, key=lambda r: (-r["n"], r["shooter"], r["slot"])):
        flag = "" if r["mode"] == 0 else "   <-- disagrees"
        declared = int(r["declared_damage"]) if r["declared_damage"] else 0
        print(f"  {r['shooter']:<13} {r['slot']:>2} {r['weapon']:<22} {r['per_tick']:>6.1f}"
              f" {r['n']:>5} {r['mode']:>+6} {100 * r['share']:>5.0f}% {r['short']:>6.1f}"
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
    ap.add_argument("--footprint", action="store_true",
                    help="print the evidence that a round stops on the victim's footprint")
    ap.add_argument("--cone", action="store_true",
                    help="print the evidence behind the ballistic class's aim-cone bound")
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
            dropped = sum(r["dropped"] for r in group)
            print(f"the {len(group)} cells whose weapon flies at a constant speed, stopped on"
                  f" the victim's\nfootprint, over every victim that can be named"
                  + (", victims that cannot move only" if args.still_victim else "")
                  + f" ({dropped} could not)\n")
        elif kind == "ballistic":
            dropped = sum(r["dropped"] for r in group)
            print(f"the {len(group)} cells whose weapon lobs a shell, launched at the flat root"
                  f" of TA's own\nfiring solution and stepped horizontally at"
                  f" weaponvelocity/30 * cos(pitch), stopped on\nthe victim's footprint, over"
                  f" the victims that could not outrun a step of it and\nthe shots the weapon's"
                  f" own aim cone could not have moved\n({dropped} pairings dropped by those two"
                  f" bounds or an unnamed victim)\n")
        else:
            dropped = sum(r["dropped"] for r in group)
            print(f"the {len(group)} cells whose weapon has a motor, flown as RWE flies"
                  f" one and stopped on the\nvictim's footprint, over the victims"
                  f" that could not outrun a step of it\n({dropped} pairings dropped by"
                  f" that bound or an unnamed victim)\n")
        print_table(group)

    # The damage figure is the independent check on the pairing: no filter looks
    # at it, so a wrongly paired event has no reason to carry the firing
    # weapon's own damage.
    checkable = [r for r in scored if r["declared_damage"]]
    agreeing = [r for r in checkable if r["damage"] == int(r["declared_damage"])]
    print(f"\n{len(agreeing)} of {len(checkable)} scored cells carry their weapon's own"
          f" [DAMAGE] default as the modal damage")

    misses = sum(r["misses"] for r in scored)
    print(f"{misses} scored pairing(s) the model has stepping over the victim's footprint"
          f" without landing in it, counted against their cell's share")

    report_unscored_ballistic(cells, units, weapons, args.min_n)

    if args.drift:
        report_drift(cells, units, weapons, args.min_n)

    if args.footprint:
        report_footprint(cells, units, weapons, args.min_n)

    if args.cone:
        report_cone(cells, units, weapons)

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
            if r["kind"] == "accelerating":
                model = (f"a motor replay from {launch_speed(weapons.get(r['weapon'], {})) :.1f}"
                         f" up to {r['per_tick']:.1f} a tick")
            elif r["kind"] == "ballistic":
                model = f"{r['per_tick']:.1f} a tick times the cosine of its launch pitch"
            else:
                model = f"{r['per_tick']:.1f} a tick"
            model += " until it stands on the victim's footprint"
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
