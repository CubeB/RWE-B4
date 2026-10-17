# GAF frames, sequence timing, layered draw trees and the sprite draw primitives parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GRAPHICS-PALETTE-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `graphics-gaf` (the 38 services between `0x4b7e60` and `0x4ba1b0`):
how a GAF file becomes frames and sequences, how a sequence is timed, how a layered frame is
drawn, and the family of sprite draw primitives and the row compressor. This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The palette tables the blend, light and gray primitives consume are in
`BT-GRAPHICS-PALETTE-PARITY.md`; the model renderer that calls the depth, tint and cut helpers is
`BT-GRAPHICS-MODEL-RENDER-PARITY.md`; particle classes that animate an FX sequence are
`BT-GRAPHICS-PARTICLES-PARITY.md`. They are only referred to here.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-gaf.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was
  read, read-only; no row touches this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `src/rwe/io/gaf/` (`GafArchive.cpp`, `gaf_util.cpp`,
  `gaf_headers.h`), `TextureService.cpp` (the GAF adapters), `atlas_util.cpp`, `render/FogTiles.cpp`,
  `CursorService.cpp`, `game/Particle.*` and `src/rwe/io/malformed_input.test.cpp` at the working
  tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in
  the tree, so every line number is "about".
- **A census of the shipped GAFs** (every `.gaf` in every archive of the retail install, 958 files
  by name, read through `tools/hpi.py`'s reader with a throw-away script kept outside the tree):
  11,900 entries, 48,617 frames, 5,127,833 compressed rows with a non-zero length and 550,788 of
  length zero, 14,540 distinct layered frames decoded to pixels and compared with RWE's layer
  crop. Numbers below are from it.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The container: header, version and the fields RWE does not name
3. Frames, hotspots and compressed rows
4. Layered frames
5. Sequence references and frame durations
6. The draw primitives
7. The row compressor
8. Malformed input
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1 frame header, load, find | none for the file layout; `TOTALA-EXE-RENDER.md` near 48 and 354 name GAF entries and loading sites | rules 2, 3 |
| 2 sequence timing | `TOTALA-EXE-RENDER.md` near 510 and 542 (a cursor marker steps at `tick / (2 x frame0 duration)`) | the duration field is the same one; rule 5 |
| 4 to 8 draw primitives | `-RENDER.md` section 100 (shadow passes), `-SHADING.md` (lit units), `BT-GRAPHICS-PALETTE-PARITY.md` (alpha and light tables) | rule 6 |
| 9 row compressor | none | rule 7 |
| tick phase "global GAF sequences" | `BYTE-TACTICS-DISCREPANCIES.md` BT-1 (resolved: the sequences are stepped inside the tick) | rule 5 |

## 2. The container: header, version and the fields RWE does not name

**Reference (section 1).** There is no signature, length, count or offset check before the load
fixups; the file is loaded whole and every offset is turned into a pointer in place. The entry count
is a signed short for iteration. Name search is case-insensitive and returns the first match.

**RWE.** `GafArchive` (`GafArchive.cpp` about 35-63) reads a 12-byte header, **requires the version
word to be exactly `0x00010100`**, reads each entry by seeking to its offset, and finds entries by
upper-cased name, first match (about 65-75).

- **Name search: MATCHES.** Case-insensitive, first match wins, null when absent.
- **Version word: DIFFERS, and shipped files are refused.** The census found **953 GAFs with
  `0x00010100` and five with version 0**: `anims/vismasks.gaf` in `totala1.hpi`, `btdata.ccx`,
  `ccdata.ccx` and `rev31.gp3`, and `anims/terrain.gaf` (seven entries of old tree and rock art) in
  `totala1.hpi`. The original never looks at the word, so it loads them. RWE throws
  "Invalid GAF version number" for all five. `vismasks.gaf` is the table of ten hand-drawn sight
  discs `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 5 compares against RWE's exact disc, and no RWE code
  reads it today (`PlayerVisibility.h` says so); **the day circular sight is ported from the mask,
  the reader will refuse the file it needs.** The version check should accept 0 (or be dropped).
- **Header and entry fields RWE leaves unnamed: no effect on retail.** Census: header `unknown1` is
  0 in all 958; every one of the 11,900 entries has `unknown1 = 1` and `unknown2 = 0`; every frame
  record has `unknown2 = 0` (by offset in the 24-byte record this is where the chapter's
  blend-kind byte and reserved word sit once the file is loaded in place; that placement is an
  inference from the layout, not something the chapter states); the frame's last word (`unknown3`, the
  second plane's pointer once loaded) is non-zero in 13,227 of 48,617 frames and is never relocated,
  so it only matters to the depth helpers. **If `unknown1` of an entry is the sequence's loop byte**
  (the chapter says the sequence reference copies a loop byte from its sequence), **every shipped
  sequence loops**, and the one-shot behaviour of an explosion comes from the caller acting on the
  step routine's return value, not from the file. That is an inference from the chapter plus the
  census, not a decoded fact.
- **Blend kind: MISSING, but no data uses it.** All 74,529 layer headers and 48,617 frame headers
  carry 0 in the field. A mod that sets it gets ordinary layering in RWE and the blended draw in
  the original.

## 3. Frames, hotspots and compressed rows

**Reference (sections 1, 3, 9).** A frame is a 24-byte header (width, height, signed hotspot offsets,
transparent colour index, compression byte, layer count, then two pointers) followed by pixels. A
leaf is drawn from "requested position minus hotspot" with inclusive right and bottom edges. A
compressed leaf is a row-length prefix (unsigned short) per row; a row of length zero is entirely
the transparent colour. A command byte's low bit set is a skip of up to 127 pixels with no value
byte; low bits `10` are a repeat of up to 64 with one value byte; low bits `00` are a literal run of
up to 64.

**RWE.** `decompressRow` and `decompressFrame` (`gaf_util.cpp` about 10-80), `BufferGafAdapter`
(`TextureService.cpp` about 11-75).

- **Header layout: MATCHES.** The 24-byte record (`gaf_headers.h`) has the chapter's fields in the
  chapter's order; sprite bounds are built from the negated hotspot and the full size, so the frame
  is placed at "position minus hotspot".
- **Command encoding: MATCHES.** The three forms, their counts and the zero-length row
  (filled transparent) are the chapter's. A literal or repeat pixel equal to the key is also
  treated as transparent (the adapter tests every pixel against the key); the original's encoder
  never emits one, so no shipped row differs.
- **Retail rows: MATCHES.** All 5,127,833 non-empty compressed rows decode **exactly** (the commands
  consume the row's width and its prefix together); none is short, overlong or has bytes left over.
- **A short row: DIFFERS (latent).** When a row's commands end before the row is full, RWE fills the
  missing pixels **starting at the row's first pixel** instead of at the first unwritten one
  (`gaf_util.cpp` about 71), overwriting good pixels on the left with the transparent colour. A
  row that is entirely transparent (prefix zero) is unaffected, which is why this has never shown.
  The original's decoder leaves the unwritten tail alone (its destination keeps what was behind it,
  that is, it is transparent in effect). A GAF made by a tool that drops trailing transparency would
  lose its left edge in RWE. Census: zero such rows in the retail set.
- **Bytes left after the row is full: DIFFERS (latent).** If the commands reach the row width with
  prefix bytes still unread, RWE stops decoding the row and the next row's length prefix is read
  from the middle of the old one; every later row is garbage. A decoder that steps by the prefix
  would not. Census: none in the retail set.
- **Uncompressed leaf: MATCHES.** Raw width x height bytes.

## 4. Layered frames

**Reference (sections 1, 4).** A frame with a non-zero layer count is a draw tree: plane0 is an
array of child-frame pointers, children are drawn in stored order, each at its own hotspot, and a
child with a non-zero blend kind is drawn blended. The load fixups relocate one level of children;
nesting is not supported.

**RWE.** `extractGafEntry` (about 108-180 of `gaf_util.cpp`) reads each child as a leaf and the
adapter composites them in order into one texture the size of the parent frame.

- **Composition order and offsets: MATCHES.** Later layers overwrite earlier ones at the offset
  parent hotspot minus child hotspot; the census shows **23,906 layered frames** (49 percent of all
  frames; 14,540 distinct ones decoded), two to twelve layers each, never nested. Nested layers
  would be read by RWE as raw leaves; the chapter's loader would not relocate them either.
- **Crop to the parent rectangle: DIFFERS for mod data only.** The adapter discards a layer's
  pixels that fall outside the parent frame's width and height (a comment names one third-party
  file). The original draws each child unclipped from its own hotspot. **No retail frame is
  affected**: of 14,540 decoded layered frames, none has an opaque pixel outside its parent
  rectangle. The crop is a deliberate choice and is only visible on a mod whose layers stick out.
- **Layer headers are read as full frame headers: MATCHES.**

## 5. Sequence references and frame durations

**Reference (section 2).** A sequence is a frame count and, per frame, a pointer and a duration. A
live reference holds an index, a countdown, a loop flag and the sequence. The step routine
decrements, advances at the end of a duration and either wraps or clears the pointer; the other
routine accumulates leftover time over several steps. Neither schedules anything; the tick does
(BT-1: global GAF sequences are stepped inside the tick, after the players and features).

**Register.** `TOTALA-EXE-RENDER.md` near 510-542 uses the sequence's first duration, doubled, as
the rate of an in-world order marker (`tick / (2 x duration)`, then modulo the frame count).

**RWE: MISSING.**

- **Durations are discarded.** `GafFrameEntry.unknown1` is the per-frame duration in the file; the
  reader hands it to the adapter, and `SpriteSeries` (`render/SpriteSeries.h`) is a bare list of
  sprites. Every animation therefore uses a rate chosen in code. The cursor uses a flat six frames a
  second of wall time (`CursorService.cpp` about 44), smoke passes constants such as 2 or 15 ticks
  (`GameScene_commands.cpp` about 1235, 1974), and particles default to 4 (`Particle.h`).
- **What the files say.** The duration is a constant inside each entry. Cursors: `cursormove` 8,
  `cursorattack` 5, `cursorpatrol` 9, `cursordefend` 4, `cursorload` 3, `cursorrepair` 8,
  `cursorreclamate` 3, `cursorselect` 6, `cursornormal` 10, `cursorhourglass` 5, `pathicon` 3.
  `anims/fx.gaf`: `smoke 1` 5, `smoke 2` 3, `steam` 5, `flamestream` 5, `fire1` 10, `Explosion` 2,
  `Explode2` and `Explode3` 2, `Explode4` and `Explode5` 3, `h2oboom1` 2, `H2oBoom2` 3, `H2o` 3,
  `Nuke1` 2, `lavasplash` and `lavasplashlg` 3, `lavasplashsm` 2, `PlasmaSm` 1, `PlasmaMd` 2,
  `blueshot` 2, `redshot` 1, `parablast` 1, `ultrashell` 2, `cannonshell`, `alfboom1`, `alfboom2`,
  `radlogo` and `nuclogo` 10. Across the whole census durations are 1 to 10, with 2 (22,812 frames)
  and 10 (20,220 frames) the common ones. **The unit of a duration is not stated by the chapter**;
  the register's marker rule suggests the clock runs at twice the duration. Comparing each
  hard-coded rate with these is the follow-up; it belongs with the particle audit, which compares
  the emitters and not the files.
- **Cursor animation speed: UNCLEAR.** Six frames a second for every cursor is not a function of
  the file (durations 3 to 10 above). Which clock the original steps its cursor on is not in this
  chapter.
- **Loop flag, countdown wrap, signed-short leftover time: not applicable** until durations are read.
  The chapter's wrap hazards (a negative step, a looping sequence with all-zero durations) cannot
  occur in RWE because nothing steps a sequence.

## 6. The draw primitives

**Reference (sections 4 to 8).** Besides the plain keyed draw: an opaque draw; a draw blended through
the 256 x 256 alpha table; a lit draw and a shadow draw through the light table (the lit draw's
uncompressed path calls the two-input blend with the palette row where the key would be); a gray
draw that remaps the *destination* through a gray table; a checkerboard eraser; nearest-neighbour
scaled draws; a 2x2 downsample through three alpha lookups; depth-composited draws used for an
unfinished product inside its factory; a blue tint and a cutoff by depth; a cut-out; a lens
(radial displacement of a captured background).

**RWE.**

- **Keyed draw, blended draw, lit and shadow draws: covered elsewhere.** RWE bakes palette indices to
  colour at load (`BufferGafAdapter`) and does blending, light and shadow in shaders; whether those
  agree with the tables is `BT-GRAPHICS-PALETTE-PARITY.md` and `BT-GRAPHICS-MODEL-RENDER-PARITY.md`.
  This is a **DIVERGENCE (documented)** in kind (true-colour pipeline, `TOTALA-EXE.md` section 88)
  and is not re-opened.
- **Gray remap of the destination, checkerboard erase, lens, supersample, scaled-blended draw:
  UNCLEAR, no RWE counterpart found.** The chapter names no callers. RWE greys a disabled button by
  drawing the extra frame the artists put in its GAF (`ROADMAP.md`), which is not the remap. A
  spec-room read of the callers of these services would say which screens use them and whether
  the missing effect is visible.
- **Depth, tint and cut helpers: see `BT-GRAPHICS-MODEL-RENDER-PARITY.md`.**
- **Null-frame and lock-failure faults in the chapter** (a null test after the frame is read; a failed
  screen lock leaving a null destination in use): **not applicable**, RWE draws through the GPU.

## 7. The row compressor

**Reference (section 9).** Row-by-row encoder with a per-row length prefix, key skips of at most 127
without a value byte, repeats and literals of at most 64, an all-key row encoded as length zero, two
shared globals (not re-entrant).

**RWE: not applicable.** RWE only reads GAFs. The decoder in rule 3 accepts everything the encoder
can write. The chapter's re-entrancy hazard has no counterpart.

## 8. Malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") and `SECURITY-AUDIT.md`
apply: GAFs come from mods. `malformed_input.test.cpp` has **one** GAF case (a header claiming four
billion entries). What is bounded: entry count (65,536), per-frame width and height (8,192 each),
every command's write against the row width, the layer array by its own count. What is not:

- **A truncated file reads uninitialised bytes.** `readRaw` (`io/io_util.h`) checks a short read
  with `assert` alone and returns an uninitialised value; release builds drop the assert, and
  neither `GafArchive` nor `extractGafEntry` looks at the stream afterwards. A GAF cut inside its
  entry table gets a garbage name, a garbage frame count (up to 65,535 per entry) and garbage frame
  offsets; a cut inside pixel data gets a zeroed buffer (the allocation is value-initialised), so the
  result is a blank or garbage sprite rather than a fault, but the garbage is a function of the
  stack. This is the same helper the FNT and HPI readers use (`BT-GRAPHICS-FONT-PARITY.md` rule 6,
  `BT-UTIL-SQSH-PARITY.md` rule 8).
- **Entry-table amplification.** The 65,536-entry cap is per file, and the entry pointers may all
  name the same entry. An entry may claim 65,535 frames, each frame record eight bytes, and each
  copy is reserved and kept (`GafArchive.cpp` about 14-28). A file of about 790 KB (the header, a
  262 KB pointer table, one 512 KB entry) therefore builds **about 34 GB** of frame tables and
  makes about 4.3 billion stream reads. A realistic cap is a total frame count across the file.
- **Frame sizes.** 8,192 x 8,192 is far past TA's largest frame (the census maximum is 600 pixels);
  each frame is expanded to a 4-byte-per-pixel buffer and uploaded as its own texture, and there is
  no total across an entry. A few hundred such frames in one entry are tens of gigabytes of
  textures.
- **Exceptions escape the loader.** `GafException` is caught only by `loadFogTileSet`. Everywhere else
  (`TextureService::getGafEntryInternal`, the unit and feature loaders, the logo atlas) it propagates
  to `main`'s "Critical Error" box: **a bad GAF in a mod costs the whole process, not that sprite.**
  Missing entries already degrade (the series defaults to a magenta square); a malformed file does
  not.

## 9. Gaps to fix

Ranked by what a player or a modder would hit. None touches the simulation (art is presentation).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **`vismasks.gaf` and `terrain.gaf` have version 0 and RWE's reader refuses them.** Blocks the circular-sight mask port. | DIFFERS (code) | code: accept version 0 (the original checks nothing) |
| 2 | **Truncated GAF reads uninitialised values** (`readRaw` asserts only) and **the entry table can multiply to tens of GB**; the loader's exceptions kill the process. | fault | code: check the stream, cap total frames, catch per asset; add cases to `malformed_input.test.cpp` |
| 3 | **Per-frame durations are discarded**; every animation and the cursor use code constants (cursor: 6 frames a second flat; files: 1 to 10 per frame). | MISSING | decision: keep `SpriteSeries` and read durations into it, then compare each constant with its entry |
| 4 | **A short compressed row is filled from its first pixel**, and bytes left after a full row desync the next. Retail unaffected. | DIFFERS (latent) | code: fill from the first unwritten pixel; skip to the row's end by the prefix |
| 5 | Layer crop to the parent rectangle (mods only), blend kind ignored (no data). | DIVERGENCE (deliberate) | none unless a mod needs it |
| 6 | Gray remap, dithered erase, lens, scaled-blended draws have no RWE counterpart. | UNCLEAR | spec-room read of the callers |
| 7 | Whether entry `unknown1` is the loop flag (all shipped entries = 1). | UNCLEAR | spec-room read of the sequence reference setup |

No source file was changed.
