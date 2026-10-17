# Steering, script geometry and command cursors parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for the motion component's
per-tick order, the ground and air steering primitives, the movement-rate script
callbacks, and the model-piece geometry queries the scripts feed (muzzle, aim,
sweet spot, nano emitter), per `AGENTS.md` and ADR-0004. The chapter's file is
filed under `units/unit_scripts` but, unlike the "unit motion" chapter audited in
`BT-UNITS-UNIT-MOTION-PARITY.md`, **it really is about movement**: its steering
section is the routine `TOTALA-EXE-MOVEMENT.md` section 102 decodes. The cursor
section is a second reading of `TOTALA-EXE-INTERFACE.md` section 103 and is audited
together with command resolution in `BT-ORDERS-ORDER-DISPATCH-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-scripts.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none
  of its source links were followed. Addresses are the ones the chapter names;
  rules are restated in prose, with constants and arithmetic, and nothing is
  copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. A census of the shipped FBIs and scripts was taken with `tools/hpi.py`
  (read-only) where a rule needed to know whether shipped data exercises it.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/cob` and
  `src/rwe/game` at the working tree of 2026-10-16 (started at HEAD `7c163690` on `ai-envoy-unload-commander-flee`; HEAD was `404f3c4d` on `bt-sim-conformance` when it finished, other agents having moved the tree meanwhile). Nothing was built, run or changed, and tests
  were not run; a MATCHES below means the code reads as the rule says. Another
  agent had uncommitted edits in `GameSimulation.cpp`, `UnitState.h` and the test
  tree while this was written; none of them touch the code audited here except as
  noted in rule 9.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

Already covered, and only cross-referenced: the ground path follower, its
look-ahead and its two brakes (`TOTALA-EXE-MOVEMENT.md` section 102); the air
flight model and the bank and pitch accumulator (section 1); `setSFXoccupy`
(`NAVAL-SYSTEMS-PARITY.md` rule 15); the blocked-cell speed clamp
(`NAVAL-SYSTEMS-PARITY.md` rule 4); the underwater speed cap (rule 3 there).

## Index

1. Motion component, behaviour choice and the per-tick order
2. Ground steering and braking
3. Aircraft steering and flight mode
4. Bank and pitch inertia
5. Position updates and collision boundaries
6. Movement-rate script callbacks
7. SFX occupancy
8. Motion persistence
9. Piece offsets, centres and world positions
10. Script-selected weapons, aim, sweet spot and nano pieces
11. Cursor classification
12. Gaps to fix

---

## 1. Motion component, behaviour choice and the per-tick order

**Reference (section 1).** The motion constructor (`0x43dc00`) clears velocity,
the inertial bank and pitch vector, speed, turn and the path-lock timestamp,
starts in **mode 1 with the obstruction flag clear**, and keeps the definition's
movement-class pointer. It allocates one of four behaviour objects by owner role
and `canfly`: a remote (type 3) owner gets the replicated-position behaviour for
aircraft and a patrol-style behaviour for non-air units; every other owner gets a
light behaviour for aircraft and the searching behaviour for non-air units.
`UpdateMotion` (`0x43dd20`) runs, **in this order**: the behaviour's update, then
aircraft or ground steering, then position update, then the movement-rate
callbacks, then SFX occupancy. The unit tick runs local motion after orders;
replicated unit-state processing invokes motion update for remote movers through
its own path (`TOTALA-EXE-MOVEMENT.md` section 118).

**RWE.** `UnitBehaviorService.cpp:575-585` (ground) and `:750-765` (the remote
puppet path): navigation, steering, position, then `updateMoveRateBand`.

- **Behaviour choice by owner role: DIVERGENCE (structural).** RWE has no
  per-owner motion object; the roles are `simulatesLocally` and the puppet driver
  (`TOTALA-EXE-MOVEMENT.md` section 118 and `TA-NETWORK.md` own that).
- **Order of work inside the motion step: DIFFERS in one place, probably
  harmless.** The original starts the movement-rate script, and only then the
  SFX-occupancy script (so on a tick that changes both, `StartMoving` or
  `MoveRate*` is created before `setSFXoccupy`). RWE raises `setSFXoccupy` inside
  the position step (`UnitBehaviorService.cpp:2051-2066`), **before**
  `updateMoveRateBand` (`:584`), so the two threads are created in the opposite
  order on such a tick. Thread creation order is the order they run in the tick's
  script pass and the order of their ids; whether either is hashed is not
  audited. **UNCLEAR** on effect; the rest of the order is **MATCHES**.
