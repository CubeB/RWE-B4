# Sight coverage, exploration, temporary eyes, TNT loading and the ray tables parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for how the
original turns a unit into revealed ground: the two information layers, the cabinet
skew, the ray-traced and circular sight algorithms, the height-band occlusion grid, the
permanent reveal, temporary eyes, the TNT header and environment parameters, the border
exclusion, and the `los.tdf` ray tables those algorithms walk, per `AGENTS.md` and
ADR-0004. Two chapters are audited together because the second is the first's data:
`map-line-of-sight` (29 entries) and `map-los-tables` (50 entries). How a unit becomes
*visible* once the ground is lit (the four model-corner samples, the sonar veto, radar,
jamming, cloak) is `TOTALA-EXE-VISION.md` and `BT-GAME-PLAYERS-PARITY.md` rule 9 and is
not restated here; terrain occupancy and the height cell record are
`BT-MAP-TERRAIN-PARITY.md`.

The register is thinner here than for weapons. `TOTALA-EXE-VISION.md` section 2 gives the
storage, the three modes, the cap of 8 cells, the 90-degree replication, the two height
bands and the drawing; it does not describe the origin cell's arithmetic, the counter
semantics, temporary eyes, the TNT versions or the border exclusion. So this audit has more
new material than the weapons audits do, and three stale lines in the register itself
(rule 11).

## Source and evidence class

