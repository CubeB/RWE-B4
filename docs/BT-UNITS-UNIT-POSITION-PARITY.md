# Unit positioning, attachment, tick execution and state replication parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how a unit's world position
relates to its footprint origin, how its height is chosen, how it is attached to
and detached from a carrier, what the activation byte does when it changes, what
a unit does in one tick and in what order, and how the original replicates unit
state to its peers, per `AGENTS.md` and ADR-0004. The per-unit order is
`TA-SYSTEMS-PARITY.md` rule 8 and `TOTALA-EXE-MOVEMENT.md` section 132; the
height branches are `NAVAL-SYSTEMS-PARITY.md` rule 2; the wire format is
`TA-DEMOS.md` and `TOTALA-EXE-MOVEMENT.md` section 118. They are cross-referenced,
not restated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-position.md`).
  `units-unit-types.md` was read for the flag names (see
  `BT-UNITS-UNIT-TYPES-PARITY.md`). Nothing else under that tree was opened: no
  source, no decompilation, and none of its source links were followed. Addresses
  are the ones the chapter names; rules are restated in prose, with constants and
  arithmetic, and nothing is copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable and, for the wire format, checked against thirteen recorded games.
  Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run for this audit. The Gaps table says where the corpus could.
- **The RWE half** is a read-only audit of `src/rwe/sim` and `src/rwe/game` at the
  working tree of 2026-10-16 (started at HEAD `7c163690` on `ai-envoy-unload-commander-flee`; HEAD was `404f3c4d` on `bt-sim-conformance` when it finished, other agents having moved the tree meanwhile). Nothing was built, run or changed, and tests
  were not run; a MATCHES below means the code reads as the rule says. **Another
  agent had uncommitted edits in `GameSimulation.cpp`, `GameSimulation.h`,
  `UnitState.h`, `UnitStateFieldTable.cpp` and `aircraftwreck.test.cpp` while this
  was written**; they add a 30-tick health sample to the severity formula
  (labelled BT-43 in the comments). That change is **not audited** here; rule 5
  says how it relates to this chapter. Line numbers in `GameSimulation.cpp` are as
  read and may have moved.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Position, footprint origin and movement mode; the heading helpers
2. Height selection and alignment
3. Attachment request, application and packet
4. Activation byte transitions
5. Per-unit execution order
6. Hazards, self-repair and camera follow
7. The full unit snapshot
8. Reading snapshots and updating occupancy
9. The player packet and the rotating full slot
10. Gaps to fix

---

## 1. Position, footprint origin and movement mode; the heading helpers

**Reference (section 1).** A unit has fixed-point world X, Y and Z and, apart
from them, its footprint-origin cell. The conversion **subtracts half the
footprint in world units, adds half a cell for alignment and divides by the
16-world-unit cell**; the same conversion must be used for old and new occupancy.
`SetUnitPosition` (`0x48a9f0`) takes a position and a **two-bit mode**. If the
derived origin and mode both match it only copies the world position; otherwise it
removes old occupancy, copies position and origin, updates the mode, reinserts
occupancy and refreshes sight. **Both paths set the height-dirty bit (0x10000)**
and return the flags. It validates no route and no construction placement.
`GetHeadingBetween` (`0x48a980`) takes the heading from the horizontal deltas
only (Y is ignored); `GetHeadingBetweenOrDefault` (`0x48a9b0`) returns the
caller's supplied heading **when both horizontal deltas are zero**.

**RWE.** `computeFootprintRegion` (`GameSimulation.cpp`, near `:1923-1941` before
the concurrent edits), `moveUnitOccupiedArea`, `UnitState::toRotation`
(`UnitState.cpp:87-90`).

- **Footprint origin: MATCHES** (`BT-MAP-TERRAIN-PARITY.md` rule 5: the
  nearest-cell rounding of the position minus half the footprint is the `+8`
  rounding of the placement box).
- **Occupancy only when the cell changes: MATCHES in effect.** RWE moves the
  occupied area when the move is applied (`UnitBehaviorService.cpp:2043-2046`),
  and the cell loop is a no-op when the region is unchanged.
- **The mode and the dirty bit: N/A.** RWE has no two-bit placement mode (air and
  ground are different physics variants) and re-snaps height only while moving
  (`NAVAL-SYSTEMS-PARITY.md` rule 2).
- **The default-heading rule: DIFFERS, at the primitive.** RWE has one conversion,
  `toRotation`, which is `atan2(x, z)` of the direction and so returns the angle
  for an all-zero direction (zero, due north). A caller has to remember the guard.
  Two do (`UnitBehaviorService.cpp:2708`, which falls back to a bearing of zero,
  and `:4275`, which keeps the unit's own rotation); roughly fifteen others do not
  (`:1749`, `:1770`, `:1788`, `:1798`, `:1816`, `:1838`, `:1852`, `:2798`,
  `:2869`, `:4113`, `:4139`, `GameSimulation.cpp:2494`, `:4019`,
  `AirMovement.cpp:235`). Where the direction is zero (an aircraft directly over
  its target, a builder standing on its job, a turret or unit ordered to face its
  own position) such a caller snaps the heading to zero. The original returns the
  current heading. **DIFFERS**, with no instance shown on shipped content; the
  fix is one helper that takes the fallback, used at every site.

## 2. Height selection and alignment

**Reference (section 2).** `SnapUnitToGround` (`0x48a7f0`) sets Y to the bilinear
ground height, or for a **`canhover` definition (flag bit 12, the private
"floats" member in the reference's own source)** at least sea level minus draft.
`UpdateUnitHeight` (`0x48a870`) runs when the height-dirty bit is set **or** the
definition can hover; it clears the bit first, and assigns only when the unit has
motion and is in mode 1. Inside that: **`upright` (bit 20)** takes the direct
ground sample (hover definitions clamped to sea level minus draft); otherwise
**`floater` (bit 19)** takes a fixed water-relative Y whose integer expression
reduces to sea level minus draft; otherwise the **four-point alignment** sets
height, pitch and bank from model support points. No motion or another mode
consumes the dirty bit without these assignments. Aircraft and attached units
need their own logic. Draft, permitted depth, slope and floating presentation are
separate inputs.

**RWE.** `updateGroundUnitPosition` (`UnitBehaviorService.cpp:1892-1941`),
`updateGroundTilt` (`:1869-1890`).

- **The branch structure: MATCHES the register and `NAVAL-SYSTEMS-PARITY.md` rule
  2.** The gating difference (every tick when dirty against only when moving) is
  that rule's.
- **This chapter and `units-unit-types` settle the names that
  `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 8 left as a CONFLICT.** Both say the
  flag in the height routines is the **loader's `canhover` key, flag-word bit 12**,
  and the unit-types chapter's flag table (rule 3 of
  `BT-UNITS-UNIT-TYPES-PARITY.md`) agrees with the register's bit assignments for
  every bit it names. The "bit 0x1000" of the targets chapter is therefore
  `canhover`, not `floater`. What survives of that conflict is the register's
  *wording*: `TOTALA-EXE-MOVEMENT.md` section 102's description of the alignment
  routine says the sea-level hold and the wave term apply to "a floater", where the
  height chain above means **a floater never reaches the alignment routine** (a
  non-upright floater is placed by the fixed water branch, which writes no pitch or
  bank at all) and the hold and the wave are `canhover`'s. The register's gate at
  `0x48A60C`-`0x48A716` needs a re-read for which bit it tests; `0x81000` (both
  bits) is what other sites test (`TOTALA-EXE-WRECKS.md`, the underwater cap), so
  it is plausible the register's wording collapsed the two. **Open**; recorded in
  `BT-NEW-CONFLICTS.md` (Batch 2).