- **Initial mode 1 and obstruction clear: MATCHES in shape** (a new ground unit
  starts landed and unobstructed).

## 2. Ground steering and braking

**Reference (section 2).** `SteerGroundUnit` (`0x43cd20`): with no active path it
clears the turn and brakes at `brakerate`. With a path it requests three
waypoints; when the **second is more than 80 world units away it pulls the
steering point back along the first-to-second segment**, limited by the segment's
length; it takes the horizontal distance and the desired heading, limits the
actual turn to `turnrate` and flags movement when the heading changes. It
**accelerates only when both comparisons leave room**, otherwise it brakes: the
distance needed for the present heading error at the present speed, and the
squared stopping distance from speed and brakerate. The shared velocity helper
(`orders/order_list`) applies the pitch-based maximum and the underwater cap
before the heading becomes X and Z velocity. Steering assumes usable turn and
brake values for its divisions and repairs none.

**RWE. MATCHES `TOTALA-EXE-MOVEMENT.md` section 102, which this chapter
corroborates point for point** (the 80-unit look-ahead and its pull-back, three
waypoints with the index clamped to the last, the heading error clamped to
`turnrate`, the two accelerate-or-brake tests, no path meaning brake and do not
turn). RWE's `followSegment`, `updateUnitRotation` and
`computeNewGroundUnitSpeed` implement them in the original's operation order, and
both divisions are guarded against a zero `turnrate` or `brakerate`, where the
original is not. That guard is a safe departure (a test fixture or a mod can
have a zero), not one to remove.

**Departures that are already recorded, not repeated:** the waypoint arrival
radius of sixteen (eight for the last) against the original's five (section 88);
the speed cap read from a slope ratio in place of the eleven-entry pitch table
(section 102 "What RWE does with this"; the table is still unported); the
underwater halving without the `canhover` and `floater` exemption
(`NAVAL-SYSTEMS-PARITY.md` rule 3).

**One thing the chapter's neighbour adds.** `units-unit-types` section 5 says the
loader derives a `maxslopevelocity` field from `maxvelocity` divided by
`maxslope` plus one, in fixed point. The register's speed cap (section 102) is
`maxvelocity` times a pitch-table percentage and does not mention it, and no
caller of the derived field is named. **UNCLEAR** whether it is the pitch table's
scale, a separate cap or dead. See `BT-UNITS-UNIT-TYPES-PARITY.md` rule 6.

## 3. Aircraft steering and flight mode

**Reference (section 3).** `SetFlightMode` (`0x43d210`) changes the mode only when
it differs. **Entering mode 1 (landed) clears speed and velocity, feeds the bank
and pitch accumulator a zero input and clears activation bit 0; entering any other
mode sets that bit**, and the common state-transition interface can start
`Activate` or `Deactivate` as a result. `SteerAircraft` (`0x43d290`) moves only in
mode 2; other modes clear velocity, speed and turn. In mode 2 the behaviour
supplies a target position, target velocity and desired heading; the routine
damps velocity by acceleration relative to `maxvelocity`, applies a horizontal
magnitude limit from the field loaded as `brakerate`, and sends excess motion as
a heading-directed correction. Vertical velocity approaches the target altitude
by one world unit or a quarter of the speed, whichever the branch selects (outside
the overflow spatial bucket). Turn is clamped to `turnrate`; horizontal steering
uses the acceleration magnitude with a **floor of eight world units on the
distance** in its coefficient. The result is added to velocity, the full length is
stored as speed, and the velocity change is passed to bank and pitch. "Field names
do not imply every aircraft formula uses the same ground braking."

**RWE.** `AirMovement.cpp`, `UnitBehaviorService.cpp:590-700`,
`transitionFromGroundToAir` (`:6578-`).

- **Flight model: corroborates `TOTALA-EXE-MOVEMENT.md` section 1** (the
  `BrakeRate` re-aim, damping against `maxvelocity`, the acceleration cap). The
  chapter's prose is not detailed enough to compare the constants; **UNCLEAR**,
  and the register's decode is the one to believe.
