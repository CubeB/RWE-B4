# Terrain placement, occupancy and spatial queries parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for terrain occupancy,
building-site legality, mobile-footprint legality and the navigation cell
values, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\map-terrain.md`).
  Nothing else under that tree was opened for this audit: no source, no
  decompilation, and none of its source links were followed. Addresses below
  are the ones that chapter names; the rules are restated in prose with
  constants and arithmetic, and nothing is copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. It is weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Where the two disagree each is stated and marked **CONFLICT** with
  no winner picked; where the reference is silent an existing RWE finding
  stands and the entry says so.
- **No executed observation was used.** No original-game play and no demo
  oracle was run. The Gaps table names where one would settle a question.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/io/moveinfotdf`,
  `src/rwe/pathfinding` and `src/rwe/game` at the working tree of 2026-10-16
  (HEAD `7c163690`, branch `ai-envoy-unload-commander-flee`). Nothing was
  built, run or changed, and tests were not run; a MATCHES below means the code
  reads as the rule says. The shipped `MOVEINFO.TDF` (in `rev31.gp3` and
  `ccdata.ccx`) and a few shipped FBIs were read with `tools/hpi.py` to check
  which classes a rule bites on.

Status words: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR** (cannot be
settled from the reference and the code alone), as in the earlier audits.
**DIVERGENCE (documented)** marks a difference the register already records as
on purpose (`TOTALA-EXE.md` section 88) or as decoded and not ported (section
91). **CONFLICT** marks a place where the reference disagrees with an existing
RWE finding.

Already covered, and not restated: the fog-aware placement gate and the map-edge
margin (`TOTALA-EXE-RENDER.md` section 27 and its correction), the blocking-bit
readers, free/tight/blocked and the movement-class record (`TOTALA-EXE-MOVEMENT.md`
section 95), the drop-legality test (`TOTALA-EXE-TRANSPORTS.md` section 37),
pad slots and the repair-patrol reclaim scan (`TOTALA-EXE-ECONOMY.md` sections 94,
130, 131), and the geothermal rule (`TOTALA-EXE-DATA.md` section 15). Where this
chapter corroborates one of those it is said once, below.

## Index

1. Cell record, the two unit slots, spatial buckets
2. Adding and removing occupancy
3. Building placement: the margin and the observer-conditioned checks
4. Building placement: yard-mask-selected checks and the whole-footprint spread
5. Building height and water buildings
6. Yard transitions
7. Mobile-footprint legality: depth and slope limits
8. Navigation cell values and the clearance strips
9. The candidate-position probe and pad slots
10. Area, rectangle and range visitors
11. Reclaimable-resource sampling
12. Gaps to fix

---

## 1. Cell record, the two unit slots, spatial buckets

**Reference (sections 1, 2).** A terrain cell is a 13-byte record: two unit ids,
sample and bound heights, a metal byte, a feature id, feature-origin offsets and
flags. A unit is separately linked into a coarse spatial bucket list, and an
attached child need not be in any bucket. The word "owner" on the bucket call
means the bucket, not the player. The two unit slots of a cell are not an
unrestricted list of overlapping models and their names do not define surface
and underwater layers.

**RWE. MATCHES in effect, with no counterpart needed for the buckets.**
`OccupiedCell` holds one `mobileUnitId`, a building record with a passable flag,
and a feature id (`src/rwe/sim/OccupiedGrid.h`); the spatial index is derived,
rebuilt each tick, never saved and never hashed, and returns a superset of every
query so the exact test still runs (`CLAUDE.md`, "Determinism"). The reference's
second slot is, by `TOTALA-EXE-TRANSPORTS.md` section 36 ("set mode 2 (take
off)"), the airborne layer: RWE reaches the same layering by taking a flying
unit out of the grid altogether (`UnitBehaviorService.cpp:6580-6592`) and
stamping it back on landing (`:6594-6628`). **UNCLEAR** only in that the
reference declines to name what mode 2 is; nothing in RWE depends on it.

**Remaining uncertainty.** None that changes a rule.

## 2. Adding and removing occupancy

