# Movement classes, passability maps and occupancy refresh parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for the 32-entry movement
class table, how a class is read from `MOVEINFO.TDF`, what a pass map is, how
occupancy by units ages in and out of it, and the small helpers filed beside
them, per `AGENTS.md` and ADR-0004. The class record's defaults and the cell
test are `TOTALA-EXE-MOVEMENT.md` section 95 and the terrain audit
(`BT-MAP-TERRAIN-PARITY.md` rules 7 and 8); they are cross-referenced, not
restated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-movement-class.md`).
  `units-unit-types.md` was also read for the loader's half of the same
  subject (it is audited in `BT-UNITS-UNIT-TYPES-PARITY.md`). Nothing else under
  that tree was opened: no source, no decompilation, and none of its source
  links were followed. Addresses are the ones the chapter names; rules are
  restated in prose, with constants and arithmetic, and nothing is copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. A census of the shipped `MOVEINFO.TDF` and FBIs was taken with
  `tools/hpi.py` (read-only) where a rule needed to know whether shipped data
  can tell two readings apart; it is cited where used.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/io/moveinfotdf`,
  `src/rwe/io/fbi` and `src/rwe/pathfinding` at the working tree of 2026-10-16
  (started at HEAD `7c163690` on `ai-envoy-unload-commander-flee`; HEAD was `404f3c4d` on `bt-sim-conformance` when it finished, other agents having moved the tree meanwhile). Nothing was built,
  run or changed, and tests were not run; a MATCHES below means the code reads as
  the rule says. Another agent had uncommitted edits in `GameSimulation.cpp`
  while this was written, so line numbers in that file are as read and may have
  moved by tens of lines.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Class table, record defaults and the MOVEINFO parser
2. Name lookup and the fall-back to FBI-local movement values
3. Two-bit storage and the 0 / 1 / 3 grades
4. The raw grade: features, depth, slope, and occupancy that ages
5. Full construction: footprint minima and edge grading
6. Local refresh and the origin-expanded rectangle
7. Unit timestamps and delayed occupancy updates
8. Teardown, logo selection, the four-field copy and the map checksum gate
9. Gaps to fix

---

## 1. Class table, record defaults and the MOVEINFO parser

**Reference (section 1; and `units-unit-types` section 2).** There are exactly
**32** class records of 32 bytes. The constructor (`0x4402e0`) seeds a record
with no name and no map, footprint zero, **maximum water depth 10000, minimum
water depth minus 10000**, and all four slope thresholds 255. The parser
(`0x440340`) overlays only the keys the block names: footprint X and Z default to
zero; depth and slope fields keep their seeded values; `BadSlope` defaults to
half of `MaxSlope` and `BadWaterSlope` to half of `MaxWaterSlope`. Three clamps
follow: **`MaxSlope` is lowered to `MaxWaterSlope`**, then `BadSlope` to the
resulting `MaxSlope`, then `BadWaterSlope` to `MaxWaterSlope`. The values are
narrowed into short and byte fields by assignment and are **not** range-checked
first. The full loader reads `CLASS0` through `CLASS31` from
`gamedata/moveinfo.TDF`; a block that is absent is skipped.

**RWE.** `parseMovementClassBlock` (`src/rwe/io/moveinfotdf/io.cpp:7-25`),
`parseMoveInfoTdf` (`:28-38`), the load in `GameSimulationLoader.cpp:335-350`.

- **Depth and slope defaults: MATCHES in effect on shipped data**, with the one
  departure `BT-MAP-TERRAIN-PARITY.md` rule 7 already records: RWE defaults a
  missing `MaxWaterSlope` to `MaxSlope` (`io.cpp:14`) where the original seeds 255.
  The shipped `MOVEINFO.TDF` (both the 13-class `totala1.hpi` file and the
  15-class `rev31.gp3` file) shows where that matters: the land classes naming
  only `MaxSlope` (KBOTSS2 32, KBOTSF2 11, TANKDS2 32, TANKSH3/TANKSH2/TANKBH3 15)
  are limited on wet cells in RWE and not in the original. Not repeated here.