- **Activation tied to the flight mode: MATCHES in shape.** RWE activates on
  take-off (`transitionFromGroundToAir`, `UnitBehaviorService.cpp:6582`) and when
  a landing is aborted or fails (`:607`, `:639`), and deactivates when the descent
  begins (`:695`).
- **The "only when the mode differs" gate is not in the primitive: DIFFERS.**
  `UnitState::activate` and `deactivate` (`UnitState.cpp:621-631`) set the flag
  and **start `Activate` or `Deactivate` unconditionally**, and
  `GameSimulation::activateUnit` / `deactivateUnit` (`:2745-2757`) then raise
  `UnitActivatedEvent` or `UnitDeactivatedEvent` every time. The original's
  state-bit setter (see `BT-UNITS-UNIT-POSITION-PARITY.md` rule 4) starts nothing
  when the byte does not change. About fifteen call sites call the primitive, a COB
  `set ACTIVATION` handler (`cob.cpp:490-500`) and the `SetOnOff` player command
  (`PlayerCommandApplication.cpp:402-410`) among them, and not all of them test
  first. The visible case is a script that spins a radar dish or opens a bay in
  `Activate` being restarted by a repeated switch-on.

## 4. Bank and pitch inertia

**Reference (section 4).** `ApplyBankAndPitch` (`0x43d0d0`) keeps about **0.95** of
its inertial vector, adds the new motion delta, rotates the horizontal components
by heading and scales them by `bankscale` and `pitchscale`; a helper compares them
against a gravity-derived denominator. **Both final angles use the rotated X
component**; the chapter calls the pitch's reuse of X a suspected original bug and
says not to replace it with conventional physics.

**RWE. MATCHES, and the chapter independently corroborates section 1's correction.**
The register decoded the lag as 0xF333 over 65536 (0.9499817), the gravity-times-
twenty cancellation, and, on 2026-09-24, that pitch reads **the same slot as
bank** (`AirMovement::pitchAngle`, ported). The chapter says the same thing in
other words; nothing to change. Recorded so the next reader does not "fix" pitch to
use the longitudinal component.

## 5. Position updates and collision boundaries

**Reference (section 5).** `UpdatePosition` (`0x43d6d0`): a **carried unit** takes
its position from the carrier's model-piece world position, floater height clamped
to the sea-minus-draft floor, inherits the carrier's motion velocity and speed
when it has them (a stationary carrier clears them) and follows no path of its
own. An uncarried unit adds velocity to position and returns if position and mode
are unchanged; otherwise it **stamps `lastMoveTick`** and derives the new
footprint origin. A same-origin, same-mode move writes the world position and
marks moved; crossing a cell or changing mode does more. For active local roles 1
and 2 it refreshes the **obstruction flag** from the footprint-placement test; if
blocked it clamps X and Z to about half a cell of the current footprint centre,
**caps speed to half the definition's maximum** and rebuilds velocity, and leaves
the old occupancy; if not blocked it removes old occupancy, writes position,
origin and mode, reinserts and refreshes sight. Other roles do not repeat the
placement test. It is not a model-triangle collision test.

**RWE.** `updateUnitPosition` and `tryApplyMovementToPosition`
(`UnitBehaviorService.cpp:1943-2066`), the carried pass in `GameSimulation::tick`.

- **Blocked move with half-speed cap: MISSING** (`NAVAL-SYSTEMS-PARITY.md` rule
  4; not repeated). RWE sets `inCollision`, tries each axis and keeps full speed.
- **The `lastMoveTick` stamp: MISSING.** Its reader is the pass-map occupancy
  refresh (`BT-UNITS-MOVEMENT-CLASS-PARITY.md` rule 7), which also answers the
  question `TOTALA-EXE-MOVEMENT.md` section 102 leaves open about `mover+0x2A`.
- **Live placement test even when the pathfinder routed there: MATCHES.** RWE
  asks `isGridPointWalkable` and `isCollisionAt` at the move itself
  (`:2040-2046`), as the chapter says a corridor does not replace.
