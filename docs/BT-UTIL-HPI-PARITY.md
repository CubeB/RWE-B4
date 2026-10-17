# HPI archives, file handles, discovery and package writing parity audit

Spec-room finding, 2026-10-17. Like `BT-UTIL-SQSH-PARITY.md`, `BT-UTIL-TDF-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
module chapter of the reference, `util-hpi`: how the original opens a file from the combined
view of loose files and mounted archives, how it reads, closes and bulk-loads, how it matches
wildcards and enumerates directories, how it loads and shadows an archive, and how it writes
one. This is the finding that implementation work cites for virtual-file-system behaviour, per
`AGENTS.md` and ADR-0004.

The compression inside a member (the `SQSH` chunk format, its checksum, the LZ77 ring) is
audited in `BT-UTIL-SQSH-PARITY.md` and is not repeated; which archives are mounted and in what
order is `BT-GAME-CD-CHECK-PARITY.md`; the language-suffixed lookup is `BT-GAME-DATA-FILES-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\util-hpi.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names (58 entries, `0x4bb190` to `0x4be950`);
  rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-159 (archive
  chunk strictness and the LZ77 ring) is resolved and not re-raised, BT-29 is the neighbouring
  audit's.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no archive was generated, mounted or run. Weaker than `TOTALA-EXE*.md`.
  Disagreements are marked **CONFLICT**, with no winner picked.
- **No executed observation of the original was used.** The Gaps table says which items an
  original-game check settles.
- **The RWE half** is a read-only audit of `src/rwe/io/hpi/` (`HpiArchive.cpp`, `hpi_util.cpp`),
  `src/rwe/vfs/` (`CompositeVirtualFileSystem.cpp`, `HpiFileSystem.cpp`,
  `DirectoryFileSystem.cpp`, `AbstractVirtualFileSystem.h`) and the call sites that list
  directories (`GameSimulationLoader.cpp`, `LoadingScene_util.cpp`, `atlas_util.cpp`,
  `MainMenuScene.cpp`, `MainMenuScene_campaign.cpp`, `AudioService.cpp`) at the working tree of
  2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in the tree,
  so every line number is "about".
