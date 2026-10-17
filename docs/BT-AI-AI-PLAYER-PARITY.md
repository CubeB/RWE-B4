# Computer player (classic engine) and AI profile grammar parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`, `TA-SYSTEMS-PARITY.md`
and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register. This is
the finding that implementation work cites for what the original computer player does and
how RWE's classic engine (`src/rwe/ai/classic/`, ADR-0005) compares, per `AGENTS.md` and
ADR-0004: the squad manager and its nine timers, unit classification, the three ratings and
the weighted draw, placement, mobile builders, assaults and reserves, aircraft routes, the
spatial probe, the retaliation postponement, and the profile script grammar.

The register already holds almost all of this subject: `TOTALA-EXE-COMPUTER-PLAYER.md`
sections 119 to 123 are the document the classic engine was written from, and a large part
of the chapter is corroboration. Where it is, the rule says so in one line. The ordinary
unit behaviour that the computer player shares with a human's units (weapon re-targeting,
return fire, the retaliation attack that a damaged unit queues) is not audited here: it is
`BT-UNITS-UNIT-TARGETS-PARITY.md` and `TOTALA-EXE-WEAPONS.md` sections 9 and 10.

**Scope of the RWE half.** Only the classic engine is audited, because ADR-0005 makes it a
port of the original and a difference there is a conformance gap. RWE's own computer player
(`src/rwe/ai/` outside `classic/`) is RWE-original and is not compared with the reference.

## Source and evidence class

