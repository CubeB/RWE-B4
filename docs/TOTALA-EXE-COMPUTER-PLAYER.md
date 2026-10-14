# The original's computer player, as the executable runs it

What `TotalA.exe` does when it plays a computer-controlled side: how its
decisions are scheduled, how it scores what to build, where it puts things, and
when it sends units out. This is the document the default computer player in
RWE is written from (ADR-0005).

**Where it comes from, and how far to trust it.** Added 2026-10-14 from a
written account of the executable's computer player that was produced in a
separate checkout by reading the reconstructed source, then re-stated here in
the register's own words: behaviour, arithmetic, constants and routine
addresses, and none of the reconstruction's names or control flow
(ADR-0004). The account's claims are *read from function bodies by
inspection*. Nothing here was executed, and none of it has been replayed
against the running original. [`TOTALA-EXE-AI.md`](TOTALA-EXE-AI.md) is a
second, independent reading of the same subject (the Nanolathe project's); the
two agree wherever they overlap and the places they do not are marked **[open]**
below. [`TA-COMMUNITY-AI.md`](TA-COMMUNITY-AI.md) is a third, from play. Where
this file and `-AI` say the same thing, treat it as established; where this file
alone says it, treat it as read-but-unreplayed.

Conventions used below: *tick* is one simulation step (30 a second);
*world units* are the position unit; *score* arithmetic is integer unless it
says otherwise, and the truncations are part of the finding. `rand(n)` means the
original's random draw below `n`: it returns 0 without advancing the generator
when `n < 2`, otherwise advances a Park-Miller generator and returns the result
modulo `n`.

## 119. The controller: who has one, what runs when, how units are sorted

**Who.** Every active player gets two cooperating objects from its slot
initialiser (`0x464f80`): a *squad manager* (hung off the player record at
`+0x74`) that owns ten timers, and a *strategy table* (one per player, in a
global array) that owns the type scores, limits, enemy lists and placement
state. Players simulated on another machine (type 3) get neither. A human
player has both objects, and the squad manager still runs the weapon
re-targeting sweep for the human's units (§122).

**The per-tick order for one player** is fixed: the squad manager's tick, then
the strategy table's update, then a refresh of the player's owned-unit lines
of sight. Decisions taken by the squad manager therefore read the lists the
*previous* refresh built. Do not reorder (`0x464f80`, `0x408c40`, `0x408cb0`,
`0x40b2c0`).

**Scheduling.** Strategic timers run only for an active type-2 (computer)
player. Every 30th call also runs a unit classification sweep (below). Each
timer fires when its own absolute next-time is at or before the current tick,
then sets that next-time itself; the ten slots are scanned in index order.

| Slot | Timer | Re-armed to | Work |
|---|---|---|---|
| 0 | none | none | nothing allocated |
| 1 | build timer | tick + 30 | idle fixed builders, metal converters |
| 2 | land assault | tick + 300 | the main land attack, fed from slot 3 |
| 3 | land reserve | tick + 150 | walks reserve land units toward slot 2 |
| 4 | scout timer | tick + 90 | mobile builders, and their fallback patrol |
| 5 | base group | its work is empty | the rally group; the next-time starts at 0 and is never advanced, so it is "due" every call and does nothing |
| 6 | water assault | tick + 300 | the same as slot 2, for water units |
| 7 | water reserve | tick + 150 | the same as slot 3, for water units |
| 8 | air routes | tick + 30 + rand(900) | aircraft group routes |
| 9 | spatial probe | tick + 30 + rand(150) | probes for a valuable position to attack |

(`0x407380`, `0x4077e0`, `0x4079f0`, `0x407ae0`, `0x4086d0`, `0x408bf0`,
`0x408c40`, `0x408cb0`, `0x40ad20`, `0x40b2c0`, `0x408100`, `0x407e90`.)

The strategy table refreshes at most once every 30 ticks, and at each refresh
it also draws `rand(30) == 0` and, on a hit, recomputes the per-type scores. So
the type scores are re-derived at random roughly every 900 ticks rather than on
a schedule.

**Standing policies are overwritten.** For every unit carrying the
computer-classification runtime flag (`0x20`), each sweep sets the standing
fire mode to 2 (fire at will) and the standing movement mode to 1 (*manoeuvre*)
for a unit whose type has the `cancapture` bit (FBI key; the source's generic
name for the bit is `special`, which it is not -- it is not a commander flag)
and 2 (*roam*) for every other eligible type. Both are reapplied on every
sweep, to grouped units as well, so a script or a capture cannot leave
anything else standing. (`0x408830`; the movement values are in
[`TOTALA-EXE-WEAPONS.md`](TOTALA-EXE-WEAPONS.md) §9.)

**Classification.** Only a unit whose group number is 0 is assigned. First
match wins, and the order is the finding:

1. runtime flag `0x20000000` (a building-class unit): armed (`0x80000000`) ->
   group 5, otherwise group 1;
2. an FBI builder -> group 4 (so an aircraft builder is group 4, not 8);
3. `canfly` -> group 8 (so **every** aircraft, armed or not, is group 8);
4. `MinWaterDepth > 0` -> group 7;
5. armed -> group 3.

An unarmed land carrier ends up in no group at all. Squad membership vectors
remove by swapping the last element into the hole, so membership order is not
stable, and anything that takes "the first member" or indexes a member at
random depends on that. Orders to a group are issued by scanning the owner's
unit array and testing each unit's group number, not by walking the
membership vector (`0x408830`, `0x480250`, `0x480460`).

**What the controller knows.** The per-refresh lists are:

- *visible enemies*: ready, non-allied units that the visibility test accepts,
  excluding runtime flag `0x8000`;
- *known enemies*: ready, non-allied units carrying runtime flag `0x100`;
- *own air bases*: completed own units with `builder` and `isairbase` and
  activation bit 0 -- the list is called "factories" in the source and is also
  the landing and repair candidate list.

It counts only the owner's *completed* units, counts builders, and records
whether an active `istargetingupgrade` unit exists. The visibility test samples
the unit's model bounds, rejects units cloaked by activation bit `0x04`, and
treats submerged bounds specially; some of its tests read the *local* player
rather than its argument (`0x40aa40`, `0x465ac0`).

An enemy query against a radius reads the visible list first and falls back
to the known list **only if the visible list produced nothing and the player
has a targeting upgrade**; it does not union them (`0x40ad80`, `0x40b0d0`).
The assault target search is broader: it scans every non-allied active
player's unit array directly, skipping dead units, airborne units, flag
`0x8000` and the activation-bit filters, and takes the smallest horizontal
squared distance, first on a tie. It does not consult the visibility lists at
all (`0x4071f0`).

**The base position** is a weighted centroid of the player's completed units:
each contributes a weight of 1, plus 40 if it is not mobile, plus 20 more if it
has a build menu. (The production multipliers in §120 are a different thing
and are not these.)

## 120. Scoring what to build

The computer player never works from a fixed build list. For a builder it
scores every type on the builder's build menu, draws one, and builds it.

**Initial tables.** Every type starts with script multiplier 100, completed
count 0, limit -1 (unlimited), and both multiplier and limit unlocked. A limit
is a bound on *completed* units: a type is allowed while `completed < limit`.
Outstanding construction is not reserved, so several builders can pick the same
type before the count moves (`0x409470`, `0x409520`, `0x4095d0`, `0x409f20`).

**The type value** (a signed byte, used for production and again by the
spatial probe, §122):

- the *weapon score* starts at 1 -- or 11 for a type that `canattack` -- and
  adds `damage / 40 + range / 100 + 5` for each nonzero weapon slot,
  clamped to [-100, 100];
- the value starts at 11 for a metal extractor and 1 otherwise, adds 10 for a
  metal converter, 10 for a negative net energy use, `trunc(metalCost * 0.01)`
  and `trunc(energyCost * 0.002)`, then the weapon score, and clamps. The
  source converts at each term, so a single final cast is not equivalent.

**The three ratings** (`0x409730`):

| Rating | Construction |
|---|---|
| *normal* | starts at 1, or 21 if the type `canattack`; +30 for a builder when fewer than 3 of the type exist; +50 for a negative net energy use; +50 for an extractor; +25 for a converter; +40 for an aircraft; +15 for sonar; +5 for radar; plus the energy it produces, clamped to [0, 30] |
| *metal* | `clamp(-0.02 * metalCost + (converter ? 25 : 0) + (extractor ? 100 : 0), 0, 100)` |
| *energy* | `clamp(-0.0025 * energyCost - 5 * netEnergyUse, 0, 100)` |

The *normal* rating is then multiplied by 4 if none of the type exist, by 2 if
exactly one does, and by 3 if `MinWaterDepth >= 0` (that exact predicate); if
the player's total unit count is above half its unit limit it adds half the
type value; it becomes 0 for a `canload` or `isfeature` type and for a wind
generator under the code's low-wind test; and it is capped at 100.
**[open]** whether the multipliers combine (4 and 3 give 12) or the last
applies; the source reads as sequential multiplication.

**The final rating** for a candidate (`0x40bb00`) is 0 when the player has
under 50 stored energy or under 25 stored metal, when the limit test fails,
and for a `downloadable` type in game type 1. Otherwise, with integer
arithmetic throughout:

```
E = trunc(max(0, (min(1000, trunc(energyStorage)) - energy) * 0.125))
M = trunc(max(0, (min( 500, trunc(metalStorage )) - metal ) * 0.25 ))
if net energy < 1:    E += 20
if net metal  < 1:    M += 20
if energy income < 50 then E += 100  else if energy income < 200 then E += 10
if metal income  < 3  then M += 100  else if metal income  <   5 then M += 20

metalWeight  = clamp(M, 0, 100)
energyWeight = clamp(E - metalWeight, 0, 100)
normalWeight = max(100 - metalWeight - energyWeight, 0)

rating = (normalWeight*normal + metalWeight*metal + energyWeight*energy)
         * scriptMultiplier / 10000
```

Metal shortage claims priority before energy shortage, and integer division can
reduce a small positive preference to zero.

**The draw** (`0x40bdb0`) is a weighted reservoir over the builder's menu in
menu order: each positive rating `r` adds to a running total and replaces the
current choice when `rand(total) < r`. The chosen type is then **rejected, with
no re-draw**, if its side string differs from the builder's.

## 121. Where it builds, and who builds

**Fixed builders and converters** (slot 1, every 30 ticks, over live fixed
units without runtime flag `0x4000`). A metal converter is switched **off**
when stored energy is at most twice stored metal. Above that threshold, a
positive net energy and a nonzero `rand(5)` switch it **on**; failing those two
does not switch an already-on converter off. An idle fixed builder draws one
item as in §120 and queues a count of one (`0x4086d0`, `0x408670`).

**Mobile builders** (slot 4, every 90 ticks). A `cancapture` builder is held
back from starting work when the completed builder count is at least five, and
until the controller's "next action" time after being attacked (§122). A unit
whose current order has metadata bit 8 is not replaced. Otherwise it draws a
type, searches for a site and issues the build. A `cancapture` builder is also
kept within `(mapWidth + mapHeight) / 3` of the base (map pixel dimensions, in
fixed point). If it has nothing to do it moves and then patrols around the base
with a displacement limit of 640 world units; any other idle builder patrols
toward the base, or to a point at least about 320 away when it is already close
(`0x408100`).

**Placement.** Two randomised lattices exist per player. The first has margin 3
and spacing `rand(10) + 11` by `rand(3) + 11`; the second has margin 6 and
spacing `rand(20) + 14` by `rand(3) + 14`, and is the one used for
`MinWaterDepth >= 0`. Each is offset about the midpoint of its spacing
(`0x40a150`, `0x40a260`).

The search has a *shared radius* that grows by 160 each time a search is
needed, up to the larger map dimension, and drops to 0 on a successful
placement, so successive builders affect each other's reach. The proposed
origin moves from the builder toward the weighted base by at most that radius
(`0x40a5d0`).

- *Extractors.* When the map's threshold value is below `rand(255)`, the
  search walks the cached resource-feature cells nearest first (a heap keyed by
  negative squared distance), footprint-adjusts each, requires it to be
  buildable, and takes the best positive metal yield; it stops once a
  candidate is more than the first success's squared distance plus 160 away.
  This is a window in *squared* distance, not a 160-unit annulus.
  **[open]** `TOTALA-EXE-AI.md` §6 reads the same draw the other way round
  (below the threshold, not above). The reading here is the one under which an
  extractor usually goes to a real deposit, which is what the original is
  observed to do.
- *Everything else* (and extractors that fall through): up to 30 candidates at
  a random direction and distance, converted to cells and snapped to the
  chosen lattice with jitter, accepted when the footprint is placeable and its
  metal score is no greater than
  `surfaceMetal * footprintX * footprintZ * 2`. Ordinary buildings therefore
  avoid high-value ground. (`0x40a7b0`, `0x40bfe0`.)

The resource-feature cache is built from map features with nonzero value and
feature flag `0x200`, once at battle entry; the lattices, the radius and the
site-choice generator are state that carries from call to call, so a site is
not a pure function of the builder's position. The surface-metal value comes
from the map's own schema, not from the global header, where it is never
authored and reads 0 (which would reject every non-extractor trial; see
`TOTALA-EXE-AI.md` §6).

## 122. Groups at war: assaults, reserves, aircraft, retaliation

**Assault groups** (slots 2 and 6; land limit 20000 and water limit 50000 in
the cohesion rule below; `0x4073b0`, `0x407410`, `0x407560`). Defaults:
minimum 3, maximum 6, `attacking` false.

- *Rebalance against the reserve first.* An empty assault takes the reserve's
  first unit. Then, repeatedly, the assault's most distant member (squared
  distance from the centroid at or above `limit * groupSize`) is sent back to
  the reserve, always keeping one; then reserve members inside that same
  criterion are taken in. It is a size-dependent cohesion rule, not a radius.
  **Quirk to keep or fix deliberately:** the source subtracts the departing
  unit's position from its running sums *after* the membership removal
  swapped the last element into the iterator, so the subtraction can name the
  replacement, not the unit that left. A port that caches the departing
  position first changes which units end up in the assault.
