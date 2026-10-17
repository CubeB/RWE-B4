# Asset paths, shared resources and model texture binding parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GRAPHICS-PALETTE-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
module chapter of the reference, `game-data-files`: how the original turns a logical resource
name into a language-sensitive path, the small adapters that load a bitmap, font, palette, radar
picture or whole file, the sound-name registry, the animation-file cache and the fixed list of
shared resources, the binding of 3DO texture names to GAF frames, and the aggregated unit
checksum. This is the finding that implementation work cites for any of them, per `AGENTS.md`
and ADR-0004.

Language-suffixed lookup (BT-30) was raised as a gap by `TA-SYSTEMS-PARITY.md` rule 33 from the
startup chapter; this chapter is the implementation, and a census of the retail install shows what
the rule would select. The palette tables (`ALP`, `SHD`, `LHT`) are `BT-GRAPHICS-PALETTE-PARITY.md`
rule 8; the sound-name registry is touched by `BT-SOUND-PARITY.md` rule 2; the cursors are
`BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 11; the side-data parsing is `BT-GAME-SIDE-DATA-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-data-files.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names (22 entries, `0x429000` to `0x42a8d0`);
  rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-30 (language
  directories) is open and is extended here, BT-50 (definitions forced to "no draw under gray"),
  BT-155 and BT-156 (GAF fields) are the neighbours' and not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no source listing, asset execution or generated archive was needed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT**, with no winner picked.
- **No executed observation of the original was used.** The Gaps table says which items an
  original-game check settles.
- **The RWE half** is a read-only audit of `src/rwe/GameLaunch.cpp` (the palette, the sound
  definitions, the cursors), `LoadingScene.cpp`, `atlas_util.cpp` (the texture GAFs),
  `TextureService.cpp`, `src/rwe/game/GameSimulationLoader.cpp` (unit, weapon, download,
  feature and builder-GUI loading), `GameScene_render.cpp` and `MainMenuScene_multiplayer.cpp` at
  the working tree of 2026-10-17. Nothing was built, run or changed. Another agent had
  uncommitted edits in the tree, so every line number is "about".
- **A census of the retail install** (the 30 archives of `D:\Total Annihilation\Total
  Annihilation`, through `tools/hpi.py`'s reader and throw-away scratch scripts kept outside the
  tree): the language-suffixed directories, `version.tdf`, the palette files, the frame counts of
  every entry of every `textures/*.gaf`, the models that name an animated texture, the 275 `.tnt` headers, and the GUI
  files a `unitname*` scan would find.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Language-sensitive paths and the version check