- **`BadSlope` / `BadWaterSlope` defaults and their two clamps: MATCHES**
  (`io.cpp:19-24`, section 95, ported 2026-09-24).
- **The first clamp (`MaxSlope` lowered to `MaxWaterSlope`): MISSING.** RWE clamps
  the two "bad" thresholds and never lowers `MaxSlope`. No shipped class names a
  `MaxWaterSlope` below its `MaxSlope` (TANKDH3 names 30 against 15; the two
  hover classes name 255), so nothing shipped can show it. A mod class naming a
  smaller water slope is limited on dry land by the water figure in the original
  and by its own figure in RWE. **DIFFERS**, mod data only. Note the order too:
  the original clamps `BadSlope` to the **already lowered** `MaxSlope`, RWE to the
  unlowered one.
- **Required keys: DIFFERS, and it is a robustness fault.** `Name`, `FootprintX`
  and `FootprintZ` are read with `expect...` (`io.cpp:9-11`), which throws on a
  missing key, and the call is not guarded in the loader
  (`GameSimulationLoader.cpp:344`), so one malformed class block ends the game
  load. The original defaults the footprint to zero and a nameless block simply
  never matches a lookup. `docs/SECURITY-AUDIT.md` ("a fault in content costs that
  content and never every peer") is the standard; a missing `MOVEINFO.TDF` also
  throws (`:337-341`), where the original's loader requires the file as well, so
  that half **MATCHES**.
- **No 32-entry bound, no `CLASSn` naming: DIFFERS, harmless.** RWE reads every
  top-level block whatever it is called. A mod with more than 32 blocks gets all
  of them; the original keeps 32. Shipped data has 13 or 15.
- **Narrowing: DIFFERS, mod data only.** The slope fields are bytes in the
  original, so `MaxSlope=300` wraps to 44; RWE stores an unsigned int and
  compares the full figure. The depth fields are shorts, the minimum **signed**.

**Remaining uncertainty.** None for shipped data.

## 2. Name lookup and the fall-back to FBI-local movement values

**Reference (section 1; `units-unit-types` section 5).** `FindMovementClass`
(`0x440420`) is a linear, **case-insensitive** search of the non-null names and a
miss returns null. A unit whose `MovementClass` names nothing found reads the
equivalent values straight from its own FBI into a temporary class: footprint X
and Z, minimum and maximum water depth, and land and water slope limits. A
named class **overrides** the FBI's own footprint and depth fields
(`TOTALA-EXE-DATA.md` section 30, which already records that an FBI's
`MaxWaterDepth=0` is ignored once a class is named). A default zero-footprint
record is not a usable navigation shape on its own.

**RWE.** `MovementClassDatabase::resolveMovementClassByName`
(`MovementClassDatabase.cpp:24-27`, an unordered map keyed by the exact string),
`parseUnitDefinition` (`LoadingScene_util.cpp:493-`), the ad-hoc branch
(`:636-642`), `GameSimulation::getAdHocMovementClass`
(`GameSimulation.cpp:2982-3005`), the FBI side in `src/rwe/io/fbi/io.cpp:77-86`.

- **Named class wins over the FBI: MATCHES.** A resolved name sets a
  `NamedMovementClass` and the FBI's own footprint and depth fields are never
  consulted for movement.
- **Case-insensitive lookup: DIFFERS, mod data only.** The map is case-sensitive.
  A census of all 272 shipped FBIs found every `MovementClass` value spelled
  exactly as in `MOVEINFO.TDF` (and no mobile, non-flying unit without one), so
  shipped data cannot show it; a mod writing `tanksh3` would silently fall to the
  ad-hoc branch, with `MaxWaterDepth` zero and a footprint of whatever its FBI
  says.
- **Ad-hoc defaults: DIFFERS, mod data only.** The original's fall-back record
  starts at maximum depth 10000, minimum minus 10000, slopes 255, so an FBI that
  names no depth key may stand in any depth. RWE reads the FBI's depths with a
  default of zero and raises the maximum to 255 only when it is **below** the
  minimum (`src/rwe/io/fbi/io.cpp:77-86`), so an FBI naming neither key gets maximum 0: it can
  stand on no wet cell at all. The same census found every shipped building
  names at least one depth key (93 only a maximum, 26 only a minimum, 6 both),
  and the ones naming only a minimum reach the same answer through the "raise to
  255" step, so **shipped data cannot tell the two apart**.
- **`MaxWaterSlope` default in the ad-hoc path** is the same departure as rule 1.

**Remaining uncertainty.** None for shipped data.

## 3. Two-bit storage and the 0 / 1 / 3 grades

**Reference (sections 2, 3).** Each pass-map location is a two-bit value. Sixteen
vertically stacked cells share one 32-bit word; words run across X within a
sixteen-row band; the word index is width times (Y divided by 16) plus X and the
shift is twice (Y modulo 16). `ResizePassMap` (`0x440470`) sizes the buffer to
width times ceiling(height / 16) words and **does not initialise it**; the build
fills it. `SetPassMapCell` (`0x4404c0`) clears two bits and ORs the supplied
shifted value and assumes 0 to 3 and valid coordinates; the inline getter masks to
3, and `InBounds` is a separate call. The grades are **0** blocked (by feature,
occupancy, depth or excessive slope), **1** passable but outside the preferred
slope threshold, and **3** preferred. A map is class-dependent.

**RWE. DIVERGENCE (structural), equivalent on static terrain.** RWE keeps one
`Grid<char>` of walkable origins per class (`MovementClassCollisionService.cpp:100-120`,
registered once at load), computed from terrain only, and answers "free, tight or
blocked" on demand: walkable from the grid, "rough" from the class's `badSlope`
and `badWaterSlope` per cell, "tight" from `isAdjacentToObstacle`
(`GameSimulation.cpp:2143-2153`). That is the same three-valued answer
(`BT-MAP-TERRAIN-PARITY.md` rule 8 records the result scale as MATCHES in effect)
with no packed table to keep current. The grid is smaller than the map by the
footprint (`:107-112`), so an origin the footprint would hang off the map from is
simply not walkable: this **MATCHES** the chapter's zero-padded boundary (rule 5).
No cache means none of the reference's cache hazards (an uninitialised map, an
out-of-range write, a stale entry) exist.

