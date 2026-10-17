# Archive mounting, CD identification and content gates parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-UTIL-SQSH-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
module chapter of the reference, `game-cd-check`: which archives the original mounts and in what
order, how it identifies a campaign or multiplayer disc, the alternate path builder, the two
global content gates that unit and weapon enumeration read, and a padded allocation helper.
`TA-SYSTEMS-PARITY.md` rule 32 already audited the same registration sequence from the startup
chapter (01 section 4.1); this chapter is the implementation behind it and **answers the question
rule 32 left open**, so the first half is a correction of that audit as much as a new one. This is
the finding that implementation work cites for archive precedence, per `AGENTS.md` and ADR-0004.

The archive reader itself (headers, metadata, shadow flags, handles) is the `util-hpi` chapter
and is audited in `BT-UTIL-HPI-PARITY.md`; compression is `BT-UTIL-SQSH-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-cd-check.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names (`0x41d4c0` to `0x41d920`); rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only: BT-29 (archive tie
  classes, extra `.gpf` and `.gp3`, ten-HPI cap) is open and is revisited here, BT-30 (language
  directories) is the neighbouring module's.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no drive, archive inventory or disc was probed. Weaker than `TOTALA-EXE*.md`.
  Disagreements are marked **CONFLICT**, with no winner picked.
- **No executed observation of the original was used.** Several findings below are about what
  the original would mount or accept; the Gaps table says which need an original-game check.
- **The RWE half** is a read-only audit of `src/rwe/vfs/CompositeVirtualFileSystem.cpp`,
  `HpiFileSystem.cpp`, `DirectoryFileSystem.cpp`, `launcher/src/common/archives.ts`,
  `src/rwe/game/GameSimulationLoader.cpp` (unit and weapon enumeration) and
  `src/rwe/MainMenuScene_campaign.cpp` at the working tree of 2026-10-17. Nothing was built, run
  or changed. Another agent had uncommitted edits in the tree, so line numbers are "about".
- **A census of the retail install** (`D:\Total Annihilation\Total Annihilation`, the GOG copy,
  every archive through `tools/hpi.py`'s reader and throw-away scratch scripts outside the
  tree): 30 archives and 7889 entries; the overlap of entries across archives; and a byte
  comparison of every entry that two archives both hold.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The registration sequence and what its integer argument means (the BT-29 answer)
3. The ten-HPI allowance, and this install
4. Order inside one pattern, and the launcher's copy of it
5. Disc identification and the alternate path builder (not applicable)
6. The two content gates: loose units and weapons
7. The padded allocation helper (not applicable)
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| registration sequence | `TA-SYSTEMS-PARITY.md` rule 32 (the same five stages) | stage order **MATCHES**; the tie-class reading is **corrected** (rule 2) |
| ten-HPI allowance | rule 32, BT-29 | the allowance is stated more exactly; a census gives it teeth (rule 3) |
| identifying a disc | `TOTALA-EXE-DATA.md` section 105 ("a Campaign-CD check") | not applicable (rule 5) |
| alternate path builder | none | not applicable (rule 5) |
| the content gates | `BT-UNITS-UNIT-TYPES-PARITY.md` rule 1 mentions "archive or CD-origin checks" in one clause | **new**, rule 6 |
| padded allocation | none | not applicable (rule 7) |

## 2. The registration sequence and what its integer argument means (the BT-29 answer)

**Reference.** `RegisterDataArchives` (`0x41d4c0`) does nothing when the global `DAT_0050289c`
is zero. When it is nonzero, it first drops registered archives that can no longer be opened,
changes the process directory to the executable's directory, and searches in this order:

| Stage | Files | Integer argument to `AddArchive` |
| --- | --- | --- |
| 1 | `rev31.GP3` | 1 |
| 2 | every visible `*.CCX` | 1 |
| 3 | every visible `*.UFO` | 0 |
| 4 | `*.HPI`, until ten additions succeed or the enumeration ends | 0 |
| 5 | every `*.hpi` at the root of each enumerated CD-ROM drive | 0 |

Each enumeration is closed, and afterwards the routine explicitly resolves archive shadow flags.
It does not restore the working directory. **The integer is the archive's stream retention, not a
priority**: mode 1 keeps an archive's stream open after metadata and member use, mode 0 lets it
close and reopen lazily. "Priority comes from insertion order." Loose files still win ordinary
opens before any archive; among archives earlier mounts win; shadow resolution suppresses later
duplicates during *discovery* (enumeration), while direct opens search the sources in priority
order without consulting the shadow flags.

**What this corrects.** `TA-SYSTEMS-PARITY.md` rule 32 read the startup chapter's "high-priority
registration argument" as possibly making `rev31.GP3` and the CCX files one tie class and the UFO
and HPI files another, and called RWE's strict ranking GP3 over CCX over UFO over HPI a
**DIFFERS / UNCLEAR**. With this chapter the integer is retention, so the ranking is **strict by
stage**: `rev31.GP3`, then the CCX files, then the UFO files, then the HPI files, then CD roots.
RWE's order is the same.

**RWE: MATCHES for the stages the retail install has.** `addToVfs`
(`CompositeVirtualFileSystem.cpp` about 168-184) adds the data folder itself (loose files) first,
then archives by extension in the reverse of its list, so `readFile` (about 13-35), which takes the
first match, searches `.gp3`, `.gpf`, `.ccx`, `.ufo`, `.hpi` in that order. The launcher's copy
(`archives.ts` about 28, `archiveExtensions`) is the same list already reversed.

- **The differences that remain are only those rule 32 already lists:** RWE also mounts `.gpf`
  and **any** `.gp3` (the original, only `rev31.GP3`), mounts every `.hpi` (the original stops
  after ten successes, rule 3), and has no CD stage. The strict `.gp3` over `.ccx` ranking itself
  is no longer a difference.
- **Case-insensitive extension match: MATCHES.** `addHpisWithExtension` upper-cases both sides.
- **Repeated registration is not a rebuild: not applicable.** RWE builds its file systems once.

**Census: how much the GP3-over-CCX ranking decides on retail data.** `rev31.gp3` (887
entries), `btdata.ccx` (1121) and `ccdata.ccx` (1877) overlap heavily, and the shared entries are
**not** all identical:

| Pair | Shared entries | Differing content | What differs |
| --- | ---: | ---: | --- |
| `rev31.gp3` and `btdata.ccx` | 887 (all of rev31) | 98 | 96 `anims/*.gaf`, `gamedata/translate.tdf`, **`weapons/armyork_weapon.tdf`** |
| `rev31.gp3` and `ccdata.ccx` | 820 | 124 | 103 `anims`, 9 `ai/*.txt`, 11 `textures`, `translate.tdf` |
| `btdata.ccx` and `ccdata.ccx` | 1051 | 112 | 91 `anims`, 9 `ai/*.txt`, 11 `textures`, `translate.tdf` |

Every `units/*.fbi`, `scripts/*.cob`, `download/*.tdf` and `objects3d/*.3do` that two of the three
hold is byte-identical, and so are all weapons but one. So the order among these three decides
**graphics** (the button and building art in `anims` and `textures`), one translation file and,
between `rev31.gp3` and `btdata.ccx`, **one weapon definition**: `weapons/armyork_weapon.tdf`
(`ARMyork_GUN`, "Flak Cannon", range 680, damage 128). `rev31.gp3`'s copy sets `reloadtime=.55`;
`btdata.ccx`'s copy has no `reloadtime` and repeats the line `edgeeffectiveness=0.90` in its place,
which reads as a copy-and-edit slip that the revision archive fixed (`ccdata.ccx` holds no copy).
Which file wins is therefore a gameplay value: with `rev31.gp3` first, which is both the
original's insertion order and RWE's, the weapon reloads in 0.55 s; if the BT copy won it would
get the loader's default. The order of `btdata.ccx` against `ccdata.ccx`, both CCX, is the
discovery order (rule 4); they differ in no unit, weapon or script.

## 3. The ten-HPI allowance, and this install

**Reference.** Stage 4 stops after **ten successful additions** (success means a new, valid
archive; `AddArchive` returns zero for a duplicate path *and* for a failed open, so neither
consumes the allowance). Stages 1 to 3 and 5 have no corresponding limit.

**RWE: DIFFERS.** No limit (`addHpisWithExtension` mounts every `.hpi`, skipping any that will
not open). That is noted in `TA-SYSTEMS-PARITY.md` rule 32 and BT-29 as "outside shipped
content". **The census says the shipped GOG install is over the limit:** its folder holds
**13** `.hpi` files: `tactics1.hpi` to `tactics8.hpi`, `totala1.hpi` to `totala4.hpi` and
`worlds.hpi`. If the enumeration returns them in name order (which is what NTFS gives), the
first ten are `tactics1` to `tactics8`, `totala1` and `totala2`, and **`totala3.hpi`,
`totala4.hpi` and `worlds.hpi` are never mounted by this routine**. What that would drop:

| Archive | Holds | Also held by |
| --- | --- | --- |
| `totala3.hpi` | 25 entries, `install/` and `InstallRes/` only | nothing needed in play |
| `totala4.hpi` | 152 `camps` files, 100 `maps`, 50 `bitmaps`, 7 `InstallRes` | `ccdata.ccx` holds `camps/arm campaign.tdf` and `camps/core campaign.tdf`; `ccmiss.ccx` holds `maps/ac03.ota` |
| `worlds.hpi` | 837 `sections/*.sct` (random-map section files) | no other archive |

RWE reads `totala4.hpi` (the campaign descriptions and maps) and ignores `sections/` (no code
reads it). So the only retail difference the cap would make to RWE's own content is **which
campaign and map files exist**, and most of those are also in the CCX files. Whether the original
actually runs this install with those three archives unmounted is the question; the GOG launcher
may also change the working directory or pass the files another way, and the chapter's
description is of the function and not of this install. **Settled by** an original-game check
(open the GOG game, look for a map or campaign only `totala4.hpi` has; or list the mounted
archives with a file-handle tool), or by a spec-room read of the callers that mount `Data\`
content outside this routine. Until then RWE's behaviour (everything mounted) is the safer
direction for a player and the less faithful for a modder who counts on the cap.

## 4. Order inside one pattern, and the launcher's copy of it

**Reference.** "Enumeration order within a pattern is not sorted here"; the exact order of two
`*.CCX` files "depends on discovery". Same-pattern order follows the operating system's
directory enumeration.

**RWE.** The engine iterates `fs::directory_iterator` and mounts in that order, with no sort
(`addHpisWithExtension`, about 140-166): NTFS gives name order, Linux an unspecified one
(`TA-SYSTEMS-PARITY.md` rule 32). The launcher **decides** a stable order instead
(`archives.ts` `sortArchivesInLoadOrder`, about 64-78): by extension in the VFS order, then by
lower-cased name. Two consequences the earlier audit did not draw:

- **The launcher's order and NTFS's differ for some names. DIFFERS (small).** NTFS orders by the
  upper-cased name, so `_` (0x5F) sorts after every letter; the launcher lower-cases, so `_`
  sorts before the letters (0x5F against 0x61). `a_b.ufo` and `ab.ufo` are in opposite orders in
  the two; the same holds for `[ \ ] ^` and the backquote. No shipped archive name has one of
  these (the census names are plain letters and digits), community packs may. The check compares
  the *launcher's* list on each side, so two players whose engines resolve the pair differently
  would be told the data matches.
- **A symbolic link to an archive is ignored by the launcher and mounted by the engine.
  Probable code fault.** `listArchives` keeps entries where `Dirent.isFile()` is true
  (`archives.ts` about 97), and Node reports a symbolic link as a link, not a file, so a linked
  archive is not hashed; `addHpisWithExtension` selects by extension alone and follows the link. A
  data folder assembled from links (common on Linux installs) would pass the lobby's check while
  loading data the check never saw. Not run; it follows from Node's documented `Dirent` semantics.

**Census: does the within-extension order matter on retail?** Between `btdata.ccx` and
`ccdata.ccx`, the 112 differing entries are art, AI text files and one translation (rule 2), none
of them a unit, weapon or script; the HPI files overlap only in `totala1`/`totala2` (85
identical entries) and the `tactics` pairs. So on retail data **no simulation value depends on
the order**. It could for a mod. The cheap fix that closes both the cross-platform ambiguity and
the launcher mismatch is to sort inside the engine by the launcher's rule (and have the launcher
use the same comparison), not to copy NTFS.

## 5. Disc identification and the alternate path builder (not applicable)

**Reference.** `FindGameCdDrive` (`0x41d6a0`) returns `.` immediately when the global is nonzero,
before looking at its side argument. Otherwise it scans CD-ROM drives in ascending letter order
(or, with `g_cdBypassDriveScan`, probes the lowercase letter `h` once without asking Windows
whether it is a CD drive) and reads each root's `TOTALA.ID`, section `Contents`, key `Campaign`
(request 0) or `Multiplayer` (request 1), default zero; the first nonzero returns the letter. A
comparison of the formatted path's first byte with the drive letter can set `g_cdPathMismatch`
to one, which nothing ever clears. `BuildCdFilePath` (`0x41d7b0`) delegates to the ordinary
language-aware path builder when the global is zero; when nonzero it builds `.\<dir>\<name>` and
**strips an existing extension unconditionally**, adding one only when the requested extension is
nonempty.

**RWE: not applicable.** RWE has no disc, no `TOTALA.ID` and no CD stage; the data is a folder.
Campaign availability is decided by the data being there (`MainMenuScene_campaign.cpp` about
83-101 lists `camps/*.tdf` and filters by side). `TOTALA-EXE-DATA.md` section 105 records the
original's "Campaign-CD check" before the new-campaign screen; the GOG release passes it by the
global being set (see rule 6), and RWE does not have the question. Worth knowing for one reason:
**the retail data folder carries no `TOTALA.ID`** among the entries listed (the census holds only
the archives), so a future "is this the campaign disc" check cannot be added from file presence.

## 6. The two content gates: loose units and weapons

**Reference.** The global from rule 2 has a second job. The chapter's consumers:

- **Weapon enumeration** (`weapon_types.cpp`): loads a weapon TDF **only when its parser flags
  identify accepted archive-backed content, or when the global is zero**.
- **Unit-type discovery** (`unit_types_42a8d0.cpp`): applies a related parser-origin test. It
  clears the unit's availability and marks bad content **when the global is nonzero and the
  definition is loose**, or when the mismatch getter returns nonzero. The version and copyright
  tests stay as additional gates in that consumer; passing the CD helper bypasses neither and
  does not guarantee multiplayer compatibility.

In the mode in which archives are registered by this routine (the global nonzero), **a unit or
weapon definition read from a loose file is rejected, and one from an archive is accepted.**
Whether a normal retail run is in that mode is not shown by the chapter (see below). Putting a loose `units/XYZ.FBI` in the game folder does not add a
unit; a mod has to ship it in a `.ufo`, `.ccx` or `.hpi`.

**Register.** `TOTALA-EXE*.md` is silent on a loose-file rule. `BT-UNITS-UNIT-TYPES-PARITY.md`
rule 1 notes that availability is "cleared by archive or CD-origin checks or a copyright
comparison" and does not follow it. `TOTALA-EXE.md` section 91 and the compatibility document do
not list it.

**RWE: DIFFERS (an undocumented divergence), and the other half is UNCLEAR.**

- RWE reads units, weapons, downloads and features through the merged file system; the loose
  data folder comes **first** and wins (`GameSimulationLoader.cpp` about 412, 483-520; the
  first element of `addToVfs`). A loose `units/NEW.FBI` becomes a unit in RWE. In the original it
  would be a rejected, "bad content" definition.
- It is unclear what "parser flags identify accepted archive-backed content" means for a *weapon*:
  the chapter says only that the flags are set by the parser from the file's origin. The weapon
  file's origin could be loose, archive, or a CD archive, and the CD-archive case is accepted.
- It is unclear whether the retail GOG binary (patched not to need the disc) sets the global at
  all. If it did not, `RegisterDataArchives` would not mount anything here and the game would
  have to register the archives elsewhere (it plainly runs), and the loose-file rejection would
  not apply. The chapter calls the global "unnamed in the source" and does not show a writer.
- **Effect on RWE:** none on retail data. It matters to (a) a modder who tests loose units in RWE
  and expects TA to behave the same, and (b) a **replay or lobby** where one peer has a loose
  definition: the archive-hash check hashes archives only, not loose files
  (`archives.ts` `isArchive`), so a loose override is invisible to it. That is a sim-affecting
  hole that exists whatever the original does.

**Settled by** an original-game check (a loose `units/ARMPW.FBI` with a changed hit-point value
in the game folder: does the Peewee change?) or a spec-room read of where the global and the
parser-origin flag are set. This is the one item in the chapter that could change what content RWE
accepts.

## 7. The padded allocation helper (not applicable)

**Reference.** `AllocZeroedWithTickOffset` (`0x41d8c0`) is in this group but reads no CD state.
It allocates the request plus **tick count modulo 1000, times seven** bytes of padding (zero to
6993), zeroes the whole block, and returns an **interior pointer** (base plus padding). Callers
cannot pass it to the ordinary release; there is no stored header; the size arithmetic and the
allocation are unchecked. The chapter notes an unused declaration of it exists in GUI code and
that its purpose is not established by the body.

**RWE: not applicable.** The effect (time-dependent address placement) is the original's
workaround or accident; the project rule is the opposite, that nothing hashed depends on the
heap (`CLAUDE.md` "A member the hash reads is initialised by the time the object exists").

## 8. Gaps to fix

Ranked by what could change content or the lobby's verdict. Items 1 and 3 can touch the
simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Loose unit and weapon definitions are rejected by the original (archive mode) and accepted first by RWE.** Also invisible to the lobby's archive-hash check. | DIFFERS, undocumented; meaning of the global UNCLEAR | original-game check (loose FBI edit), then document in section 88 or restrict loose files; add loose files to the lobby comparison either way |
| 2 | **Thirteen HPIs on the shipped install against a cap of ten**: `totala3`, `totala4`, `worlds` would not be mounted in name order. RWE mounts all. | DIFFERS, UNCLEAR whether the install runs that way | original-game check or a spec-room read of the other mount callers |
| 3 | **Within-extension order is unspecified in the engine and differs between the launcher and NTFS** for names with `_` and a few punctuation marks. Retail data does not care; mods may. | DIFFERS (cross-platform) | code: sort in the engine with the launcher's rule, in step |
| 4 | **Symbolic-link archives are loaded by the engine and ignored by the launcher's hash.** | probable code fault | code: use `stat`, not `Dirent.isFile`, in `listArchives` |
| 5 | **BT-29's tie-class half is closed** (the integer is retention); the extras (`.gpf`, other `.gp3`) and the cap remain. Update the register row. | doc correction | none |
| 6 | **GP3-over-CCX decides one weapon file on retail** (`armyork_weapon.tdf`) and 96 to 103 art files; document so a change to the order is a conscious one. | information | none |
| 7 | **No `TOTALA.ID` or disc stage**; campaign availability is by data presence. | design | none |

No source file was changed.