- **What that predicts for shipped units.** The census of the 272 shipped FBIs
  found 22 floaters (all ships, none a building), 13 `canhover` (the nine hover
  craft, the two Pelican/Gimp amphibious bots and the two hover transports) and 36
  `upright` (all the amphibious and submarine hulls, and the bots). So the wave
  and the sea-level hold are a **hovercraft and amphibious-bot** feature, and a
  ship rides flat at sea level minus draft with no tilt, which is what RWE does.
  An `upright` hover unit (ARMAMPH and CORAMPH) takes the ground sample clamped to
  the sea floor.
- **The sea floor for a hover unit in RWE:** a floor at sea level, not sea level
  minus draft (`NAVAL-SYSTEMS-PARITY.md` rule 1, shipped data cannot tell).

## 3. Attachment request, application and packet

**Reference (section 3).** `AttachUnitToPiece` (`0x48aac0`) requires: the child
live, without the **immobile-associated runtime bit 0x20000000**, and **carrying
nothing of its own**. A non-null target must be live, differ from the child and
**have no carrier**; a null target requests detachment. So nested transport in
either direction is excluded, but a root target can hold a chain of children. It
broadcasts and applies locally a 7-byte type-10 record (child id, optional target
id, piece byte, low-mode byte). It checks no capacity, no alliance, no pickup
range and no free pad piece: callers do. `ApplyAttachUnit` (`0x48ab70`) repeats
the guards, removes the child from its carrier's linked list or from its root
spatial bucket, stores the transport piece and either **prepends** the child to the
new carrier's chain or restores it as a spatial root. **Piece minus one sets
runtime bit 17 on attachment** (hidden while carried); detachment clears it and the
sibling and carrier pointers; the mode byte is copied into the child's motion; an
active local child attached to a target **without definition bit 9
(`isairbase`)** is given the carried order; selection eligibility is refreshed. The
body assumes consistent list membership and does not repair a corrupt graph.