- **Carried units follow the carrier piece: MATCHES** in effect
  (`TOTALA-EXE-TRANSPORTS.md` section 38); inheriting the carrier's velocity has no
  RWE counterpart and no sim-visible effect, because a carried RWE unit runs no
  physics (`loadUnitIntoTransport` zeroes its speed, `GameSimulation.cpp:899-908`).

## 6. Movement-rate script callbacks

**Reference (section 6).** `UpdateMoveRate` (`0x43da70`) selects rate **0 when the
unit is blocked, carried, or has neither speed nor turn**; otherwise rate 1 up to
`moverate1`, rate 2 above that through `moverate2`, rate 3 above both. It compares
with the stored two-bit state. A change **to** zero starts `StopMoving`; from zero
to non-zero starts `StartMoving`; rates 1, 2 and 3 start `MoveRate1`, `MoveRate2`
and `MoveRate3`, then the two runtime bits update. "A unit turning in place can
meet the speed-or-turn condition, so a moving animation is not proof of non-zero
translation." The names and thresholds are confirmed; no particle emission is
implied.

**RWE.** `updateMoveRateBand` (`UnitBehaviorService.cpp:3704-3750`); the loader
default (twice `maxvelocity`) at `LoadingScene_util.cpp:503-506`.

- **Bands, thresholds, callback names and the change-only rule: MATCHES**
  (`TOTALA-EXE-DATA.md` section 24, "`MoveRate1` / `MoveRate2`"). The default
  threshold of twice the top speed is the same.
- **What counts as moving: DIFFERS.** RWE reads the distance the unit actually
  moved this tick and calls anything under 0.1 world units stopped
  (`:3716-3727`). The original tests the movement state's **speed or turn**
  against zero and forces zero for a **blocked** or **carried** unit. Three
  consequences: a unit turning in place (speed zero, turn non-zero) is band zero
  in RWE and band one in the original, so `StartMoving` and the walking animation
  do not run while a ground unit pivots; a blocked unit pressed against a wall at
  non-zero speed keeps walking in the original's script until the blocked flag
  drops, where RWE's frozen position puts it at band zero; and a carried unit is
  band zero in both only because RWE's carried units are not stepped. Shipped
  scripts that read this are few (`TOTALA-EXE-DATA.md`: five of two hundred define
  any `MoveRate`), but every walker's leg animation hangs off `StartMoving`.
- **A `MoveRate1=0` explicit zero: DIFFERS, mod data only.** The loader treats a
  zero threshold as absent and substitutes twice `maxvelocity`
  (`LoadingScene_util.cpp:503-506`); the original keeps a zero it was given, which
  puts every moving unit above band one. No shipped FBI names zero (the census
  found twelve naming `moverate1`, one naming `moverate2`).

## 7. SFX occupancy

**Reference (section 7).** `UpdateSfxOccupy` (`0x43db50`) derives the state from
mode, whole Y, sea level, draft and model top: 0 outside modes 1 and 2; 4 above
sea; 1 when at or below sea and less than five units below it; then, as **later
tests that override**, 2 when Y plus draft equals sea level and 3 when Y plus model
top is below sea; if none of the three below-water tests matches, the previous
value stays. The script is started only on a change, then the value is stored.

**RWE.** Entirely `NAVAL-SYSTEMS-PARITY.md` rule 15 (values 4, 1, 2 and the
"previous value kept" quirk MATCH; state 3 and the stationary-unit trigger
differ). Nothing new in this chapter. The one new detail is the **ordering of the
two script starts** within a tick, rule 1 above.

## 8. Motion persistence

**Reference (section 8).** `SaveMotion` (`0x43dd70`) writes **0x23 bytes** under a
name built from the unit id with a `mob` suffix: velocity, the inertial vector,
speed, turn, the path-lock timestamp, mode and the obstruction flag. `LoadMotion`
(`0x43de30`) reads the same fields and **does not check a complete read** before
copying. Behaviour objects and route-search internals are not saved raw; they are
rebuilt from goal and order reconstruction.

**RWE. N/A for parity.** RWE has its own save format (`save_util.cpp`) and does
not read TA saves. The two rules worth carrying over are the one RWE already
follows, that **a half-finished search is serialised as five integers and rebuilt**
(`CLAUDE.md`, "Derived state that can change an outcome is not exempt"), and the
one the original fails, that a load must reject a short read.

## 9. Piece offsets, centres and world positions