- *Rally or attack.* With three or fewer members, or before the assault has
  begun attacking with fewer than six, it rallies: the centroid of the first
  nonempty of groups 5, 1, 4 -- and if there is none it falls through to attack
  regardless of size. Rallying clears `attacking` and issues move mode 2 with
  the source's extra argument 160. Once attacking, four or five units continue.
  The target is the **nearest enemy to the assault's centroid** (§119).
- *Reserve groups* move toward their assault's centroid every 150 ticks.

**Aircraft** (group 8, slot 8, not the thresholds above). With fewer than five
members it creates two or three random points near the base: the first as a
move (mode 2), the rest as patrol (mode 9). If the base position is exactly
zero in both axes it patrols to the nearest enemy's position instead, and that
branch does not check for there being none. With five or more it picks a random
map edge and patrols there. Aircraft fight on those routes through the ordinary
order handlers, not because the controller sent them at anything
(`0x4077e0`, `0x4079f0`, `0x407ae0`).

**The spatial probe** (slot 9; `0x407d40`, `0x407e90`, `0x40b1c0`) keeps a best
position, a probe position, a step and a best rating, initialised about the map
centre. Each call has a 1-in-10 chance to reset the probe to the best point and
pick a 320-unit step in a random direction; it then advances, tests that the
square is visible/explored, and sums the type values of visible enemies within
160 units. A candidate is accepted when `rand(candidateValue)` exceeds
`rand(bestValue)`. Attack-capable members able to reach it are ordered to
attack it. **Nothing in the ordinary classifier ever puts a unit in group 9**
(agreeing with `TOTALA-EXE-AI.md` §2), so this does nothing unless a mission
or a control-group assignment fills it.