**Reference (sections 3, 4).** Adding a mobile unit writes the primary slot (mode
1) or the secondary slot (mode 2). An existing occupant is not always
overwritten: the routine considers whether it has an active type-3 owner, sets
conflict and reclaim flags on the units involved and either replaces the slot or
leaves it for later reconciliation. A building stamps cells by its yard mask and
refreshes the pass maps over a rectangle padded by one cell. Removal clears a
unit slot only where it still holds the removed unit's own id, clears a
building's blocking bits, recomputes the padded height bounds, and a conflict
flag then makes neighbours reclaim their footprints. A pending claim is retained
rather than pretending every conflict resolved at once.

**RWE.** `GameSimulation::tryAddUnit` (`GameSimulation.cpp:1767-1800`) refuses
the creation outright if any footprint cell is already blocked
(`isCollisionAt`, `:1999-2033`); there is no conflict flag, no deferred
reconciliation and no replacement. This is the opposite policy: the original
creates and reconciles, RWE refuses. For a factory the original's own site check
only waits (`TOTALA-EXE.md` section 88, "A unit blocking a build site..."), so
the observable result is the same stall, and for a mission building RWE clears
the scenery (section 88, last entries). **DIVERGENCE (documented)** for those
two; **UNCLEAR** for any other creator, since no case where the original
overwrites a live occupant on purpose has been traced.

Removal differs in a small way: `moveUnitOccupiedArea` clears every cell of the
old rectangle without asking whose id is in it (`GameSimulation.cpp:2275-2285`),
and `transitionFromGroundToAir` does the same (`UnitBehaviorService.cpp:6580-6592`).
**DIFFERS**, latent: the old rectangle is the unit's own, and the collision test
refuses any move that would overlap another mobile unit
(`cellBlocksUnit`, `:2090-2111`), so no case where it clears a stranger has been
found. It becomes a fault the moment two mobile units may legitimately share a
cell, which the reference's secondary slot would allow for an airborne layer RWE
does not stamp.

**Remaining uncertainty.** Whether the original's replace-or-defer decision ever
fires in ordinary play. The "type-3 owner" qualifier is the network-authority
role (`TA-NETWORK.md`), not a game rule.

## 3. Building placement: the margin and the observer-conditioned checks

**Reference (section 5).** The placement routine first rejects footprints that
violate its map-border rules, then optionally applies observer sight and
exploration checks. A null observer takes a different route from a
player-visible query. The existing-blocking-cell rejection (mask bit `0x01`) and
the foreign primary-slot occupant rejection (bits `0x02` and `0x04`) are the two
checks conditioned on the observer's visibility; the blocking-feature rejections
(bits `0x20`, `0x40`) are not described as conditioned.

**RWE.** The margin is the register's (`GameSimulation::isInsideBuildableArea`,
`GameSimulation.cpp:1802-1815`): **MATCHES** (`TOTALA-EXE-RENDER.md` section 27).
The fog gate is `canBeBuiltAtAsSeenBy` (`:1842-1921`), kept on the presentation
side because the simulation's occupancy test must be identical on every peer.

This chapter supplies the **mechanism** that the register's correction said was
unknown: the original's occupant rejections are conditioned on what the observer
has seen, and its feature rejections are not. RWE does exactly that split: a unit
the player cannot see is ignored, a blocking feature always blocks
(`:1886-1904`). **MATCHES**, now with a reason. Two parts are not settled by it:

- **Seen means visible or explored?** RWE tests `canSeeUnit`, the live
  line-of-sight answer. The reference says "visibility-conditioned" and the
  register's play observation says "refuses once the structure has been seen".
  A structure seen once and now in fog is "seen" in the second reading and
  hidden in the first. **UNCLEAR**; one retail play check settles it (see Gaps).
- **Unexplored ground.** RWE returns "cannot build" for any footprint cell the
  player has not explored (`:1865-1874`), explained in a comment as a survey
  leak. The reference says nothing of the kind and the register records no
  original behaviour for it. This is an RWE invention until shown otherwise.
  **UNCLEAR**, and not covered by `docs/compatibility.md`.

**Remaining uncertainty.** The null-observer route is the one the computer
player and the mission loader use; RWE has no separate route and uses the
omniscient `canBeBuiltAt` (`:1817-1840`).

