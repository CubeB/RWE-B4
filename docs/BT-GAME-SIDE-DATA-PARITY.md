# Side definitions, interface rectangles, fonts and logos parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
module chapter of the reference, `game-side-data`: how `gamedata/sidedata.TDF` becomes the side
table (name, prefix, commander, fonts, colours and thirty interface rectangles), how the shared
logo graphics are loaded and released, and what each side owns. This is the finding that
implementation work cites for the side table, per `AGENTS.md` and ADR-0004.

What the rectangles are *used for* (the resource readouts, the footer, the damage bars) is
`TOTALA-EXE-INTERFACE.md` ("What it draws, rectangle by rectangle") and
`BT-INGAME-INFO-PANEL-PARITY.md`; this audit is only about reading them. The `intgaf` panel files
and `baseheight` are `BT-GAME-DATA-FILES-PARITY.md` rule 6; the `[CANBUILD]` blocks that share the
file are `BT-UNITS-UNIT-TYPES-PARITY.md` rule 9.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-side-data.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names (five entries, `0x4318c0` to
  `0x432520`); rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row concerns the
  side table, so nothing is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no faction count, panel coordinate, font or colour was established from an
  installation. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT**, with no
  winner picked.
- **No executed observation of the original was used.** The Gaps table says which items an
  original-game check settles.
- **The RWE half** is a read-only audit of `src/rwe/io/sidedatatdf/SideData.{h,cpp}`,
  `GameLaunch.cpp` (the side map), `LoadingScene.cpp` (commander and fonts),
  `game/GameScene_render.cpp` (the readouts), `MainMenuScene.cpp` and `MainMenuScene_campaign.cpp`
  (the sides on offer) and `ai_arena.cpp` at the working tree of 2026-10-17. Nothing was built,
  run or changed. Another agent had uncommitted edits in the tree, so every line number is
  "about".
- **A census of the shipped `sidedata.TDF`** (read through `tools/hpi.py` from `rev31.gp3`,
  `btdata.ccx`, `ccdata.ccx` and `totala1.hpi`; the first three are the same 15,339 bytes, the
  last 10,771) with a throw-away scratch script outside the tree: the sides, their scalar keys and
  the geometry of every rectangle.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Side discovery: contiguous, five slots, and which sides RWE offers