**Reference (section 9).** `GetPieceOffset` (`0x43def0`) returns **zero for a
missing object or state or an out-of-range piece index**; otherwise it starts from
the piece's base offset plus animated translation and walks the parent chain,
each parent rotating the accumulated offset by its piece angles, the root
incorporating the unit's base angles, then adding the translation; **the final Z is
negated** to match the engine's convention. `GetPiecePosition` (`0x43e060`) adds
the unit's world position, so **an invalid index becomes the unit origin**, not a
failure coordinate. `GetPieceCenter` (`0x43e0b0`) builds component minima and
maxima over the piece's **vertex array** with the bounds **starting at zero** (a
piece whose vertices are all positive on an axis still includes zero in that
axis's range) and returns their midpoint plus the unit's world position; it does
not validate the index. `GetPieceAngles` (`0x43e180`) combines unit and piece
angles. "Origin, offset, vertex-bound centre and angle state are not
interchangeable: weapon muzzle, aim origin, target sweet spot and nano emitter can
be distinct positions on one visible unit."

**RWE.** `getPieceTransform` (`src/rwe/sim/util.cpp:6-70`),
`UnitBehaviorService::getPieceLocalPosition` and `getPiecePosition`
(`UnitBehaviorService.cpp:5523-5538`).

- **Parent walk with piece offsets and angles: MATCHES in shape**, and RWE's walk
  is bounded by the piece count (issue #75), which the original's is not.
- **An out-of-range piece index from a script: DIFFERS, and it is a fault in
  content that costs every peer.** Every query consumer except
  `QueryTransport` (`:3024-3029`, which range-checks) converts the script's answer
  with `_script->pieces.at(index)` (`:5527`, `:5544`). A script returning a piece
  number the model does not have (a mod, a damaged `.cob`, or a model and script
  from different versions) throws `std::out_of_range` out of the simulation tick
  on every peer. The original returns the unit origin. No shipped unit does it
  (the query paths were not run over all 157 base scripts; this is a reading of
  the code, not a measurement). `docs/SECURITY-AUDIT.md`: "a fault in content costs
  that content and never every peer"; `runCobQuery` already turns a script *fault*
  into the caller's fallback (`:2071-2113`) and should turn this into one too.
- **Piece centre as a vertex-bound midpoint: MISSING, and it feeds the aim point.**
  RWE has no vertex-bounds query (the model definition carries pieces and a
  whole-model height, `UnitModelDefinition.h`). See rule 10.
- **The coordinate convention (Z negated): N/A**, an internal convention.

## 10. Script-selected weapons, aim, sweet spot and nano pieces

**Reference (section 10).** `QueryWeaponPiece` (`0x43e1e0`) asks `QueryPrimary`,
`QuerySecondary` or `QueryTertiary`, default **piece zero**.
`GetWeaponPiecePosition` (`0x43e240`) uses an explicit non-negative piece or that
query. `GetAimFromPosition` (`0x43e2e0`) asks `AimFromPrimary/Secondary/Tertiary`,
default **minus one**; a retained minus one **falls back to the corresponding
Query** result. `GetSweetSpot` (`0x43e3c0`) asks `SweetSpot` with default zero and
**passes the piece to `GetPieceCenter`**. `GetNanoPiecePosition` (`0x43e400`) asks
`QueryNanoPiece` and uses its world offset; it creates no particles. Weapon
indices are expected to be 0 to 2.

**RWE.** `getLocalAimingPoint` and `getLocalFiringPoint`
(`UnitBehaviorService.cpp:2117-2140`), `getSweetSpot` (`:2142-2151`),
`getNanoPoint` (`:5504-5521`).

- **AimFrom falling back to Query: MATCHES.** A missing `AimFrom` function gives
  the Query piece; the "default minus one" is represented by the query's
  absence (`:2120-2124`).
- **Query default: DIFFERS in a corner.** A unit with no `Query*` function aims
  and fires from a zero local vector (`:2133-2137`), the unit origin; the original
  uses **piece 0's** offset, which for most models is the base at or near the
  origin. Unobservable on shipped content (every armed unit defines the queries).
- **`QueryNanoPiece`: MATCHES** with one RWE addition: the answer is cached for the
  tick so a two-nozzle unit's alternation is not advanced twice
  (`UnitState::nanoPointQueriedAt`).
- **The sweet spot: DIFFERS, and it is the aim point of every shot.**
  `TOTALA-EXE-WEAPONS.md` section 126 says a unit target is aimed at "the sweet
  spot, `0x43E3C0`, ... turned into a world position by `0x43E0B0`" and leaves
  `0x43E0B0` as a bare reference. This chapter says what it is: **the midpoint of
  the vertex bounds of the piece, with zero included in each range, plus the unit's
  world position, with no rotation by heading and no piece-chain offset applied.**
  RWE aims at the **origin of the named piece, transformed through the whole
  chain** (`getPiecePosition`), and at the unit's position when the script has no
  `SweetSpot` (`:2144-2149`). For a script that answers the base piece (the usual
  case) the original's point is half the base piece's vertical extent **above** the
  origin (the bounds start at zero), and RWE's is the origin itself: a
  height-of-the-base difference in the aim point, on the three-dimensional distance
  that feeds the lead computation (`BT-UNITS-UNIT-TARGETS-PARITY.md` rule 3), the
  ballistic arc solve, and the point a round steers at. It is **not** the point a
  homing round steers at, which `TOTALA-EXE-WEAPONS.md` section 116 shows is the
  unit's origin (`unit+0x6A`); that section and this chapter are about different
  routines and do not conflict.
- **Why it is only UNCLEAR how big it is.** The chapter says the centre is added
  to the world position with no heading rotation; a model whose base piece is not
  centred on the origin horizontally would then be aimed at off-centre in the
  original. Whether that is what `0x43E0B0` does is a single routine's worth of
  reading. The arithmetic of the lead (distance divided by weapon speed, 0.8) is
  unaffected.

## 11. Cursor classification

**Reference (section 11).** `OrderModeTakesTarget` (`0x43e470`) is false for modes
**5, 10 and 14**, true otherwise. `GetOrderCursor` (`0x43e490`) returns numeric
cursor codes with **0x13 (19) as the general "unavailable" result**. It shares
capabilities and interaction tests with command resolution but creates no order.
Attack can return 2 for a dropped primary weapon, 1 for an attack-capable mover, or
1 or 3 from a stationary unit's weapon reachability; a point attack also rejects
an anti-air-only primary. **Special attack checks current energy and metal against
per-shot costs**, which merely selecting ATTACKSPECIAL does not. Patrol 7, repair
6, follow 5, capture 4, air pickup 8, ground pickup 0xc, unload 0xd, mobile build
0x10, teleport 9, ordinary move 0xe, resurrection and reclaim 0xa and 0xb. Move
priority tests visible resurrectable features before target interactions, then
capture, reclaim, service, landing, pickup, follow; default mode with interface
type 1 favours selectable-local code 0xf, then enemy and friendly codes 0x11 and
0x12. Local selectability requires the local player index, the ready/selectable
flag, zero `buildLeft`, no transfer holdoff and carrier permission. "The cursor
does not apply the dispatcher's initial non-live-target guard identically".

**RWE.** `GameScene.cpp:760-880` (cursor by mode), `DefaultAction.cpp`,
`CursorService.h:13-42`.

- **The command-to-cursor map and the Load and Move arms: MATCHES
  `TOTALA-EXE-INTERFACE.md` section 103**, which is the same decode (the register's
  table is the better one).
- **Armed-mode cursors are unconditional: DIFFERS.** With ATTACK, GUARD, REPAIR,
  RECLAIM, CAPTURE, PATROL or UNLOAD armed, RWE shows that mode's cursor over
  anything in the world (`GameScene.cpp:803-840`). In the original the chooser
  returns 19 when no selected unit could carry out the command on the thing under
  the cursor, and **the click is the cursor** (`TOTALA-EXE-INTERFACE.md` section
  103, "The cursor is the decision": a left click issues an order exactly when the
  chooser returned an id below 17). So RWE lets a click issue orders the original
  would refuse at the chooser; see `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 3.
- **Missing cursor art and states: MISSING, presentation only.** `CursorType` has
  no airstrike (id 2), too-far (id 3), teleport (id 9), revive (id 10), find-site
  (id 16) or hourglass (id 20) (`CursorService.h:13-42`). The register's note that
  `cursorrevive` is absent from the base `CURSORS.GAF` stands; the others are in
  it. The D-gun cursor is the attack cursor with no energy test
  (`GameScene.cpp:806-809`), where the chooser makes special attack depend on
  having the energy and metal for a shot.
- **The select-first arm: MATCHES** (the register's fourth arm of the left-click
  chain).

## 12. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | `Activate` / `Deactivate` run on every call, not only when the state changes; the events fire every time too. Rule 3 | `src/rwe/sim/UnitState.cpp:621-631`, `src/rwe/sim/GameSimulation.cpp:2745-2757`, then audit the call sites in `UnitBehaviorService.cpp` (`:607`, `:639`, `:695`, `:5287`, `:5312`, `:5339`, `:5362`, `:5391`, `:5442`, `:5490`, `:5499`, `:6112`, `:6582`) and `sim/cob.cpp:490-500` | **Light.** A retail look at a unit whose `Activate` plays an animation (a radar dish) being toggled twice. The change is script-visible, so it moves COB state; check it against the replay keyframe test. |
| 2 | A script returning a piece index the model lacks throws out of the simulation tick. Rule 9 | `src/rwe/sim/UnitBehaviorService.cpp:5527`, `:5544`, `:2117-2151`; add a case to `src/rwe/io/malformed_input.test.cpp` or a `cob` test | No. Behaviour to copy is the original's: the unit origin. Security-audit class. |
| 3 | The sweet spot is the piece's vertex-bound centre (zero included) plus the unit position, not the piece origin. Rule 10 | `src/rwe/sim/UnitBehaviorService.cpp:2142-2151`, `src/rwe/sim/UnitModelDefinition.h` and the 3DO loader (a per-piece vertex bound), `UnitPieceDefinition.h` | **Spec-room first** (one routine, `0x43E0B0`: is the centre rotated by heading, does it include the piece offset). Then a corpus comparison of recorded round impacts against the two aim heights. Hashed: every aimed shot moves. |
| 4 | Movement-rate band: count a turning-in-place unit as moving; force band zero for a blocked unit. Rule 6 | `src/rwe/sim/UnitBehaviorService.cpp:3704-3750` (needs the steering turn and the `inCollision` flag) | Light. Walkers' leg animation while pivoting is the visible case; one retail look. Script thread creation only. |
| 5 | Armed-mode cursors gated by what the click could do; airstrike and too-far cursors. Rule 11 | `src/rwe/game/GameScene.cpp:803-840`, `src/rwe/CursorService.h`, `DefaultAction.h` | No for the cursors; the gating belongs with dispatch rule 3. |
| 6 | Order of `setSFXoccupy` against `MoveRate*` and `StartMoving`. Rule 1 | `src/rwe/sim/UnitBehaviorService.cpp:575-585`, `:2051-2066` | No, unless the replay hash moves. Do it with the SFX state-3 change (`NAVAL-SYSTEMS-PARITY.md` rule 15). |
| 7 | A unit with no `Query*` function fires from piece 0, not the origin. Rule 10 | `src/rwe/sim/UnitBehaviorService.cpp:2129-2139` | No. Unobservable on shipped content. |

### Documented divergences this audit leaves alone

The arrival radius, the pitch-table speed cap and the missing underwater halving
exemption (section 88, section 102); the unit-tick stage order (section 132,
deferred on purpose); the missing pitch for ground units was ported (section 1
"Deliberately not ported", struck through).

### Conflicts with existing RWE findings, in one place

1. **The sweet spot's world position.** Register (`TOTALA-EXE-WEAPONS.md` section
   126): the sweet spot is the `SweetSpot` query "turned into a world position by
   `0x43E0B0`", with no statement of what that does; RWE reads it as the piece's
   origin through the transform chain. Reference: the vertex-bound midpoint, zero
   included, plus the unit position. The register is silent rather than contrary.
   Open.
2. **`mover+0x2A`.** Register (section 102): stamped, no reader. Reference: the
   occupancy refresh reads the motion timestamp. See
   `BT-UNITS-MOVEMENT-CLASS-PARITY.md`. Open.
3. **No other conflict.** The steering, brake and bank arithmetic agree with
   sections 1 and 102, including the pitch quirk.