## 4. The raw grade: features, depth, slope, and occupancy that ages

**Reference (section 3; terrain module `0x47de60`).** The raw test resolves
**feature continuation cells** to their origin, applies feature blocking,
checks the primary occupant, then the terrain: cell bounds against sea level
minus the maximum depth and the minimum depth, then the land or water slope
threshold chosen by the cell's lower bound. **Occupancy depends on time.** A
primary occupant **without a motion object blocks; one with a motion object whose
timestamp is older than the class's timestamp blocks; a more recently moving
occupant can be treated differently**, "so that transient movers do not become
identical to permanent structures in cached route data". Immediate collision and
placement still have separate live-object tests.

**RWE.**

- **Features, depth and slope: MATCHES** `TOTALA-EXE-MOVEMENT.md` section 95
  (the feature's type, not instance, blocks; no hover exemption; the wet or dry
  threshold per cell is the one place RWE differs, `BT-MAP-TERRAIN-PARITY.md`
  rule 7 item 2, not repeated).
- **Occupant ageing: DIFFERS, and not recorded in section 88.** RWE has no
  notion of how recently an occupant moved. The search's collision test is
  `GameSimulation::isCollisionAt(rect, self, passable)` / `cellBlocksUnit`
  (`GameSimulation.cpp:2060-2110`), called from the rough-terrain and walkability
  steps of the pathfinder (`pathfinding/AbstractUnitPathFinder.cpp:258-264`), and
  it treats **every** other mobile unit in a cell as a wall, however long it has
  been standing or moving. In the original a unit that has moved in roughly the
  last 30 ticks is not a wall to a search made now (rule 7), so a column of units
  in motion does not wall in the units behind it. The register is silent: section
  87 says only that the table is "kept current by units stamping themselves in"
  and section 95 treats units as absent from the question.
  `TOTALA-EXE-EXTERNAL.md` (not read by this project, "Occupancy ageing") states
  the same rule independently: the watermark is the larger of the tick and 30,
  minus 30, so a unit committed at tick *c* becomes a wall to a same-class search
  at tick *c* + 31 or later, and nothing blocks by occupant age for the first 31
  ticks of a game. Two independent static readings agreeing is the strongest
  evidence available without a runtime trace; neither was executed.