3. The scalar keys: required or defaulted
4. Fonts: the per-side font RWE does not read
5. The thirty rectangles
6. Colours
7. Logos, borrowing and release
8. Lifetime and the second read of the same file
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| side discovery, scalar fields | `TOTALA-EXE-DATA.md` section 115 (a campaign's side is the SIDEDATA `name`) | the side names agree; the reading rules are new (rules 2 and 3) |
| fonts | none | rule 4 |
| the rectangle schema | `TOTALA-EXE-INTERFACE.md` (footer rectangles, offsets from the parser at `0x432310`; `RELOAD1..3` "parsed, never read") | agrees on the schema; adds failure behaviour (rule 5) |
| colours | none | rule 6 |
| logos, release | `BT-GAME-DATA-FILES-PARITY.md` rule 7 (the logos file in model binding) | rule 7 |
| lifetime | none | rule 8 |

## 2. Side discovery: contiguous, five slots, and which sides RWE offers

**Reference.** `LoadSideData` (`0x431a60`) loads `gamedata/sidedata.TDF` through the
language-sensitive path builder (without checking the load's return), and walks `SIDE0`, `SIDE1`,
... storing the **side number** into the current slot *before* it tests whether the record
exists. The first absent record ends the walk, so the count is the number of **consecutive**
records, and a record after a gap is ignored. Storage is **five** records of `0x232` bytes with no
limit in the loop, so a file with five valid sides makes the loop write a side number through a
sixth slot before `SIDE5` ends it; the chapter calls this a storage-boundary concern and says the
number's meaning is not in doubt. Reloading is not a protocol: fonts are not freed first, absent
values retain old state.

**RWE.**

- **Contiguous walk: MATCHES.** `parseSidesFromSideData` (`SideData.cpp` about 120-137) finds
  `SIDE0`, `SIDE1`, ... and stops at the first missing block.
- **No limit of five: DIFFERS, harmlessly.** The result is a vector. A sixth side is read and
  stored; the out-of-bounds write has no counterpart.
- **The side number is not stored: DIFFERS (representation).** RWE has no index field; the side
  is identified by its `name` string, and the map is keyed by that name
  (`GameLaunch.cpp` about 366-376). A duplicate `name` keeps the **first** record
  (`unordered_map::insert`; `GameLaunch.cpp` and a copy in `ai_arena.cpp` about 115-120).
- **Case: DIFFERS inside RWE.** Block and key lookup in the TDF is case-insensitive, but the side
  map is exact-case: `sideData.find(player.side)` and `.at(...)` (about a dozen call sites,
  `GameScene*.cpp`, `LoadingScene.cpp`) need the string to equal the file's `name`
  character for character. The sides RWE passes are the literals `"ARM"` and `"CORE"`
  (`MainMenuScene.cpp` about 1788-1796, `MainMenuScene_campaign.cpp` about 27), and the command line's
  `--player` side must be exactly `ARM` or `CORE` (`parseSideFromString`, `GameLaunch.cpp` about
  525-533, "Unknown side string" otherwise, so `arm` is refused). They equal the retail names. A mod whose `name` is spelt differently
  works in the original (it never compares by name) and cannot be chosen in RWE.
- **Which sides the player can pick: DIFFERS.** The original builds its side table from the file; RWE
  offers exactly two sides, hard-wired, and ignores any `SIDE2` onward (`getSideName` throws
  `Invalid side` for anything else). **Census: the shipped file has exactly two sides**, `SIDE0`
  (`ARM`) and `SIDE1` (`CORE`), in every copy. Whether the original's lobby lists a third side if the
  file has one is not in the chapter (the lobby is another module); this is a mod-data question.
- **A missing side is an uncaught `std::out_of_range`.** `.at(...)` with a side string that is not in
  the map (a saved game or a mission naming one) throws with an unhelpful message from inside the
  game scene, and `LoadingScene::getSideData` throws "Missing side data for ..." at load. The
  original has no such path (a side is an index into five slots).

## 3. The scalar keys: required or defaulted

**Reference.** For each side the loader asks for `name`, `nameprefix` and `commander` with
destination capacities of **30, 4 and 32** bytes and copies each **only if the getter reports the
field found**, so a missing field leaves the slot's previous or initial contents rather than an
empty default. `energycolor` and `metalcolor` are integers with default zero, stored with no
clamping, conversion or validation. The commander entry is the name `SpawnCommanderAtStartPos` looks
the starting unit up by (`BT-GAME-MAIN-LOOP-PARITY.md` rule 9). (`intgaf` is read by a different
routine, `BT-GAME-DATA-FILES-PARITY.md` rule 6.) The chapter does **not** mention `fontgui`.

**RWE: DIFFERS in strictness; one key is required that the original never reads.**
`parseSideData` (`SideData.cpp` about 62-77) uses `expectString`/`expectUint` for `name`,
`nameprefix`, `commander`, `intgaf`, `font`, **`fontgui`**, `energycolor` and `metalcolor`: any
missing key throws `TdfValueException`, which nothing in `GameLaunch` catches, so the launch ends
with the critical-error box (the pattern `BT-NEW-CONFLICTS.md` M9 lists for every loader).

- **`fontgui` required: DIFFERS.** The retail file sets it (`armbutt`, `corbutt`), RWE stores it and
  **nothing reads it** (a search of `src/rwe` finds only the parse). A mod `sidedata.TDF` without
  `fontgui` runs in the original and ends RWE's launch.
- **Colours: DIFFERS, with a defaulted zero versus a throw.** An absent `energycolor` is a throw in
  RWE and palette index 0 in the original.
- **Length limits: not applicable.** `std::string` has none. The original's prefix is four bytes;
  **the retail prefixes are `ARM` and `COR`** (note `COR`, not `CORE`: the prefix and the name
  differ for Core, and RWE builds the panel file names from the prefix: `<prefix>MAIN2`, `<prefix>GEN`
  (`GameScene.cpp` about 353, `GameScene_commands.cpp` about 918-927) and `mbrief<prefix>`
  (`MainMenuScene_campaign.cpp` about 358)), so the 4-byte limit holds a three-letter prefix with its
  terminator.
- **A missing optional field keeps old state: not applicable.** RWE builds each record fresh.

## 4. Fonts: the per-side font RWE does not read

**Reference.** A present `font` key resolves **`fonts/<value>.FNT`** through the path builder and
loads the whole file; a null load is fatal with the path; an absent key leaves the font pointer as it
was. Each side loads its own copy even when two sides name the same file (no font cache).
`FreeSideFonts` (`0x431a20`) frees all five slots regardless of the side count.

**RWE: DIFFERS (the side's font is parsed and ignored).** `SideData::font` is stored and never used.
The in-game console font is the fixed `fonts/CONSOLE.FNT` (`LoadingScene.cpp` about 315), the speech
font a fixed GAF entry, and the HUD text uses the GUI font. **Census:** both shipped sides name
`font=console`, so the fixed file is the same file. A mod side naming another font would show the
original's choice and RWE's console font. Low; one line to change if such a mod exists.

## 5. The thirty rectangles

**Reference.** Each side record has **thirty** child records, each with `x1`, `y1`, `x2`, `y2`
integers. A **missing coordinate** defaults to zero individually; a **missing rectangle** is a fatal
error naming the rectangle and the side. There is no width or height calculation, **no endpoint
sorting**, no clipping and no scaling in the loader. The first eleven are read by an inlined helper,
the rest by `ReadSideRect` (`0x431950`); both save the parser's current record, select the child,
read four values and restore the record. The names, in schema order: `LOGO`, `ENERGYBAR`,
`ENERGYNUM`, `METALBAR`, `METALNUM`, `TOTALUNITS`, `TOTALTIME`, `ENERGY0`, `METAL0`, `ENERGYMAX`,
`METALMAX`, `ENERGYPRODUCED`, `ENERGYCONSUMED`, `METALPRODUCED`, `METALCONSUMED`, `LOGO2`,
`UNITNAME`, `DAMAGEBAR`, `UNITMETALMAKE`, `UNITMETALUSE`, `UNITENERGYMAKE`, `UNITENERGYUSE`,
`MISSIONTEXT`, `UNITNAME2`, `DAMAGEBAR2`, `NAME`, `DESCRIPTION`, `RELOAD1`, `RELOAD2`, `RELOAD3`.
A rectangle's presence enables no gameplay: a `RELOAD3` position does not give a unit three weapons.
The standalone helper restores the cursor after reporting a missing record, the inlined one after
either branch; neither invents a fallback.

**Register.** `TOTALA-EXE-INTERFACE.md` (the footer table, near 839) names these with their parser
offsets and records which are drawn and how (`UNITNAME` "centred on x1", `UNITENERGYMAKE` printed
as `+%.0f`, the unit name "centred on x1" etc.), and that `RELOAD1..3` are "parsed, never read".

**RWE.**

- **The schema: MATCHES.** The thirty keys are `SideData.h` fields and `parseSideData` reads each by
  the same name, case-insensitively.
- **A missing rectangle is an error, a missing coordinate is also an error: DIFFERS (stricter).**
  `expectSideDataRect` throws `TdfValueException("Failed to find block ...")` and
  `parseSideDataRect` uses `expectUint` for all four (`SideData.cpp` about 40-61); the original
  yields zero for a missing coordinate. Retail has all 30 rectangles with all 4 coordinates on both
  sides (**census**), so this is mod data only.
- **No endpoint sorting in the original; RWE sorts for some and not for others: MATCHES in effect,
  by luck of the data.** `SideDataRect::toDiscreteRect` (about 21-47) orders the corners and adds one
  to each extent, so the bars and logos (`LOGO`, `ENERGYBAR`, `METALBAR`, `LOGO2`, `DAMAGEBAR`,
  `DAMAGEBAR2`) are drawn as inclusive boxes (`GameScene_render.cpp` about 214, 226, 260, 320, 334,
  431), while the number and label fields are used raw (`x1`, `y1`; about 234-290, 354-372, 395-505).
  **Census: four rectangles have `x1 > x2`, in both sides:** `UNITMETALMAKE` (350,458 to 170,478),
  `UNITMETALUSE` (350,468 to 170,478), `UNITENERGYMAKE` (400,458 to 170,468) and `UNITENERGYUSE`
  (400,468 to 170,468). In the original these are anchors read at `x1`, `y1` (the register's
  `"+%.1f"`/`"-%.1f"` fields), and RWE draws them raw, so they come out right; **normalising them
  with `toDiscreteRect` would flip the anchor to 170**. The division of the thirty into "box" and
  "anchor" fields is implicit in the call sites and not written anywhere; a future refactor that
  normalises every rectangle at parse time would move four labels 180 pixels. Worth one comment
  and a test.
- **`RELOAD1..3`: MATCHES** (parsed, never drawn; `SideData.h` keeps them).
- **Parser-offset detail:** the chapter's offset table is the register's (`0xE8` for `LOGO2` ...
  `0x1C8` for `RELOAD3`); nothing in RWE depends on offsets.
- **Mod data with a rectangle absent:** the original is fatal, RWE throws: the same outcome.

## 6. Colours

**Reference.** `energycolor` and `metalcolor` are integers, default zero, stored unclamped, no palette
validation.

**RWE: DIFFERS, a probable fault for mod data.** `GameScene_render.cpp` about 229-230 and 263-264
index the palette with the value as given: `sceneContext.palette->at(colorIndex)`. The palette is a
`std::vector<Color>` of 256 (`ColorPalette.h`), so **any value of 256 or more throws
`std::out_of_range` out of the HUD draw, on every frame**, which ends the frame loop for that game.
The original, with an integer stored in a wider field and used as an index of a byte palette, would
read an out-of-range entry or wrap; the chapter does not say which. The shipped values are 208 and
224 (**census**, both sides).
A guard (clamp or low byte, to match the original's narrower use once it is known) is cheap.
`docs/SECURITY-AUDIT.md`'s rule: a fault in content costs that content.

## 7. Logos, borrowing and release

**Reference.** `LoadLogos` (`0x4318c0`) resolves `textures/logos.GAF`, loads it, stores the
allocation and immediately fetches the `32xlogos` entry as a **borrowed pointer**, with no null
check between load and lookup. The whole logos allocation is also the fallback source in model
texture binding (`BT-GAME-DATA-FILES-PARITY.md` rule 7). `FreeLogos` (`0x431920`) frees and clears
the top-level pointer and **leaves the `32xlogos` entry pointer stale**. The side loader's
lifecycle: sides then logos at game initialisation, freed at application resource shutdown; loading
a map does not redefine the side table.

**RWE.**

- **`32xlogos` ten frames, indexed by player colour: MATCHES.** The HUD draws
  `sprites.at(playerColorIndex)` of the entry from `textures/LOGOS.GAF`
  (`GameScene_render.cpp` about 211-216), the lobby does the same
  (`MainMenuScene.cpp`, `MainMenuScene_multiplayer.cpp`). **Census:** the shipped `logos.gaf` has
  `32xlogos` with ten frames (and 17 of its 18 entries ten-frame; `onoff01` is two).
- **A missing logos file is a quiet absence in the HUD:** `tryGetGafEntry` returns nothing and the
  logo is skipped, where the original's lookup has no null check. Safer; no gap.
- **The stale entry pointer: not applicable.** The cache owns the GAF.
- **Side table redefined by a map: MATCHES.** RWE loads the side map once at launch
  (`GameLaunch.cpp` about 366) and no map or mission changes it.

## 8. Lifetime and the second read of the same file

**Reference.** The same `sidedata.TDF` is read **independently** by general resource loading (for
`baseheight` and the `intgaf` panel assets, `BT-GAME-DATA-FILES-PARITY.md` rule 6); the two reads
share the filename and populate different state. `[CANBUILD]` blocks in the same file are not read
by the side loader.

**RWE: MATCHES in effect.** One read per launch builds the map the whole game uses
(`GameLaunch.cpp` about 358-376). The second read has no counterpart because RWE reads `intgaf`
from the same parsed record and has no `baseheight`; the `[CANBUILD]` blocks are ignored by
`parseSidesFromSideData` (which looks for `SIDEn` only) and are `BT-UNITS-UNIT-TYPES-PARITY.md`'s.
The parse is duplicated in two places (`GameLaunch.cpp` and `ai_arena.cpp`, `loadSideData`), a
maintenance note and not a behaviour difference.

---

## 9. Gaps to fix

Ranked by what a modder would meet. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **`energycolor` or `metalcolor` of 256 or more throws every frame** in the HUD draw (`palette->at`). | probable code fault (mod data) | code: clamp, with the original's narrower use confirmed by a spec-room read |
| 2 | **Only the two literal sides ARM and CORE can be chosen**; a third `SIDE2` or a differently spelt `name` is unreachable, and an unknown side string is an uncaught `out_of_range`. | DIFFERS (design), UNCLEAR for the original's lobby | decision |
| 3 | **`fontgui` is required and never read**, and any missing scalar or coordinate ends the launch where the original defaults to zero. | DIFFERS (stricter) | code: make `fontgui` optional; decide the policy for the rest (the security rule favours rejecting that content, not the launch) |
| 4 | **The side's `font` is parsed and ignored**; the console font is fixed. Same file on retail. | DIFFERS, no retail effect | none |
| 5 | **Four anchor rectangles have `x1 > x2`** (the four unit make/use fields); normalising them would move labels. Add a comment and a test. | MATCHES by the data; fragile | code: a test pinning the unnormalised read |
| 6 | **The side-data parse is duplicated** (`GameLaunch.cpp`, `ai_arena.cpp`). | maintenance | code |

No source file was changed.