- **A census of the retail install** (the 30 archives of `D:\Total Annihilation\Total
  Annihilation`, through `tools/hpi.py`'s reader and throw-away scripts kept outside the tree):
  7889 entries; every directory checked for two entries of one name; every archive's last bytes;
  zero-length entries; the spellings of one path across archives; and the directory listings the
  engine builds, compared with and without case.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Opening a file: loose first, archives in order, how a name is looked up
3. Reading: partial reads, empty files and the whole-file adapters
4. The outer obfuscation and the header key
5. Wildcards and directory enumeration
6. Opening an archive: the gate, the copyright footer and the metadata
7. Shadow resolution
8. Package generation (not applicable)
9. Drive and working-directory helpers (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| opening and selecting | `TA-SYSTEMS-PARITY.md` rule 32 (loose first, archive order) | agrees; adds duplicate names, separators, directories (rule 2) |
| seeking, reading, block caches | `BT-UTIL-SQSH-PARITY.md` (the chunk format) | RWE reads whole members; the cursor and cache are not applicable (rule 3) |
| whole-file adapters | `BT-GRAPHICS-MOVIES-PARITY.md` and `BT-NEW-CONFLICTS.md` item M9 (a zero-length film) | the **cause** is here (rule 3) |
| outer obfuscation | `tools/hpi.py` and `hpi_util.cpp` (read once, agree on the key) | agrees on every retail archive (rule 4) |
| wildcards, enumeration | `BT-UTIL-TDF-PARITY.md` rule 5 (duplicate names in TDF), none for files | rule 5 |
| archive open, relocation | `docs/SECURITY-AUDIT.md`, `BT-NEW-CONFLICTS.md` Batch 8 and 9 hardening notes | the footer gate is new (rule 6) |
| shadows | `BT-GAME-CD-CHECK-PARITY.md` rule 2 | rule 7 |
| package writing, drives | none | not applicable (rules 8 and 9) |

## 2. Opening a file: loose first, archives in order, how a name is looked up

**Reference.** `HAPI_OpenFile` (`0x4bb2e0`) first tries an ordinary `fopen` on the name; a
successful loose open **wins immediately**. Otherwise it searches the mounted archives in
registration order. In each archive the lookup is **case insensitive**, splits components on
**backslashes only**, and scans each directory's entries **backwards**, so a duplicate name in
one directory resolves to the **last** matching entry. A missing entry or a **directory** is
rejected and the search goes on to the next archive; the first file entry accepted wins. There is
no slash conversion, no `..` interpretation and no canonical path form. Direct opens do **not**
test the shadow flag. If the winning archive's stream is closed it is reopened, and a **reopen
failure returns null at once, without trying later archives**.

**RWE.**

- **Loose before archives, archives in mount order: MATCHES.** `CompositeVirtualFileSystem`
  `readFile` (about 13-35) searches the vector in order; the data folder is its first element.
  A directory in an earlier archive is skipped (`findFileInner`, `HpiArchive.cpp` about 182-200,
  returns no file for a directory entry) and the search continues, as the chapter says.
- **Case-insensitive lookup: MATCHES.** `HpiArchive::findFile` (about 202-233) compares
  upper-cased names per component; `DirectoryFileSystem` resolves each path component against the
  directory's real spelling (`findPathCaseInsensitive`, about 17-40), which a case-sensitive
  file system needs and the original did not.
- **Duplicate names inside one directory: DIFFERS, unreachable on retail.** RWE's `find_if`
  returns the **first** match (`HpiArchive.cpp` about 182-200, 213-220); the original returns the
  **last**. **`tools/hpi.py` keeps the last** (it stores each path in a dictionary, so a later
  entry overwrites), which makes the Python and the C++ readers disagree with each other as well
  as with the chapter; `CLAUDE.md` says they must change together, and `BT-NEW-CONFLICTS.md`
  already lists the two readers' other disagreements (the ring, a short chunk). **Census: no
  retail archive holds two entries of one name in a directory** (30 archives, 7889 entries,
  compared without case), so no shipped file depends on it. A mod archive written by a tool that
  appends rather than replaces could.
- **Separators: DIFFERS in convention, and an inconsistency between platforms.** The original
  splits only on a backslash; RWE splits only on `/` (`split(path, '/')`), and neither the
  composite nor the leaf file systems normalise. Every RWE call site passes forward slashes. A
  name that arrives with a backslash from data resolves from **no archive** in RWE, and from a
  **loose file only on Windows** (where `std::filesystem::path` iterates both separators). The
  chapter's "no slash conversion" is the original's own choice; the RWE consequence is that
  loose-versus-archive behaviour for a backslashed name depends on the operating system. No
  retail caller was found passing one (not exhaustively searched: this is the data-driven path
  names, such as a unit's model or a GUI's bitmap reference, resolved by the call sites and not
  by the file system).
- **`..` and absolute names: RWE is stricter.** `findPathCaseInsensitive` searches each
  component among the real directory entries, and a directory listing never yields `..`, so a
  traversing name finds nothing. The original's loose `fopen` has no such guard. This is the right
  side for RWE (`docs/SECURITY-AUDIT.md`); recorded so it is not later "fixed".
- **Reopen failure: not applicable.** `HpiFileSystem` opens its stream once at mount and keeps it
  (`HpiFileSystem.cpp` about 28-36); there is no closed-stream state.
- **Handles, member counts, mounted-archive lifetime: not applicable.** RWE has no
  file-handle object; a read returns a copy of the bytes. The chapter's hazards (closing an
  archive with live members, no synchronisation of the shared stream) have no counterpart.
  The cost is that **the whole member is copied** on every read (`HpiFileSystem.cpp` about
  15-30), capped at 512 MiB (`MaxFileBytes`).

## 3. Reading: partial reads, empty files and the whole-file adapters

**Reference.** Reads clamp to the logical size, but zero-byte reads and positions past the end
are not safe no-ops in the compressed path (an underflowing block count). The whole-file
adapters promote a **positive partial read to success**: `LoadFile` (`0x4bbe50`) rejects a missing
or **nonpositive-sized** file, allocates exactly the logical size, accepts any positive read and
then reports the **declared** length even if the read was short; the result has no terminating
NUL. `LoadFileWithProgress` reads in ten equal chunks and a remainder and ignores the last read's
result. The reader-into-caller-buffer variants are inconsistent about who frees the buffer on
failure. `Length by name` reports zero for both a missing and an empty file.

**RWE.**

- **A short read is a failure: DIFFERS, the right way.** `HpiArchive::extract` throws on a chunk
  that ends early; `HpiFileSystem::readFile` and the composite catch it and carry on to the next
  source (`CompositeVirtualFileSystem.cpp` about 18-31, `LOG_WARN "Ignoring ..."`). The
  original would hand the caller a buffer whose tail is whatever the allocator left, and report
  the full length. That is `BT-UTIL-SQSH-PARITY.md`'s territory (the chunk reader) and is
  recorded there as the strict side being RWE's.
- **An empty file is a present file: DIFFERS, and this is the cause of an already-open item.**
  The original's loader treats a **nonpositive size as no file** (and `Length` conflates the
  two). RWE returns an empty vector from `DirectoryFileSystem::readFile` and
  `HpiFileSystem::readFile` and the caller sees a file with no bytes. `BT-NEW-CONFLICTS.md`
  (Batch 9, M9) records the visible symptom, a zero-length `.ZRB` film handed to the Smacker
  decoder as a present film, which then throws; "the original skips it". The same holds for any
  consumer: an empty `.fbi` parses as a block-less file and is then rejected for a missing
  `UNITINFO`, an empty `.tdf` is zero blocks, an empty `.pcx` or `.gaf` throws. **Census: one
  retail entry has size zero** (`camps/useonly/example.tdf` in `Example.ufo`, an unused template
  in a subdirectory the engine never lists), so nothing shipped is affected. A single decision at
  the file-system layer (an empty file reads as absent, the original's rule) would close the whole
  class rather than the film.
- **No NUL terminator: MATCHES.** RWE's callers build a `std::string` of the exact size.
- **Progress reads: not applicable** (rule 4 of `BT-GAME-DATA-FILES-PARITY.md`).

## 4. The outer obfuscation and the header key

**Reference.** Metadata after the first 20 bytes, compressed block tables, packed chunks and raw
file contents are obfuscated with the low byte of the **absolute archive position**, a runtime key
byte and a bitwise inversion; an all-zero runtime key disables it. The stored header key is
turned into the runtime key on load by rotating left two bits and complementing when the stored
byte is nonzero; the writer's inverse maps a requested key of 255 to a stored zero, which the reader
treats as disabled (that edge value does not round-trip). A compressed member is read by undoing
this layer first and then letting `SQSH` do its own inner transform.

**RWE: MATCHES in effect.** `transformKey` (`hpi_util.cpp` about 41) rotates left two bits;
`decrypt` (about 20-32) does nothing for a zero key and otherwise XORs each byte with the low
byte of its absolute position and the key; the seed is the stream position
(`readAndDecrypt`, about 34-39). The complement the chapter describes is folded into the key
representation (the chapter's "key byte plus bitwise inversion" and RWE's "key" are the same
mapping written two ways). **Every one of the 30 retail archives decodes with RWE's
arithmetic**, which is the check that matters. The writer's 255 edge is not applicable (RWE
writes no archives). The layered order (outer first, inner second) is the order `extractCompressed`
uses.

