# Unit references, weapon targets, damage and ground alignment parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how a damage event is
built and applied, how paralysis works as an order effect, how a weapon aim point
is resolved and led, and how a ground unit is aligned to the terrain, per
`AGENTS.md` and ADR-0004. The damage arithmetic, the kill counters and the aim lead
are `TOTALA-EXE-WEAPONS.md` sections 6, 126 and 127; they are cross-referenced.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-targets.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none
  of its source links were followed. Addresses are the ones the chapter names;
  rules are restated in prose with constants and arithmetic, and nothing is
  copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** One data check was made with
  `tools/hpi.py`: the unit script name tables in `totala1.hpi` were searched for
  the two damage-reaction function names (rule 5). Nothing was played.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/cob` and
  `src/rwe/game` at the working tree of 2026-10-16 (HEAD `7c163690`, branch
  `ai-envoy-unload-commander-flee`). Nothing was built, run or changed, and tests
  were not run; a MATCHES below means the code reads as the rule says.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Reference lists protect reusable unit slots
2. Compact weapon-target encoding
3. Aim position and lead
4. Damage event construction and scaling
5. Applying healing, ordinary damage and script reactions
6. Paralysis is an order effect
7. Build-list membership and property text
8. Four-point ground alignment and water bobbing
9. Gaps to fix

---

## 1. Reference lists protect reusable unit slots

**Reference (section 1).** A reference node is a 0x10-byte object holding a
virtual table, the referenced unit, a next link and an optional listener. A unit
keeps the head of its incoming-reference list. Linking prepends and clears the
reference if the unit is null or its definition index is zero (a freed slot).
`ClearRef` notifies the listener with event 8 and unlinks only if the notification
left the same unit associated; the listener may have redirected it. `ClearUnitRefs`
repeatedly processes the head until none remain, and unit death calls it before the
slot is reused. A separate notify walks the chain once and forwards any event
without clearing. The point is that a unit id is a reusable slot and a stale one
could name a later occupant.

**RWE. DIVERGENCE (structural), and safer.** Ids carry a generation
(`src/rwe/collections/VectorMap.h`), so a stored `UnitId` cannot silently name a
later unit; the lookups return nothing (`tryGetUnitState`). The owners of long-lived
references (orders, the weapon target variant, the AI blackboard) validate on use.
There is no listener protocol to match, and no rule here for RWE to copy. The only
observable the chapter implies is that **a target that dies is cleared and its
weapon is told**, rule 3.

## 2. Compact weapon-target encoding

**Reference (section 2).** Each of three 0x1c-byte weapon slots begins with a pair
of 16-bit values; a second value of 0x8000 means "unit id" (the first value zero
then means no target); any other second value makes the pair a **world X/Z point in
whole units** (the high words of the fixed-point coordinates; a Z of 0x8000 becomes
0x8001 to avoid the tag). Setting a target clears bits 10 to 14 of the unit's
network-dirty field and checks nothing: no range, alliance, sight, sonar, altitude
or damage eligibility. The empty pair is written without callbacks by one routine;
the clearing routine writes it only when non-empty and then calls the script's
target-cleared function with the weapon index (the chapter notes the script lookup
used is the one named for `StartBuilding`, which is a name collision, not
construction). The chooser prefers slot 0 when its enable bit is set, then slot 1,
then returns slot 2's masked bit, **so a returned zero does not prove slot 0 is
usable**.

**RWE.** RWE's target is a variant of a unit id, a position or a projectile id
(`UnitWeapon.h:22`). **DIVERGENCE (structural).** Two consequences of the original's
encoding are worth stating:

- **Ground points are quantised to whole world units** in the original and carry full
  precision in RWE. At the typical weapon range the aim difference is under a
  hundredth of a degree. **DIFFERS**, negligible, and visible only in a demo
  comparison of shell landing points. A TA-protocol reader
  (`src/rwe/io/tad`, `TaLiveSender`) already works in the wire's own units.
- **The enable bit and the chooser.** RWE keys each weapon by index and has no
  "which weapon first" chooser; what the original uses it for (the weapon whose
  range sets an attack order's stand-off distance, presumably) is not stated in the
  chapter. **UNCLEAR**, and not a rule until a consumer is named.

## 3. Aim position and lead

**Reference (section 3).** The point-target position reconstructs X and Z in fixed
point and takes Y as the **greater of the sampled ground height and sea level**,
discarding any Y the order carried. For a unit target it rejects the empty pair; if
the referenced slot's definition index is zero it **clears the pair, calls the
target-cleared function and returns zero**. Otherwise it takes the target's script or
model sweet spot. Lead is added only when: the weapon slot's enable bit is set, the
weapon is not `cruise`, the target has a motion object, **the firing unit has more
than five kills**, and the weapon speed is non-zero. The arithmetic is distance
(three-dimensional) to the sweet spot, divided by weapon speed, times 0xcccc over
65536 (about 0.8), added to the target's motion vector. It is a velocity
approximation with explicit gates, not an intercept solver.

**RWE.**

- **Lead: MATCHES, and the chapter corroborates section 126 independently** on every
  gate and on the 0.8 factor. `computeAimLead`
  (`UnitBehaviorService_util.cpp:140-`, called at `UnitBehaviorService.cpp:1182`)
  refuses at five kills or fewer, for `cruise`, and for zero weapon velocity, and does
  the integer arithmetic of the register. The chapter's wording "more than five
  kills" settles the threshold section 126 stated as six or more; the two agree.
- **Dead target clears and calls `TargetCleared`: MATCHES.**
  `createThread("TargetCleared")` exists (`UnitState.cpp:581`), and
  `TOTALA-EXE-WEAPONS.md` section 126 has the same rule.
- **Ground-point Y: UNCLEAR.** The original discards the order's Y and re-reads
  `max(ground, sea)` at aim time (the register has the same figure for a cruise round
  near its target, `TOTALA-EXE-WEAPONS.md` near line 2609). RWE's `getTargetPosition`
  returns a position target unchanged (`UnitBehaviorService.cpp:5574-5577`); whether
  the order that created it already clamped Y to the sea was not found. If it did not,
  a ground attack on open water aims at the sea bed in RWE and at the surface in the
  original, which matters for a torpedo-less shell fired at a point in a bay.

## 4. Damage event construction and scaling

**Reference (section 4).** The damage routine builds a nine-byte type-11 event: victim
id, optional attacker id, 16-bit amount, heading **high byte**, damage kind byte. The
amount is computed first, **except for kind 10 (repair), which bypasses all the
reductions**. For other kinds, the activation bit 2 (the script's "armoured" state)
enables the definition's 16.16 damage modifier **when the requested amount is below
30000**; experience then reduces damage by **four percent per five kills, up to five
steps (20 percent at 25 kills)**; the integer computation applies both before
narrowing into the amount field. The event is applied locally and then broadcast when
the victim has an active type-3 owner and the kind is not 11 (acid water). The sender
identity is the source player's network id, otherwise the local identity. The routine
does **not** spread damage to all units of the same definition (a provisional comment
suggested it). Only the high byte of the 16-bit direction reaches the event.

**RWE. MATCHES.** `applyDamage` (`GameSimulation.cpp:3494-3650`): the 30000 armour
cut-out (`ArmourBypassDamage`, `GameSimulation.h:57`), the armoured state from the
script, the victim's four percent per tier up to five tiers (`veterancyTier`,
`:3483`), applied in the register's order (attacker bonus, armour, victim bonus).
The register's section 6 has the same figures, and the chapter **corroborates** them.
The broadcast and the type-3 role are lockstep concerns (N/A); the direction byte is
rule 5.

## 5. Applying healing, ordinary damage and script reactions

**Reference (section 5).** The apply routine rejects an absent victim, one lacking the
live flag, or one already marked with the pending-death bit. **Kind 10** adds the
event amount to health, clamps to the definition's maximum and returns; it runs no
reaction. Other kinds mark the victim recently damaged, call the **attack reaction
(except kind 11)**, record the kind, and **update the last-attacker pointer and player
slot** when an attacker exists (an event without one does not reset them). A local
participant's hit adds an activity sample for music. For ordinary kinds it subtracts
the amount; at non-positive health an active type-1/type-2 owner gets the pending-death
bit and the function returns, other roles clamp to zero. **For a surviving kind-1
hit it converts the heading byte to two directional script arguments using
trigonometric helpers with scale 400, starts `HitByWeapon`, and starts `TakeDamage`
with the health percentage clamped to 0..100.** Lethal local hits return before the
callbacks.

**RWE.**

- **`HitByWeapon` and `TakeDamage` are never started: MISSING.** The only function
  names RWE starts on a unit script are `Create`, `StartBuilding`, `StopBuilding`,
  `Activate`, `Deactivate`, `StartMoving`, `StopMoving`, `MoveRate`, `SetSpeed`,
  `SetDirection`, `SetMaxReloadTime`, `RockUnit`, the aim and fire
  functions, `Killed`, `TargetCleared`, `setSFXoccupy` and the transport functions
  (a search of `createThread` calls under `src/rwe`). Neither damage function
  is started by any code under `src/rwe` (the string `UnitTakeDamage` in
  `src/rwe/io/tad` is the demo's `0x0b` subpacket, a different thing) and
  neither is named in `docs/`. In the shipped base data, **25 of the 157
  unit scripts in `totala1.hpi` define `HitByWeapon`** (the name tables of, among
  others, ARMFLASH, ARMBULL, ARMCROC, ARMFAV, ARMJAM and ARMARAD; none defines
  `TakeDamage`). So every one of those units loses whatever reaction its script
  gives it to being hit: the shaking, the turret flinch, and anything the script
  does to its own state there. This is the largest behavioural gap in the chapter
  that is not in the register at all.
- **What it takes to port it.** The event must carry a **hit direction**, which
  RWE's `applyDamage` does not receive: the source is the projectile's heading at
  impact, the argument is its high byte (256 steps a turn), and the two script
  arguments are a sine-like and a cosine-like component of it scaled by 400. The
  chapter does not give the sign or which argument takes which component, nor
  whether a hit with no attacker or by a non-weapon kind runs it (the text says kind
  1 only). Those three facts are a spec-room item. The call itself is deterministic
  (COB runs inside the tick) and unit script state is not in the sync hash, so a
  wrong argument would not desync, which is why it needs the prose first.
- **Heal: MATCHES in effect.** Repair adds hit points and clamps to the maximum
  (`UnitBehaviorService` repair path; `UnitState.cpp:183-190` for build progress);
  kind 10 skipping armour and veterancy is the register's.
- **Attack reaction except for acid water: MATCHES.** `returnFire` runs only when
  there is an attacker (`GameSimulation.cpp:3550-3557`); acid water has none.
- **Last attacker: DIFFERS** (see `BT-UNITS-UNITS-PARITY.md`, rule 8): RWE keeps no
  last-damager field.
- **Music activity sample: MATCHES in shape** (`UnitDamagedEvent`, `:3531`).
- **Pending-death bit: DIVERGENCE (section 132, deferred).**

## 6. Paralysis is an order effect

**Reference (section 6).** Kind 2 uses the amount as a **tick duration** rather than
health. It needs a live, non-pending-death unit, an active type-1/type-2 owner and
**absence of the definition's immunity bit**. If the unit's **current order** is
already a paralyse order it extends that order's ticks; otherwise it appends a newly
built paralyse order. The existing-effect test looks at the **order head only**, not
every queued order. The direct paralyse routine does the same guarded operation and,
despite a comment about completed construction, **does not test build progress**. A
kind-2 event that fails the owner or immunity conditions still does not fall through
into ordinary health subtraction. Expiry, animation and the blocked actions belong to
the paralyse order handler.

**RWE. MATCHES the register, with a structural difference.** `applyDamage`
(`GameSimulation.cpp:3595-3614`): immunity (`immuneToParalyzer`), the duration added to
the time left and clamped to 1800 ticks (`MaxParalysisTicks`), the weapon aim dropped
on entry, and no health taken. This is `TOTALA-EXE-DATA.md` section 24; the chapter
**corroborates** its additive stacking and the no-damage fall-through. **DIVERGENCE
(structural)**: RWE holds paralysis as `paralyzedUntil`, not as an order, so the
original's head-only test has no counterpart. The cases where the two part: a stunned
unit that is given a new command (the original queues an order ahead of the stun
order or behind it; whether the stun still runs is the order machine's) and a second
hit while the stun is not the head order (a second stun order, each clamped
separately). Rare, and the register says what a stunned unit can do is "reasoning
about the order machine, not a gate". **UNCLEAR**, low. Unfinished units can be
stunned in both.

## 7. Build-list membership and property text

**Reference (section 7).** The membership test scans the definition's buildable-id
table and nothing else: no resources, site, per-type limit, factory state or range.
The property list builder allocates 0xc0 bytes and writes terminated strings: the
costs, one more statistic, separators, and for a mobile unit the speed, acceleration
and turn rate with translated units (speed and acceleration through fixed point, the
tick rate and a **0.4 display factor**); a non-mobile unit gets "N/A". The appends are
unbounded within the fixed buffer and the caller owns the allocation.

**RWE.** Membership: **MATCHES in effect** (the build menus are data driven,
`GameSimulationLoader.cpp:902` and the download-menu entries). The property text:
**not located**. RWE's unit information panel is drawn from the GUI data
(`TOTALA-EXE-INTERFACE.md` section 99) and no code was found that formats speed,
acceleration or turn rate as the chapter describes. **UNCLEAR / MISSING**,
presentation only. The fixed 0xc0 buffer and unbounded appends are a fault to avoid,
not to copy (`docs/SECURITY-AUDIT.md`); a port must size the string from the data.

## 8. Four-point ground alignment and water bobbing

**Reference (section 8).** The alignment routine takes a **designated row of four
vertex indices from the unit model** (it returns unchanged for a negative model count).
Each vertex's X and Z are rotated by the heading, translated to world coordinates and
used to **bilinearly sample the terrain's neighbouring height bytes**; a sample outside
the usable right or bottom neighbour range **returns before any final write**. For a
definition with **bit 0x1000**, while live and not pending death, the sampled height is
**raised to at least sea level**, and a small trigonometric **bob** is added whose phase
comes from the time service (`GetTicks`), the vertex number and the unit's randomised
hover phase, and whose amplitude depends on current speed relative to **half the
definition's speed** and fades with the simulation ticks since the motion timestamp,
**clamped at 60**. It mixes a wall-clock phase with a simulation-age envelope. From the
four heights the routine sets the **whole word of world Y to the average support
height**, computes pitch from the difference between the two end-pair means and bank
from the side-height difference; heading is used for sampling and not rewritten. A
provisional name calls the Y word a roll, but its offset lies inside the position's Y;
it is not a third rotation.

**RWE.** `updateGroundTilt` (`UnitBehaviorService.cpp:1869-1890`) and `computeGroundTilt`
(`UnitBehaviorService_util.cpp:1168-1202`); position in `updateGroundUnitPosition`
(`:1892-1941`).

- **Pitch and roll: MATCHES the register's shape** (`TOTALA-EXE-MOVEMENT.md` near line
  736): the mean of the front pair minus the back pair over the span, the mean of the
  side pairs likewise, atan2; `upright` units stay level.
- **Where the four corners come from: CONFLICT.** The register (`TOTALA-EXE-MOVEMENT.md`,
  "`upright` and `norestrict`") says each corner is "the definition's corner offset"
  rotated to the heading, and RWE implements footprint half extents (width by 8, length
  by 8, `UnitBehaviorService.cpp:1884-1887`). The chapter says the four points are
  **four vertices of the unit's model**, selected by a designated row. For a ship the
  model's hull corners and the footprint's corners are very different (a 5 by 5
  footprint against a long narrow hull), so the two readings give different bow-to-stern
  spans. Neither is chosen here.
- **Y: DIFFERS from the register and from the chapter.** Both say the unit's height is
  the mean of the four samples. RWE sets Y from one sample at the unit's centre and
  clamps floaters and hovercraft to sea level (`:1898-1903`). On a uniform slope the
  two agree; on a bowl or a ridge they differ by a unit or two. Hashed.
- **Sea-level clamp and the bob: gating flag CONFLICT, wave term MISSING.** The chapter
  gates both on definition **bit 0x1000**. The register puts `canhover` at bit 12 of
  the flag word at `def+0x241` (0x1000) and `floater` at bit 19 (`TOTALA-EXE-MOVEMENT.md`
  section 95; the shadow gate `0x81000` is both in `TOTALA-EXE-WRECKS.md`), and says
  the **floater** is held up to sea level "with a wave term folded in". RWE clamps for
  `floater || canHover` and has no wave term. If the chapter's bit is `canhover`, then
  the bobbing is a **hovercraft** behaviour and a ship (a floater without `canhover`)
  rides flat; if the register is right it is the other way round. The chapter's own
  prose ("a ship's visible motion") does not decide it. Open.
- **The bob is wall-clock driven: any port must be render-only.** The original writes a
  value derived from `GetTicks` into the unit's world Y and tilt. RWE hashes position,
  pitch and roll (`UnitPhysicsInfoGround`, `GameHash_util.cpp`), and `CLAUDE.md`
  ("Nothing a frame knows may reach the simulation") forbids a frame or wall-clock term
  there. So the bob has to be a **presentation offset** applied in the renderer from the
  effects clock, with the speed envelope read from the simulation's own ticks; the
  simulation Y and tilt stay the bob-free values. This is a deliberate divergence in
  waiting and belongs in section 88 when it is built.
- **Off the right or bottom edge: DIFFERS, latent.** The original returns without
  writing if any sample falls outside the usable range, leaving the previous Y and tilt.
  RWE's `getHeightAt` returns 0 off the map (`MapTerrain.cpp:253-277`), so a unit standing
  within half its footprint of the east or south edge on high ground is tilted toward a
  phantom zero-height drop. The map's playable area stops short of the edge
  (`bottomCutoffInWorldUnits`, `rightCutoffInWorldUnits`) so this is rarely seen.
- **Bilinear sampler: DIFFERS** (`BT-UNITS-UNITS-PARITY.md`, rule 11).

---

## 9. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed. The demo
corpus carries damage records (`0x0b`) but not script calls, so no demo oracle applies to
rows 1 and 2.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | `HitByWeapon` (and `TakeDamage`) are never started, so 25 of the 157 base scripts lose their damage reaction. Rule 5 | `src/rwe/sim/GameSimulation.cpp:3494-3650` (the surviving-hit branch), the projectile's heading at impact (`Projectile.h`, `applyDamageInRadius` `:3717`), `src/rwe/sim/UnitBehaviorService.cpp` (the call site, beside `RockUnit` `:1715`) | **Spec-room first**, for the argument formula, the sign and axis order, and which damage kinds run it. Then a retail look at ARMFLASH or ARMBULL taking fire. Script state is not hashed; the call is deterministic. |
| 2 | Alignment: which four points, Y as the mean of four samples, the gating flag for the sea clamp and the bob, and the bob itself as render-only. Rule 8 | `UnitBehaviorService.cpp:1869-1941`, `UnitBehaviorService_util.cpp:1168-1202`, `UnitDefinition.h`, the renderer's unit transform in `game/GameScene_render.cpp` | **Yes** for the flag (CONFLICT): one retail look at a hovercraft and a ship in a swell. The corner source is a spec-room question (what the "designated row" is in the 3DO). Y change is hashed; the bob is not. |
| 3 | Ground-point target Y: re-read `max(ground, sea)` at aim time. Rule 3 | `UnitBehaviorService.cpp:5574-5577`, the order's creation in `game/GameScene_input.cpp` | Light. Static in the register; check whether the order already clamps. |
| 4 | Ground targets quantised to whole units | `UnitBehaviorService.cpp:5574` | No. Negligible; record as a divergence if the demo comparison ever cares. |
| 5 | Unit property text (speed, acceleration, turn rate, 0.4 factor) | the unit information panel in `game/GameScene_menu.cpp` | Spec-room: the exact formulas; presentation only. |
| 6 | Edge bail in alignment; paralysis as an order head | `MapTerrain.cpp:253-277`, `GameSimulation.cpp:3595-3614` | No. Latent / rare. |

### Documented divergences this audit leaves alone

The pending-death two-phase kill (section 132); the lack of a reference-list protocol
(generational ids); paralysis held as a timer rather than an order (the register's own
"reasoning about the order machine" note); the unit-tick stage order (section 132).

### Conflicts with existing RWE findings, in one place

1. **Ground alignment corners and clamp flag.** Register: the corners are the
   definition's corner offsets and the sea-level hold with a wave term is for a
   **floater** (`TOTALA-EXE-MOVEMENT.md`). Reference: the corners are four model vertices
   and the clamp and bob are for definition **bit 0x1000**, which the register names
   `canhover`. Open.
2. **Aim-lead threshold.** Not a conflict: the chapter's "more than five kills" and the
   register's "six or more" are one rule.