**RWE.** `canLoadUnitIntoTransport` and `loadUnitIntoTransport`
(`GameSimulation.cpp:773-912`), `UnitState::carriedBy` and `carriedUnits`.

- **Child must not carry cargo: MATCHES** (`canLoadUnitIntoTransport` refuses a
  passenger with `carriedUnits`, `:813`). But that test is in the *predicate*;
  the primitive `loadUnitIntoTransport` (`:856-912`) checks only that the child
  is not already carried, both are alive and they differ. **DIFFERS in the
  primitive**, harmless while every caller goes through the predicate; the
  original keeps the guards in the primitive that all callers share.
- **Target must have no carrier: MISSING in both.** Neither function asks whether
  the **transport** is itself carried. A carried transport has its orders cleared
  (`:888-897`) and is not selectable, so no command reaches it; a script or the AI
  path that did could nest. `TOTALA-EXE-TRANSPORTS.md` section 37/38 does not list
  nesting.
- **Immobile child refused: MATCHES in effect** (`!targetDefinition.isMobile`,
  `:803`; the register's `cantbetransported` is checked first).
- **The carried order only for non-air-base carriers: DIVERGENCE (structural).**
  RWE has no carried order; `carriedBy` is the state, and a unit loaded onto a pad
  does not use this path (`UnitBehaviorService.cpp:6543-`).
- **Hidden while carried:** the register records the same bit (`TOTALA-EXE-DATA.md`,
  "Unit instance fields": hidden-while-carried, bit 17 of `unit+0x110`, "set iff
  attached to piece -1"). **Piece sign.** The register says piece **-1**; the
  chapter says piece **minus one** is the value that sets the bit, i.e. the value
  of the piece byte after the subtraction. These are the same statement in two
  spellings; flagged only so nobody reads "piece 1" into it.
- **The 7-byte record: N/A** to the simulation; `TA-NETWORK.md` owns it.

## 4. Activation byte transitions

**Reference (section 4).** `Unit::SetStateBits` (`0x48b090`) sets or clears the
requested bits of the activation byte and compares old with new. **No change: no
callback and no packet.** On a change: bit 0 gained or lost starts `Activate` or
`Deactivate` and queues the matching speech; bit 3 gained or lost starts
`StartBuilding` or `StopBuilding`; bit 2 gained queues speech and marks linked
order and listener objects with status flag 0x10000, and bit 2 lost queues the
opposite speech. **Bit 2 also takes part in foreign-unit visibility suppression**,
so it is the **cloak** bit, as `TOTALA-EXE-VISION.md` says (`unit+0x10E` bit 2,
"cloaked", written by `0x48B090`); "provisional comments calling every bit a
generic working state should not replace that concrete use". Any change marks the
selection's orders dirty. Active type-1 and type-2 owners **broadcast a 4-byte
type-17 event** (unit id and the new byte); received snapshots apply a set mask and
a clear mask through this same interface, so the transition machinery and not a raw
byte copy reconstructs the state.

**RWE.**

- **No change, no callback: DIFFERS.** `UnitState::activate` / `deactivate`
  (`UnitState.cpp:621-631`) start `Activate` or `Deactivate` every time, and
  `GameSimulation::activateUnit` / `deactivateUnit` (`:2745-2757`) raise an event
  every time. `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 3 lists the call sites and the
  gap. The scene's speech and animation are driven by those events.
- **`StartBuilding` / `StopBuilding` as a state-bit transition: DIVERGENCE
  (structural).** RWE starts them from the build behaviour at the points the
  original's work bit changes (`UnitBehaviorService.cpp:5183-5497`, several sites),
  with an arm-stowing check (`:5230-5245`) the original reaches through the busy
  flag (`TOTALA-EXE-ECONOMY.md` section 110 area). Not compared line by line.
- **Cloak speech and the cloak transition as the same setter: UNCLEAR.** RWE
  charges and applies cloak in the economy settle (`GameSimulation.cpp`, "Cloak is
  paid for out of the same second's energy"), outside any state-bit setter, and a
  cloak-change announcement was not found.
- **The activation byte on the wire: N/A to the sim** (`TA-NETWORK.md`).
- **Naming warning for an earlier audit.** `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 4
  (damage scaling) says "the activation bit 2 (the script's 'armoured' state)"
  enables the damage modifier. This chapter's bit 2 is the cloak bit. The register
  places ARMORED at `unit+0x10E` **bit 1** (mask 0x02) and cloaked at bit 2 (mask
  0x04) (`TOTALA-EXE-WEAPONS.md` near its section 6 discussion of `0x489BC3`;
  `TOTALA-EXE-VISION.md`). The targets chapter evidently means "the bit whose
  value is 2". Read rule 4 there as bit **1**; RWE's `UnitState::armored` is set by
  the script's ARMORED value (`cob.cpp:551`) and is correct.

## 5. Per-unit execution order

**Reference (section 5).** `UpdateAllUnits` (`0x48ad30`) clears the active-unit
count and visits units of active roles 1, 2 and 3 with a valid slot; for every
slot with a non-zero definition it counts the unit and runs, in this order:
(1) wind-generator callbacks; (2) weapons, for active local roles 1 and 2; (3)
existing COB scripts with one tick of execution input; (4) decrement the
recent-damage and ownership-transfer timers when non-zero; (5) clear a
selection/action flag when construction, transfer holdoff, readiness or carrier
permission makes the unit ineligible; (6) **every 30 ticks, retain the previous
health percentage and calculate the current clamped percentage**; (7) for active
local ownership, water damage and self-repair, then primary and secondary orders,
then motion and height; (8) apply pending death when runtime bit 14 is set, using
the stored damage kind. Remote units take the shared presentation, script and
timer work; the local-authority branches are guarded by owner role.

**RWE.** `GameSimulation::tick` (`GameSimulation.cpp`, the behaviour, piece, COB,
carried-unit, self-repair and water-damage passes).

- **The stage order: DIVERGENCE (documented as section 132, deferred on purpose;
  section 91).** `TA-SYSTEMS-PARITY.md` rule 8 already says this chapter "adds
  nothing the register lacks" about the order. That was true of the order; it is
  not true of step 6.
- **Step 6, the 30-tick health sample, is the missing writer of `unit+0xF7`.**
  `TOTALA-EXE-WRECKS.md` and `TOTALA-EXE-WEAPONS.md` section 22 give the kill
  severity as one half of the sum of the overkill percentage and a byte at
  `unit+0xF7` that "has no known writer" and which RWE takes as zero (as of HEAD).
  `BT-UNITS-UNITS-PARITY.md` rule 6 read the chapter for that unit as "the previous
  health percentage" and asked whether it is the value before the lethal blow or
  a byte updated on each hit. This chapter answers it: **it is neither**. It is a
  pair of bytes refreshed on a **fixed 30-tick cadence**, the current clamped
  percentage and the one before it, so the severity term a kill reads is the health
  the unit had **between 30 and 59 ticks earlier** (the sample taken at the last
  but one boundary), not the health just before the blow. Consequences that follow
  from the arithmetic and need no further data: a unit that has been at full health
  for the last second and is destroyed by one blow has 100 in the term; a unit
  killed within its first 30 ticks has whatever the pair held at creation, which
  the creation list does not say (zero is the natural reading); and a unit that
  took most of its damage inside the last second reads the percentage from before
  that damage. The corpus claim that a victim at full health never reads below 50
  becomes testable (severity is at least half of the term, rounded down, and at
  least 1).
- **Status of that change in RWE.** At HEAD there is no such field. An uncommitted
  working-tree change (`sampleUnitHealth`, `UnitState::healthPercentSample` and
  `previousHealthPercentSample`, labelled BT-43) appeared while this audit was
  being written and implements a 30-tick sample read at `gameTime % 30 == 0`. It
  was **not** read for correctness, built or run here. It should be checked against
  two details of this chapter: the pair is refreshed for **every live unit,
  including unfinished ones** only if the original's loop does, and the first
  sample tick (the original numbers its first tick 1, so the first boundary is
  tick 30); and whether the setter's "retain previous" step happens before or
  after the current sample is taken (the chapter says retain then calculate).
- **Step 4, the recent-damage and ownership-transfer timers: UNCLEAR / MISSING.**
  The chapter names no duration, and the register has no finding on either.
  RWE has no equivalent field (`UnitState::isSelectableBy` has no holdoff term,
  `UnitState.cpp:612-619`). If a captured or gifted unit cannot be selected for a
  short time in the original, that is a selection rule RWE lacks; the capture
  section (`TOTALA-EXE-ECONOMY.md` section 96) was not found to mention one.
- **Step 7 hazards: see rule 6.** Step 8 (pending death): section 132 and
  `BT-UNITS-UNITS-PARITY.md` rule 7.

## 6. Hazards, self-repair and camera follow

**Reference (section 6).** **Water damage** needs map water damage enabled and
non-zero, the 30-tick boundary, the unit's whole Y at or below sea level and **no
`canhover`** (not floater); it calls damage kind 11, which the damage helper
excludes from the special reaction and the broadcast. **Self-repair** needs a
non-zero definition rate, health below maximum and the tick's low three bits
zero; it passes the integer `(rate times 8) divided by 30` as float repair
progress, so the truncation happens before the conversion and very small rates
need not produce a positive increment. The repair-progress helper decides
health and account effects. After a player's range, network flag 1 enables
`SendUnitStates` for active local roles; after all players an automatic
camera-follow mode decrements its timer unless key 0xf9 is down, and on expiry
resets it to 90, cycles the selection and the camera.

**RWE.**

- **Water damage: MATCHES** (`NAVAL-SYSTEMS-PARITY.md` rule 14; `canhover`
  exemption only).
- **Self-repair: MATCHES.** `GameSimulation::updateSelfRepair` (near `:5070`):
  every eighth tick, `work = healtime * 8 / 30` in integer arithmetic, nothing when
  it is zero, one hit point and a one-energy request otherwise. This is
  `TOTALA-EXE.md` section 94's decode and `TA-SYSTEMS-PARITY.md` rule 9; the
  chapter's wording ("integer truncation happens before that conversion") is the
  zero-work rule RWE has.
- **Camera follow: N/A** (a scene feature). The reset value 90 and the 0xf9 key are
  presentation; `TOTALA-EXE-KEYBOARD.md` is the place to look if RWE grows it.

## 7. The full unit snapshot

**Reference (section 7).** `WriteUnitState` (`0x48b200`) writes a type index using
the configured definition-index bit width; definition zero ends the record
(an empty slot). A non-empty record then carries health (16 bits), **quantised
remaining construction** (8), the activation byte (8), the low movement mode (2)
and an attached flag (1). An attached unit adds carrier id (15 bits) and piece (8)
and **omits** world position, rotation and speed. An unattached unit adds X, Y, Z
as three 32-bit values, three 16-bit angles and, when it has motion, a 32-bit
speed. Construction zero is encoded as zero; otherwise the byte is **one plus the
truncated product of the remaining fraction and 254**, narrowed to eight bits; the
reader multiplies by about 1/255. It is "a quantised representation, not
bit-exact persistence".

**RWE.** `TaWireTape.cpp:610-640` (the construction byte: zero once complete,
else `1 + trunc(254 * remaining)`, `:621-628`), `tadUnitStateLayout`.
**MATCHES `TA-DEMOS.md` ("The full-state record") and the chapter.** The
register's table row is `1 + trunc(254 * remaining)`; the chapter's "1 minus
trunc(buildLeft times minus 254)" is the same number. The attached form and the
omitted position are `TaWireTape`'s too (not re-read line by line here;
`TA-NETWORK.md` records a real TA peer accepting them).

## 8. Reading snapshots and updating occupancy

**Reference (section 8).** `ReadUnitState` (`0x48b3f0`) reads the definition
first. **Zero marks an occupied local slot pending death** and returns. A
differing non-zero type reconstructs the slot through replicated creation at the
current position and id, invalidates the cached object-picture field, restores
health and quantised construction and marks the construction display dirty (bit
13) when the fraction changes. It restores activation through the setter (rule 4)
and reads mode and attachment: attached records apply the attachment and return;
unattached records first detach a currently carried unit, read position and
angles, and **remove and reinsert occupancy only when the origin or mode
changed**, otherwise copy the position; then set height-dirty, copy angles and
restore speed if motion exists. "Zero type means absent; a zero construction byte
on a non-zero type means complete." The speed field's presence depends on both
sides agreeing on the definition's runtime components.

**RWE. N/A to the simulation, audited elsewhere.** RWE's receive side for a TA
peer is the puppet driver (`src/rwe/puppet/TadPuppetDriver.cpp`,
`TA-DEMOS.md` "Tier 2", `TOTALA-EXE-MOVEMENT.md` section 118: the owner's full-state
record overrides the receiver, an empty-slot record deletes a unit). The one point
this chapter adds is the **"zero marks a local slot pending death"** wording: the
original does not delete on receipt, it marks the unit for the death pass, which
is the two-phase death of section 132. `TA-NETWORK.md` records "only the owner's
`0x0c` kills a unit"; the two are about different records and do not conflict.
Nothing in the sim should copy the original's trust in the record's contents
(`docs/SECURITY-AUDIT.md`).

## 9. The player packet and the rotating full slot

**Reference (section 9).** `SendUnitStates` (`0x48b710`) builds a type-44
bitstream with packet length and current tick, stores the tick as the player's
sync timestamp, visits live units with motion whose motion serialiser qualifies
them, writes the slot index relative to the player's first id, the definition index
and the motion state, and **stops after the write that brings the length to at
least 0x200 bytes**: a soft threshold. It writes the 0xffff terminator, a presence
bit and **one full-unit snapshot selected by game tick modulo `maxUnits`**,
rotating across capacity slots **including unused ones**, so empty snapshots
eventually communicate removal as well as refresh. It patches the length in bytes
1 and 2 (little endian) and broadcasts. `ReceiveUnitStates` (`0x48b920`) mirrors
it: it returns early if the player has no range, reads relative indices to
0xffff, reconstructs on a type mismatch, updates motion and height for every live
moving unit in the player's range, and reads the rotating slot from the packet
tick modulo `maxUnits`. It consumes the advertised length but does not use it for
full bounds validation.

**RWE.** `TaWireTape.cpp:18-28` (the 512-byte cap), `:700-735` (the full-state
slot at `tick % maxUnits`, "whether or not" a unit stands in it): **MATCHES**,
and `TaLiveBatch.h:22` documents the agreed block size. The receive side's missing
bounds checks are a fault to avoid, not to copy: `docs/SECURITY-AUDIT.md`
(every count, size, offset and index from outside is checked before it indexes
anything).

## 10. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.
The demo corpus oracle is `tad_probe` / `tad_episodes` over the recorded `0x0c`
death records (`TOTALA-EXE-WRECKS.md`, "What 60,075 recorded deaths say").

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Make the kill severity's second term the **30-tick health sample pair** and check the in-progress BT-43 change against this chapter (retain-then-calculate order, first boundary, creation value, whether unfinished units are sampled). Rule 5 | `src/rwe/sim/GameSimulation.cpp` (`sampleUnitHealth`, `computeKilledSeverity`, `killUnitWithSeverity`), `src/rwe/sim/UnitState.h`, `game/save_util.cpp`, `sim/GameHash_util.cpp`, `game/dump_util.cpp`, `game/UnitStateFieldTable.cpp` | **Yes, and cheap.** Corpus test: with the term a sample from 30 to 59 ticks ago, a victim whose recorded health was full for a whole second before its death cannot read below 50. `tad_episodes` can score it by the prior-hit counts already in the death table; a victim killed inside its first 30 ticks tests the creation value. Hashed and replay-breaking: version-gate. |
| 2 | State-bit setter semantics: no script and no event when nothing changed. Rule 4 | `src/rwe/sim/UnitState.cpp:621-631`, `src/rwe/sim/GameSimulation.cpp:2745-2757` and the call sites listed in `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 3 | Light. See that audit's rank 1. |
| 3 | One default-heading helper for a zero direction, used at every `toRotation` call. Rule 1 | `src/rwe/sim/UnitState.cpp:87-90` and the fifteen unguarded call sites in rule 1 | No. Hashed only where a zero direction actually occurs; check with the hash oracle. |
| 4 | Attachment guards in the primitive: transport not itself carried, passenger carries nothing. Rule 3 | `src/rwe/sim/GameSimulation.cpp:856-870` | No. Latent. |
| 5 | Ownership-transfer holdoff on selectability, if the original has one. Rule 5 | `src/rwe/sim/UnitState.cpp:612-619`, capture and gift paths | **Spec-room first:** the duration and what sets it. Not found in the register. |
| 6 | Cloak transition as a state change with speech; recent-damage timer's consumers. Rules 4, 5 | `src/rwe/sim/GameSimulation.cpp` (cloak charge), the scene's speech hooks | Spec-room: who reads the recent-damage timer. Presentation-side. |

### Documented divergences this audit leaves alone

The stage order inside a unit's tick (section 132, deferred on purpose); the
two-phase death (section 132); the lack of a two-bit placement mode; the height
snap only while moving (`NAVAL-SYSTEMS-PARITY.md` rule 2); the lack of a carried
order (structural).

### Conflicts with existing RWE findings, in one place

1. **`unit+0xF7`.** Register (`TOTALA-EXE-WRECKS.md`, `TOTALA-EXE-WEAPONS.md`
   section 22): no known writer, taken as zero. Reference: the unit pass refreshes
   a pair of health-percentage bytes every 30 ticks. This answers the register's
   open question, and answers `BT-UNITS-UNITS-PARITY.md`'s reading of the other
   chapter ("previous health percentage") with a narrower meaning. The
   corpus does not contradict it (the register says the distribution "does not
   require" a large term). Open until the corpus test.
2. **The alignment routine's gate.** Register (`TOTALA-EXE-MOVEMENT.md` near the
   `upright` discussion): the sea-level hold with a wave term is for "a floater".
   Reference, now from two chapters naming the loader's keys: the hold and the
   wave are `canhover`'s, and a floater is placed by a separate fixed branch.
   Reduces `BT-NEW-CONFLICTS.md` item 2 to a wording question about one address
   range. Open.
3. **The earlier targets audit's "activation bit 2 is ARMORED".** Reference-internal
   numbering clash (see rule 4); the register is unambiguous (bit 1). Not a
   register conflict, listed because RWE's armour code was written from the
   register and is correct.
