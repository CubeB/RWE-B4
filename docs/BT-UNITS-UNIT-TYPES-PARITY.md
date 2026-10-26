# Unit type definitions: FBI fields, flag words, yard maps and buildable lists parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how an FBI becomes a unit
definition: which keys set which flag bits, what each field is narrowed to, how a
yard map is decoded, and where the buildable list comes from, per `AGENTS.md` and
ADR-0004. The flag-word layout and the class record are `TOTALA-EXE-MOVEMENT.md`
section 95 and `TOTALA-EXE-DATA.md`; the type-index order is `TOTALA-EXE.md`
section 109 and `TA-DEMOS.md`; the movement-class half is
`BT-UNITS-MOVEMENT-CLASS-PARITY.md`. They are cross-referenced, not restated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-types.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none
  of its source links were followed. Addresses are the ones the chapter names;
  rules are restated in prose with constants and tables of letters and bits.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT**
  with no winner picked.
- **No executed observation was used for the reference half.** The Gaps table says
  where an original-game check would help.
- **A census of the 272 shipped FBIs** (retail install, read with `tools/hpi.py`
  from totala1 to totala4, `ccdata.ccx` and `rev31.gp3`) supplies the "what would
  this change on shipped data" figures. It counts keys in files; it is not a play
  result. Throughout, "shipped" means those 272.
- **The RWE half** is a read-only audit of `src/rwe/io/fbi`, `src/rwe/LoadingScene_util.cpp`
  and the places that use the results, at the working tree of 2026-10-16. Nothing
  was built, run or changed, and tests were not run; a MATCHES below means the
  code reads as the rule says. Another agent had uncommitted edits in
  `GameSimulation.cpp` and related files; none of the files cited here are among
  them.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Enumeration, versions, availability, checksums
2. Type order and the wire index
3. Flag words: what each key sets, and the naming correction
4. Field widths and defaults
5. Movement class, footprint and derived fields
6. Weapon and explosion binding
7. Yard-map decoding
8. Models, scripts and GUI pages
9. Buildable lists and download menus
10. Reload and teardown
11. Gaps to fix

---

## 1. Enumeration, versions, availability, checksums

**Reference (section 1).** `LoadUnitInfo` (`0x42a8d0`) opens every FBI once,
computes a checksum of its bytes (a same-name override bank can replace the
checksum, not the text), requires `UNITINFO`, and decides **availability** (bit
0x800000): kept for an older major version, or the same major with a supported
minor (the minor is the first decimal digit after the point), and cleared by
archive or CD-origin checks or a copyright comparison. Unavailable and
incompatible entries are removed by moving the last record into the hole, with a
notice shown for version-related removals. A missing `UNITINFO` ends the
enumeration (returns zero); an unopenable file leaves a zeroed unavailable
candidate. `RefreshUnitInfo` repeats when the count is zero or a dirty flag is set.

**RWE.**

- **Version, origin and copyright filtering: MISSING.** `parseUnitInfoBlock`
  (`io/fbi/io.cpp:5-20`) requires the block (throwing when absent) and RWE reads no
  `Version` key and applies no availability test. Census: the `Version` key reads
  `1` (159 FBIs), `1.2` (112) and `3.0` (1), so every shipped unit passes the
  chapter's rule on a 3.x game and nothing is filtered. Mods built for a later
  version, which the original hides, load in RWE. Low priority; a player-facing
  effect only for mod packs.
- **The checksum: N/A to the simulation.** RWE's lobby compares archive hashes
  (`launcher/src/common/archives.ts`, `CLAUDE.md`), and `TaChecksums` covers the
  DirectPlay handshake. The original's per-FBI checksum (and its override file) is
  `TA-NETWORK.md`'s unit CRC; hosting needs none there.
- **Missing `UNITINFO` as a throw: DIFFERS in behaviour.** A malformed FBI makes
  RWE's loader throw (`:11`); the original skips or aborts the enumeration. That
  is the right side for RWE (`docs/SECURITY-AUDIT.md`: a fault in content costs
  that content), and `io/malformed_input.test.cpp` is the place for the case.

## 2. Type order and the wire index