- **Why it could matter, and why it is hashed.** RWE's goal relaxation, the
  `blockedGoalSearchRadius` ring and the crowd measurements in section 88 were
  all taken against a world where every unit is a wall, and the worst of them
  ("nine path requests in ten aim at a cell something is standing on") is partly
  an artefact of that. Which units count as obstacles changes every search result
  and so every unit position, hashed. **Replay-breaking if changed.**

**Remaining uncertainty.** Which of a unit's two stamps the "older than the class
timestamp" test reads (the motion object's last-move tick, rule 7), and whether a
unit that has stopped for under 30 ticks blocks. Both are readable from the same
spec-room routine.

## 5. Full construction: footprint minima and edge grading

**Reference (section 4).** `BuildPassMap` (`0x440500`) allocates one scratch row
sized for the larger of the map and footprint axes plus padding. **Pass one**
walks each terrain row, obtains raw grades, takes the **minimum over the
horizontal footprint extent** and writes the packed map, **downgrading a fully
preferred interval to 1 when its neighbouring samples are below 3**. **Pass two**
reads each packed column into scratch and repeats the minimum and the edge grading
over the vertical extent. The scratch's zeroed padding makes the boundary read as
blocked neighbours, so a footprint never continues off the map. A one-cell-wide
gap is blocked for a large ship even when every cell of it is passable; a grade-1
fringe can steer a route without making a position impossible. `BuildAllPassMaps`
(`0x440940`) builds every named class and drives a progress byte that starts at
100 and adds 100 per class before dividing, so intermediate progress values are
not bounded; with no classes the final value is still 100. `NopAfterFeatureEnum`
(`0x440930`) is empty.

**RWE.**

- **Footprint minimum: MATCHES in effect.** `isGridPointWalkable`
  (`sim/movement.cpp:5-18`) tests every cell of the footprint for slope and depth
  and refuses on the first, which is the same answer as a minimum over the
  footprint; the pathfinder asks it per origin. The one-cell-gap property follows.
- **Edge grading from the neighbouring samples: UNCLEAR whether RWE reaches the
  same cells.** This chapter builds the full map by two separable passes
  (horizontal, then vertical), each downgrading from the **neighbouring raw
  samples** just outside the interval. `TOTALA-EXE-MOVEMENT.md` section 95
  describes the same effect as the footprint answer followed by the four
  one-cell strips around it, and the chapter's own local refresh (rule 6) uses the
  strip form. If the two forms were exactly equal the original would have no
  reason to carry both; whether a full build and a refresh of the same cell can
  disagree (for instance at a corner cell that is individually unusable) is not
  decidable from the prose. RWE has only the strip form, on occupancy alone
  (`BT-MAP-TERRAIN-PARITY.md` rule 8).
- **Progress byte: N/A** (a loading-screen artefact).

**Remaining uncertainty.** The separable-versus-strip equivalence. A spec-room
read of the corner case is enough; no original check is needed to settle it.

## 6. Local refresh and the origin-expanded rectangle