- **Source.** Two prose chapters of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ai-ai-player.md` and
  `ai-ai-profile.md`). Nothing else under that tree was opened: no source, no decompilation,
  and none of its source links were followed. Addresses are the ones the chapters name; rules
  are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that rows
  already resolved there are not raised again; the row this audit leans on is BT-8
  (resolved: an absent `MinWaterDepth` is the signed value -10000).
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play, no AI-versus-AI match and no
  demo oracle was run. The Gaps table says which items an original-game check settles.
- **The RWE half** is a read-only audit of `src/rwe/ai/classic/` at the working tree of
  2026-10-16. Nothing was built, run or changed, and tests were not run; a MATCHES below
  means the code reads as the rule says. Another agent had uncommitted edits elsewhere in the
  tree while this was written, so every line number below is "about".
- **A census of the shipped data** (retail install through `tools/hpi.py`, scratch scripts
  only) supplies the "what would this change on shipped data" figures: the 272 FBIs, the 271
  `.ota` files, `gamedata/moveinfo.tdf` and the ten `ai/*.txt` profiles. It is a count of keys
  in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape, and what the register already holds
2. The per-tick order, the timers and the strategy refresh
3. Counts, the builder count and the base estimate
4. Classification and the standing orders
5. The type value and the three ratings (the `MinWaterDepth` sign; the low-wind test)
6. The final rating, the limit test and the weighted draw
7. Placement: lattices, the shared radius and the extractor search
8. Fixed builders and converters (slot 1)
9. Mobile builders (slot 4) (DIFFERS in three places)
10. Assaults and reserves (slots 2, 3, 6, 7)
11. Aircraft routes (slot 8)
12. The spatial probe (slot 9)
13. Being attacked
14. The script directives, limits and the missing-argument default
15. The profile tokenizer and what the shipped profiles contain
16. Difficulty and the handicap
17. Gaps to fix

## 1. Shape, and what the register already holds

Reference (`ai-ai-player` sections 1 and 6): two state objects per player. The squad manager
holds nine timers (slot 0 is null) and an assignment countdown; the player AI object holds
the caches, the per-type tables, the placement state and the base estimate. Each tick, for an
eligible active player, the order is: the squad manager's tick, then the player AI's 30-tick
refresh, then the sight update. Both objects are created unless the player is both active
and type 3.

RWE: one `ClassicAiController::Impl` holds both halves (`ClassicAiController.cpp` about
lines 110 to 1000); `tick` (about 1085) runs `purge`, `noticeAttacks`, the classify sweep
every 30th call, `runSlots` and `strategyUpdate`, in that order. The squad manager's tick
before the refresh matches.

Status: **MATCHES** in shape. An idle seat, or a dead player, does nothing.

## 2. The per-tick order, the timers and the strategy refresh

Reference (`ai-ai-player` sections 1 and 6, register section 119): the timers and their
cadence are slot 1 every 30 ticks, slots 2 and 6 every 300 with the cohesion limits 20000
(land) and 50000 (water), slots 3 and 7 every 150, slot 4 every 90, slot 5 an empty callback
that is "due" every call, slot 8 at 30 plus a draw of 900 (30..929), slot 9 at 30 plus a draw
of 150 (30..179). Every timer's first due tick is zero. The strategy table refreshes when the
tick is at least the last refresh plus 30, and on each refresh a 1-in-30 draw recomputes the
ratings. A large tick jump is not caught up.

RWE (`ClassicAiController.cpp` about 1008 to 1065, 384 to 398): `runSlots` has exactly these
cadences and constants (`LandCohesionLimit` 20000, `WaterCohesionLimit` 50000, about lines 43
and 44); the first due tick is zero; slot 5 is skipped; `strategyUpdate` refreshes at 30 and
draws `rng.below(30) == 0`.

Status: **MATCHES**. One reading is **UNCLEAR**. The chapter says the tick of the timer set
"decrements the assignment countdown, resets it to 30 when due, then invokes any due timer".
It does not say whether the timers are only invoked on the tick the countdown fires (a
30-tick granularity on every timer) or on every tick. The register reads it as every tick,
and so does RWE (`runSlots` is called every tick). A timer at a 90-tick cadence cannot tell
the two apart; only slots 1 (30), 8 and 9 (drawn) could, and only by a few ticks. Not
counted as a gap.

## 3. Counts, the builder count and the base estimate

Reference (`ai-ai-player` section 2): the refresh counts only the owner's completed units
(`buildLeft` exactly zero) and a type's count is a snapshot, not a reservation. The builder
counter tests the definition field at `+0x156`, "the build-option ID pointer", **not a fresh
test of the FBI builder flag**. The base estimate is the weighted mean of the owner's completed
units: weight 1, plus 40 for an immobile definition, plus 20 for a nonnull build-option
pointer. A zero total leaves the estimate at zero. `GetMirroredBasePosition` subtracts both X
and Z from the map **width** (so Z uses the width on a rectangular map).

RWE (`refreshLists`, about lines 330 to 370): counts completed units of this player;
`completedBuilders` counts `def->builder`; the weight is `1 + (isMobile ? 0 : 40) + (builder ?
20 : 0)`; the base stays at its previous value if the total weight is zero.

Status: **MATCHES** for the weights and the completed-only rule. Two small points:

- **UNCLEAR: `builder` is the FBI flag, the original's field is "has a build menu".** A unit
  with `Builder=1` and an empty menu would count and weigh differently. The register already
  says "has a build menu". Not counted: no shipped case was looked for, and the data this
  would need (which FBIs have `Builder=1` and no build entries) lives in the GUI pages, not
  the FBI.
- **DIVERGENCE (documented by the code, "[open]")**: a zero total leaves the previous estimate
  rather than zero. The estimate is only ever read after units exist, so no effect.
- The mirrored-base helper is used only by the mirrored move of rule 9, which RWE does not
  have.

## 4. Classification and the standing orders

Reference (`ai-ai-player` section 6): the sweep runs on a 30-tick countdown, assigns only
units whose group is still zero, and tests in this order: the immobile flag (`0x20000000`)
puts the unit in slot 5 if the armed flag (`0x80000000`) is also set, else slot 1; a builder
goes to slot 4; a flying definition to slot 8; a positive `MinWaterDepth` to slot 7; any
remaining armed unit to slot 3. There is no initial slot-9 branch. The sweep also rewrites two
standing policies: fire at will, and a movement mode of maneuver (for `cancapture`) or roam.

RWE (`classify`, about 543 to 590; `classicClassify` in `ClassicAiGroups.cpp`): the same
order; only a unit whose group is `None` is assigned; the standing fire and movement orders
are written only where they differ, which gives the same end state. `ClassifyInterval` is 30
calls.

Status: **MATCHES** in order. Three stand-ins are **UNCLEAR**:

- **"Armed" is any named weapon slot** (`isArmed`, about line 82). The original's flag is
  `0x80000000` on the runtime record and the register does not say what sets it. In the
  ordinary data it is the same set (a unit with a weapon name and `canattack`), but a unit
  with a weapon and `canattack=0`, or the reverse, would split. Not counted: no cause to
  believe the shipped set disagrees, and no census was made.
- **The `MinWaterDepth > 0` test for slot 7** reads RWE's unsigned field (`waterDepthPositive`,
  about line 221). That test is the same under the signed default of rule 5, so it is correct
  as written. Recorded because rule 5's bug is on the line above it.
- **`heldByMission` and `carriedBy` units are skipped** (`eligible`, about line 523). The
  chapter has no such condition; the original's runtime flag filters (`0x4000` clear,
  `0x10000000` set) are the closest. Neither is in the shipped skirmish data; no gap.

## 5. The type value and the three ratings

Reference (`ai-ai-player` section 4, register section 120): the weapon score and type value
(extractor 11 else 1; +10 converter; +10 negative net energy use; the two truncated cost
terms; weapon damage over 40 plus range over 100 plus 5 per slot; clamped); the ordinary rating
(1, or 21 for `canattack`; +30 builder with fewer than three; +50 net energy producer; +50
extractor; +25 converter; +40 air; +15 sonar; +5 radar; energy make clamped 0..30; x4 with
none, x2 with one; **x3 when `MinWaterDepth` is nonnegative**; plus half the type value above
half the unit allowance; zero for `canload`, `isfeature` and a wind generator under the
low-wind test; capped at 100); the metal and energy ratings.

RWE: `ClassicAiScoring.cpp` (about 100 to 150) reproduces every term and the order.

Status: **MATCHES** for every term except the two below.

**The sign of `MinWaterDepth`: DIFFERS (high impact).** Reference (BT-8, resolved): a key
that is absent defaults to the *signed* value -10000, so a unit or structure with no
`MinWaterDepth` is **negative** and takes neither the x3 nor the second lattice of rule 7.
RWE: `MovementClassDefinition::minWaterDepth` is `unsigned int` and an absent key reads as 0
(`io/moveinfotdf/io.cpp:12`, `io/fbi/io.cpp:79`), and `ClassicAiController.cpp` about line 209
tests `static_cast<int>(mc.minWaterDepth) >= 0`, which is **always true**. So every type
takes the x3, and every site search uses the second lattice. Census:

- 208 of the 272 shipped FBIs (76%: 123 fixed, 85 mobile) have no `MinWaterDepth`; the 64 that
  do never write 0 (values 1 to 40). The moveinfo classes 0 to 5 and 11 to 14 have no key;
  `CLASS6` and `CLASS8` are 15, `CLASS7`, `CLASS9` and `CLASS10` are 3.
- In the original therefore only the swimming and floating types (ships, amphibious and the
  water-placed structures) take the x3; in RWE all do. The x3 is applied before the cap of 100,
  so it separates the water types from the rest in the original and, in RWE, mostly just lifts
  every land type toward the cap.
- Every land structure's site search in RWE uses the sparser lattice (margin 6, spacing
  `rand(20)+14` by `rand(3)+14`) where the original used the denser one (margin 3, spacing
  `rand(10)+11` by `rand(3)+11`). The effect is a looser base layout.

A fix is a signed field, or a "key present" bit, read at the two sites. The same field also
feeds the pathfinder and the build-site checks (`pathfinding/AbstractUnitPathFinder.cpp:34`,
`sim/movement.cpp:81`), which rely on the unsigned zero default, so the AI should read a
separate predicate rather than change the field's type.

**The low-wind test: DIFFERS (small).** Reference (`ai-ai-player` section 4): a wind generator
is zero-rated "when `windSpeedMax` is below half `windSpeedDenominator`". RWE (about line 302):
`((minWind + maxWind) / 2) < MaxUtilizableWindSpeed / 4`, which is the mean under 1250. The
register itself says only "the code's low-wind test", and the code marks it `[open]`. The
denominator is the generation factor's divisor, `0x1388` = 5000, the same number as
`MaxUtilizableWindSpeed` (`GameSimulation.h:54`; `TOTALA-EXE-ECONOMY.md` about line 624), so the
original's test is `max < 2500`. Census of 271 `.ota` files (absent keys read as 0): the two
tests agree on 262 maps (206 high-wind, 56 low-wind) and disagree on **9 (3%)**, all of them
maps with a maximum under 2500 and a mean of 1250 or more, where the original calls the map low-
wind and RWE calls it high-wind: `(600, 2000)` on three maps, `(1000, 1500)` and `(1000, 2000)`
on two each, `(500, 2000)` and `(781, 2000)` on one each. On those maps RWE rates a wind
generator as a normal build and the original rates it zero.

## 6. The final rating, the limit test and the weighted draw

Reference (`ai-ai-player` section 4, register section 120): zero below 50 stored energy or 25
stored metal; zero for a failed limit test (`-1` unlimited, else the count must be strictly
below the limit; zero blocks); the storage ceilings of 500 (metal) and 1000 (energy) with
deficits weighted 0.25 and 0.125; +20 for a net below 1; the income tiers (metal 3 and 5,
energy 50 and 200); metal urgency clamped first, energy urgency less the metal share, the
ordinary share the remainder; the weighted sum times the script percentage over 10000, integer
truncation. The draw is a running-total replacement over the builder's option list in menu
order, ignoring nonpositive ratings; a side mismatch after the draw returns no choice, with no
second draw; the draw does not reserve resources or increment the cached count.

RWE (`classicShortage`, `classicFinalRating`, `classicWeightedDraw` in `ClassicAiScoring.cpp`
about 150 to 228; `chooseType` about 469 to 540): every constant, tier and the integer
truncation as above; the draw is `rng.below(total) < r`; the side filter drops the pick.

Status: **MATCHES**. Three points where RWE adds structure:

- **`wantMobile` restriction (UNCLEAR).** RWE scores only the menu entries whose mobility
  equals what the builder can make with an order RWE expresses (a factory makes units, a mobile
  builder structures; `facts[t].mobile != wantMobile` is skipped). The original scores the whole
  option list. On the shipped data a factory's menu is units and a mobile builder's is
  structures, so the restriction removes nothing there. A mod that mixes the two would differ.
- **The menu order** is the build tree's name order, not the order of the GUI pages (marked
  `[open]` in the code). The weighted replacement draw gives each entry a probability
  proportional to its rating whatever the order, so the distribution is the same; only a
  seed-for-seed match with the original would see the order. No gap.
- **`excluded[t]`** stands for the original's "downloadable type in game type 1" refusal.
  Whether `excludedByMission` is the same set is **UNCLEAR**.

## 7. Placement: lattices, the shared radius and the extractor search

Reference (`ai-ai-player` section 5, register section 121): two randomised lattices
(margin 3, spacings 11..20 by 11..13; margin 6, spacings 14..33 by 14..16), each offset by its
spacing minus half the spacing; the first for a negative `MinWaterDepth`, the second for a
nonnegative. A feature-cell list is built once, from every map tile whose feature index is below
`0xfffb`, whose feature value is nonzero and whose flags contain `0x200`, with multi-cell
features contributing several cells. One shared search radius grows by 160 while below the
larger map dimension; the search origin moves from the builder toward the base by at most the
radius; for an extractor, when the mission threshold is less than a draw of 255, the cell list is
searched **at four times the radius**; otherwise (or on failure) a random placement makes at
most 30 attempts, accepting a placeable footprint whose metal is at most twice the surface metal
times the footprint area. Success resets the radius to zero; failure leaves it for the next
builder or type. The extractor search stops once a candidate is beyond the first success's
squared distance plus 160 (a squared-distance window, not a 160-unit ring), taking the best
yield.

RWE (`ClassicAiPlacement.cpp`, `initialise` about 60 to 85, `findSite` about 90 to 240): the
lattice constants and offsets, the radius growth and reset, the origin move, the squared-
distance window of 160, the threshold draw (`surfaceMetal < rng.below(255)`, which is the
reading the register chose over `TOTALA-EXE-AI.md` section 6), the 30 trials and the metal
limit are all there.

Status, per point:

- **The lattices and the threshold draw: MATCHES**, apart from the `MinWaterDepth` sign of rule
  5, which makes the second lattice the only one used (**DIFFERS** through that cause; counted
  once, as rank 1).
- **The extractor search has no radius bound: DIFFERS (new, medium impact).** RWE sorts the
  *whole* cached deposit list by distance from the origin and accepts the first buildable one
  at any distance (about lines 130 to 170). The chapter bounds the cell search at four times the
  shared radius, which is 640 units on the first search of a game and grows by 640 per failed
  search. The register states only "walks the cached resource-feature cells nearest first" and
  has no radius. In RWE the first extractor therefore reaches the nearest deposit on the whole
  map, where the original reaches only deposits near its builder (and near the base, since the
  origin has moved toward it). Early extractor placement and its exposure to raids both depend
  on this. The reading needs one original-game check (does the first Metal Extractor of a
  computer commander go to a deposit more than 640 units away on a large map?).
- **The feature-cell cache: UNCLEAR.** RWE builds one entry per feature whose definition has
  `metal != 0 && indestructible`; the chapter scans tiles, requires flag `0x200`, and counts a
  multi-cell feature once per cell. What `0x200` maps to in RWE's feature definition is not
  stated anywhere in the register; `indestructible` is the code's guess. For a one-cell
  deposit the two agree.
- **The random placement distance: UNCLEAR, as the code already says.** The chapter gives "a
  random radial offset" with no range; RWE draws `rng.below(320 + radius)` (marked `[open]`),
  keeps the cell a margin from the map edge and jitters by one cell. The chapter also says the
  jitter range subtracts margin and footprint with "no local repair" for an oversized footprint;
  RWE clamps the node instead. Neither affects an ordinary footprint.

## 8. Fixed builders and converters (slot 1)

Reference (`ai-ai-player` section 7): a metal converter is switched off when stored energy is at
most twice stored metal; above that, a positive net energy and a nonzero draw of 5 switch it on;
failing both leaves it as it is. Another eligible fixed unit with a nonempty build-option count
and **no primary order list** chooses one product and queues it.

RWE (`runBuildTimer`, about 694 to 735): the same converter policy; an idle fixed builder (`orders`
empty, `buildQueue` empty and the factory in its idle state) draws one type and queues a count
of one.

Status: **MATCHES** for the converter. The builder test is **UNCLEAR**: RWE adds "queue empty
and idle" where the chapter has only "no primary order list". A factory with an order list
that is empty but a queue still running would be skipped in RWE; the original, which also
queues through the order list, would probably do the same, because the queue is its order
list. Not counted.

## 9. Mobile builders (slot 4)

Reference (`ai-ai-player` section 7, register section 121): two passes over group 4 every 90
ticks.

- **First pass.** A unit with build options and no order carrying flag 8 ("work in hand")
  may start work. A `cancapture` unit additionally needs **fewer than five** cached builders and
  `gameTick >= nextAction`. It chooses a product, searches a site and, for a `cancapture`
  unit, **rejects a site** farther than `(mapPixelWidth + mapPixelHeight) / 3` from the base
  (the comparison appears twice). A found site issues mode 14 with count 1.
- **Second pass** (no orders, or flag `0x4000`). A `cancapture` unit takes part **only when at
  least five builders exist**: it moves to the point mirrored through the base from itself
  (the mirrored-base helper, width for both axes) and is queued a patrol back to the base; if
  its base displacement exceeds 640 a random 640-unit direction replaces the displacement.
  Any other builder patrols toward the base; within 320 the vector is extended to 320, and a
  random direction is used if it is closer than 16.

RWE (`runBuilders`, about 824 to 908):

- A unit whose first order is work in hand is left alone (`isWorkOrder`, a stand-in for flag 8).
- For a `cancapture` unit: if `completedBuilders >= 5` **or** `gameTick < nextAction`, **skip it
  entirely** (`continue`); otherwise, if the **builder itself** is farther than
  `(mapW + mapH) / 3` from the base, order it to move to the base and skip.
- Then draw a type and search; on a site, issue the build.
- On no type or no site: a `cancapture` unit gets a move to a random point within 640 of the
  base on each axis plus a queued patrol to another; any other builder patrols to the base if
  it is more than 320 away, else to a random point 320 to 480 from it.

Status: **DIFFERS in three places**, one of them in the register's own wording.

- **The range limit is applied to the builder, not the site: DIFFERS / CONFLICT in reading.**
  The register says "a `cancapture` builder is also kept within `(mapWidth + mapHeight) / 3` of
  the base", which RWE implements as an order home. The chapter says the **chosen site** is
  rejected beyond that range, an effect on where it builds, not where it stands. The two give
  different games: RWE pulls a far-flung constructor back and never lets it build out of
  range; the original lets it stand anywhere and refuses sites. No winner picked; one original
  check settles it.
- **The idle fallback is gated the wrong way round: DIFFERS.** In the chapter a `cancapture` unit
  with nothing to do wanders back via the mirrored move and patrol **only when five or more
  builders exist**; with fewer it does nothing (the first pass is where it works). RWE skips a
  `cancapture` unit at five builders (so it never reaches the fallback with five or more) and
  gives it a random wander when it has fewer and found nothing to build. The mirrored move
  and its width-for-Z quirk are not implemented anywhere.
- **The non-`cancapture` patrol direction: DIFFERS (cosmetic).** The chapter extends the
  displacement toward the base to 320 (random direction only below 16); RWE draws a random angle
  and a distance of 320 to 480.
- The cap of five counts every completed unit with a build menu, factories included, in both.
  That agrees.

Impact: the early game of a classic seat is mostly its commander and a few constructors working
near the start position, where neither the range test nor the fallback often fires, so the
effect is small until the base spreads or the builder count reaches five. Rank 3.

## 10. Assaults and reserves (slots 2, 3, 6, 7)

Reference (`ai-ai-player` section 8, register section 122): minimum 3, maximum 6, attacking
false. Rebalance: an empty assault takes the reserve's first unit; while it has more than one
unit the member farthest from the whole-group centroid is sent back when its squared distance is
at least `limit * size`; then reserve units nearer than that are taken in. The removal loop
subtracts the departing unit's position **after** the swap-removal moved the last element into
the slot, so the running sums can name the replacement (a quirk to keep). Rally: with at most
three members, or before attacking with fewer than six, send the group to the centroid of the
first nonempty of groups 5, 1, 4 with mode 2 and an extra argument of 160, clearing attacking; no
rally point falls through to attack. Once attacking, four or five continue. The target is the
nearest non-allied active player's unit to the assault's centroid, by horizontal squared
distance, skipping dead units, the airborne, flag `0x8000` and **a unit cloaked by activation
bit 4**; it does not consult visibility. Reserves move to the assault's centroid.

RWE (`classicRebalanceAssault` in `ClassicAiGroups.cpp` about 90 to 190; `runAssault`, `runReserve`,
`nearestEnemy` in `ClassicAiController.cpp` about 627 to 822): the swap-removal and the stale
subtraction are reproduced and commented as the source's quirk; the rally order of groups
(defences, buildings, builders = 5, 1, 4) is right; attacking persists; the target is the
smallest horizontal squared distance, first on a tie; reserves go to the assault's centroid.

Status: **MATCHES** for the rebalance, the thresholds and the rally rules, apart from:

- **Cloaked enemies are targets: DIFFERS (small).** `nearestEnemy` skips the dead, the `immune`
  (the `0x8000` stand-in), the carried and the airborne, but not `UnitState::cloaked`. The
  register's "activation-bit filters" and the chapter's explicit "cloak activation bit 4" both
  skip them. A cloaked Spy or Commander (cloak is on a few shipped units) is therefore aimed at
  by RWE's assault group and not by the original's. A group sent at a cloaked unit it cannot
  see has no way to attack it (the attack order only walks toward the last known position).
  Whether the original filter reads the activation bit (the player's switch) or the actual cloak
  state (the unit is cloaked only when it has energy and is still) is **UNCLEAR**; the code field
  `cloaked` is the second.
- **The extra argument of 160 on the rally move: UNCLEAR.** RWE's move order has no such field
  (the code says so). The chapter calls it the "radius argument"; its effect on arrival is not
  stated. Probably the "stop within 160" of a group move; no gap raised.
- **The 30-tick vs 1-tick classification** is in rule 4.

## 11. Aircraft routes (slot 8)

Reference (`ai-ai-player` section 8): with fewer than five aircraft, two or three random points
around the base, the first a replace-mode move and the rest appended patrols; if the base is
exactly zero on both axes, instead take the group's **average position** and patrol toward the
**nearest enemy to it**, with no null check; with five or more, patrol toward a random map edge
(destination Y zero). Nothing is sent at an enemy: the aircraft fight through their ordinary
order handlers.

RWE (`runAir`, about 910 to 1005): the same three branches. The two or three points are a
400-unit draw each way around the base (marked `[open]`), the edge point is a draw along a random
edge kept 64 in from the border (marked `[open]`).

Status: **MATCHES** in structure. The zero-base branch **DIFFERS in a detail**: RWE searches
for the nearest enemy to the point (0, 0) (`nearestEnemy(sim, baseX, baseZ)`), the chapter to
the group's average position. The branch is reached only while the base estimate is still zero,
that is before any of the player's units exist, and the group is empty then; no effect. The
missing null check is correctly turned into a return. The random ranges are RWE's own and cannot
be checked against a chapter that does not give them.

## 12. The spatial probe (slot 9)

Reference (`ai-ai-player` section 9, register section 122): a probe point that restarts at the
best point one time in ten with a random 320-unit step, is scored by the visible-enemy type values
within 160, and replaced when `rand(new) > rand(best)`. The visibility helper reads the player's
byte sight grid when `mapFlags` bit 2 is set, else the **permanent** bit for
`g_game->playerIndex` (the local viewing player, not the AI's, a quirk). **Nothing in the ordinary
classifier ever puts a unit in group 9.**

RWE (about 1050): the timer is re-armed at `30 + rng.below(150)` and nothing else happens, with
the comment that group 9 is empty.

Status: **MATCHES** (both reach the same observable behaviour: the probe has no one to order).
The `g_game->playerIndex` quirk is moot for the same reason, so it is correct that it is not
ported; if a mission ever fills group 9 it becomes a real difference, because the original's
choice of the viewing player's mask would then change what the AI sees in a network game. Recorded
so a future mission-script port knows to read that line rather than the register's "explored".

## 13. Being attacked

Reference (`ai-ai-player` section 11, register section 122): a `cancapture` victim owned by an
active computer player postpones the manager's `nextAction` to `tick + 30 + rand(300)`, "30..329"
and deletes its orders (the register: "clears replaceable orders"). The rest (the retaliation
attack, weapon re-targeting, the under-attack speech) is unit-level and applies to humans too.

RWE (`noticeAttacks`, about 659 to 692): every tick, for each completed `cancapture` unit of this
player, compare `hitPoints` with the value remembered last tick; on a drop, set
`nextAction = gameTime + 30 + rng.below(300)` and, if the unit has orders and none of them is a work
order, issue a Stop.

Status: **MATCHES** in effect on the postponement and its range. Two details:

- **Detection is by a hit-point drop between ticks**, not by the damage event. A unit that is
  damaged and repaired in the same tick, or damaged by exactly zero, is not noticed; a unit that
  loses hit points to something that is not damage (a transfer) would be. The original reacts
  to every damage call, including a zero or a fully absorbed hit. Not worth a fix on its own;
  the right fix is a hook at the damage site, which also covers rule 13's unit-level reaction.
- **"Deletes its orders" vs "Stop only if none is work in hand".** The register's wording
  ("replaceable orders") is what RWE does; the chapter's "deletes its orders" is
  unconditional. Compatible only if the original's order-flag filter on delete is what
  "replaceable" means; **UNCLEAR**, recorded as wording.
- The unit-level reaction (the retaliation attack, `RetargetWeapons`) is `BT-UNITS-UNIT-TARGETS-PARITY.md`
  and `BT-74` and is not repeated.

## 14. The script directives, limits and the missing-argument default

Reference (`ai-ai-player` section 3, register section 123): three directives, `plan`, `weight`,
`limit`, registered under mask 8. `plan` first disables the other two, then re-enables them for
a matching `easy`, `medium`, `hard` or `any`; the `any` test **repeatedly reads argument 1**
whichever difficulty it is on; no arguments leaves them off. `weight` reads a type or category
set and a float **defaulting to zero**, applied to every player with an AI object; `limit` reads
the same set and an integer **defaulting to zero**, applied only to active type-2 players. A name
is a one-type set that locks (lock 1); a category is a mask that does not (lock 0). Scaling
truncates and clamps to 0..100 unless locked. A limit of -1 is unlimited; **any other negative
limit blocks everything** (the test is "count strictly below the limit"), and zero blocks.

RWE (`ClassicAiScript.cpp`, `parseClassicScript` 127 to 172, `applyClassicScript` 287 to 369): the
lock semantics, the clamp and the plan gate are as above; `any` is matched in **any** argument
position; a directive with **fewer than two arguments is ignored**; a negative limit becomes
-1 (`limitFrom`, about line 113: `!(value > -1.0)` returns -1).

Status: **MATCHES** for the locks, the clamp, the gate and the per-seat scoping. Three
differences:

- **A missing argument is ignored in RWE, zero in the original: DIFFERS (small, shipped).** The
  chapter's `limit` and `weight` default the number to zero when the line ends early. One shipped
  line has that shape: `Limit ARMSABO` (no count, in the `missions` profile). The original
  would set that type's limit to **zero** (never built); RWE skips the line, so the type stays
  unlimited. Fix: take the number as 0 when there is a subject and no number.
- **A negative limit other than -1: DIFFERS.** RWE maps every negative to unlimited; the original
  keeps it negative and so blocks the type. No shipped profile writes a negative limit (the census
  of all `ai/*.txt` lines found none), so this is latent.
- **`plan` with `any` not in position 1: UNCLEAR, latent.** RWE accepts `any` anywhere in the
  list; the chapter reads only argument 1 for it. A shipped line is always `plan easy`, `plan
  medium` or `plan hard` alone, so the two agree on the data.

The first listed difference changes a shipped file's meaning; whether the lock and the weight
arithmetic matter is carried by the register already (section 123).

## 15. The profile tokenizer and what the shipped profiles contain

Reference (`ai-ai-profile`): a parser with a 128-byte token buffer, a sticky error flag and a
pending-token flag. The scanner skips whitespace; **punctuation is classified before numbers
and identifiers**, so a minus and a leading period are their own punctuation tokens; an identifier
is alphanumeric characters only and must be 1 to 126 long; a number starts with a digit and
continues with digits or periods; `ReadInt` requires a number token and converts with `atoi`
(truncating at the first non-digit), and any other token type sets the sticky error and returns 0;
`TokenEquals` is case-insensitive. Once the error flag is set every read returns the error
sentinel or 0 until the buffer is reset. The module does not say how the three directives read
their *float* argument, nor whether the sticky error spans the rest of one line or the rest of
the file.

RWE: not a tokenizer. `parseClassicScript` splits each line on whitespace and `;`, upper-cases it,
strips `//` and `#` comments, and keeps `plan`, `weight` and `limit` lines;
`classicParseNumber` reads a number with an optional sign, a decimal point (a leading `.` is fine)
and an exponent.

Status: **UNCLEAR, and worth one original-game check.** The chapter says the tokenizer is
"consumed" by the AI player, but the AI-player chapter reads the directive arguments through the
command interpreter's own selector and float readers, and neither chapter says the profile files
are read through `AIProfileParser`. The profile tokenizer might only serve the mission and
selector-integer paths. If the profiles *are* tokenized as described, shipped files contain
constructs it would mis-read:

- **96 `weight` lines** in the shipped profiles write the multiplier with a leading period (`.1`,
  `.5`). A punctuation-first scanner returns `.` then `1`; whether the float reader accepts that
  depends on a reader the chapters do not describe. RWE reads `.1` as 0.1. If the original read
  these as 0 or 1 the whole weighting of the shipped profiles would be different from what the
  register and RWE assume.
- **Three malformed shipped lines** (one archive's copy of `missions.txt` and `krogoth.txt`):
  `Limit ARMSABO` (no count), `Limit ARM DECOM 4` (three arguments after the directive; the
  profile `krogoth` is used by one map) and `Limit CORFORT O` (a letter O for a count).
  RWE reads them as (ignored), "category `ARM`, number `DECOM` = 0, so every unlocked `ARM` type
  limited to 0 and `4` unread" and "name `CORFORT`, number `O` = 0, so `CORFORT` limited to
  0". Whether the original does the same depends on whether the selector reader takes a
  multi-word selection (`ARM DECOM`) and whether a sticky error discards the command. One
  original check settles all three.
- **Unit names are alphanumeric**, so the identifier rule costs nothing on shipped data; a mod
  that writes an underscore in a type name would split in the original and not in RWE.

The profile *selection* (the OTA `aiprofile` key, default `DEFAULT`, then `ai/default.txt`) is
register section 123. The census of `aiprofile` over 271 maps: `default` 110, `missions` 58,
`seabattle` 25, `hover` 21, `airbattle` 19, none 14, `waterwrld` 10, `acid` 7, `metal` 3, `urban`
3, `air` 1, `krogoth` 1. Only the `krogoth` map is affected by the two malformed lines in that file.

## 16. Difficulty and the handicap

Reference (register section 123, corroborated by `ai-ai-player`): the resource handicap on a
computer player is 0.5 on easy, 0.7 on medium and 1.0 on hard; the script's `plan` selects the
same three names.

RWE (`ClassicAiController` constructor, about 1060): easy 0.5, standard 0.7, anything else 1.0;
`config.difficulty` selects the plan.

Status: **MATCHES**. The handicap is also applied wherever refunds use the 0.5 and 0.7
factors (`BT-INGAME-CONTROL-PANEL-PARITY.md`, Batch 5).

## 17. Gaps to fix

Ranked by how much of a classic game each changes.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| --- | --- | --- | --- |
| 1 | `MinWaterDepth` is read as unsigned with default 0, so `>= 0` is always true: every type takes the x3 and the second placement lattice (rule 5). The original defaults to -10000 (BT-8), so 76% of shipped FBIs and the moveinfo classes 0 to 5 and 11 to 14 take neither. Fix by a "key present and nonnegative" predicate for the AI only. | `ai/classic/ClassicAiController.cpp` (about 209), `ai/classic/ClassicAiPlacement.cpp` (about 198), `io/fbi/io.cpp`, `io/moveinfotdf/io.cpp` (a presence flag) | No (BT-8 resolved) |
| 2 | The extractor search has no radius bound; the original searches the feature cells at four times the shared radius (rule 7), so RWE's first extractor reaches the nearest deposit on the map. | `ai/classic/ClassicAiPlacement.cpp` (about 130 to 170) | Yes (distance of a computer commander's first extractor on a large map) |
| 3 | Mobile builders (rule 9): the range limit is applied to the builder rather than the chosen site; the idle fallback is gated by a count of five the wrong way round (the mirrored move is missing); the non-`cancapture` patrol direction is random where the original extends the displacement. | `ai/classic/ClassicAiController.cpp` (about 824 to 908) | Yes for the range reading (register versus chapter) |
| 4 | A missing number on a `limit` or `weight` line is ignored where the original defaults to zero; `Limit ARMSABO` is a shipped case (rule 14). A negative limit other than -1 becomes unlimited. | `ai/classic/ClassicAiScript.cpp` (about 308 and 113) | No for the default; the negative is latent |
| 5 | The assault target search does not skip cloaked enemies (rule 10). | `ai/classic/ClassicAiController.cpp` (about 627) | Yes (activation bit or actual cloak) |
| 6 | The low-wind test uses the mean under 1250 where the original reads the maximum under half the denominator; 9 of 271 maps disagree (rule 5). | `ai/classic/ClassicAiController.cpp` (about 302) | No (the denominator is 5000; the comparison on the maximum is the chapter's only claim) |
| 7 | Whether the shipped profiles are read through the tokenizer, and so what `.1` and the three malformed lines mean (rule 15). | `ai/classic/ClassicAiScript.cpp` | Yes (play a computer player with `krogoth` and with `missions`) |
| 8 | Stand-ins whose original predicate is not decoded: "armed", "work in hand" (flag 8), "immune" (`0x8000`), "has a build menu", the feature flag `0x200`, "downloadable" (rules 3, 4, 7, 8). | `ai/classic/ClassicAiController.cpp`, `ClassicAiPlacement.cpp` | Yes, one predicate at a time |
| 9 | Attack detection is by a hit-point drop, not the damage event (rule 13); the zero-base air branch uses (0, 0) rather than the group average (rule 11). | `ai/classic/ClassicAiController.cpp` | No |

### Documented divergences this audit leaves alone

One classic seat per controller, so the original's "`weight` applies to every player with an AI
object and `limit` only to active type-2 players" has no counterpart and needs none; the
command-fire and per-weapon re-targeting paths, which are unit behaviour shared with human
players; the debug dump of the whole AI object and its unterminated format string; the
memory-protection wrappers; the `[open]` random ranges the code already labels (the aircraft
points, the placement distance, the wander); RWE's own computer player.

### Conflicts with existing RWE findings, in one place

1. **Where the mobile builder's range limit applies.** Register (`TOTALA-EXE-COMPUTER-PLAYER.md`
   section 121): "a `cancapture` builder is also kept within `(mapWidth + mapHeight) / 3` of the
   base." Chapter: the **chosen site** is rejected beyond that range, and the comparison is in
   the product-choosing path. RWE follows the register's wording by ordering the builder home.
   Open.
2. **The extractor search radius.** Register: the cached resource cells are walked nearest first,
   with no range stated. Chapter: the search is at four times the shared radius. RWE follows the
   register (no bound). Open; the chapter adds a number the register lacks rather than
   contradicting it.
3. **The low-wind test.** Register: "the code's low-wind test", unstated. Chapter: the maximum
   under half the denominator. RWE: the mean under a quarter of `MaxUtilizableWindSpeed`,
   marked `[open]`. Open.
4. **`MinWaterDepth`'s default.** Not a source conflict (BT-8 is resolved in the register's
   favour of -10000), but RWE's classic engine contradicts the resolved row: the comparison
   `>= 0` it ports is always true on the unsigned field it reads. Recorded so BT-8 is not
   read as already fixed in the code.
5. **A stale comment.** `ClassicAiPlacement.cpp` (about line 195) says "RWE reads an absent key
   as 0, so this is always true of a loaded type"; that is the bug of gap 1 written as a
   remark, and the comment should go with the fix.