## 4. Building placement: yard-mask-selected checks and the whole-footprint spread

**Reference (section 5).** The scan walks the footprint and the yard mask byte of
each cell selects which checks that cell takes: bit `0x01` existing blocking cell,
`0x02`/`0x04` foreign primary-slot occupant, `0x08` the cell joins the support
height minimum and maximum, `0x10` the cell joins a separate maximum constraint,
`0x20` a blocking feature rejects, `0x40` a feature carrying the high
(indestructible) property rejects, `0x80` the cell wants a vent and one matching
cell somewhere is enough. Every visited cell also adds its metal byte to an
accumulator published for the extractor display. **If support samples exist,
their maximum minus minimum must fit the definition's slope limit** and the
proposed height is their minimum. If there are none, the height is sea level
minus draft (the unsigned-byte waterline), and the separate maximum constraint
cannot exceed that height. The seabed bounds must also fit the unit's maximum and
minimum water depths.

**RWE.**

- **Per-cell mask selection: MATCHES after #684.**
  `GameSimulation::isYardMapPlacementValid` walks the footprint reading
  `yardMapMask` for each
  cell. Bits `0x01` (blocking building), `0x02`/`0x04` (occupant), `0x20`
  (blocking feature), `0x40` (indestructible feature) and `0x80` (vent wanted)
  now apply only where the letter selects them; a `.` cell is not part of the
  building. The loader also skips an unknown character without consuming a cell
  and no longer accepts `g`. `TOTALA-EXE-DATA.md` section 15's vent rule remains
  **MATCHES**.
- **Whole-footprint support spread: MATCHES after #684.** Support cells (`0x08`)
  feed the running minimum low corner and maximum high corner; the spread must
  not exceed `MaxSlope`, and the standing height is the minimum low. Clamp cells
  (`0x10`) feed a separate high maximum that must not exceed that height. The
  stored sample height is not used.
- **Seabed depth bounds: MATCHES after #684**, see rule 7. The minimum support
  low must be at least sea level minus `MaxWaterDepth`, and the larger of the two
  running maxima at most sea level minus the signed `MinWaterDepth`.
- **Metal accumulator: MATCHES in shape.** The extractor's metal figure is a sum
  over the footprint (`GameSimulation.cpp:4400-4405`); the plus-one question is
  `TA-SYSTEMS-PARITY.md` rule 17 and is not reopened here.

**Remaining uncertainty.** None for the letter table or the support-spread bytes;
BT-51 resolves both. RWE keeps its stricter explored-ground rule for the
interface, which is a documented RWE choice rather than this rule's subject.

## 5. Building height and water buildings

**Reference (sections 5, 7).** The footprint height is the minimum support
height, or sea level minus draft when no cell supports. It also computes a
maximum it does not use. Snapping a position to the footprint skips mobile
definitions, aligns in eight-unit increments relative to the sixteen-unit grid
by footprint parity, and takes Y from the height routine. A returned height alone
is not proof that construction is legal.

**RWE.** `GameSimulation::computeBuildHeight` (`GameSimulation.cpp:1607-1681`)
takes the lowest of the four corners of each supporting cell (yard letters that
set bit 3) and falls back to sea level minus `WaterLine` when none supports:
**MATCHES** `0x47D820` as the register and `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md`
read it. The footprint rectangle is the nearest-cell rounding of the position
minus half the footprint (`computeFootprintRegion`, `:1923-1941`), which is the
`+8` rounding of the placement box (`TOTALA-EXE-RENDER.md` section 27):
**MATCHES**. The waterline narrowing to an unsigned byte is not modelled
(`WaterLine` is a plain unsigned integer); no shipped value exceeds 255.

**Remaining uncertainty.** None for shipped data.

## 6. Yard transitions

**Reference (section 6).** A yard-open change is accepted only if the cells the
new state would block hold no other unit; the unit's own id is allowed; features
are not part of the test. The door animation and the navigational transition are
separate outcomes. A successful change reclaims the footprint and refreshes the
pass maps.