## 5. Wildcards and directory enumeration

**Reference.** `MatchWildcard` (`0x4bc370`) is case insensitive, with `?` for one character and `*`
for zero or more, a bounded set of **100** active pattern positions and no classes, escapes or
Unicode. `FindFirst`/`FindNext` split the last backslash into a directory and a leaf pattern, turn
`*.*` into `*` for archives, drain the **physical** directory first, then each archive in order,
returning **leaf** names, accepting an archive entry only when its **shadow flag is clear**.
Archive timestamps are zero; archive directories report attribute `0x11`. The "recursive" argument
controls continuation across sources, not descent. `ListDirectory` appends to an existing vector
without clearing or sorting it. `FindFilesRecursive` descends into non-dot directories and
returns full paths, with no guard on its local path buffer or depth.

**RWE: DIFFERS in shape, MATCHES in effect for every shipped call.**

- **No wildcard grammar.** The interface is `(directory, extension)`: a suffix test, upper-cased
  on both sides (`HpiFileSystem::getFileNamesInternal`, about 67-80; `DirectoryFileSystem`, about
  95-118, which compares the `extension()` instead). Every call site passes an extension
  (`.fbi`, `.tdf`, `.cob`, `.gaf`, `.ota`, `.mp3`). The original's `unitname*` form (used by the
  unit checksum's GUI scan, `BT-GAME-DATA-FILES-PARITY.md` rule 8) has no equivalent; RWE reads
  builder GUI pages by number instead (`GameSimulationLoader.cpp` about 301).
