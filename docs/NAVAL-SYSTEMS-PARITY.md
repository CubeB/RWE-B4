# Naval systems parity audit

Spec-room finding, 2026-10-16. Like `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md`, it
allocates no section number in the main register. This is the finding that
implementation work cites for naval behaviour, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** The prose document `docs/naval-systems.md` in the TA: Byte Tactics
  reference tree, at commit `97d7ac59b460417bcdfa629cf90da99f31818c65`
  (worktree path `D:\bytetactics\.worktrees\naval-reference\docs\naval-systems.md`).
  Nothing else under that tree was opened for this audit: no source, no
  decompilation. Addresses below are the ones that prose document names, and the
  rules are restated in prose, not copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed, and its own limits section says so. It is weaker than
  `TOTALA-EXE*.md`, which were read from the retail executable, and the two
  disagree in a few places below; each is called out as a CONFLICT and left open
  rather than settled.
- **One executed observation outranks both.** The owner's 2026-10-16 play of
  retail Total Annihilation, recorded verbatim in
  `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md` ("Original-game observation: submarine
  under construction"): while a submarine is being built its top pokes out of the
  water above the shipyard, and on completion it snaps to the sea floor, still
  directly under the pad. By eye, not instrumented: no faction, depth, tick or
  height was recorded.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/game`,
  `src/rwe/cob` and `shaders/` at the working tree of 2026-10-16 (branch
  `ai-envoy-unload-commander-flee`). Nothing was built, run or changed. Tests
  were not run; a MATCHES below means the code reads as the rule says.

Status words: **MATCHES** (the code reads as the rule), **DIFFERS** (it does
something else), **MISSING** (no implementation found), **UNCLEAR** (cannot be
settled from the reference and the code alone). **CONFLICT** marks a place where
the reference disagrees with an existing RWE finding; both are stated and
neither is chosen.

## Index

1. Attached unfinished product height
2. Height update branches
3. Underwater speed cap
4. Blocked-cell speed clamp
5. Weapon reachability near water
6. Coordinate targets
7. Sea-level collision, water art, entry splash
8. Torpedo-like motion and air-dropped water weapons
9. Area damage
10. Sensor geometry
11. Underwater model visibility and the composite water pass
12. Transport load predicate
13. Repair reach across water
14. Hazardous water
15. `setSFXoccupy`
16. Script effect IDs, wakes and bubbles
17. Factory placement retry and creation failure
18. Unfinished-unit decay and cancellation
19. Construction recolour regimes
20. Moving features sinking
21. Gaps to fix

---

## 1. Attached unfinished product height

**Reference (section 6.1, 7.5; and the construction findings).** A factory
product is created, attached to the build piece and follows that piece in all
three coordinates. A product carrying the raw FBI `Floater` flag has the piece
height limited from below by sea level minus its `WaterLine`; nothing is pushed
down. A non-floater, which includes the submarine, gets no such floor. On
completion the product is detached, and ordinary height placement then applies:
an `Upright` unit without `CanHover` takes the terrain height under its centre,
which for a submarine is the sea floor. The reference's static reading of the
mover went further and said placement still ran every tick while attached and put
the sub on the sea floor throughout. **The owner's observation refutes that for
the construction phase** and agrees with the other half (sea floor on completion).
The reference's "ARM shipyard slip piece is 8.75 units below the factory root,
CORE's is at the root" is a rest-pose offset only; script animation moves it.

**RWE.**

- Unfinished product, factory branch: `src/rwe/sim/UnitBehaviorService.cpp:5396-5410`.
  A floater or hover product is raised to at least sea level (`:5396-5399`);
  any other mobile, non-flying product, including a submarine, has its height
  set to the terrain under the build piece (`:5400-5410`) on every tick it is
  being built. The comment there records a play-test that disliked the sub
  floating at the pad. That play-test disagrees with the owner's later
  observation. **DIFFERS** for non-floaters, as the construction findings already
  say.
- Creation: `src/rwe/sim/GameSimulation.cpp:1698-1712` puts a new mobile
  non-flying unit on the terrain height at creation, citing the original's
  placement at creation. The reference agrees creation runs placement, and the
  observation does not say what the first tick looks like. A fix that only
  changes the per-tick branch can leave a one-tick seabed appearance before the
  first attach. **UNCLEAR** (needs the observation to be tightened, or accepted
  as one tick).
- Completion: `UnitBehaviorService.cpp:5367-5394` hands the finished unit its
  inherited orders and sends the yard idle; it sets no height. See rule 2:
  RWE re-snaps ground height only while a unit is moving, so a sub left at the
  pad height by a corrected construction branch would stay there when it
  completes unless the completion step snaps it.
- Floor for floaters: RWE floors at sea level and not at sea minus waterline.
  Shipped data cannot tell the two apart (see `TOTALA-EXE-WEAPONS.md` section 12,
  waterline: the shipped `Floater` and `WaterLine` sets do not overlap), so this
  is MATCHES on shipped data. It is **DIFFERS** for hover products: RWE floors a
  hover product too, and the reference gives the attached floor to `Floater`
  only. No shipped yard builds a hover product over water that matters.

**Remaining uncertainty.** Which faction, depth, and how long the snap takes
(same tick or later) are not known. The shadow of an attached sub is drawn from
the ground in RWE (the comment at `:5402-5409` says the original projects it
from the ground); whether that still looks right at pad height is untested.
The reference's `0x43D6D0` / `0x48A870` reading of why the observation is true
is a hypothesis only.

## 2. Height update branches

**Reference (7.5).** The snap helper puts a hover unit at the higher of ground
and sea minus waterline, everything else on the ground. The periodic update runs
when the unit is flagged dirty or can hover, and while in a mobile movement
state; `Upright` selects between a hover floor (hover units) and plain ground
height; a floater that is not upright sits at sea minus waterline; the rest take
terrain alignment. Position setting marks the height dirty even inside one cell.

**RWE.** `UnitBehaviorService.cpp:1892-1941` sets ground height whenever a unit
**moves** (speed above zero) and floors floaters and hover units at sea level.
`:1869-1890` handles tilt, with `upright` meaning no tilt and floaters sampled at
sea level. A unit that is standing still is not re-snapped. Floater floor and
upright semantics: **MATCHES** on shipped data, as in rule 1. The gating
difference (every tick when dirty, against only when moving): **DIFFERS**, with
no player-visible case on shipped maps because ground does not move under a
standing unit, but it is the reason the completion step in rule 1 needs an
explicit snap.

**Remaining uncertainty.** None for shipped data; mods with `WaterLine` floaters
are untested.

## 3. Underwater speed cap

**Reference (7.2).** The speed cap is scaled by the pitch table, and halved when
the unit origin is below sea level and the unit is neither a floater nor a hover
unit.

**RWE.** `src/rwe/sim/UnitBehaviorService_util.cpp:811-848` halves the cap when
`position.y` is below sea level, with no floater or hover exemption, and uses a
rise-over-one-tile slope factor in place of the pitch table
(`TOTALA-EXE-MOVEMENT.md` already records the unported pitch table and the
`canhover`/`floater` exemption at the field table). Floaters and hover units are
held at or above sea level by the height code, so the missing exemption never
fires: **MATCHES in effect** for shipped data, **DIFFERS** structurally. A
`WaterLine` floater built by a mod would be halved.

**Remaining uncertainty.** Whole-unit versus fractional comparison of the origin:
the original compares the integer part, RWE the fixed-point value. An origin of
19.5 against a sea of 20 is "below" in both; an origin of 20.5 is above sea in
both. The only case that differs is an origin strictly between sea and sea plus
one, which is above in both; no case found.

## 4. Blocked-cell speed clamp

**Reference (7.4).** When the footprint test blocks a move, the unit is clamped
inside its current cell and, if its speed exceeds half the type maximum, the
speed is capped at half and velocity recomputed.

**RWE. MISSING.** `UnitBehaviorService.cpp:1892-1941`: on a blocked move RWE
sets a collision flag and tries each axis separately; speed is not changed. The
original's `0x43D9A2` half-speed is already in the field table of
`TOTALA-EXE-MOVEMENT.md` and is not ported. At a crowded shipyard mouth the
visible result is ships that keep pressing at full speed. A change moves unit
positions and so needs the hash oracle (`rwe-hash-oracle-baseline`).

**Remaining uncertainty.** Whether the clamp touches stored speed or only the
velocity used that tick; the reference says it recomputes velocity.

## 5. Weapon reachability near water

**Reference (10.2, 10.3, 10.4).** An ordinary (non-water) weapon is rejected
when either the shooter's or the target's model top is at or below sea level;
this uses whole-unit origin plus whole-unit model height compared strictly
above sea (the construction findings fixed this as whole units). Then the
air-only restriction (against the unit's runtime air state, not the FBI flag),
an optional ballistic solution test, and flat range. A water weapon takes a
separate branch: a target that is not a floater is rejected if its origin is
above sea level, and a hover target is additionally rejected if its origin plus
half its model-top height is above sea level; if those pass, flat range decides.
That branch returns before the air and ballistic checks.

**RWE. MATCHES.** `src/rwe/sim/GameSimulation_visibility.cpp:361-429`
(`weaponCanHitUnit`) is built in the reference's order: water-weapon branch with
the floater exemption and the half-height hover rule at `:371-389`, then the
shooter/target strictly-above-sea gate at `:396-399` (through
`modelTopIsAboveSea`, `:347-359`, whole units, equality rejects), the air-only
rule at `:405`, and the ballistic arc test at `:417-426`. The D-gun and point
attacks go through the same gate (`UnitBehaviorService.cpp:4838-4851`, per
`NAVAL-CONSTRUCTION-DGUN-FINDINGS.md`).

**Small difference.** The air-only test uses RWE's flying physics state rather
than the original's low movement-state bits equal to the air state. Equal on
shipped data; **UNCLEAR** for a landed aircraft.

**Remaining uncertainty.** Candidate selection ordering (50 candidates, the
randomised preferred/fallback split, section 10.5) was not audited here; it is
the subject of `TOTALA-EXE.md` section 17a and the candidate-list notes in `TOTALA-EXE-VISION.md`.

## 6. Coordinate targets

**Reference (10.6).** A coordinate target takes height max(ground, sea level).
The position reachability routine checks flat range first, then returns success
for water weapons without any floater or hover classification. For a non-water
weapon the shooter's dry gate applies and nothing else.

**RWE.** `weaponCanFireFrom` (`GameSimulation_visibility.cpp:342-345`) is
exactly that: a water weapon, or a shooter whose model top is above sea.
**MATCHES.** The coordinate's height: the order carries a vector chosen by the
interface layer (`UnitBehaviorService.cpp:5574-5599` returns it unchanged), and
whether the click resolves to max(ground, sea) was not traced: **UNCLEAR**.

## 7. Sea-level collision, water art, entry splash

**Reference (11.6, 12.2, 12.3).**

- A projectile below sea level over a wet cell detonates unless it is a water
  weapon; at exactly sea level it does not trigger. A map option suppresses the
  sea trigger. `unitsonly` returns before the feature, terrain and sea tests.
- Water-impact art: the detonation classifies the cell as water from its terrain
  height. **If it is water and no unit target was supplied, the water sound and
  water explosion are used; with a direct unit hit, the ordinary hit sound and
  ordinary explosion are used.**
- Water-entry splash (11.6): a still-live projectile that crosses from strictly
  above sea level to at or below it, over an actual water cell on a map that
  allows sea-level effects, requests the weapon's splash. This is separate from
  and earlier than any underwater impact.

**RWE.**

- Sea trigger, strictly-below rule, water-weapon exemption, map flag,
  `unitsonly` ordering: **MATCHES** (`src/rwe/sim/GameSimulation.cpp:3185-3198`,
  `:3173-3183`, `:3029-3090`).
- Water art: `GameSimulation.cpp:4146-4150` picks water impact whenever the
  impact square is under the sea, **whatever the round struck**; the comment cites
  `0x499ECF` and says a hit on a wading unit in a wet square splashes.
  **CONFLICT.** The reference says a direct unit hit takes ordinary art. The art
  only changes sound and sprite; no damage depends on it. The square test itself
  (highest of four corners below sea, `MapTerrain.cpp:233-251`) matches the
  reference's "from its terrain height relative to sea level".
- Entry splash: **MISSING.** No code was found that looks at a projectile
  crossing the surface in flight; the only water art comes at detonation
  (`GameScene_audio.cpp:607-618`, `GameScene_util.cpp:73-76`). The effect is
  visible on a `noexplode` round or a water weapon that passes the surface and
  continues.

**Remaining uncertainty.** The conflicting art rule needs an original check (a
unit hit over shallow water) before either side is believed. The "upper bound
only" first unit slot, noted in the construction findings as not implemented, is
unchanged.

## 8. Torpedo-like motion and air-dropped water weapons

**Reference (11.4, 15.3).** A self-propelled water weapon at or above sea level
falls and has its pitch reset; once below the surface it follows the propelled
branch (acceleration to maximum speed, optional guidance, turn limits). A
torpedo dropped from an aircraft therefore falls into the water, may produce an
entry splash, then runs. The reference names this as dependent on the weapon's
flags and values.

**RWE. DIFFERS (approximation).** `UnitBehaviorService.cpp:1511-1514` moves the
launch point to at most a quarter unit above sea level, and `:1593-1598` forces
the direction level. A torpedo from a ship or an aircraft therefore always starts
running at the surface at once; there is no fall and no later phase. For ship
launches this is plausibly the same; for aircraft launches it skips the fall and
the splash.

**Remaining uncertainty.** Unverified against retail; the reference marks the
whole description as flag-dependent.

## 9. Area damage

**Reference (12.4; findings).** Area damage uses the distance from the blast to
the unit's axis-aligned collision box (including height), reduced to whole
units; effective radius is half the loaded area of effect; strict less-than; the
falloff is `(1 - distance/radius)^2` with the edge-effectiveness floor; the firer
alone is excluded; a direct hit on a small-AoE weapon takes the direct path. No
water filter and no ally filter.

**RWE. MATCHES.** `GameSimulation.cpp:3697-3714` (`blastDamageScaleForUnit`: floor
the distance, strict radius), `:3717-3908` (box distance, one firer exempt, ally
damage allowed, flying units after the grid), and
`LoadingScene_util.cpp:267` (radius is half the area of effect). Already recorded
in `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md` "Changed".

**Remaining uncertainty.** None new.

## 10. Sensor geometry

**Reference (9.2, 9.3).** Detection skips empty units, the viewer's own units and
units flagged as excluded from detection. Sonar marks a target whose origin is at
or below sea level and whose horizontal distance is **strictly within** sonar
range. Radar marks a target whose model top reaches or exceeds sea level and
whose horizontal distance is **strictly within** radar range, where the radar
threshold includes a lift from the detector's whole Y, and the enumeration radius
is the larger of radar and sonar. Jammers run after detection.

**RWE.** `GameSimulation_visibility.cpp:579-716`. The predicates are the
reference's: sonar on origin at or below sea (`:689`), radar on model top at or
above sea (`:688`), the radar lift capped by max(radar, sonar) (`:602-606`), the
exclusion flag (`:677`), jammers after detection (`:694-701`). **MATCHES** except
for the boundaries.

**CONFLICT, boundaries.** RWE tests range with "less than or equal" for both radar
and sonar (`:41`, `:204`). `TOTALA-EXE-VISION.md` (visibility pass, `0x467440`
section) records radar as strict (`<`) and sonar as inclusive (`<=`), read from
the executable. The reference says strict for both. RWE is therefore inclusive
where the executable-derived finding says strict for radar; the executable-derived
sonar reading and the reference disagree. Differences arise only for a target at
exactly the integer range, which the shipped geometry practically never produces.

**UNCLEAR.** The original compares whole-unit heights; RWE uses fixed point
(`:687-689`). A fractional origin height differs only within one unit of sea level.

## 11. Underwater model visibility and the composite water pass

**Reference (9.4, 6.3).** The model-visibility predicate accepts the viewer's
own units, rejects cloaked units, and **without the sonar flag rejects a model
whose top is strictly below sea level** before the fog samples. With sonar it
carries on to explored/current-sight sampling at several model corners. Separately,
in the renderer, after a shipyard's children are composed, if the parent origin
lies below sea level, below-threshold pixels are either cut (transparent) for an
enemy owner without sonar, or remapped through the blue palette for the local
owner or a sonar-flagged unit. The comparison with the threshold is inclusive.

**RWE.**

- Visibility predicate: **MATCHES.** `GameSimulation_visibility.cpp:213-259`
  (own, cloak, the below-sea veto lifted by a sonar contact, then fog), with the
  display predicate repeating it at `src/rwe/game/GameScene_commands.cpp:706-740`.
  The reference's four-corner sampling is the known gap already recorded in the
  code comment at `:256-258` (RWE asks the fog once).
- Blue tint: every fragment below sea level is tinted by one factor
  (`shaders/unitTexture.frag:46,126`, `shaders/unitBuild.frag:40,70`). It does not
  distinguish owner or sonar.
- Pixel cut for an enemy without sonar: **MISSING.** A partly submerged enemy
  model that the viewer can see (because its top is above sea) is drawn in full
  with the lower part tinted, where the reference cuts the below-threshold
  pixels. Cosmetic only; targeting is unaffected.

**Remaining uncertainty.** The reference derives the cut from the composite's
parent origin being below sea level; a shipyard root sits at sea level minus its
`WaterLine` (1 for the ARM yard, 22 for the CORE yard), so whether the cut hides
an enemy yard's own underwater part is a prediction that was never observed. It
needs an original check before anything is built.

## 12. Transport load predicate

**Reference (13.3).** Rejected for: forbidden-transport flag, no load permission,
capacity count reached, the target's movement object missing, footprint width over
the carrier's capacity field, runtime air mode; for a **non-flying carrier a
target with nonnegative minimum water depth**; any target whose model top is at or
below sea level; any target not exactly complete.

**RWE.** `src/rwe/sim/GameSimulation.cpp:800-854`. All of it is present and
ordered as the finding in `TOTALA-EXE-TRANSPORTS.md` has it. The top-below-sea
test is at or below (`:843`) and the build test last (`:848`): **MATCHES**. One
difference: the depth rejection is `minWaterDepth > 0` (`:835`) where the
reference and the executable finding say the target is rejected when
`minwaterdepth >= 0`. RWE stores the depth as an unsigned integer
(`src/rwe/sim/MovementClassDefinition.h:12`), so the signed distinction between
"zero" and "negative" is gone at load. **DIFFERS** for a unit whose movement
class names a minimum depth of exactly zero. Whether any shipped unit does is not
known.

**Remaining uncertainty.** A one-off read of the shipped movement classes (the
`hpi.py` tool) settles it.

## 13. Repair reach across water

**Reference (13.1).** Repair requires a target, repair permission, a target below
maximum health and not in the air state. For a flying repairer without the
amphibious flag, a target whose model top is strictly below sea level is
rejected. For a non-flying repairer, the target's model top must not be below sea
level minus the repairer's maximum water depth.

**RWE. MISSING.** `UnitBehaviorService.cpp:4690-4702` gates repair on the reclaim
key and the target's health only. No search under `src/rwe/sim` found a model-top
test for repair. The user-visible effect: a construction aircraft can be told to
repair a submerged submarine, and a ship repairs targets at any depth. The game
layer's command validation was not read, so a block there is possible.

**Remaining uncertainty.** Whether shipped construction ships and aircraft ever
meet a case where this matters; unverified against retail.

## 14. Hazardous water

**Reference (13.5).** Once every 30 ticks, when map water damage is non-zero,
for each unit whose origin is at or below sea level and which lacks the
**`canhover`** bit (not `floater`), apply the damage, subject to ownership and
authority conditions.

**RWE. MATCHES.** `GameSimulation.cpp:3424-3468`: thirty-tick boundary, the
integer part of the origin, hover exemption only, damage applied after the scan.
Already covered by `TOTALA-EXE-WRECKS.md` (cause 11).

## 15. `setSFXoccupy`

**Reference (8.2).** For units in the qualifying movement states: origin above
sea level gives 4. Otherwise: origin fewer than five whole units below sea level
gives 1; origin plus draft equal to sea level gives 2 (overriding); **origin plus
model-top height strictly below sea level gives 3** (overriding). Outside the
qualifying movement states the value is 0. If no underwater test fires, the
previous value is kept. The script is told only when the value changes.

**RWE.**

- Values 4, 1, 2, 0 and the "previous value kept" quirk: **MATCHES**
  (`src/rwe/sim/UnitBehaviorService_util.cpp:29-70`).
- State 3: **MISSING.** The function's own comment (`:62-67`) says it was left out
  because the unit definition then carried no model height. `modelHeightOf` has
  since been added (`GameSimulation_visibility.cpp:490`, header `:1522`), so the
  data exists now. A fully submerged submarine reports 2 (or 1) where both the
  reference and `TOTALA-EXE-WEAPONS.md` section 12 say 3.
- When it fires: RWE calls the script only from the movement path
  (`UnitBehaviorService.cpp:2052-2066`, inside the position-apply function).
  The reference describes the original as calling it whenever the occupancy
  changes; `TOTALA-EXE-MOVEMENT.md` puts the routine in the per-tick mover. A
  stationary unit, such as a ship spawned and never moved, never gets its initial
  state. **DIFFERS** (probable; the trigger point in the original is the mover,
  which runs every tick for a mobile unit).

**CONFLICT, which field.** `TOTALA-EXE-WEAPONS.md` names the state-3 height as an
unidentified definition word at `+0x170` ("behaves like a hull height"), while
`TOTALA-EXE-VISION.md` uses `+0x16E` for the model height in the radar test. The
reference calls it model-top height without giving an offset. Which of the two
RWE's `modelHeightOf` corresponds to for state 3 is **UNCLEAR**.

## 16. Script effect IDs, wakes and bubbles

**Reference (8.4-8.6).** IDs 0 and 1 thrust-style particles; 2, 3 wake with period
16 and 8; 4, 5 the same with endpoints reversed; `0x101` white smoke; `0x102`
black smoke; `0x103` bubble helper from the piece point towards the same X/Z at
sea level, built from the same wake class with the descending palette direction.
Effects are skipped unless the local player can see the unit. A wake dot is
jittered by up to three whole units on each axis, steps about half a unit per
tick, lives six times its period, cycles palette entries `0x61` to `0x67`, and
expires once the terrain under it is no longer strictly below sea level.

**RWE. MATCHES.** The numbering (`src/rwe/cob/CobSfxType.h`, with `257`/`258`
white/black and `259` bubbles), the dispatch (`src/rwe/sim/cob.cpp:249-279`), the
visibility refusal, the three-unit integer jitter, the period 16 and 8, the
bubble endpoint at sea level (`src/rwe/game/GameScene_commands.cpp:1636-1674`,
`:2006-2091`) and the shore expiry (`src/rwe/game/GameScene_util.cpp:1480-1483`)
all read as the reference. The two-dots-per-call detail is RWE's reading from the
executable (`TOTALA-EXE.md` section 4) and the reference's "emits immediately and
the next emission advances one tick" is compatible with it. IDs 0 and 1 are named
`Vtol` and `Thrust` in RWE; the reference's "first and second timing variants"
cannot be told apart from that and need no change.

**Remaining uncertainty.** The 400-system cap on particle-system lists
(reference 8.6) was not audited; cosmetic.

## 17. Factory placement retry and creation failure

**Reference (4.2).** A failed footprint test at the production position retries
after **15 ticks**. A failed allocation or creation waits **300 ticks** and prints
"Unable to create any more units".

**RWE.** The 15-tick retry: **MATCHES** (`GameSimulation.cpp:52` and `:4920-4934`).
The 300-tick failure: **MISSING**; RWE's creation does not fail for a factory
product (`GameSimulation.cpp:4920`), and a resurrection that fails is dropped
(`:4986-4995`). The construction findings already record this as not implemented.
Not reachable in an ordinary game: allocation fails only at the unit cap.

## 18. Unfinished-unit decay and cancellation

**Reference (4.6).** An unfinished unit's `GetBuilt` order waits **300 ticks**,
then uses **30-tick** waits and a conditional **11-tick** decay step; decay calls
the progress routine with negative work, raises the remaining fraction, removes
health and credits metal; at fully unbuilt the unit is destroyed. Cancellation
refunds invested metal, with no energy refund; some computer-player difficulty
branches scale it.

**RWE. MATCHES.** `GameSimulation.cpp:1381-1472` with the constants 330 first
check (300 plus the first 30), 30 re-check and 11 decay at
`GameSimulation.h:849-862`; the cancellation refund is metal only and truncated
(`UnitBehaviorService.cpp:5460-5474`). The computer-player difficulty scaling of
the refund is not modelled and is not a naval rule.

## 19. Construction recolour regimes

**Reference (5.3, 5.4).** The remaining fraction times 255, truncated, selects
five regimes at 236-255, 201-235, 116-200, 31-115 and 0-30. Two palette indices
within `0xA0`-`0xAF` animate on game ticks with a per-unit phase, so the colours
pulse while paid progress stalls. The band is four shade levels wide (lower bound
clamped at zero). The two middle thresholds wrap negative values into the byte
range; clamping would change the display. Exactly zero remaining bypasses the
routine.

**RWE. MATCHES.** `src/rwe/game/GameScene_util.cpp:1148-1205` has the same five
thresholds and per-regime fill modes, the wrap by keeping the low byte, the two
triangle waves over sixteen greens with different rates and per-unit offsets
(`:1127-1146`), the band width in `shaders/unitBuild.frag:36`, and the call is
made only for a unit still being built (`:675-677`). Already recorded in
`TOTALA-EXE-SHADING.md` and the interface notes.

## 20. Moving features sinking

**Reference (12.6).** A moving feature above the seabed but below sea level zeroes
its horizontal velocity and descends at a fixed 0x2CCC fixed-point units a tick
(about 0.175 world units); at ground contact it stops.

**RWE. MATCHES.** `GameSimulation.cpp:4641-4669`, with the constant set where the
sink speed is defined (`:4585-4586`). Covered by `TOTALA-EXE-WRECKS.md`.

---

## Not audited here

Reference sections that do not change simulation or interpreted behaviour on a
shipped map, or that overlap findings already settled: path-finder cost tables
(7.1), steering and braking arithmetic (7.3, `TOTALA-EXE-MOVEMENT.md`), wake
callbacks `StartMoving`/`MoveRate` (8.1), nano emission counts (4.4, 8.6),
weapon reload and veterancy (11.3), stockpile (11.3), selection (10.5, section 17a
of `TOTALA-EXE.md`), multiplayer packing of the build fraction (14.1) and
saved state (14.4). The factory's integer worker-time truncation in 4.4 is part of
the economy findings (`TOTALA-EXE-ECONOMY.md`) and was not re-derived.

## 21. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play
or capture on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Hold an unfinished non-floater (submarine) product at the build piece's height; snap it to the terrain on completion. Rule 1 | `src/rwe/sim/UnitBehaviorService.cpp` (factory building branch, the completion branch at `:5367-5394` so the snap does not depend on the unit moving), `src/rwe/sim/GameSimulation.cpp` (`trySpawnUnit` creation height, `:1698-1712`), the tests that assume the old seabed behaviour (search `waterbuilding`, `waterline`, factory tests), `docs/NAVAL-CONSTRUCTION-DGUN-FINDINGS.md` "Diverges, to fix" | **Authorised already** by the owner's observation. A short retail look at faction, depth and snap timing would tune it but is not a prerequisite. |
| 2 | `setSFXoccupy` state 3 (model top below sea) and an initial/steady trigger for units that do not move. Rule 15 | `src/rwe/sim/UnitBehaviorService_util.cpp` (`computeSfxOccupyState`), `src/rwe/sim/UnitBehaviorService.cpp` (`:2052-2066` and the per-tick call site), `src/rwe/sim/waterline.test.cpp`, `src/rwe/sim/GameHash_util.cpp` only if new state is added | Partly: which definition field is the state-3 height (`+0x170` against the model-top field) must be settled before it is coded. |
| 3 | Blocked-cell half-speed clamp. Rule 4 | `src/rwe/sim/UnitBehaviorService.cpp` (`updateGroundUnitPosition`), `src/rwe/sim/UnitBehaviorService_util.cpp` (speed), hash-oracle baseline | No; the executable finding already names it. Confirm whether the stored speed or only the tick's velocity is capped. |
| 4 | Repair reach across water (submerged target, flying repairer without amphibious, non-flying repairer depth). Rule 13 | `src/rwe/sim/UnitBehaviorService.cpp` (repair order handler near `:4690`), the game-layer command validation for repair and assist | Yes, light: confirm a construction aircraft cannot repair a sub in retail. |
| 5 | Water-entry splash for a projectile crossing the surface in flight. Rule 7 | `src/rwe/sim/GameSimulation.cpp` (projectile update), a new event consumed in `src/rwe/game/GameScene_commands.cpp` and `GameScene_audio.cpp`; touches saved or hashed state only if the previous height is stored on the projectile | Yes: confirm that a noexplode or water-weapon round splashes on crossing. |
| 6 | Air-dropped torpedo falls and then runs (not snapped to the surface). Rule 8 | `src/rwe/sim/UnitBehaviorService.cpp` (`:1511-1514`, `:1593-1598`), `src/rwe/sim/GameSimulation.cpp` (projectile motion) | Yes: needs a retail drop (bomber with torpedo) to see the real path. |
| 7 | Direct-unit-hit water art (CONFLICT). Rule 7 | `src/rwe/sim/GameSimulation.cpp` (`:4146-4150`), `src/rwe/game/GameScene_audio.cpp` | Yes: a unit hit over shallow water in retail; art only, no hash impact. |
| 8 | Enemy without sonar: cut, not tint, the below-sea pixels of a visible partly submerged model, and the shipyard-composite effect. Rule 11 | `shaders/unitTexture.frag`, `shaders/unitBuild.frag`, `src/rwe/game/GameScene_render.cpp`, `src/rwe/game/GameScene_util.cpp` | Yes: the shipyard-origin consequence is only a prediction. |
| 9 | Transport depth test `>= 0` against `> 0`. Rule 12 | `src/rwe/sim/GameSimulation.cpp:835`; a signed minimum depth in `MovementClassDefinition.h` if shipped data needs it | A read of the shipped movement classes only. |
| 10 | Boundary conflicts: radar and sonar range inclusive against strict. Rule 10 | `src/rwe/sim/GameSimulation_visibility.cpp:41,204` | Resolve between the executable finding and the reference first; negligible on shipped data. |
| 11 | Hover products get the sea floor while attached; the reference gives it to `Floater` only. Rule 1 | `src/rwe/sim/UnitBehaviorService.cpp:5396` | No shipped case; low. |
| 12 | 300-tick creation-failure wait. Rule 17 | `src/rwe/sim/GameSimulation.cpp` (`spawnNewUnits`) | Not reachable except at the unit cap; low. |

### Conflicts with existing RWE findings, in one place

1. **Unfinished submarine height.** The reference's static trace and
   `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md`'s earlier "Already matching" entry both said
   seabed throughout; the owner's play (executed) says otherwise for the
   construction phase. The executed observation stands (rule 1).
2. **Direct-hit water art.** Reference: ordinary art when a unit was hit. RWE and
   the comment citing `0x499ECF`: water art by the impact square regardless
   (rule 7). Open.
3. **Sensor boundaries.** Reference: strict for both radar and sonar.
   `TOTALA-EXE-VISION.md`: radar strict, sonar inclusive. RWE code: inclusive for
   both (rule 10). Open.
4. **State-3 height field.** Reference: model-top height. `TOTALA-EXE-WEAPONS.md`:
   an unidentified word at `+0x170`, distinct from the `+0x16E` used elsewhere
   (rule 15). Open.
5. **Reference limits.** The reference itself lists as unresolved the full
   naval-build AI, tidal output, all stock yard scripts and the exact appearance
   of the construction stages; nothing in it is used here to settle those.