**Being attacked** (`0x408920`, `0x4089a0`). The controller first notifies unit
references with event 16. For a locally controlled computer's `cancapture`
unit it postpones builder work until `tick + 30 + rand(300)` and clears
replaceable orders. The rest -- completed units inserting an attack on a
non-allied attacker, and individual weapons being re-targeted -- is ordinary
unit behaviour that humans' units get too, subject to the standing policies.

**The weapon re-targeting sweep** (`0x40b7b0` and its caller) runs for every
player, human or not: each call scans `floor(maxUnits / 30) + 1` unit slots
from a wrapping cursor, considering completed armed units in fire-at-will
mode, handling the three weapon slots, dropping allied, category-disallowed or
otherwise invalid targets before finding replacements. Computer players pass
`force = 1`. The target chooser samples and swap-removes at most 50 candidates,
gives each accepted candidate `rand(horizontalSquaredDistance)`, and the
**lowest random score wins**, with a per-weapon-category candidate as the
fallback. It is not a nearest-first sort (`TOTALA-EXE-WEAPONS.md` §9, §10).

## 123. Scripts, difficulty and the handicap

**The script** (`0x4648e0`, `0x406c90`..`0x40a100`). The loader tries the
mission's AI profile file (the OTA `aiprofile` key, default `DEFAULT`) and then
`ai\default.txt`, runs its command text, then runs each downloadable type's
`aiWeight` text for each active computer player. Three directives are
registered under mask 8: `plan`, `weight`, `limit`.