**Reference (section 5).** `MovementClass::RefreshPassMap` (`0x440830`) takes a
changed origin and size, **extends the rectangle left and up by the class footprint
and one cell right and down**, clips it to the map and recomputes each origin
with the whole-footprint-plus-edge-strips query. The changed cells and the
affected origins are **not the same rectangle**: one tree can invalidate a wide
footprint's origins several cells away, and its edge preference changes nearby
grades. `RefreshAllPassMaps` (`0x440a40`) applies it to every named class.
Feature placement and removal and unit occupancy and yard changes call it.

**RWE. N/A by construction.** Nothing is cached, so nothing needs refreshing, and
the "wider than the changed cell" rule is satisfied by the footprint-sized test
at the moment of the question. The one RWE structure that *is* derived from the
terrain, the connected-component labelling used for the unreachable-goal early
out (`MovementClassCollisionService.cpp:11-76`, `tryGetComponentGrid`), is built
from the terrain-only grid and so cannot be made stale by a building or a wreck;
it can only answer "disconnected by terrain", which stays true.

## 7. Unit timestamps and delayed occupancy updates

**Reference (section 6).** `RefreshPassMapsForUnit` (`0x440a70`) uses the unit's
**motion timestamp** when it has a motion object: it saves the old stamp, stamps
the current tick and refreshes the classes whose timestamp is newer than the
saved one; a unit without motion refreshes all classes over its own footprint.
`MovementClass::RefreshMovedUnits` (`0x440af0`) advances a class's reference time
to **current tick minus 30**, clamped so early ticks do not underflow, stamps the
requester current, refreshes its footprint if its previous stamp predates the old
class time, and, when the window advanced, scans live moving units whose stamps
crossed the old-to-new interval and refreshes their footprints, then restores the
requester's prior stamp. `RefreshUnitIfStale` (`0x440be0`) refreshes only a
requester whose stamp is older than the class's. These paths do not imply every
route is rebuilt every tick, that all moving units are collision-free, or that a
stamp alone authorises movement through an occupied footprint.

**RWE.**

- **The stamp itself: MISSING in the sense that matters, though a stamp exists.**
  `TOTALA-EXE-MOVEMENT.md` section 102, "What is not settled", records
  `mover+0x2A`, "stamped with the tick the unit last changed map square or
  movement mode", as having **no reader found**. This chapter supplies one: it is
  the **motion timestamp** that the occupancy refresh compares with the class's
  moving window. That answers the register's open question as a reading, not a
  measurement. RWE keeps no equivalent field; the nearest are the movement
  state's request tick and the unit's previous position, neither of which is a
  last-moved tick.
- **The 30-tick window: MISSING**, see rule 4.

**CONFLICT with the register's wording, not with a finding.** Section 102 says the
stamp "may be a stuck timer read from somewhere further away, or dead". The two
readings are compatible if the reader is the occupancy refresh, which that
section did not trace. Recorded in `BT-NEW-CONFLICTS.md` (Batch 2).

## 8. Teardown, logo selection, the four-field copy and the map checksum gate

**Reference (sections 7, 8).** `FreeMovementClasses` (`0x440a00`) releases each
name and pass map and clears dimensions and pointers; it frees no unit records and
no global terrain. `FindUnusedLogo` (`0x440c10`) marks the logos used by active
controller roles 1, 2 and 3 with valid slot indices (a colour index of 9 or above
occupies mark 9) and returns the lowest free index, **or zero when none is free**,
so zero is both a valid answer and the all-used fallback. `CopyPathLockFields`
(`0x440ca0`) copies four integers from source offset +4 to destination +0x99 with
no stated geometry. `CheckMapCrc` (`0x440cd0`) rejects a zero terrain length;
with a host-info record advertising **version 2 or later, or major 1 and minor 2
or later**, it compares the mission's computed map checksum with the advertised
one; older versions or no host skip the comparison. It does not hash the pass-map
bytes and is not a pathfinder check.

**RWE.**