- **Merge by union of names, not by first-seen with shadows: DIFFERS in order, MATCHES in
  content.** `CompositeVirtualFileSystem::getFileNames` (about 52-62) puts every source's names
  in a `std::set<std::string>` and returns them **sorted** (ASCII). The original returns names in
  discovery order (physical, then archive 1, then archive 2 ...), each archive's directory in
  stored order, with later duplicates suppressed. For the unit table this does not matter: the
  register recovered the original's unit numbering as the byte-sorted merged file names
  (`TOTALA-EXE-DATA.md` section 109, `BT-UNITS-UNIT-TYPES-PARITY.md` rule 2), which is exactly what
  sorting gives. For maps and campaigns the listing order is a display order the menu then sorts or
  not (`MainMenuScene.cpp` about 2051); not traced.
- **The dedupe is case-sensitive: DIFFERS, harmless on retail.** The shadow suppression in the
  original is by name without case; `std::set` is not. **Census of the listings RWE builds:** the
  `units`, `weapons`, `download`, `scripts`, `camps`, `maps` and `textures` listings return leaf
  names, and no retail leaf differs from another only in case within a listed directory (two
  case-variant leaves exist in all 7889 entries, in `fonts` and `unitpics`, which nothing lists).
  The **recursive** listing of `features` returns relative paths including the **directory's**
  spelling, and the archives disagree about that: `features/Corpses/arm_corpses.tdf` in one
  archive, `features/corpses/arm_corpses.tdf` in another. **Ten of the 177 feature `.tdf` files
  are listed twice** for this reason (`GameSimulationLoader.cpp` about 600-620), each read and
  parsed twice, the second insert ignored (`featureTdfs.insert` keeps the first). It costs a
  little load time and changes no value. A mod with a loose file differing in case from the
  archive's would double the same way.
- **`*.*` and `?`: not applicable.**
- **Loose recursive names use the host separator:** on Windows the loose
  `getFileNamesRecursive` returns `Sub\file.tdf` (`DirectoryFileSystem.cpp` about 150-175, an
  `fs::path` `string()`), the archive reader returns `Sub/file.tdf`; the composite's `std::set`
  then keeps **both spellings** of a loose override of an archived file, so a loose
  `features/Sub/x.tdf` shadowing `features/Sub/x.tdf` is parsed twice. The values agree, since
  both reads return the loose file. Same cost as above.
- **100-position limit, attribute bits, timestamps: not applicable.**
- **`GetDirectoryEntry`'s missing success flag, `CountDirectoryEntries`'s dot skipping: not
  applicable;** RWE's listings never contain `.` or `..`.

## 6. Opening an archive: the gate, the copyright footer and the metadata

**Reference.** `OpenArchive` (`0x4bdd70`) opens in binary-read mode, reads the 20-byte header
(magic `HAPI`, version bytes `00 00 01 00`, the metadata allocation size at offset 8, the stored key
at 12, the root directory offset at 16), **checks the magic and the version, and checks a
trailing copyright string**, normalising its four year characters before comparing with
`Copyright 0000 Cavedog Entertainment`. "This suffix check is a format gate, not a
cryptographic authentication check." It then reads the declared metadata size, derives the key,
decodes everything after the header, and relocates every offset to a pointer; file-info data
offsets stay physical. The sizes, counts and offsets are **not** otherwise checked. A mode-zero
open closes the stream after the metadata.

**RWE.**

- **Magic and version: MATCHES.** `HpiArchive.cpp` about 121-130 (`HpiMagicNumber`,
  `HpiVersionNumber`), with the shipped archives all passing.
- **The copyright footer: MISSING.** RWE reads no trailing bytes. An archive that has the right
  header and a damaged or absent footer is accepted by RWE and, by the chapter, **refused by the
  original**. **Census: all 30 retail archives carry the footer**, with the years 1997, 1998,
  1999 and 2000 in the four year characters (`CorNecro.ufo` is 2000, `corplas.ufo` 1999,
  `totala1.hpi` 1997), which is exactly why the original normalises them. So the check is
  invisible on shipped data and a **one-directional difference**: RWE loads archives the
  original would refuse, such as those produced by a packer that omits the footer. This is
  the same pattern as the loose-file gate of `BT-GAME-CD-CHECK-PARITY.md` rule 6 and is the
  harmless direction; the register has nothing to correct.