**Reference (section 2).** `LoadUnitTypes` (`0x42d2e0`) compacts away unavailable
definitions, then **sorts nonzero definitions case-insensitively by internal
unitname** and assigns final sequential ids. The **definition-index bit width** is
the number of shifts to bring the count to zero, and it sizes the type field of
network state records. A pre-sort id kept across the process would be wrong.

**Register.** `TA-DEMOS.md` ("The type index is a 1-based load-order index"):
every `units\*.FBI` **file** name the merged archives present, **byte-sorted**,
numbered from one; global across archives; `_` sorting after `Z`; verified by
build-time ratios on two mods; `tadUnitLoadOrder` implements it.

- **Agreement: MATCHES, with two open differences.** (a) The register sorts by
  **file name** and the chapter by **internal unitname**; the two are the same on
  all 272 shipped FBIs (census: every `UnitName` equals the file stem
  case-insensitively). (b) The chapter says "case-insensitive"; the register found
  a **byte** sort, where `_` after `Z` is a case-insensitive comparison that folds
  to upper case and not to lower. Neither is distinguishable on shipped data: no
  shipped file name has an underscore. The register's evidence (Escalation's
  `ALL_L2.FBI`) outweighs the chapter's summary phrase. **UNCLEAR** only for
  availability-filtered mods, where the chapter says the sort runs after
  removal and the register's list is the merged directory: a removed (unavailable)
  unit would shift every later index by one in the original and not in the register's
  model. Open; matters for demos recorded on mods with version-hidden units.
- **The width: MATCHES.** The register lists the type field as a width *W*
  (`TA-DEMOS.md`, "The full-state record"), the same statement.

## 3. Flag words: what each key sets, and the naming correction

**Reference (section 4).** The two flag words (`def+0x241`, `def+0x245`) take
their bits from the loader's keys:

| flags1 bits | Source |
| --- | --- |
| 0 to 3 | `standingmoveorder`, `standingfireorder`, two bits each, default 2 |
| 4, 5, 6, 7 | `init_cloaked`, `downloadable`, `builder`, `zbuffer` |
| 8, 9, 10, 11 | `stealth`, `isairbase`, `istargetingupgrade`, `canfly` |
| 12, 13, 14, 15 | `canhover`, `teleporter`, `hidedamage`, `shootme` |
| 16 | derived: at least one nondefault weapon |
| 17 to 21 | `armoredstate`, `activatewhenbuilt`, `floater`, `upright`, `amphibious` |
| 23 | availability from enumeration |
| 24 to 28 | `isfeature`, `noshadow`, `immunetoparalyzer`, `hoverattack`, `kamikaze` |
| 29, 30, 31 | `antiweapons`, `digger`, page-zero GUI presence (set at asset binding) |

| flags2 bits | Source |
| --- | --- |
| 0 to 3 | `mobilestandorders`, `firestandorders`, `onoffable`, `canstop` |
| 4 to 8 | `canattack`, `canguard`, `canpatrol`, `canmove`, `canload` |
| 9 | **copy of the `canreclamate` bit, used by repair eligibility** |
| 10, 11, 12 | `canreclamate`, `canresurrect`, `cancapture` |
| 13 | derived: positive `cloakcost` |
| 14, 15, 16 | `candgun`, `norestrict`, the enumeration's `wacky` |
| 17, 18, 19 | `showplayername`, `commander`, `cantbetransported` |
| 20 to 22 | three-bit `selfdestructcountdown` |

The chapter then says the flag's meaning "comes from the loader keys, which
correct several misleading private-view member names elsewhere": a member called
`floats` may actually be `canhover`, one called `over_water` may be `upright`, and
`init_cloaked` is not `canattack`.

**Register.** `TOTALA-EXE-MOVEMENT.md` section 95, `TOTALA-EXE-ECONOMY.md` lines
255 to 270 (the bit-9 mirror, `0x42CA3B`-`0x42CA4D`, and the `CanRepair` row),
`TOTALA-EXE-INTERFACE.md` lines 70 to 85, `TOTALA-EXE-MISSIONS.md` line 275
(`hoverattack` is bit 27), `TOTALA-EXE-DATA.md` section 30. Every bit the register
names agrees with the table above: canfly 11, canhover 12, floater 19, upright 20,
amphibious 21, kamikaze 28, hoverattack 27, armoredstate 17, builder 6, isairbase 9,
canattack 4, canmove 7, canreclamate 10 (bit 9 the copy), cloakable 13.
**MATCHES. No conflict with the register in this table.**