- **Teardown: N/A.** A simulation is built per game and dropped whole.
- **Logo selection: not audited.** It is the lobby's colour and side-logo
  allocation (`MainMenuScene_multiplayer.cpp`, presentation). The edge case worth
  knowing is the zero fallback: a lobby that wants "no free logo" to be an error
  cannot read it from this routine's result.
- **The four-field copy: no semantics to compare.**
- **The map checksum gate: the routine is `TOTALA-EXE-DATA.md` section 117's;
  the version gate is new.** `src/rwe/net/ta/` and `TA-NETWORK.md` own the TA
  host and joiner side; whether RWE's joiner compares the checksum for every host
  version, where TA skips it below version 2, was not traced. **UNCLEAR**, and a
  mismatch there only matters against a real TA host.

## 9. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Occupant ageing: a unit that has moved within the last 30 ticks is not a wall to a search; every unit is one in RWE. Rules 4, 7 | `src/rwe/sim/GameSimulation.cpp` (`cellBlocksUnit`, `isCollisionAt` overloads), `src/rwe/pathfinding/AbstractUnitPathFinder.cpp:258-264`, a last-moved tick on `UnitState` (`UnitState.h`, `game/save_util.cpp`, `sim/GameHash_util.cpp`, `game/dump_util.cpp`) | **Yes, but it is a trace rather than a play:** spec-room read of `0x47DE60`'s occupant arm and of which stamp it reads; then `path_bench`'s crowd and `pressed-*` scenarios A/B. Hashed and replay-breaking: version-gate. The register is silent, the two static readings agree. |
| 2 | A malformed or partial class block ends the whole game load; missing `FootprintX/Z` should default to zero and the class be skipped or unusable. Rule 1 | `src/rwe/io/moveinfotdf/io.cpp:9-12`, `src/rwe/game/GameSimulationLoader.cpp:344-349` | No. Add a case to `src/rwe/io/malformed_input.test.cpp`. |
| 3 | Case-insensitive class lookup | `src/rwe/sim/MovementClassDatabase.cpp:16-27`, `MovementClassDatabase.h` | No. Mod data only. |
| 4 | `MaxSlope` lowered to `MaxWaterSlope`, then the bad thresholds clamped to the lowered figure. Rule 1 | `src/rwe/io/moveinfotdf/io.cpp:19-24`, and the same pair in `src/rwe/io/fbi/io.cpp:77-78` | No. Mod data only; do it together with `BT-MAP-TERRAIN-PARITY.md` rule 7 items 1 and 2, which change the same arithmetic. |
| 5 | FBI-local depth defaults (10000 and minus 10000) in place of zero with the "raise if below minimum" patch. Rule 2 | `src/rwe/io/fbi/io.cpp:77-86`, `src/rwe/sim/GameSimulation.cpp:2982-3005` | No. Mod data only. |
| 6 | Whether a full build and a local refresh of the same origin can disagree (corner cells). Rule 5 | none until answered | Spec-room read only. |

### Documented divergences this audit leaves alone

The goal-relaxation ring and the 64-unit repath tolerance (section 88); the
arrival radius of sixteen and eight world units against the original's five
(section 88, section 102); the pathfinder's queue in place of the original's round
robin (section 88).

### Conflicts with existing RWE findings, in one place

1. **The motion timestamp's reader.** Register (`TOTALA-EXE-MOVEMENT.md` section
   102): `mover+0x2A` is stamped when a unit changes map square or movement mode
   and has no reader. Reference: the occupancy refresh reads the motion
   timestamp. Compatible; the register's "may be dead" is the part this
   contradicts. Open.
2. **Units in the search.** No register finding says how units figure in a pass
   map; RWE treats every mobile unit as a wall. The reference and an independent
   second reading (`TOTALA-EXE-EXTERNAL.md`, not read by this project) agree they
   do not, for 30 ticks after a move. A conflict with an RWE design choice, not
   with the register. Open.
3. **No other conflict.** The class record, its defaults, the three clamps and the
   32-entry table agree with section 95 and `TOTALA-EXE-DATA.md` section 30.