**RWE.** `GameSimulation::trySetYardOpen` (`:2685-2704`) refuses when
`isYardmapBlocked` (`:2113-2141`) finds a blocker, then rewrites the building
record: **MATCHES** in shape. **DIFFERS** in the blocker set: RWE also refuses on
another building's cells and on a blocking feature, where the reference names
only a foreign primary-slot occupant. Neither can occur for a shipped factory
yard, whose cells lie inside its own footprint, so this is latent. The door
animation is the script's and is separate in RWE too.

## 7. Mobile-footprint legality: depth and slope limits

**Reference (sections 6, 8).** For a mobile definition the footprint test takes
every cell and applies, per cell: the deepest point (the cell's lowest corner)
must be no deeper than the maximum water depth; the shallowest point (the cell's
highest corner) must be at least the minimum water depth below the surface;
then the cell's slope (highest corner minus lowest) is compared with the limit,
using the ordinary limits for a cell whose lowest corner is at or above sea
level and the water limits for a cell whose lowest corner is below it. The
water-slope allowance applies only to underwater cells. Draft affects vertical
placement elsewhere and must not stand in for the depth limit. This is the same
rule `TOTALA-EXE-MOVEMENT.md` section 95 and `TOTALA-EXE-TRANSPORTS.md` section
37 decode; the chapter **corroborates both** and adds nothing that contradicts
them.

**RWE.** Four departures from that rule, all in the shared walkability test used
by placement, the walkable grid and the pathfinder (`src/rwe/sim/movement.cpp`,
`MovementClassCollisionService.cpp:107-120`):

1. **Default water-slope limit: DIFFERS from the register.** The original seeds
   `MaxWaterSlope` at 255 and `MaxSlope` at 255, and the cross-clamp only lowers
   `MaxSlope` to `MaxWaterSlope` (section 95; `TOTALA-EXE-DATA.md` section 30,
   "Record defaults matter"). RWE defaults a missing `MaxWaterSlope` to the
   class's `MaxSlope` (`src/rwe/io/moveinfotdf/io.cpp:14`). Over the shipped
   `MOVEINFO.TDF` that bites on every land class that names `MaxSlope` and not
   `MaxWaterSlope`: KBOTSS2 (32), KBOTSF2 (11), TANKDS2 (32), TANKSH3 (15),
   TANKSH2 (15), TANKBH3 (15). In the original a wet cell on those classes is
   limited only by depth; in RWE it is limited by the dry slope.
2. **Wet or dry is chosen once for the whole footprint: DIFFERS.** RWE asks
   whether any corner of the (width+1) by (height+1) block is below sea level and
   then applies the water limit to every cell (`movement.cpp:41-70,148-166`).
   The original chooses per cell from the cell's own lowest corner. With the
   original default of 255 this matters far more than it does with RWE's, because
   one wet corner would hand every dry cell of the footprint an unlimited slope.
   Items 1 and 2 have to change together. The pathfinder's rough-terrain cost
   already picks dry or wet per cell (`AbstractUnitPathFinder.cpp:270-300`), so
   only the passability test is out of step with it.
3. **Depth is read from one corner per cell: DIFFERS.** RWE reads the depth at
   the cell's top-left corner only (`movement.cpp:73-102`). The original reads the
   worst case over the cell's four corners (lowest for the maximum, highest for
   the minimum). RWE therefore never examines the far row and column of corners
   (the right and bottom edge of the footprint), and a ship needing
   `MinWaterDepth=15` (BOATD3, BOATD6) can sit one cell nearer the east and south
   shore than the original allows.
4. **Defaults: MATCHES in effect.** RWE defaults the maximum depth to 255 and the
   minimum to 0, the original to 10000 and minus 10000; with depths never above
   255 and never negative they agree. The one place they could part is a mod
   naming a negative `MinWaterDepth` (a ceiling on how high the cell may sit
   above the surface); `extractUint` cannot represent it and the shipped
   `MOVEINFO.TDF` has none. **UNCLEAR**, mod data only.

**Remaining uncertainty.** None on the rule; the size of the effect on play (how
far a tank may climb a steep shoreline while wading) is a retail observation.

## 8. Navigation cell values and the clearance strips