**Earlier RWE parity documents and the old names.** The correction warns against
the reference's own earlier documents, which used provisional names. In the RWE
parity set:

- `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 8 and `BT-NEW-CONFLICTS.md` item 2
  recorded the reference's "definition bit 0x1000" as a conflict with the register's
  "floater" for the alignment routine. The table here puts bit 12 at **`canhover`**,
  the register's own assignment; the reading of the *reference* is therefore not a
  contradiction of the register's flag table. What remains is the wording of the
  register's description of the alignment routine
  (`BT-UNITS-UNIT-POSITION-PARITY.md` rule 2). Nothing in RWE relied on the old name.
- `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 4 calls the damage-reduction bit "activation
  bit 2". That is a different word (the unit's runtime activation byte, in which
  bit 2 is cloak, and **armoured is bit 1**), and it is a reference-internal
  numbering clash, not a flag-word one (see `BT-UNITS-UNIT-POSITION-PARITY.md`
  rule 4). RWE's armour code follows the register and is right.
- No other earlier RWE parity document used the provisional names. The audit of
  `TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md` is by key name throughout.

**RWE, key by key (`io/fbi/io.cpp`, `LoadingScene_util.cpp:493-660`).**

- **Read and used:** `standingmoveorder`/`standingfireorder` (with the two-bit wrap
  at `LoadingScene_util.cpp:361-395`, **MATCHES** the chapter's two-bit field),
  `init_cloaked`, `builder`, `zbuffer`, `stealth`, `canfly`, `canhover`, `shootme`,
  `hidedamage`, `floater`, `upright`, `amphibious`, `isfeature`, `noshadow`,
  `immunetoparalyzer`, `hoverattack`, `kamikaze`, `activatewhenbuilt`, every
  flags2 key except the ones listed next, `commander`, `cantbetransported`,
  `showplayername`. Cloakable is `cloakable || cloakcost > 0`
  (`LoadingScene_util.cpp:525`), which **MATCHES** the chapter's bit 13 with the
  explicit key as an RWE addition.
- **Not read at all: MISSING.** `Teleporter` (2 shipped FBIs, `ARMGATE` and
  `CORGATE`), `Digger` (3), `AntiWeapons` (2), `IsTargetingUpgrade` (2),
  `NoRestrict` (14), `Downloadable` (114), `SortBias` (5 set it, all zero). Grepped
  across `src/rwe` for each name, case-insensitively: the only hit is a comment in
  `GameScene_render.cpp:923` that cites `antiweapons` as the original's gate and
  hard-codes the Core and Arm anti-missile units by weapon. `Downloadable` and
  `NoRestrict` are the two with a menu consequence (rule 9). None changes the
  simulation on shipped data except `teleporter`'s mission (see
  `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 9).
- **Derived bit 16 (has a nondefault weapon): N/A.** RWE has no such field; the
  weapon slots are checked where they are used.
- **`floater` set from a water yard map: DIFFERS, and it is not in the chapter.**
  `parseUnitDefinition` sets `u.floater = true` for any unit whose yard map contains
  a water cell (`LoadingScene_util.cpp:650-653`). The original's bit 19 comes only
  from the `floater` key. Census: 22 FBIs set the key (all ships), 24 carry a `w`
  in the yard map (the sea-based buildings: `ARMTIDE`, `CORTIDE`, `ARMFMKR`,
  `CORFMKR`, `ARMFRT`, `CORFRT`, `ARMFHLT`, `CORFHLT`, `ARMFDRAG`, `CORFDRAG`,
  `ARMSY`, `CORSY`, `ARMASY`, `CORASY`, `ARMPLAT`, `CORPLAT`, `ARMSONAR`,
  `CORSONAR`, `ARMATL`, `CORATL`, `ARMTL`, `CORTL`, `ARMASON`, `CORASON`), and
  **the two sets do not overlap**. So RWE marks 24 buildings as floaters that the
  original does not, and the 22 ships come from the key. The visible consequence is
  the water-weapon exemption (`GameSimulation_visibility.cpp:375`: a floater is
  exempt from "above sea means unhittable by a torpedo") and the floor at sea level
  at placement for floaters and hover craft (`GameSimulation.cpp:1698`; which branch
  a non-mobile floater takes there was not traced). Whether any of the 24 stands high enough for the
  distinction to bite was not measured. The upstream author's intent (a building on
  water is a "floating" building) is plausible, but nothing in the register backs it.
  **Needs a decision:** keep as an RWE rule, document it in `docs/compatibility.md`
  / S:88, or read the key only.

## 4. Field widths and defaults

**Reference (section 3).** Integer costs are held as floats; `buildtime` integer;
`workertime` and `healtime` narrowed to shorts; `maxvelocity`, `brakerate`,
`acceleration`, `bankscale`, `pitchscale`, `damagemodifier`, `moverate1/2` fixed
point; **`bankscale` and `damagemodifier` default to 65536 (1.0)**; `moverate1/2`
default to **twice `maxvelocity`**; other listed movement values default to zero;
`makesmetal` a byte; `maxdamage` integer; sight, radar, sonar, both jammer
distances and `mincloakdistance` shorts; `waterline`, `transportsize`,
`transportcapacity` bytes; build and turn angles, build range, cruise altitude and
the tactical distances (maneuver leash, attack-run length, kamikaze distance)
shorts. `cloakcostmoving` defaults to `cloakcost`; a positive stationary cost
derives bit 13. **`selfdestructcountdown` defaults to 5 and is stored in three bits.**
A cloak-capable definition with zero `mincloakdistance` gets **80** after parsing.
Sound category resolves by name, then `atoi`, then zero. The loaders apply no
physical range clamp.

**RWE.**

- **Defaults for bankscale, damagemodifier, moverate, cloakcostmoving: MATCHES.**
  `io.cpp:108,133,66`, `LoadingScene_util.cpp:535,554`; the move-rate defaulting
  and the cloak cost truncation (`LoadingScene_util.cpp:622-626`) follow the register
  and are in `UnitBehaviorService.cpp:3704` onwards.
- **The 80 default: DIFFERS in form, same on shipped data.** RWE defaults
  `mincloakdistance` to 80 whenever the key is **absent** (`io.cpp:66`), for every
  unit; the original assigns 80 only to a cloak-capable unit whose value is **zero**
  after parsing (so an explicit `0` also becomes 80). Census: 18 FBIs set a cloak
  cost, and `mincloakdistance` is set to 40 (3), 10 (12), 80, 90 and 110 (one each),
  never to zero. No difference on shipped data; a mod with an explicit zero differs.
- **Three-bit self-destruct countdown: DIFFERS for mods.** RWE keeps the value as
  an unsigned (`io.cpp:51`, default 5). The original keeps the low three bits, so a
  value of 8 or more wraps. Census: shipped values are 1 (10 FBIs) and 2 (2), the
  rest default to 5. No shipped difference.
- **Narrowings to short and byte: UNCLEAR.** RWE reads these as wider integers or
  floats (`LoadingScene_util.cpp:493-660`), so a value above the original's width
  does not wrap. The chapter's own note is that the loader applies no clamp, which
  is the same fault in the original and a different one in RWE; a mod value of 70000
  for a sight distance is the case. Not scanned for on shipped data. Low priority.
- **Sound category by `atoi` fallback: not compared.**

## 5. Movement class, footprint and derived fields

**Reference (section 5).** The loader resolves `movementclass` by name; if absent
it reads an equivalent record **directly from the FBI** into a temporary class. Either
source supplies footprint X and Z, depths and slopes. `maxslopevelocity` =
`maxvelocity / (maxslope + 1)` in fixed point. Horizontal extents are plus and
minus half the footprint scaled by the 16-unit cell; `extentsize` is their
difference; the **radius is the sum of the horizontal extents divided by three**,
not a bounding circle. Model loading supplies the vertical extent later.

**RWE.**

- **Named class overrides, else FBI-local: MATCHES** (`LoadingScene_util.cpp:630-648`,
  `NamedMovementClass` versus `AdHocMovementClass`); the lookup case rules and the
  required keys are `BT-UNITS-MOVEMENT-CLASS-PARITY.md` rules 2 to 4.
- **`maxslopevelocity`: MISSING as a field.** The speed cap it supports is
  `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 4 (the register's section 102 shape against
  RWE's rise-over-run factor with a floor of a quarter, documented as unchanged in
  `TOTALA-EXE-MOVEMENT.md` near line 1880).
- **The radius rule: UNCLEAR.** The sum-over-three radius is the unit's collision
  radius for the original's selection and bounds tests (the chapter says "placement,
  collision and rendering can use different geometric approximations"). RWE has no
  per-definition radius field (searched `UnitDefinition.h` and `UnitState.h`); its
  tests use footprint cells, so the question has no direct counterpart. Open
  question for the spec room: where the original reads it.

## 6. Weapon and explosion binding

**Reference (section 6).** `weapon1/2/3`, `explodeas` and `selfdestructas` resolve
through the weapon-name table with the **default weapon record as the fallback**
when a name is missing; flag bit 16 is set when any of the three is nondefault.
Bad-target-category masks, no-chase masks, `canattack`, weapon type, aim script and
sensors stay separate gates.

**RWE.** The names are stored as strings (`LoadingScene_util.cpp:561-565`) and
resolved where used. **UNCLEAR**: what a name that matches no weapon definition does
was not traced (an unknown `weapon1` could be a startup error or a unit with no
weapon). Not shipped-data relevant (the default record is the do-nothing weapon).
The `BadTargetCategory` key is deliberately not read, a documented RWE choice
(`io/fbi/io.cpp:40-44`, citing that only the slot-prefixed forms exist in the
executable) which the chapter's "three bad-target-category masks and noChaseCategory"
agrees with. **MATCHES.**

## 7. Yard-map decoding

**Reference (section 7).** For `bmcode` zero the loader allocates one byte per
footprint cell and scans the string. Recognised letters, case-sensitive:

| Letter | Byte | Letter | Byte |
| --- | --- | --- | --- |
| `.` | 0x00 | `w` | 0x37 |
| `f` | 0x6f | `C` | 0x35 |
| `o` | 0x2f | `y` | 0x29 |
| `c` | 0x2d | `Y` | 0x31 |
| `O` | 0x2b | `G` | 0x8f |

**Any other character is skipped without consuming a cell.** A recognised last
character followed by the terminator **does not advance the cursor**, so it fills
the remaining cells. An empty or unusable string has no safe end branch. For
nonzero `bmcode` the yard-map pointer is null. The bytes combine support height,
open or closed occupancy, feature, water and other placement tests; their mask
meanings are `BT-MAP-TERRAIN-PARITY.md` rule 4.

**RWE.** `parseYardMapCell` and `parseYardMap` (`LoadingScene_util.cpp:290-359`),
`parseUnitDefinition` (`:636-660`).

- **Letters: MATCHES for the ten in the table**, by the map-terrain audit's mask
  meanings (`YardMapCell::Ground` is `o`, `GroundPassableWhenClosed` is `O`,
  `GroundNoFeature` is `f`, and so on).
- **Final letter repeats: MATCHES** (`:347-359`, with a comment citing
  `0x42D040`-`0x42D049`).
- **Unrecognised character: DIFFERS.** RWE's `default` returns `YardMapCell::Ground`
  (`:317-318`), consuming a cell, where the original skips it. Also `g` is accepted
  as `GroundGeoPassableWhenOpen` (`:300-301`), which the chapter's table does not
  list, so the original skips it. Census of shipped yard maps: only
  `o` (1152 occurrences), `c` (317), `C` (290), `w` (187), `y` (76), `G` (32) and
  `f` (4) appear, and the shipped maps use neither `O`, `Y`, `.` nor `g`. No
  shipped difference; the gap is for mods and for hand-written maps, and a malformed
  yard map in a mod misaligns the footprint by one cell per stray character in
  RWE.
- **An empty string: RWE fills with ground** (`:356`); the chapter says the original
  has no safe branch. Not a thing to copy.
- **Built only on the FBI-local branch: DIFFERS in structure.** RWE parses the yard
  map only when the movement class was **not** found by name (`:636-658`); a
  building that names a class gets no yard map, and a mobile unit with an unresolved
  class gets a ground-filled one. The original builds the map for every `bmcode`-zero
  unit regardless of the class source. On shipped data buildings name no class, so
  nothing changes; a mod that gives a building a class would lose its yard map.
  `bmcode` is read (`io.cpp:110`, as `isMobile`) but not consulted for this.

## 8. Models, scripts and GUI pages

**Reference (section 8).** After final ids are known, each FBI's `objects3d` model
is loaded (a missing model is fatal); the loader then probes `unitname0.GUI` and
numbered pages `unitname1.GUI`, `unitname2.GUI` and so on **until the first missing
file** (a gap ends the count, a later file does not extend it); page zero sets flag
bit 31; with numbered pages it records the suffix convention, with only page zero
one page, with none zero. It loads `scripts/unitname.COB` as shared script data.

**RWE.** UNCLEAR for page discovery (the build menu code in `game/DownloadMenus.cpp`
and `UiFactory` was not read for the consecutive-page rule; the census in rule 9
used the same rule against shipped data and found it sufficient). A missing model
is fatal in the original; what RWE does with one was not traced. **UNCLEAR.**

## 9. Buildable lists and download menus

**Reference (section 9).** For each builder the loader reads consecutive
`canbuild1`, `canbuild2`, and so on from `CANBUILD/unitname` in `gamedata/sidedata.TDF`;
unknown names are skipped. The list buffer reserves 0x3c bytes: **30 ids**, and the
loop does not itself stop at 30. `LoadDownloadMenus` reads download TDFs (a 0xbd-byte
record with five entries, not locally capped), resolves `UNITMENU` against unit names,
raises page counts and sets `downloadable` when absent. `AddDownloadBuildOptions`
appends an entry's id to the builder's list with a **less-than-or-equal-to-30** guard,
so a write at index 30 overruns a 30-element allocation (a concrete off-by-one,
"not a supported promise of 31 safe entries").

**RWE.** RWE does not read `CANBUILD` for the build menu. The menu is the builder's
GUI pages plus the download menus (`buildTreeFromBuilderGuis`,
`game/DownloadMenus.cpp`, tested in `DownloadMenus.test.cpp:228-240`), and the
simulation has no buildable-list check (`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 10).
**DIVERGENCE (structural).**

- **Census of the shipped data: the two sources differ.** Comparing the 45 `CANBUILD`
  lists of the shipped `sidedata.TDF` with the unit names each builder's numbered
  GUI pages show: **at least 15 of 45 differ** with `CANBUILD` taken alone, and the
  count rises (24 and 31 under the two ways of folding the download menus in) as the
  menus are added. Typical shapes: the list names a unit the pages do not show
  (`ARMCS` lists `ARMUWMEX`, `ARMUWES`, `ARMUWMS` and four floating structures
  (`ARMFDRAG`, `ARMFHLT`, `ARMFMKR`, `ARMFRT`) the pages lack; `CORACK` lists nine
  structures, including `CORGANT`, `CORBUZZ`, `CORFLAK`, that the Core advanced
  construction pages lack), and a page names a unit the list does not (`CORCS` shows
  `CORLLT` and `CORSY`; `ARMPLAT` shows `ARMCSA` and `ARMFIG`; `CORASY` shows
  `CORACSUB`). These are counts from a throwaway script over the shipped files, run
  once, matching unit names in `<builder><n>.gui` pages (`name=` lines) against the
  list; a page layout the script does not recognise would show as a difference, so
  treat the exact figures as indicative. **Not verified against play**: whether the original lets
  `ARMCS` actually build `ARMUWMEX` from the list alone (the chapter says consumers
  read the recorded count) or the menu is the only door. That is the reference
  rule ("the buildable list is a gate") that RWE would be missing.
- **No shipped list exceeds 30**: the union with the download menus never passes it,
  so the off-by-one is dormant on shipped data; RWE has no such buffer and needs
  nothing.
- **`Downloadable` is not read** (rule 3); RWE decides what a download menu offers
  from the menu files alone, and it sets nothing on the unit.

## 10. Reload and teardown

**Reference (section 10).** `ReloadUnitType` re-parses an FBI and swaps the shared
script data without touching the model, the sort or the menus; `FreeUnitTypes`
releases everything and assumes no live unit still depends on it; the loader sets
progress to 100 and marks enumeration dirty. **N/A to RWE**: definitions are loaded
once per game (`LoadingScene`), there is no mid-session reload, and ownership is by
value. Nothing to compare.

## 11. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Decide on the water-yard `floater` rule: 24 shipped buildings are floaters in RWE and not in the original. Keep and document, or remove. Rule 3 | `src/rwe/LoadingScene_util.cpp:650-653`, `docs/compatibility.md` (or S:88) | **Yes, narrow.** Does a torpedo or depth charge hit a tidal generator or a fixed sea defence in the original, and does it hit an `ARMSY`. The weapon-eligibility routine is `GameSimulation_visibility.cpp:361-410`. |
| 2 | Parse `Teleporter`, `Digger`, `AntiWeapons`, `IsTargetingUpgrade`, `NoRestrict` and `Downloadable`, or document why each is unused. Rule 3 | `src/rwe/io/fbi/io.cpp`, `src/rwe/sim/UnitDefinition.h`, `LoadingScene_util.cpp` | No for parsing. The behaviour of each key is a spec-room question; `ARMGATE` and `CORGATE` are the two `teleporter` units. |
| 3 | Yard-map letters: skip an unknown character without consuming a cell, drop `g` (or record it as an RWE extension), and parse the yard map for every unit with `bmcode` 0 regardless of class source. The skip and `g` parts landed in #684; the named-class parse remains. Rule 7 | `src/rwe/LoadingScene_util.cpp:290-359,636-660`, `io/malformed_input.test.cpp` | No. Mod-data-only. |
| 4 | Decide whether the buildable list is a gate: census says the sidedata list and the GUI pages disagree for 31 of 45 builders. If the original gates on the list, `refusalOf` needs it. Rule 9 | `src/rwe/game/PlayerCommandApplication.cpp:245-294`, `src/rwe/game/DownloadMenus.cpp`, `LoadingScene` (read `sidedata.TDF`'s `CANBUILD`) | **Yes.** Can an `ARMCS` build `ARMUWMEX` and `CORACK` a `CORGANT` in the original, and is any button the pages show but the list omits refused. |
| 5 | `mincloakdistance` default 80 for a cloak-capable unit with zero; three-bit `selfdestructcountdown`; short/byte narrowing. Rule 4 | `src/rwe/io/fbi/io.cpp:51,66`, `LoadingScene_util.cpp:493-660` | No. Mod-data-only. |
| 6 | Version/availability filter (`Version`, copyright, origin). Rule 1 | `io/fbi/io.cpp`, `LoadingScene` | No. Mod-pack-only. |
| 7 | Sort order for availability-filtered mods (the wire index shifts when a unit is removed first). Rule 2 | `src/rwe/io/tad/tad_events.h` (`tadUnitLoadOrder`) | Spec-room: does a removal precede the numbering; check against a demo recorded on a pack with version-hidden units. |

### Documented divergences this audit leaves alone

The `BadTargetCategory` key RWE does not read (a deliberate choice); RWE's
`cloakable || cloakcost > 0`; the lobby's archive hash standing in for the per-FBI
checksum; no reload path.

### Conflicts with existing RWE findings, in one place

1. **None between the chapter's flag tables and the register's.** Every named bit
   agrees. The earlier "bit 0x1000" conflict in `BT-NEW-CONFLICTS.md` is therefore
   about the register's *description of the alignment routine* and not about the flag
   table.
2. **Type-index sort key.** Register: the byte sort of merged **file names**. Chapter:
   a case-insensitive sort of internal **unitnames** after unavailable units are
   removed. Identical on all 272 shipped FBIs; differs on names with `_` or removed
   units. Open.
3. **The `floater` bit.** Not a chapter-register conflict: an RWE departure from a
   key-only bit (rule 3), listed here because it is where the flag-word correction
   bites a port.