- **Bounds on sizes, counts and offsets: RWE is stricter, and that is the point.**
  `HpiArchive.cpp` about 37-103, 136-155: a directory block capped at 64 MiB, a walk budget of one
  entry per record, a depth limit of 128, every name and data offset bounds-checked
  (`docs/SECURITY-AUDIT.md`, Issue #75). The chapter records the original's unchecked reads as
  hazards, not behaviour to match.
- **Relocation in place, destructive and once: not applicable.** RWE builds a tree of values.
- **`AddArchive` duplicate and failed adds, `DropMissingArchives`, `RemoveArchive`:** the mount
  list is a vector built once; `BT-GAME-CD-CHECK-PARITY.md` rule 2. A failed open skips that
  archive with a log line (`CompositeVirtualFileSystem.cpp` about 142-160), which is the
  original's "failed add returns zero" without the silence.

## 7. Shadow resolution

**Reference.** `0x4be320` first clears every shadow flag in every mounted tree, then walks the
combined root: for each visible file it marks the same-named entries in **later** sources. **A
loose file shadows all archives; a file in archive N shadows archive N+1 onward.** Directories
are traversed, not marked. Enumeration suppresses marked entries; **opening ignores the flags and
searches the sources directly** with the same intended priority. Changes to the mount list or the
loose files need an explicit resolution pass for the discovery flags to catch up.

**RWE: MATCHES in effect, with no flag state at all.** The priority is the order of the vector;
enumeration merges names and a read takes the first source holding the file, so discovery and
open can never disagree, which the original's stale flags can. The one difference is the
listing's case sensitivity (rule 5).

## 8. Package generation (not applicable)

**Reference.** `PackDirectory` (`0x4bd160`) builds the metadata, writes the data (raw in 4 KiB
copies, or `SQSH` LZSS or zlib blocks of at most 64 KiB, with the inner transform on), rewrites the
final metadata and appends the footer with the local calendar year. It ignores source-read,
pack and write results, does not fall back to raw when compression expands, writes straight to the
destination with no temporary file, includes mounted content when packing a directory because
it enumerates through the combined view, and can divide by zero in its progress callback for an
empty total.

**RWE: not applicable.** RWE has no archive writer; `tools/hpi.py` is a reader. The hazards are
not RWE's. One piece is worth keeping for a future tool: **the footer carries the calendar year of
the day of packing** (rule 6's census shows five different years), so a packer that writes a fixed
year is also accepted by the original.

## 9. Drive and working-directory helpers (not applicable)

**Reference.** CD-ROM letter scanning, volume serial queries that ignore the success flag,
current-drive and directory wrappers that discard their results, 256-byte directory fields copied
without forcing a terminator, and `MakeDirectoryPath` copying into a 260-byte buffer without a
bound.

**RWE: not applicable.** RWE uses `std::filesystem` and takes its data path from the command line
(`--data-path`) or the configuration; it keeps no saved start directory. The only mutation of
process-wide state in the original that has a gameplay-adjacent effect is the working-directory
change done by archive registration (`BT-GAME-CD-CHECK-PARITY.md`), which RWE does not have.

---

## 10. Gaps to fix

Ranked by what a player or modder could meet. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **An empty file reads as a present file**, where the original's loaders treat a nonpositive size as no file. The cause of the zero-length film fault and the same for every other consumer. | DIFFERS (a class of latent faults) | code: `readFile` returns no file for an empty one, in both leaf systems, with a test in `malformed_input.test.cpp` |
| 2 | **Duplicate names inside one directory**: original last, RWE first, `tools/hpi.py` last. Unreachable on retail data. | DIFFERS, three-way disagreement | decision, then make both readers agree (they must change together) |
| 3 | **No copyright-footer gate**: RWE loads archives the original refuses. The harmless direction. | MISSING | none (document) |
| 4 | **Backslash and forward-slash names** resolve differently from archives and loose files, and differently on Windows and Linux. | DIFFERS (inconsistent) | code: normalise at the composite, or document that callers pass `/` |
| 5 | **Case-sensitive dedupe** in listings: ten feature files are parsed twice on retail data. | DIFFERS (cost only) | code: dedupe on the upper-cased name, keeping the first spelling |
| 6 | **Listing order is sorted**, the original's is discovery order; matches the register for units. | DIFFERS, MATCHES where it counts | none |
| 7 | **No wildcard** beyond a suffix. | MISSING, no shipped caller | none |
| 8 | **`..` and absolute names cannot escape** in RWE. | MATCHES the security rules; recorded so it stays | none |

No source file was changed.