- **Source.** Two prose chapters of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree paths
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\map-line-of-sight.md` and
  `map-los-tables.md`). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the ones the
  chapters name; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that rows
  already resolved there are not raised again; the rows this audit touches are BT-6, BT-39,
  BT-56 and BT-81.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was run.
  The Gaps table says which items an original check settles.
- **The RWE half** is a read-only audit of `src/rwe/sim` (`PlayerVisibility`, `LosTables`,
  `GameSimulation_visibility.cpp`, `GameSimulation.h`), `src/rwe/io/lostdf`, `io/tnt`,
  `io/ota` and `src/rwe/game/GameSimulationLoader.cpp` at the working tree of 2026-10-16.
  Nothing was built, run or changed, and tests were not run; a MATCHES below means the
  code reads as the rule says. Another agent had uncommitted edits in the tree while this
  was written, so every line number below is "about".
- **Measurements made for this audit** (retail install through `tools/hpi.py`, scratch
  scripts, nothing in the repository changed): `gamedata/los.tdf` was parsed and both
  replication schemes applied to tables 3, 5 and 8; `anims/vismasks.gaf` was decoded (ten
  frames) and compared cell by cell with RWE's disc; the 272 shipped `.ota` files, the 271
  `.tnt` headers and the 272 FBIs' `SightDistance` were counted. They are counts of keys and
  of cells, not play results.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Information layers and coordinates
2. Where a unit's sight is centred (the cabinet skew, and which height)
3. The ray-traced algorithm and what its counters are
4. The ray tables: loading, numbering and replication
5. The circular algorithm and the vismasks frames
6. Permanent reveal, mapping and the three modes
7. Updating on movement: the cache, the hysteresis and the timing
8. The unit wrappers and what they register
9. Temporary eyes
10. The height-band grid
11. TNT versions and environment parameters
12. Border exclusion
13. Fog drawing and terrain picking (presentation)
14. Gaps to fix

---

## 1. Information layers and coordinates

**Reference (section 1).** Four grids: 16-unit terrain cells with 13-byte records, a 32-unit
information grid, 32-pixel artwork tiles, and a coarse spatial bucket grid. Two information
layers matter. A **global unsigned-short `visibilityMask`** holds one persistent
reveal/mapping bit per player (the register's "LOS group") at each information cell and is
set-only. A **per-player byte grid** at `+0x7c` counts *current sight contributions*,
incremented and decremented as eyes move or expire; the chapter warns that its provisional
name "explored" does not make it permanent. Fog combines them: unseen history is black,
mapped but currently unobserved terrain is gray and dithered. Map flags: **mask 1** the
permanent reveal path, **mask 2** dynamic current sight, **mask 4** height-aware rays,
**mask 8** cached viewport fog tiles. A displayed option can set several bits.

**RWE: MATCHES in shape.** `GameSimulation::explored` is one shared grid of `ExploredMask`
words with one bit per line-of-sight group (`nextLosGroupBit`, `GameSimulation_visibility.cpp`
about 74-100: a player takes the earliest teammate's bit or the lowest free one);
`PlayerVisibility::visible` is the per-player byte grid. The register's description is the
same. The modes map as `LineOfSightMode::{Permanent, True, Circular}` and `MappingMode`
(`GameParameters.h` about 95); the fog-cache bit (mask 8) is presentation.

- **The shared-mask width: UNCLEAR.** The original has 16 bits (one per player slot at most).
  RWE hands out the lowest free bit with no bound; with at most 10 players this cannot
  overflow a 16-bit word.
- **Cell size: MATCHES.** The vision cell is two heightmap tiles, 32 world units
  (`PlayerVisibility::VisionCellSizeInTiles`).

## 2. Where a unit's sight is centred

**Reference (sections 1, 8, 9).** In height-aware mode `UpdateLineOfSight` takes the origin
cell as whole X divided by 32 and **projected Y as (whole Z minus half a clamped 0..255
`eye elevation`) divided by 32, where eye elevation is the query's eye height plus the whole
world Y**. The query is filled by `UpdateUnitLineOfSight` from the unit's owner, cached cell,
the definition's sight range, a frame byte, the unit's position (its Y **floored at sea
level plus one**) and the **model-top height byte**. So the origin row of a ground unit is
shifted up the screen by half of (terrain height plus model height), and that of an aircraft
by half of (altitude plus model height), clamped at 255. In mask mode the same projection
applies to the 32-unit origin with the unit's Y. The fog test for *seeing a unit* uses the
same projection with the sample point's own Y (`BT-GAME-PLAYERS-PARITY.md` rule 9).

**RWE: DIFFERS (new).** `visionCellAt` (`GameSimulation_visibility.cpp` about 140, doc comment
in `GameSimulation.h` about 880-910) projects with **the terrain height under the position
and explicitly not the position's own Y** ("so the cell is a function of (x, z) alone and a
flying unit shares a cell with the ground beneath it"), via `heightmapToVisionCell`
(`PlayerVisibility.cpp` about 40). The eye *height* fed to the rays is right (ground level
floored at sea plus one, plus the model height, clamped 0..255; `updateVisibility` about 515);
it is the *cell* the rays start from that differs. The register's own sketch of the lit test
(`TOTALA-EXE-VISION.md` about 360-371) has `y = unit Y + model top`, so the comment's claim
that this is "exactly as Total Annihilation indexes it" holds for the ground under a unit,
not for the unit.

**What it changes.**

- **Ground units:** the original's origin row sits half the model height further up the
  screen than RWE's. Typical model heights run from about 10 to 60 world units, so the shift is
  5 to 30 units, a sixth to nearly a whole 32-unit cell: for a fraction of positions the
  origin cell, and therefore the whole stamp, is one row different.
- **Aircraft:** the shift is half the altitude. An aircraft at a cruise altitude of 60 (the Brawler's) or more
  reveals ground **a cell or more up the screen from where its shadow is** (two at 120, four at
  250) in the
  original and the cell under its shadow in RWE. The sight patch of an aircraft is where the
  aircraft is *drawn*, not where it is.
- **The query side:** `canSeeUnit` asks the fog at the terrain skew of the unit's position
  (`GameSimulation_visibility.cpp` about 255-258). Implementing BT-81's four samples with the
  terrain skew would leave this wrong; the samples need their own Y.

**Needs an original check:** an aircraft over a map edge of fog (is the lit patch where the
aircraft is drawn?), and a tall building at the edge of a vision circle. Hash-visible: the
cell decides what the weapon scan and the computer player can target.

## 3. The ray-traced algorithm and what its counters are

**Reference (section 7).** `AddLineOfSight` and `RemoveLineOfSight` are paired. In height-aware
mode: check the cached origin, pick the ray table from the clamped `sightDistance / 32`,
**adjust the origin cell's byte by plus or minus one**; for each ray, best horizon starts at
index 0 and height difference -1; each step adds its stored offsets, bounds-checks the
**short** coordinates and reads two height-band values relative to the cached eye height. A
cell qualifies when `first difference * best index > best difference * current step ordinal`;
qualifying cells **increment or decrement the current-coverage byte**, and a stronger second
band updates the horizon. **A cell that lies on several rays is incremented once per ray**;
the bytes are not saturated and removal is not guarded against zero. Neither function changes
permanent bits, and removing the last contribution grays already-mapped ground.

**RWE.**

- **The slope test and horizon: MATCHES.** `revealWithLineOfSight` (`PlayerVisibility.cpp`
  about 108-198): bestSlope starts at -1 with bestStep 0, the step ordinal `t` counts
  skipped out-of-grid steps, `lo * bestStep > bestSlope * t` qualifies a cell and
  `hi * bestStep > bestSlope * t` moves the horizon, only on cells that were revealed. The
  first cell on every ray always qualifies (index 0). The register's description (section 2)
  is the same.
- **Out-of-map origins: MATCHES.** A unit off the map still casts (the comment at about 125
  says the original does not bounds-test, which the chapter's "short coordinates, checked
  per step" supports).
- **Multi-ray increments: DIFFERS (unobservable below the wrap).** RWE stamps a cell at most
  once per unit (`seenStamp`, `revealCell` about 90-105) and saturates the byte at 255.
  The original adds once per ray through the cell. Visibility is "byte non-zero", so the
  difference is invisible until a byte wraps.
- **Byte wrap: DIFFERS, latent.** Measured on the real `los.tdf` (flat terrain, all rays
  qualifying): in the radius-8 fan **one cell is on up to 7 of the 48 rays**
  (the diagonal neighbours of the origin), the radius-9 table up to 9, the radius-12 table up
  to 11. So **37 units** whose radius-8 stamps all cross one cell at that worst position are
  enough to wrap its byte to zero in the original (256 / 7), and a cell on 2 rays on average
  over a stamp needs about 130 units. The second is a large army; the first is a packed
  base. When a byte wraps the cell reads unseen for that player and units on it flicker out
  of view and out of fire. Whether the retail game shows this was not checked. RWE saturates,
  so it cannot.
- **Per-tick recompute versus incremental stamps: DIVERGENCE, observably equal.** See rule 7.
- **The origin byte "plus or minus one":** the chapter's extra adjustment of the origin cell
  (in addition to its place on the rays) is irrelevant to a per-unit stamp.

## 4. The ray tables: loading, numbering and replication

**Reference (`map-los-tables`).** `LoadLosTables` loads `gamedata/los.TDF`, selects
`TABLEINFO`, reads `numtables` (default 0) into a signed short, resizes and loads tables
0 to `numtables - 1`, each from section **TABLE(n+1)**; a missing section leaves that table
unchanged (empty on first load), `numlines` (signed short, default 0) sizes the table to
**four times** that value, and line *i* is loaded into positions *i*, *numlines + i*,
*2 numlines + i*, *3 numlines + i* with four transforms:

| Mode | First stored | Second stored |
| --- | --- | --- |
| 0 | first | negative second |
| 1 | second | first |
| 2 | negative first | second |
| 3 | negative second | negative first |

`GetLosTable(n)` is **one-based** (it subtracts one and narrows to a short); callers that pass
a clamped `sightDistance / 32` can pass 0, which indexes -1. A line is a count followed by
that many pairs of numbers; a missing field resizes the line to zero. Order is preserved: the
step ordinal matters to the consumers.

**Register.** `TOTALA-EXE-VISION.md` section 2: tables `TABLE1`..`TABLE9`, the index
`min(sight / 32, numtables - 1)` with `numtables = 9`, so the radius is capped at 8, and
"the quadrant replication is 90-degree rotations, not sign flips; measured ... sign flips
leave ten holes along the east-west axis".

**RWE.**

- **File and header: MATCHES for the shipped file.** The retail `los.tdf` says `numtables=9`
  and defines **twelve** tables (TABLE10 to TABLE12 are never read by either engine);
  `parseLosTdf` (`io/lostdf/io.cpp` about 46-100) sizes the vector to `numtables` and loads
  TABLE1 to TABLE(numtables - 1) into slots 1 to 8, leaving slot 0 empty. TABLE9 is therefore
  not loaded by RWE and is loaded and unreachable in the original (the clamp stops at 8).
  `maxRadius()` is 8.
- **The replication: MATCHES, and the two descriptions agree.** The chapter's four transforms
  are reflections (each has determinant -1); the register's are rotations. They look
  different and give **the same set**: applying both to tables 3, 5 and 8 of the real file
  gives identical sets of 16, 32 and 48 rays covering 36, 100 and 212 distinct cells. Each
  quadrant gets a different member of the fan, in a different order, which does not matter to
  independent rays. RWE's `rotateIntoQuadrant` (about 30-45) is the register's rotation. Both
  tile the circle (the first ray of each block starts exactly where the previous one stops
  one cell short). Nothing to change; the register's "rotations, not sign flips" is right as
  far as it goes, and the chapter's modes are the other equally valid tiling.
- **Radius 0 and the one-based accessor: DIFFERS in a case the data cannot reach.** In the
  original a unit with `SightDistance` below 32 asks for table 0 and reads slot -1 (undefined
  behaviour); RWE treats radius 0 as "only the cell it stands in" (`LosTables.h` about 28-32,
  `PlayerVisibility.cpp` about 130). The census: **all 272 shipped units name `SightDistance`
  of 32 or more**, none below. A mod unit with a small or absent sight is a mod-data case;
  RWE's behaviour is the safe one.
- **Missing or malformed input: DIVERGENCE (the security rule).** The original leaves a
  missing table empty, accepts a negative `numtables` as a huge size, and passes a missing
  token to `atoi`. RWE rejects a file with no `TABLEINFO`, `numtables < 1`, a missing table,
  a missing `lineN` or a step count that does not match its numbers (`parseLosLine`), and
  **falls back to generated tables** (`generateLosTables(8)`, `GameSimulationLoader.cpp`
  about 380) so the game still plays. That is the right call under `docs/SECURITY-AUDIT.md`,
  with one determinism note: a peer whose `los.tdf` fails to parse silently plays with
  different rays from a peer whose file parses, and nothing in the hash covers the tables.
  The lobby's archive check (`common/archives.ts`) is what keeps the files equal; a file in a
  loose data directory is not covered by it. **UNCLEAR whether that gap is real**; recorded.
- **Step output narrowing, short coordinates, resize and copy machinery, the string-pair and
  unit-sort helpers that share the file's address range: not applicable.**

## 5. The circular algorithm and the vismasks frames

**Reference (section 7, 8).** In mask mode the cached frame index is `sightDistance / 32 - 5`
clamped to the available frames; the mask GAF circle is clipped to the half-map rectangle and
only cells whose mask byte is not the transparency colour are adjusted. Zero frames has no
fallback. The permanent reveal walks the same mask.

**Register.** Section 2 and `TOTALA-EXE.md` section 88 ("Circular sight is a computed disc,
and it has no floor"): ten hand-drawn masks of radius 5 to 14; RWE stamps `dx^2 + dy^2 <= r^2`
and keeps the ceiling of 14 but drops the floor of 5; "differs only around the rim"; the floor
is dropped because "a unit with no `SightDistance` sees only the ground it stands on".

**RWE.** `revealCircle` (`PlayerVisibility.cpp` about 200-228) is the exact disc;
`MaxCircularSightRadiusInCells = 14` (`GameSimulation.h` about 63).

- **Rim: DIFFERS, and by more than "the rim".** `anims/vismasks.gaf` was decoded
  (one entry `vismask`, ten frames of sizes 11 to 29, centre at the middle). Compared with the
  disc of the same nominal radius (5 to 14):

  | Frame | Nominal radius | Opaque cells in the original | Cells in RWE's disc | Differing cells |
  | ---: | ---: | ---: | ---: | ---: |
  | 0 | 5 | 89 | 81 | 8 |
  | 1 | 6 | 121 | 113 | 8 |
  | 2 | 7 | 177 | 149 | 28 |
  | 3 | 8 | 221 | 197 | 24 |
  | 4 | 9 | 277 | 253 | 24 |
  | 5 | 10 | 349 | 317 | 32 |
  | 6 | 11 | 413 | 377 | 36 |
  | 7 | 12 | 489 | 441 | 48 |
  | 8 | 13 | 561 | 529 | 32 |
  | 9 | 14 | 638 | 613 | 25 |

  Every original frame is a superset of the disc: frames 0, 1, 3 and 4 match a disc of radius
  *r* + 0.25 exactly, frames 2, 5 and 7 a disc of radius *r* + 0.5, and frames 6, 8 and 9
  are hand-edited near-discs of about *r* + 0.5. So the original sees **4 to 19 percent more
  cells** than RWE in Circular mode, spread round the whole rim. This is the register's "differs
  only around the rim" with the number put on it.
- **The floor of 5: DIVERGENCE (documented), but the stated reason does not hold on shipped
  data.** The section 88 entry argues from a unit "with no SightDistance". The census has **no
  such unit**: all 272 name one of 32 or more. What the floor changes on real data is the
  **47 units whose `SightDistance` is 32 to 159** (radius 1 to 4 cells: 8 at radius 1, 3 at 2,
  17 at 3, 19 at 4): in the original they see a 5-cell mask in Circular mode, in RWE a 1 to
  4-cell disc. These are the small structures (solar collectors, mines, wind generators and
  similar). A floor of 5 for any nonzero sight, and none for a zero, would restore them and
  keep the section 88 reason. Circular is a non-default mode (True is the default), so the
  affected games are few.
- **Where the stamp is centred in Circular mode: UNCLEAR.** The chapter's mask path derives
  "projected 32-unit origin and subtracts frame offsets", presumably with the unit's Y as in
  rule 2; not stated.
- **Zero frames without a fallback:** not applicable (the data has ten).

## 6. Permanent reveal, mapping and the three modes

**Reference (sections 8, 9).** `RevealAroundUnit` walks the same rays or mask and sets the
owner's bit only where absent. When mapping changes for the displayed player it clears the
fog-valid bit and marks the view dirty. `RecalculateLineOfSight` resets the permanent bits to
zero **if mask 1 is enabled, otherwise to 0xff**, fills the current coverage of live player
roles 1, 2 and 3 (not 10) with **zero when dynamic sight is on and one otherwise**, rebuilds
every live unit's query and initialises sight under mask 2.

**RWE.**

- **Set-only shared bits: MATCHES** (`ExploredMark::mark`, `revealCell`).
- **Mapped, Permanent: MATCHES in effect.** `Mapped` explores every cell at `addPlayer` (about
  63-68) and `Permanent` promotes explored cells to visible once per tick
  (`makeExploredVisible`, about 233; the call at `GameSimulation_visibility.cpp` about 645).
  The chapter's "current coverage one everywhere when dynamic sight is off" is the same idea
  seen from the other side: with mask 2 clear the visible test reads the shared bitmask, not
  the byte grid (`TOTALA-EXE-VISION.md`, the `lit` backends). The register's "Permanent LOS
  has not been looked at" line is stale (rule 11).
- **A full recalculation that excludes temporary eyes: not applicable** (rule 9).
- **Roles 1, 2, 3: UNCLEAR.** The original limits the fill to certain controller roles; RWE
  has one visibility object per player, whatever its controller.

## 7. Updating on movement: the cache, the hysteresis and the timing

**Reference (section 8).** The original does not recast every tick. In height-aware mode the
update runs only when the **origin cell changes or the cached eye elevation differs by more
than 5**; a valid old nonzero height removes the old coverage when dynamic sight is on, then
installs the new cache and adds. In mask mode only a cell or frame change triggers it.
Unchanged state does not recast. The register (section 2) records the same condition
(`UpdateUnitLOS`, `0x4827B0`).

**RWE: DIVERGENCE, observably equal in all but two cases.** `updateVisibility` clears every
player's current grid and re-stamps every unit every tick (`GameSimulation_visibility.cpp`
about 500-560). Results agree except:

- **The 5-unit eye hysteresis.** In the original a unit whose eye rises or falls by 5 or less
  (a vehicle on a gentle slope, a hovering aircraft) keeps the horizon of the height it was
  last stamped at, and only recasts when it moves cell or crosses the threshold. RWE casts with
  the exact eye every tick. Different occlusion for a unit on a slope by up to 5 height units,
  in marginal cells only. Not a quantity anyone can see without a debugger.
- **Within-tick ordering.** The original updates a unit's sight in its own turn in the unit
  pass, so a weapon scan by a later unit this tick sees the earlier units' new stamps and the
  later units' old ones. RWE recomputes everything at the **end of the tick**
  (`GameSimulation::tick` about 5352), so every unit's scan this tick sees last tick's picture.
  A uniform one-tick lag, equal on every peer, so it cannot desync; it shifts when a
  newly visible target can first be shot by at most one tick.

Both are consistent across peers and neither is worth the cost of an incremental stamp (the
per-tick recompute is what made removal bugs impossible; see the comment on
`clearCurrent`).

## 8. The unit wrappers and what they register

**Reference (section 9).** `InitUnitSightCircleReveal` and `RevealNewUnit` initialise only
when mask 2 is on; the circle path "calls reveal regardless of an extra mask-1 test".
`RecalculateLineOfSight` scans live unit definition slots; the chapter says the scan "does
not independently exclude unfinished units, carrier attachments or every gameplay invisibility
state; surrounding lifecycle decides when they are registered and removed".

**RWE: UNCLEAR for unfinished and carried units.** `updateVisibility` skips only dead units
(about 507); a nanoframe under construction and a unit inside a transport both reveal from
their position with their definition's sight. Whether the original registers a unit's sight at
creation of its frame (rather than at completion) and whether cargo sees from inside a carrier
is a lifecycle question for the unit chapters (`BT-UNITS-UNITS-PARITY.md`); the radar side does
exclude a frame (`detectorActive` tests `isBeingBuilt`, about 560), so RWE has already answered
it for detectors and not for sight.

## 9. Temporary eyes

**Reference (section 10).** `InitEyeballs` allocates **twenty** 36-byte records. `AddEyeball`
accepts only when **dynamic sight is on and fewer than twenty exist**, assigns the displayed
player, range, eye height, a position floored above sea and an expiry of current tick plus the
supplied lifetime, initialises coverage and mapping, increments the count; capacity failure is
silent. `IsEyeballExpired` tests expiry **strictly less than the current tick** (equality stays
active). `ExpireEyeballs` removes the expired coverage and compacts; its changed flag is read
uninitialised, so a no-expiry pass can still compact; assignment repairs the self-pointers. A
temporary eye is not a sensor unit and not a targetable object, and its lifetime uses
simulation time. A full recalculation does not loop through eyes.

**Register and earlier audits.** BT-56 (resolved): "the temporary sight is a 60-tick patch at
the death of the viewer's own unit, fog games only". `BT-UNITS-UNITS-PARITY.md` rule 7 marks it
UNCLEAR / MISSING.

**RWE: MISSING.** There is no eye list and no temporary sight (the only reveal is the
per-tick unit stamp). The rules a port needs, from the chapter: capacity twenty with silent
failure, strict-less-than expiry measured in simulation ticks, removal exactly once, and (by
the register's resolution) a 60-tick patch at the death of the *viewer's own* unit in games
with dynamic sight. Since RWE re-stamps every tick, an eye would be a list of (position, range,
eye height, expiry) consumed by `updateVisibility` before the permanent promotion; it would
have to be saved and hashed (it changes what a unit can see for 60 ticks), and a list capped
at twenty must be ordered by creation for determinism.

## 10. The height-band grid

**Reference (section 5).** `BuildDerivedLayers` makes, besides the 128-unit bucket grid, a
**sight-height grid of half the terrain width and height, two bytes per entry**. It starts
with extrema 0 and 255; projects each terrain column using the screen-height displacement,
**distributes sampled and interpolated heights into neighbouring half-resolution columns** and
collects maxima and minima; and finishes by replacing them with `(2 max + min) / 3` and
`(max + 2 min) / 3`, each floored at sea level. These are "a compact occlusion approximation".

**Register.** Section 2: the same two thirds, the first deciding reveal, the second occlusion.

**RWE.** `computeVisionHeights` (`PlayerVisibility.cpp` about 265-337):

- **The thirds and the sea floor: MATCHES.**
- **Projection and distribution: DIFFERS in method, UNCLEAR in effect.** Each heightmap
  sample goes to exactly one vision cell by its own height; there is no interpolation along the
  skew and no spreading into neighbouring columns. A cell that no sample lands in (the skew
  carried the ground up the map) takes the plan-view block at the same index
  (about 295-313), a stand-in the original does not need because it interpolates. Differences
  are confined to cliff edges and steep slopes. Not measured.
- **Height of the grid used for occlusion on the query side:** no query uses it other than
  the rays.

## 11. TNT versions and environment parameters

**Reference (sections 2, 3, 4).** `LoadTntMap` accepts TNT versions **0x1020 and 0x2000**,
anything else is "Unknown TNT version" through `FatalError`; no embedded offset or count is
validated against the file length. Both supply dimensions, sea flag, tile pointers and counts.
The older version has eight-byte attributes, byte feature ids and threshold `0xfc`; the newer
four-byte attributes, short ids and threshold `0xfffb`. **For 0x2000, nonnegative mission
minimum and maximum wind override the defaults; gravity is the mission value if nonnegative,
otherwise a nonzero header value, otherwise the fixed 0x1fdb (112 times 65536 / 900);
tidal strength is the mission float if nonnegative, otherwise 0.5; the sea level is narrowed
to a byte;** world dimensions are terrain dimensions times 16. An optional generated minimap is
copied into the TED picture.

**RWE.**

- **Versions: DIFFERS, latent.** `TntArchive.cpp` about 21 accepts the magic `0x2000` only and
  throws "Invalid TNT version number" otherwise. The 0x1020 layout is unsupported. Census of
  the 271 shipped `.tnt` headers: **all are 0x2000**. A pre-release map in the old format is
  the only case.
- **Offset and count validation: DIVERGENCE (the security rule).** RWE bounds-checks the
  header (`TntArchive` size checks); the original does not.
- **Gravity: DIFFERS.** `ota.cpp` about 51 reads `gravity` with a default of 112 and nothing
  uses it: `BT-WEAPONS-WEAPONS-PARITY.md` rule 6. 83 of 272 shipped maps are not 112.
- **Wind: MATCHES for the shipped data; default DIFFERS.** `minwindspeed` and `maxwindspeed`
  default to 0 (`ota.cpp` about 49-50) and are clamped nonnegative
  (`GameSimulationLoader.cpp` about 693); every shipped `.ota` names both. The original's
  "nonnegative overrides the default" implies a different sentinel and an unstated default.
- **Tidal strength: DIFFERS, latent.** RWE reads an integer with default 0 (`ota.cpp` about
  44, `max(0, ...)` at about 694); the original's float defaults to 0.5 when the mission gives
  none. Every shipped map names it (values 0 to 48, mostly 20, 0, 25).
- **Sea level, dimensions: MATCHES.**

## 12. Border exclusion

**Reference (section 6).** `ClearBorderFeatures` walks the **last two terrain columns**, the
projected rows above the visible top and the rows beyond the projected bottom (the playable
and rendered extents are world width minus 32 and height minus 128), and rewrites each cell's
feature slot **from 0xffff (none) or 0xfffe (continuation) to the exclusion marker 0xfffd**; it
does not remove a real feature. For `lavaWorld` it applies the same rewrite to every cell
**at or below sea level (by the cell's low bound)**. The routine's name overstates it.
`FindFeatureAtPos` treats any id at or above 0xfffb as none.

**Register.** `TOTALA-EXE-MOVEMENT.md` (about 1194, 1230) notes that `0xFFFB`-`0xFFFD` are
"blocked" for the path code. The border walk and the lava rule are not recorded.

**RWE: MISSING (UNCLEAR effect).** There is no exclusion marker. `GameSimulationLoader.cpp`
places map features and leaves every other cell's slot empty. What the marker blocks is not
said by the chapter; the natural reading is that a feature slot that is not 0xffff cannot take
a new feature, so **tree regrowth, wreck placement and resurrection** are refused on the map
edge strip and, on a lava world, anywhere at or below sea level. The pathfinder's reading
(`0xFFFB`..`0xFFFD` blocked, register) would also make those cells impassable. Neither is in
RWE. On a lava world 44 of the 272 shipped maps (16 percent) would be affected.
**Needs a spec-room read** of the placement routines' test on the slot.

## 13. Fog drawing and terrain picking (presentation)

- **`DrawMapTiles`, `BuildFogTiles`, `DrawFogOfWar`:** the register's marching-squares account
  (section 2, "Drawing") is the same as the chapter's: four masks of 1, 2, 4 and 8 per source
  sample, black and gray composed as separate layers, 14 authored frames in `fog.gaf`, the
  variant `(cellX + cellY + scroll) & 3`. RWE uses the original frames. **MATCHES.** The
  gray layer contributes only when dynamic sight is on, as in RWE's Permanent mode.
- **`ClampWorldPosToTerrain`:** a projected-row search with interpolation, returning the
  last candidate when no crossing is found. RWE picks by casting a ray at the terrain mesh
  (`GameScene_input.cpp`). The original's function is an approximation with a zero-divisor
  fault; the user-visible rule (a click lands on the terrain or the sea under the cursor,
  max(ground, sea)) is the same. Cross-reference `BT-INGAME-CONTROL-PANEL-PARITY.md`
  (the `Z - Y/2` projection).
- **`UpdateCellHeightRange` leaves the last row and column unrebuilt**, and `GetMapCell*`
  bound-check: terrain-cell detail covered by `BT-MAP-TERRAIN-PARITY.md` (`MapTerrain`'s
  edge clamps); the effect is on the unplayable edge strip.

---

## 14. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | A unit's sight is centred on the cell for its terrain height, not for its eye elevation (model top plus Y); aircraft reveal under their shadow, not where they are drawn. Rule 2 | `sim/GameSimulation_visibility.cpp` (`visionCellAt`, `updateVisibility`, `canSeeUnit`), `sim/PlayerVisibility.cpp` (`heightmapToVisionCell`), `sim/GameSimulation.h`, `game/GameScene_render.cpp` (the fog renderer must apply the same transform), `visibility.test.cpp` | **Yes.** One fighter over fog: is the lit patch at its shadow or at its drawn position? Hash-visible; replay gate; do together with BT-81. |
| 2 | Circular masks are the authored frames, not discs of radius *r*: 8 to 48 more cells each, 4 to 19 percent more area; and the floor of 5 excludes 47 real units (sight 32 to 159) though the stated reason is a blind unit. Rule 5 | `sim/PlayerVisibility.cpp` (`revealCircle`), a table of ten masks, `sim/GameSimulation_visibility.cpp`, `TOTALA-EXE.md` section 88 | No for the masks (the GAF is in the install and can be turned into a table, or a radius *r* + 0.25/0.5 rule reproduces 7 of 10 exactly); the floor is a choice to revisit. Circular is not the default. |
| 3 | No temporary eyes: no 60-tick sight patch at the death of the viewer's own unit in fog games. Rule 9 | `sim/GameSimulation.cpp`, `sim/GameSimulation_visibility.cpp`, `game/save_util.cpp`, `GameHash_util.cpp`, `dump_util.cpp` | Light: BT-56 is resolved; confirm duration and the own-unit condition in play. State is saved and hashed. |
| 4 | Border-exclusion marker and lava-world cells absent: features can take the edge strip and lava cells; the pathfinder reading of `0xFFFD` is not mirrored. Rule 12 | `game/GameSimulationLoader.cpp`, `sim/OccupiedGrid`, feature placement | **Yes**, and a spec-room read of the placement test; 44 of 272 maps are lava worlds. |
| 5 | The current-sight byte is incremented once per ray and wraps at 256 in the original; RWE stamps once per unit and saturates. A dense base could black out a cell in TA. Rule 3 | none unless confirmed | **Yes**: a stack of 40 or more units on a cell. Do not port a wrap without seeing it. |
| 6 | Height-band construction has no interpolation (cliff-edge occlusion). Rule 10 | `sim/PlayerVisibility.cpp` (`computeVisionHeights`) | Static first: the spec-room read of `0x482c20`. Marginal. |
| 7 | Defaults: tidal strength 0.5 and the wind sentinel; `numtables`, TNT 0x1020. Rules 4, 11 | `io/ota/ota.cpp`, `io/tnt/TntArchive.cpp`, `io/lostdf/io.cpp` | No. Mod and pre-release data only. |
| 8 | Visibility is rebuilt at the end of the tick, so a scan sees last tick's picture; the 5-unit eye hysteresis is not reproduced. Rule 7 | none | No. Uniform across peers; leave. |

### Documented divergences this audit leaves alone

The computed disc and the missing floor of 5 in Circular mode (section 88, with the
corrections of rule 5); the per-tick recompute; the exact eye on every tick; rejecting
malformed `los.tdf`, `.tnt` and `.ota` input and falling back to generated rays; the
lobby-team alliance (ally sight is shared as current vision); the fog fade between lit
and gray.

### Conflicts with existing RWE findings, in one place

1. **Where the vision cell sits.** `GameSimulation.h` and `visionCellAt`: the terrain height
   under the position, "exactly as TA indexes it". Register (`TOTALA-EXE-VISION.md`, the lit
   sketch): `y = unit Y + model top`. Chapter: the origin uses eye elevation, the query the
   sample's Y. Open; the chapter and the register agree with each other and not with the
   code.
2. **Stale lines in `TOTALA-EXE.md` section 91** (not reference conflicts):
   - "TA's **Permanent** LOS mode has not been looked at" -- RWE has `LineOfSightMode::Permanent`
     and `makeExploredVisible`.
   - "**Circular** LOS mode ... is understood but not implemented; RWE always uses True" --
     `LineOfSightMode::Circular` exists and `revealCircle` implements it (section 88 describes it
     correctly).
   - "RWE's explored grid is **per-player** ... equivalent until allied vision groups exist" --
     `GameSimulation::explored` is one shared grid with a bit per line-of-sight group and
     `nextLosGroupBit` gives allies a shared bit.
3. **The section 88 reason for dropping the floor of 5** (a blind unit) holds for no shipped
   unit; the real consequence is the 47 low-sight units of rule 5.
4. **Quadrant replication.** Register: "90-degree rotations, not sign flips". Chapter: four
   reflections. Compatible: they produce the same set of rays (measured on tables 3, 5 and 8).
5. **Table count.** The file defines twelve tables but says `numtables=9`; the register's
   "`TABLE1`..`TABLE9`" is right for the original (nine loaded, eight reachable) and RWE loads
   `TABLE1`..`TABLE8`. Equivalent in use.