3. The file adapters: bitmap, font, palette, radar picture
4. The whole-file loader with progress
5. The sound-name registry
6. The animation-file cache and the fixed list of shared resources
7. Texture archives and model binding (animated textures, the logos family)
8. The unit script checksum
9. Fonts, the default palette and lifetimes
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| language-sensitive path | `TA-SYSTEMS-PARITY.md` rule 33; BT-30; `TOTALA-EXE-DATA.md` near 818, 1636 (localised keys, mission paths) | the mechanism is wholly missing; census gives it a concrete case (rule 2) |
| version check | none | rule 2 |
| bitmap, font, palette, radar adapters | `BT-GRAPHICS-PALETTE-PARITY.md` rule 8 (the three derived tables) | the `.PAL`-or-`.PCX` rule is new (rule 3) |
| whole-file loader with progress | none | rule 4 |
| sound-name registry | `BT-SOUND-PARITY.md` rule 2 | agrees (rule 5) |
| animation cache, fixed resource list | `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 11 (cursors) | rule 6 |
| texture binding | `TOTALA-EXE-RENDER.md` section 55 (the texture flags), `TOTALA-EXE-SHADING.md` | the animated half is not ported (rule 7) |
| unit script checksum | `TOTALA-EXE-DATA.md` sections 109 and 117; `TA-NETWORK.md` | a second checksum, separate from the FBI-bytes one (rule 8) |
| fonts, default palette | `BT-GRAPHICS-FONT-PARITY.md` | rule 9 |

## 2. Language-sensitive paths and the version check

**Reference.** `BuildDataPath` (`0x4290f0`) asks for the preferred-language string and, when it is
non-null, builds **`<directory>-<language>\<name>`** first; for a nonempty requested extension
it removes the existing extension and appends the requested one. It opens the candidate through the
HPI layer and, only if the probe succeeds, closes it and accepts the localised path; otherwise it
builds the ordinary `<directory>\<name>` path with the same extension rule and returns it
**without probing it**, so a returned path does not guarantee a file. A null or empty requested
extension leaves the name's own extension. Caller buffers are filled by unbounded string
operations. The source comment calls the suffix a "side", but its producer is the preferred-language
getter: this is language, not faction. A localised file therefore overrides the common one
**whether or not the common one exists**. `CheckGpfVersion` (`0x429000`) resolves
`gamedata/version.tdf`, loads it, selects the `Version` block and requires the `GPFVersion`
field to equal `v3.0` (case-insensitive); otherwise a translated warning about `Revision.GPF`
compatibility appears. A loaded file with no `Version` block or no field also warns; a file that
**fails to load bypasses the warning entirely**. The check never rejects startup, inspects
signatures or establishes multiplayer compatibility.

**Register.** `TOTALA-EXE-DATA.md` (near 818) records the localised-key reader: the language string
at `0x51FDC0` goes in front of a key (`Germanmissionname`), and (near 1636) the mission reader's path
builder `0x435430` tries `<dir>-<language>\` before `<dir>\`. `TA-SYSTEMS-PARITY.md` rule 33 and
BT-30 record that RWE has no such lookup.

**RWE: MISSING (the whole mechanism), and the census names what it would select.**

- **No preferred language exists in RWE.** `src/rwe/vfs` has no language handling; the campaign
  reader reads `missionname`, `brief` and `narration` unprefixed; `GameLaunch.cpp` has no
  language setting. BT-30 stands.
- **Census: the shipped data contains four language-suffixed directories.**
  `bitmaps-French`, `bitmaps-German`, `bitmaps-italian` and `bitmaps-spanish`, each holding exactly
  one file, `battleroom.pcx` (115,443 to 116,983 bytes), in each of `rev31.gp3`, `btdata.ccx` and
  `ccdata.ccx`. (The directory spellings differ in case, which the case-insensitive lookup absorbs;
  there is no `bitmaps-English`.) So on a French, German, Italian or Spanish setting the original
  would draw a different battleroom background, and on an English one the ordinary `bitmaps/` copy.
  RWE draws `battleroom` or `battleroom2` from `bitmaps/` only
  (`MainMenuScene_multiplayer.cpp` about 1446-1454), i.e. the English one always. It is the **only**
  localised art in the shipped archives; the text localisation is by key prefix, not by directory.
- **Version check: MISSING.** RWE never reads `gamedata/version.tdf`. **Census:**
  `rev31.gp3`, `btdata.ccx` and `ccdata.ccx` each carry it with `GPFVersion=v3.0`; `totala1.hpi` has
  none. So on the shipped install the original is silent, RWE is silent, and for a data folder
  without the revision archive (`totala1.hpi` and the others alone) the original is silent too,
  because the file fails to load. The warning would appear only for a revision archive whose value
  is not `v3.0`. Cosmetic, and not worth porting until a data set exists that triggers it.

## 3. The file adapters: bitmap, font, palette, radar picture

**Reference.**

| Entry | Rule |
| --- | --- |
| `LoadBitmapByName` (`0x429290`) | `bitmaps/<name>.PCX` through the PCX loader with the supplied palette; a failure reports the path in a fatal error |
| `LoadFontByName` (`0x4292e0`) | `fonts/<name>.FNT`, raw whole-file bytes; a null result is fatal |
| `LoadPaletteByName` (`0x429330`) | tries `palettes/<name>.PAL`, **using the file's length as the existence test**; if absent, decodes `palettes/<name>.PCX` into a new 1024-byte palette, **tries to write the `.PAL` cache** and **removes the common `PALETTE.ALP`, `.LHT`, `.SHD` derivative files**; an empty `.PAL` is treated as absent; an existing `.PAL` is not validated for size; cache writes and removals discard their errors |
| `LoadRadarPic` (`0x4295b0`) | opens a path, reads a 64-byte header; **only when bit 0 of the header byte at `+0x2c` is set** seeks to the offset at `+0x28`, reads width and height and allocates a `RADARPIC` frame; if both output pointers are given copies the dwords at `+4` and `+8`, if only one is given fills neither; dimensions are not checked before allocation |

**RWE.**

- **Bitmaps: MATCHES.** `TextureService.cpp` about 269 reads `bitmaps/<name>.pcx`.
- **Fonts: DIFFERS in source.** RWE's HUD font is fixed (`LoadingScene.cpp` about 315,
  `fonts/CONSOLE.FNT`; the speech font is a GAF), not named by the side (`BT-GAME-SIDE-DATA-PARITY.md`
  rule 4).
- **Palette: DIFFERS, one direction.** RWE reads `palettes/PALETTE.PAL` and `palettes/GUIPAL.PAL`
  only (`GameLaunch.cpp` about 286-306) and throws "Couldn't find palette" when a `.PAL` is absent.
  **There is no `.PCX` fallback, no cache write and no deletion of derivative files.** Census: the
  shipped `PALETTE.PAL` and `GUIPAL.PAL` exist (1024 bytes each); `GUIPAL.PCX` exists too and is
  a raw 1024-byte palette rather than a PCX picture (already noted in Batch 9), so the original's
  `.PAL`-first rule never reaches it; **no `PALETTE.PCX` ships.** So retail is unaffected. A mod
  that ships only `palettes/PALETTE.PCX` works in the original (decoded, cached, and the derived
  tables regenerated) and fails to launch in RWE. A mod that **replaces** `PALETTE.PAL` while the
  stock `PALETTE.SHD` stays in the archive is accepted by both (RWE trusts the `SHD` when its size
  is right; `BT-GRAPHICS-PALETTE-PARITY.md` rule 8); the original's cache deletion only follows its
  own regeneration.
- **Empty `.PAL` is absent: DIFFERS.** RWE reads a present empty file and `readPalette` then fails
  with a message; the original falls through to the `.PCX`. Same class as `BT-UTIL-HPI-PARITY.md`
  rule 3.
- **Radar picture: UNCLEAR, and the chapter's offsets do not fit the `.tnt` header.** The routine
  reads a 64-byte header, takes width and height from the dwords at `+4` and `+8` (which is where the
  `.tnt` keeps them), then wants an offset at `+0x28` and a flag at `+0x2c`. In the `.tnt` layout RWE
  uses (`TntArchive.h`) `+0x28` is the **sea level**, `+0x2c` the **minimap pointer** and `+0x30` the
  unnamed `unknown1`. **Census of all 275 `maps/*.tnt` headers in the 30 archives:** the dword at
  `+0x30` is zero in every one and the minimap pointer at `+0x2c` is nonzero in every one. If the
  chapter's offsets were simply four bytes early (flag = `unknown1`), the original would find bit 0
  clear in every shipped map and load no radar picture for any of them, which cannot be the
  behaviour of a game that shows map previews. So the routine either reads a file that is not a
  `.tnt` or the chapter has the offsets wrong. RWE reads the minimap unconditionally
  (`TntArchive::readMinimap`, `TntArchive.cpp` about 131) and nothing here suggests it should do
  otherwise. **Settled by** a spec-room read of the routine's callers (which file the path names).

## 4. The whole-file loader with progress

**Reference.** `LoadFileWithProgress` (`0x429660`) allocates the full length and reads it in **ten
chunks of length/10** plus the remainder, setting the loading-progress byte to **9 through 90** as
it goes. It does not check each read; a negative read could move the offset backwards; the last
read's result is ignored; a file under ten bytes produces zero-length reads, which are unsafe for
compressed members (`BT-UTIL-HPI-PARITY.md` rule 3). The allocation has no terminating NUL.

**RWE: MISSING in effect, presentation only.** RWE loads in one synchronous pass and its loading
screen shows five light bars that are **static demonstration values**: `LoadingScene.cpp` about 79
sets each bar to `i / 5.0` with the comment "for demo/debugging purposes", and nothing updates them
afterwards. The five labels (`Animation`, `3D Data`, `Explosions` and two more) match the original's
categories, so the original's bars fill as each category loads and RWE's do not move. The progress
figure is not simulation state and nothing reads it back. If the loading screen is ever driven, the
original's stage values are 9 to 90 in tenths of the map file read, not a percentage of the total
load.

## 5. The sound-name registry

**Reference.** `LoadSoundByName` (`0x429470`) keeps parallel arrays of sample pointers, 32-byte
logical names and 32-byte source filenames and a count. With a logical name it reuses an existing
nonempty logical name (case-insensitive within 32 bytes); with a null name it compares
filenames. A new entry loads the file, stores the filename and the logical name (or an empty string)
and returns the former count; it refuses at **count + 1 >= 256**, returning zero, which is also a
valid index. A null loaded sample is not rejected. The two reuse modes differ on purpose: reusing a
filename under a new logical name does not share the earlier registration, while reusing a logical
name with another filename returns the old one.

**RWE: not applicable, MATCHES where it matters.** RWE's sound bank is keyed by name and has no
256 limit (`BT-SOUND-PARITY.md` rule 2). The "logical name already taken" reuse corresponds to
RWE's name-keyed map; the filename-keyed mode has no counterpart because RWE resolves a sound by
its TDF name to a file once. The limit of 255 is the original's; **the retail `allsound.tdf` has 41
names** (BT-SOUND census), nowhere near it.

## 6. The animation-file cache and the fixed list of shared resources

**Reference.** `LoadAnimGaf` (`0x429700`) searches a cache of (64-byte logical name, GAF pointer)
records case-insensitively; a miss loads `anims/<name>.GAF` (fatal if it fails), reallocates the
record array by one 68-byte record and appends. The key is the **logical name, not the resolved
path**, so changing the preferred language does not invalidate an entry. `LoadGameResources`
(`0x429870`) then fetches a **fixed list** and stores borrowed entry pointers:

| File | Entries |
| --- | --- |
| `fx` | `smoke 1`, `smoke 2`, `fire1`, `alfboom1`, `radlogo`, `radlogohigh`, `nuclogo`, `h2oboom2`, `lavasplash`, `cannonshell`, `plasmasm` (twice, in two fields), `plasmamd`, `ultrashell`, `flamestream`, `explosion`, `explode2` to `explode5`, `nuke1`, `shadow` |
| `igtitles` | `igvictory`, `igdefeat`, `igpaused` |
| `vismasks` | `vismask` |
| `fog` | `Black1` to `Black4`, `Gray1` to `Gray4` |
| `cursors` | 21 cursor names, including `pathicon` |
| per-side `intgaf` | `PANELTOP`, `PANELSIDE`, `PANELBOT` |

For `alfboom1`, the water and lava splashes, `explosion`, `explode2` to `explode5` and `nuke1`
loading **clears the byte at entry offset 2**, an entry-state change and not a general "start every
animation". Missing entries are not checked. The same routine reads `gamedata/sidedata.TDF`:
`GENERAL/baseheight` defaults to **480**; it then walks `SIDE0`, `SIDE1`, ... to the first
missing record and, when `intgaf` exists, loads that animation file and the three panel entries; the
walk does not enforce the five-element panel array's capacity. `FreeAnimFiles` frees the cache and
the per-side panel files and does not individually free the borrowed entry pointers.

**RWE.**

- **The fixed list is not a list in RWE: DIFFERS in mechanism, MATCHES in effect for what RWE
  uses.** Each entry is fetched where it is used. Present: `smoke 1`/`smoke 2`, `cannonshell`,
  `plasmasm`, `plasmamd`, `ultrashell`, `flamestream`, the explosion and `fire1` entries
  (`GameSimulationLoader.cpp` about 645-665), `radlogo` (`LoadingScene.cpp` about 291, checked for
  its frame count), `igvictory`, `igdefeat`, `igpaused` (`GameScene_endgame.cpp`,
  `GameScene_render.cpp`), `Black1` to `Black4` and `Gray1` to `Gray4` (`render/FogTiles.cpp`), the
  fifteen cursors (rule below), and the `PANELTOP` and `PANELBOT` entries from the side's `intgaf`
  file (`GameScene_render.cpp` about 173-176).
- **Not fetched by name in RWE:** `alfboom1`, `h2oboom2`, `lavasplash`, `nuclogo`, `shadow`,
  `radlogohigh`, the `vismask` entry (RWE computes its own disc; `BT-MAP-LINE-OF-SIGHT-PARITY.md`
  compared it with the shipped frames) and `PANELSIDE`. The explosion and splash names are resolved
  from the weapon and explosion TDFs by their `explosiongaf`/`explosionart` keys, so they are loaded
  when the data names them. `shadow` and `radlogohigh` and `PANELSIDE` have no RWE use found; whether
  the original's left column draws `PANELSIDE` where RWE draws the side GUI's own background was not
  traced. **UNCLEAR, low.**
- **Cursors: DIFFERS, already audited.** Fifteen of 21 are loaded; `cursorairstrike`, `cursortoofar`,
  `cursorteleport`, `cursorrevive` (used by the default action only), `cursorfindsite` and
  `cursorhourglass` are not (`BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 11).
- **The entry-state clear: DIFFERS (presentation).** RWE has no per-entry "started" byte; an
  explosion sprite series begins at its first frame by construction.
- **`baseheight` (480): not applicable.** RWE reads no `GENERAL` block of `sidedata.TDF`
  (`SideData.cpp`) and scales its own interface (`effectiveUiScale`); **census:** neither shipped
  `sidedata.TDF` has a `GENERAL` block, so the default applies. Side counting: contiguous, as in
  `BT-GAME-SIDE-DATA-PARITY.md` rule 2.
- **Cache key by logical name: MATCHES in effect.** `TextureService` caches a GAF by its path and
  entry; with no language change it makes no difference.

## 7. Texture archives and model binding (animated textures, the logos family)

**Reference.** `LoadTextureGafs` (`0x42a440`) resolves `textures/*.GAF`, counts the entries, assumes
**exactly one is `logos.GAF`**, stores the block count as count minus one, allocates count pointer
slots and loads every file except the logos. Progress is computed from count minus one, so a
directory holding only a non-logo file divides by zero. `LoadObject3d` (`0x42a2c0`) loads
`objects3d/<name>.3DO` and calls `BindModelTextures` (`0x42a140`), which recurses into both child
links and, for every non-null texture name in an object's elements, resolves it **against the loaded
texture GAFs in order, then against the logos GAF**. It clears flag value 4 first; a logos match
with **exactly ten frames** sets that flag (the special logo family); an unresolved name sets flag 1
and colour `0xd1`; a matched entry with **more than one frame** initialises a **GAF sequence
directly over the element's former name storage**, sets flag 2, and (for an ordinary animated
texture, not a logo) **appends the sequence's address to a global animation-play list** so it is
stepped every frame; a one-frame entry stores its frame pointer and clears flag 2. The binding is
destructive (the name slot becomes state), so it cannot be repeated. `AddAnimplayPointer`
(`0x42a0e0`) grows that list by one.

**Register.** `TOTALA-EXE-RENDER.md` section 55 (about 1400-1410): the draw path takes the owning
player's colour index for a ten-frame team texture, the **current animation frame** (`0x4B7EE0`) for
any other multi-frame texture, except that a freeze flag from the model-instance routine
(`0x458810`) pins frame 0; single-frame textures use the stored pointer. `TOTALA-EXE-RENDER.md`
line 1412 says the freeze condition ("texture animation runs only while the unit is active") is an
inference.

**RWE: DIFFERS (animated textures are not ported), MATCHES for team colour and single frames.**

- **Team-colour (ten-frame) textures: MATCHES in effect.** `createTextureAtlases`
  (`atlas_util.cpp` about 270-300) skips `LOGOS.GAF` and handles the colour textures through a
  separate team atlas (`createTeamColorAtlases`, above it), which is the frame-by-owner-colour rule.
- **Animated (2 to 12 frame) textures: DIFFERS, a known TODO.** `atlas_util.cpp` about 303-308
  keeps **frame 0 only**: "just drop multi-frame (animated) textures for now and use the first
  frame ... TODO: support animated textures". It is not in section 88 or section 91. **Census:**
  among the shipped texture files, the animated entries are `flashing02` (6 frames) and `glow` (8) in
  `armbldg.gaf`, `CorPlat` (4) and `flashing01` (6) in `corbldg.gaf`, three `AlienGlo` entries
  (12 each) in `exp1.gaf` (the expansion data), and `onoff01` (2 frames) in the logos file; every
  other entry in the eleven files is a single frame or a ten-frame colour. A byte search of the
  596 distinct models for those names finds `glow` in about 19 (including `armcom` and `corcom`,
  the commanders), `flashing01` in 8 (the Core labs, vehicle plant, shipyard, `corgant`),
  `flashing02` in 8 (Arm aircraft and vehicle plants, shipyards, sonar, `armtarg`, `cortarg`,
  `corgant`), `CorPlat` in 2 (`corap`, `coravp`) and `AlienGlo` in 4 (the expansion's beacon and
  `dev1` units). The count is a substring search and `glow` may over-count. So in the original those
  parts pulse or blink (status lights on factories, glowing eyes on commanders), and in RWE they
  show their first frame permanently.
- **Whether the animation runs only while the unit is active** is the register's inference (the
  freeze flag, `-RENDER.md` section 55); a port would need that read first. A low-cost version is
  to animate always.
- **Recursion into both child links, the destructive binding, the `0xd1` unresolved colour:** the
  unresolved-name rule (flag 1, colour `0xd1`, grey) is the register's (`-RENDER.md` section 55) and
  RWE's flat-colour path; not re-audited here. The chapter's uninitialised search variable when no
  texture GAF is loaded has no analogue (a data folder with no `textures/` fails earlier).
- **Exactly one `logos.GAF` assumed: DIFFERS in the safe direction.** RWE compares the file name and
  skips it; with none present or two it still loads the rest.
- **The logos fallback:** a model texture name that matches no ordinary GAF is looked up in the logos
  file by the original. RWE's logos file is read separately for the player logo and team colours
  (`GameScene_render.cpp` about 211-216, `MainMenuScene.cpp`); whether model-texture names resolve
  through `LOGOS.GAF` in `mesh_util` was not traced. **Census:** `logos.gaf` has 18 entries (17 of
  ten frames, `onoff01` of two); the ten-frame names include `colorslt`, `colorsmd`, `colorsdk`,
  `Solid1a`, `32xlogos`, `Arm32Lt`, `Core32Dk` and the Gouraud logos, which are the player-colour
  entries, so the fallback is how `32xlogos` ends up on models and is the team-colour path.

## 8. The unit script checksum

**Reference.** `ComputeUnitScriptChecksum` (`0x42a610`) returns at once if a definition's
`scriptChecksum` is nonzero; otherwise (with the definition table made writable) it XORs into that
field: the language-resolved `scripts/<unitname>.cob` bytes; **every** file matching
`guis/<unitname>*.gui` (a wildcard scan of the merged view, each loaded individually through the same
path builder); the language-resolved `download/<unitname>.tdf` bytes if the file opens and has a
positive size; and the definition's `weaponHashXor`. Missing files are skipped. XOR is order
independent but permits cancellation, and a result of zero is indistinguishable from "not computed"
and is recomputed. Network compatibility decisions that use it belong to their callers.

**Register.** `TOTALA-EXE-DATA.md` sections 109 and 117: the `0x1a` unit-table record's content id is
the checksum over the **raw bytes of the unit's FBI** (`taUnitTypeChecksum`, `TaChecksums.h`); the
FBI-bytes id is what a joining TA is looked up by. `BT-UNITS-UNIT-TYPES-PARITY.md` rule 1 notes a
per-FBI checksum with an override bank. Nothing in the register mentions the **script** checksum.

**RWE: MISSING; the consumer is not identified.** RWE computes no script checksum; its lobby
compares archive hashes (`launcher/src/common/archives.ts`) and its TA-host path echoes the joiner's
FBI ids. Two facts for whoever reads the consumer:

- **The `unitname*` wildcard is wider than the numbered pages.** RWE reads builder pages as
  `<unit>1.GUI`, `<unit>2.GUI`, ... to the first missing number (`GameSimulationLoader.cpp` about 301).
  **Census:** 188 distinct GUI names against 278 units; 54 units have a page; for **two units the
  wildcard picks up another unit's pages** (`armcs*` finds `armcsa1` to `armcsa5`, `corcs*` finds
  `corcsa1` to `corcsa5`, the advanced construction ships' pages) and one GUI (`corbuild`) is unnumbered.
  So the original's checksum for `ARMCS` and `CORCS` includes pages that are not theirs. It only
  matters if two machines' checksums are compared and one of them lacks the other's pages.
- **If a real TotalA.exe host compares this value** against a joining RWE's, `TA-NETWORK.md`'s "an RWE
  that joins still needs [the CRCs]" has a second number to supply. Whether the script checksum is ever
  transmitted is not in this chapter (the unit table's record carries the FBI one). Hosting needs
  neither (`TA-NETWORK.md`).

## 9. Fonts, the default palette and lifetimes

**Reference.** `LoadGameFonts` (`0x42a320`) loads the `COMIX` and `smlfont` fonts into two game
fields; `FreeGameFonts` frees both. `LoadDefaultPalette` (`0x42a400`) loads `PALETTE` through the
palette adapter, copies **1024 bytes** into the game's embedded palette and frees the temporary
buffer, relying on the loader's size and not on the returned length. Fonts, the animation cache and
the battle textures have separate cleanup functions called at different boundaries. The final owned
entry, `0x42a870`, is `TdfFile`'s compiler-generated vector deleting destructor.

**RWE: not applicable (fonts and palette lifetime), DIFFERS (which fonts).** The font set is RWE's
own (bitmap fonts through `getFont`, GAF fonts for the speech and loading text; `BT-GRAPHICS-FONT-PARITY.md`).
The palette's exact 1024 bytes: `readPalette` (`ColorPalette.cpp`) validates its input. Lifetimes are
RAII; the MSVC deleting destructor has no counterpart.

---

## 10. Gaps to fix

Ranked by what a player or modder would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Animated model textures are drawn frame 0 only** (status lights, commander eyes; about 40 shipped models). A known TODO, absent from sections 88 and 91. | DIFFERS (presentation) | code: step the frame, decide the freeze rule (always, or while active, an inference) |
| 2 | **Language-suffixed lookup is missing wholesale**; the shipped data has four `bitmaps-<Language>/battleroom.pcx` files. English only. | MISSING (BT-30) | decision (a language setting); then a path builder in the composite file system |
| 3 | **A palette shipped as `.PCX` only fails to launch** in RWE; the original decodes it, caches it and regenerates the derived tables. An empty `.PAL` is "absent" in the original. | DIFFERS, mods only | code: `.PCX` fallback in `GameLaunch.cpp`; the empty-file rule of `BT-UTIL-HPI-PARITY.md` |
| 4 | **The loading screen's bars never move** (static demonstration values), where the original fills five bars as the map loads. | MISSING (presentation) | code, if the screen is wanted |
| 5 | **The radar-picture loader's header offsets do not fit the `.tnt` header** (flag at `+0x2c` where the `.tnt` has the minimap pointer; the shifted field is zero in all 275 shipped maps). RWE reads the minimap unconditionally. | UNCLEAR, probably a chapter slip | spec-room read of the callers |
| 6 | **`version.tdf` and `GPFVersion = v3.0` warning** are not read. | MISSING, cosmetic | none |
| 7 | **The script checksum** (COB + GUI wildcard + download TDF + weapon hash, XORed) is not computed; consumer unknown. | MISSING, UNCLEAR | spec-room read of the checksum's readers |
| 8 | **Fixed resources with no RWE use** (`shadow`, `radlogohigh`, `nuclogo`, `PANELSIDE`). | UNCLEAR, low | none |

No source file was changed.