**Reference (section 8).** The cell query returns 0 for unusable terrain, 1 for
usable terrain outside the preferred slope, 3 for fully preferred. It resolves
feature blocking, checks the primary occupant **and the associated motion and
timestamp state**, checks depth, then slope with the land or water limit chosen
by the cell's lower bound. The footprint query propagates any 0 at once and
downgrades to 1 on any merely acceptable cell. The edge-strip query first checks
the occupied rectangle and, if that is fully preferred, checks the four touching
strips including corners; **a non-preferred strip downgrades the result to 1**.
Clearance, not slope, is what "tight" measures.

**RWE.** The strip test lives in `AbstractUnitPathFinder::computeRoughTerrain`
(BT-52, ported in #685) and the slope half uses the class's
`badSlope`/`badWaterSlope` per cell, as `TOTALA-EXE-MOVEMENT.md` section 95
records. It now probes the four pinwheel strips and asks the same rectangle
questions as the footprint: map bounds, blocking features, occupants under the
search's stamp, both depth bounds against the cell's low and high corners, and
the dry or wet slope limit. The differences this audit found are addressed:

- **Strips test the full cell function: MATCHES after #685.** A steep or
  too-deep strip now also makes the cell tight; it is no longer occupancy-only.
- **The bottom strip is placed by the footprint's width: MATCHES after #685.**
  The bottom strip is at `y+h`, `w+1` wide, with the corners belonging to
  exactly one strip each.
- **The occupant only counts in certain motion states: MATCHES the documented
  reading after #685.** The strip uses the search's thirty-tick occupancy stamp,
  so a recently moved mobile unit is excused and a stationary one blocks. The
  finding's confidence in the stamp reading is medium.
- **Result scale.** 0/1/3 maps to blocked/tight/free; RWE keeps a walkable bit and
  a rough bit per cell. **MATCHES** in effect.

## 9. The candidate-position probe and pad slots

**Reference (section 9).** The "may this unit stand at this world position" probe
converts the position to a footprint rectangle, rejects out-of-bounds cases, and
**returns success early when the visibility-mask sample lacks the unit owner's
bit**, so a planning probe is not omniscient. The sample index uses footprint X in
both centre-offset terms (a suspected original bug, only visible for a non-square
footprint). Past that it rejects blocking features, blocking cell flags, foreign
occupants in either slot, bad depth and steep slope; an aircraft without
amphibious permission has its lower terrain bound raised to sea level. The pad
slot test rejects a pad that is itself attached and scans its child list for the
piece index; it does not check the aircraft's footprint, repair permission or
approach.

**RWE.**

- **Pad slot: MATCHES the register** (`TOTALA-EXE-ECONOMY.md` sections 94, 130;
  `UnitBehaviorService.cpp:2263`), which already has the same two conditions.
- **The probe: UNCLEAR / DIVERGENCE (not documented).** RWE's touchdown test is
  the ordinary collision test with the pad's own building ignored
  (`tryTransitionFromAirToGround`, `UnitBehaviorService.cpp:6594-6628`) and the
  landing-site search is separate (`TOTALA-EXE-MOVEMENT.md` near line 2432 has
  the sixteen-unit snap). RWE's test is omniscient; the original's is optimistic
  about ground the owner has not seen. The case is an aircraft ordered to land on
  unexplored ground, rare enough that no play report exists. Not listed in
  section 88.

## 10. Area, rectangle and range visitors

**Reference (section 10).** The footprint-area visitor tests overlap against
roots and attached children; the world-rectangle visitor clamps bucket
coordinates and tests the **root's origin** against inclusive X/Z limits; the
range visitor takes squared horizontal distance against the squared range. A
range visit is a shortlist, not proof the candidate is allied, visible or
targetable.

**RWE. MATCHES in effect.** Every range test in RWE is flat since section 127,
and `UnitSpatialIndex` is a deliberate superset with the exact test run on live
positions. RWE has no origin-only rectangle query to compare; the area-of-effect
scan walks grid cells (`applyDamageInRadius`, `GameSimulation.cpp:3717`) and
measures to the unit's box (`TOTALA-EXE-WEAPONS.md` section 6), which is a
separate, already-matched rule.

## 11. Reclaimable-resource sampling

**Reference (section 11).** The sampler walks a square around the centre in
48-world-unit steps, half the supplied side each way, with **no circular
filter**; it needs two feature flag bits (reclaimable and autoreclaimable), puts
each nonzero resource on its own list (a feature with both is on both), draws
three random indices per non-empty list and returns the highest amount among the
three; it reports success if either list produced one.

**RWE. MATCHES.** `src/rwe/sim/RepairPatrol.cpp:19,153-232`: a 48-unit step, a
square scan, both flags required, two lists, best of three draws with replacement
and a strict-greater rule. `TOTALA-EXE-ECONOMY.md` sections 97 and 131 record the
same routine from the executable; the chapter **corroborates** them and states
the no-circle point more plainly than they do.

---

## 12. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed;
"spec-room" means a prose finding is needed first. Every item below moves
pathfinding or placement and is **replay-breaking**; none may land without a
version gate.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Water-slope default (255, not the dry limit) **and** wet-or-dry chosen per cell. Rule 7 items 1, 2 | `src/rwe/io/moveinfotdf/io.cpp:14-22` and `io.test.cpp`, `src/rwe/sim/movement.cpp:41-70,148-166`, `MovementClassCollisionService.cpp:107-120` (the walkable grid is rebuilt from it), `ai/ReachabilityMap.cpp` (same predicate) | Light. The register already decodes both defaults and the per-cell split. Acceptance: a retail tank wading a steep shoreline. |
| 2 | Depth bounds from the cell's four corners, so the far row and column are examined. Rule 7 item 3 | `src/rwe/sim/movement.cpp:73-102`, `MapTerrain.cpp:71-94,201-213` (the depth cache) | Light. Register decodes it; acceptance is a ship at the south or east edge of a bay. |
| 3 | Whole-footprint support spread for buildings, and per-cell mask-selected checks. Rule 4 | `src/rwe/sim/GameSimulation.cpp:1817-1921`, `:1607-1681`; callers in `ai/BuildManager.cpp`, `ai/classic/ClassicAiPlacement.cpp:54`, `game/GameScene.cpp:775-781` | **Yes.** Spec-room prose first (yard letter to mask bit; which cell bytes feed the spread), then a retail ramp placement. Hashed, and changes where the computer player can build. |
| 4 | Clearance strips ignore terrain; bottom strip uses width. Rule 8 | `src/rwe/sim/GameSimulation.cpp:2143-2153`, `pathfinding/AbstractUnitPathFinder.cpp:262-300` | The width fault: no (latent, fix freely). The terrain-in-strip question: spec-room. |
| 5 | "Seen" for the placement gate (visible vs explored) and the unexplored-ground rule. Rule 3 | `GameSimulation.cpp:1842-1921`, `game/GameScene.cpp:775-781` | **Yes**, one retail play: place over a structure seen earlier and now in fog; place into black. |
| 6 | Landing probe's optimistic early success; no consumer known for the sampled clearance routine | `UnitBehaviorService.cpp:6594-6628`, aircraft landing search | Spec-room: name the callers of the line-clearance sampler (it has no entry in the register). |
| 7 | Occupancy clears only the unit's own id; yard blockers limited to units | `GameSimulation.cpp:2275-2285,2113-2141`, `UnitBehaviorService.cpp:6580-6592` | No. Latent. |

### Documented divergences this audit leaves alone

Creation refuses on a blocked footprint where the original reconciles (section 88,
the build-site entries); a mission building clears the scenery under it (section
88); the fog gate lives on the presentation side so the simulation stays
identical across peers (`TOTALA-EXE-RENDER.md` section 27).

### Conflicts with existing RWE findings, in one place

None found. Two apparent ones are naming only: the chapter calls yard-mask bit
`0x01` an "existing blocking cell" test where `TOTALA-EXE-DATA.md` section 15
calls it the terrain flag at cell byte `+0xC`, and the chapter's "type-3 owner"
is the network-authority role. Both name the same thing. The one place RWE
contradicts the register (the `MaxWaterSlope` default, rule 7) is an RWE
departure from section 95, not a disagreement of the reference with it.