- `plan` first disables the weight/limit directives, then enables them again for
  a matching `easy`, `medium`, `hard` or `any`. (`any` tests argument 1 on
  every iteration.) Directives before the first `plan` are unconditional on
  difficulty.
- `weight <name|category> <n>` scales the existing multiplier, truncating and
  clamping to [0, 100], unless that entry is locked. `limit <name|category>
  <n>` writes the per-type limit unless locked.
- A name resolves to a one-type set and locks (argument 1); a category
  resolves to a mask and does not lock (argument 0). A rule naming one unit
  therefore sticks, and category rules can be applied again to anything
  unlocked. The downloadable pass checks the multiplier lock and the limit
  lock separately.
- Resetting the computer players re-runs the script from scratch.

The profile's `limit` directive is live; the *FBI key* `ai_limit` is parsed
and never read (`TOTALA-EXE-AI.md` §7).

**Difficulty has two parts.** The profile gates by the plan; and for a
locally simulated computer player the resource credit is scaled **0.5 easy,
0.7 medium, 1.0 otherwise**, on production, on transfers to it and on refunds
for cancelled construction (`TOTALA-EXE-ECONOMY.md` §23, `0x40144E`). It is a
handicap on income, not an aggression setting; nothing in the decode is a
timid/aggressive decision tree.

**What the files are not here.** The shipped `ai\default.txt`, a map's profile
and each downloadable type's `aiWeight` string are game data, not part of the
executable, so the exact build frequencies of a named faction on a named map
come from those files at run time and are not a finding.
