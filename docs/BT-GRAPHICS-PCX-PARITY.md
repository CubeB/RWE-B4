# PCX decoding and writing, screenshots, shared 3DO geometry helpers and BMP output parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md` and `BT-GRAPHICS-PALETTE-PARITY.md`, it
allocates no section number in the main register. It covers one chapter of the reference,
`graphics-pcx` (21 services from `0x4caa40` to `0x4cbab0`): PCX decoding and writing, the screenshot
file naming, the bitmap writer, the **shared 3DO loading and geometry helpers** (relocation, primitive
sort, mirror, height, bounds) and a display exception adapter. The chapter groups them by address,
not by subject; this audit follows the chapter. This is the finding that implementation work cites
for any of them, per `AGENTS.md` and ADR-0004.

The shading that consumes a model's primitives is `TOTALA-EXE-SHADING.md` and
`BT-GRAPHICS-MODEL-RENDER-PARITY.md`; the palette a PCX's colours land in is
`BT-GRAPHICS-PALETTE-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-pcx.md`). Nothing else
  under that tree was opened: no source, no decompilation, no source links. Rules are restated in
  prose. The spec-room discrepancy register was read, read-only; no row touches this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `src/rwe/io/pcx/pcx.*`, `scene/Screenshot.*`,
  `io/_3do/_3do.*`, `vertex_height.*`, `mesh_util.cpp` (the selection plate) and
  `TextureService.cpp` (the PCX callers) at the working tree of 2026-10-17. Nothing was built, run
  or changed; line numbers are "about".
- **A census of the shipped data** through `tools/hpi.py`'s reader with throw-away scripts kept
  outside the tree: every `.pcx` in every archive of the retail install (783 distinct files) and
  every `.3do` (610 distinct models).

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The PCX decoder
3. Loading a picture or a palette
4. Writing a PCX and the screenshot
5. The shared 3DO helpers
6. The BMP writer and the exception adapter (not applicable)
7. Malformed input
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1, 2 PCX decode and load | none | rules 2, 3 |
| 3, 4 PCX write, screenshot naming | `TOTALA-EXE-KEYBOARD.md` section 77 (Ctrl+F9, `SHOTnnnn.pcx`; "where the original's numbering starts is not decoded") | the chapter bears on the open point; rule 4 |
| 5 3DO load, relocation, sort | `TOTALA-EXE-RENDER.md` near 1235, 1493, 1538 and `-SHADING.md` near 151, 226 (the selection plate swapped into index 0) | agrees on the swap; rule 5 |
| 6 mirror, height, bounds | none | rule 5 |
| 7, 8 BMP writer | `-KEYBOARD.md` section 77 (the developer movie-output keys) | rule 6 |
| 9 exception adapter | none | rule 6 |

## 2. The PCX decoder

**Reference (sections 1, 2).** The decoder accepts a 128-byte header whose **manufacturer byte is 10 and
version byte is 5**; it does **not** check the encoding, bit depth, plane count or bytes-per-line
fields. Width and height are maximum minus minimum plus one. The body is decoded **width pixels per row
into a packed buffer**; the bytes-per-line padding in the header is ignored. A byte with its top two
bits set is a run header (six-bit count, then the repeated value); other bytes are literals; an
overlong run is shortened to the rest of the row. The 768-byte palette is read from **file length
minus 768**; the 0x0c marker before it is not checked. Only the header read is checked: short body or
palette reads use stale bytes and the decode still reports success.

**RWE.** `PcxDecoder` (`pcx.h` about 60-210).

- **Run and literal coding: MATCHES.** Same top-two-bit test, same count, an overlong run clamped to the
  rest of the row (the malformed-input test "a PCX whose last run is longer than the row" pins it),
  a zero count still consumes its value byte.
- **Header acceptance: DIFFERS in both directions, no retail effect.** RWE does **not** test the
  manufacturer or version bytes (any 128 bytes whose window is not inside out is accepted for the
  generic decode), and it **does** require eight bits and one plane for the paletted path
  (`decodePalettedImage`), which the original never checks.
- **Row width: DIFFERS where the file is padded, and RWE is the correct reading.** RWE decodes
  `planes x bytes-per-line` per row and crops to the width, so a file with a padded line (odd width,
  bytes-per-line = width + 1) comes out right; the original reads `width` bytes per row and would
  shear it (the test "a PCX with padded rows is cropped to its width"). **Census: all 782 real PCX
  files have bytes-per-line equal to the width**, so the two agree on every shipped picture.
- **Palette: DIFFERS in strictness.** RWE also reads the palette from the last 769 bytes but
  **requires the 0x0c marker** and throws "invalid palette" without it; the original ignores the
  marker. Census: all 782 real PCX files have it.
- **Short data: DIFFERS in RWE's favour.** A body that ends mid-row throws ("reached end of input
  before row could be finished") where the original uses stale bytes and reports success.
- **One shipped file is not a PCX.** `palettes/guipal.pcx` in `totala1.hpi` is a **1,024-byte raw
  four-byte-per-colour palette** (it begins `00 00 00 00 00 00 aa 00`): manufacturer 0, no window, no
  marker. It is the same data as `palettes/guipal.pal` under a misleading name. RWE reads the `.PAL`
  (`GUIPAL.PAL`, `ai_arena.cpp` about 504) and never asks for the `.pcx`; the original's decoder
  would refuse it on the header test.

## 3. Loading a picture or a palette

**Reference (section 2).** The surface loader validates the header, allocates a surface, decodes, and
expands the 256 colours into four-byte entries. If its second decode fails it still expands the
palette and returns the old pointer (a dangling image). The palette-only loader decodes the **whole
image** to get the colours, ignores the decode result, and always reports success.

**RWE.** `TextureService::getUnitPic` and the bitmap loader (about 165-310) decode the picture and the
palette separately; `decodePalette` reads only the trailer.

- **Dangling-pointer and always-success branches: not applicable.** RWE has no such path; a failure
  throws `PcxException`.
- **Palette-only decode work: not applicable** (RWE does not decode the body to read the colours).
- **Who catches `PcxException`: UNCLEAR, probably nobody.** `getUnitPic` (called for every buildable
  unit when a builder's menu is built, `ui/UiFactory.cpp` about 1023) has no `try`; nothing between
  it and `main` catches, so a malformed unit picture in a mod ends the process with the "Critical
  Error" box instead of leaving that button blank. See rule 7.

## 4. Writing a PCX and the screenshot

**Reference (sections 3, 4).** `WritePcx` opens in **append** mode, writes a version-5 run-length header
with one plane, eight bits, bytes-per-line equal to the width, the first 48 palette bytes in the header,
and the full palette after a 0x0c marker. Rows are exactly `width` bytes; runs are at most 63 long and
never cross a row; a single high-valued byte (top two bits set) is written as a run of one. The
screenshot routine requires display override mode, enumerates `prefix*.pcx`, parses what follows the
prefix with `atoi`, takes the **highest positive** number "starting from zero", and writes the next
four-digit-minimum name; it does not fill holes or reserve the name; the directory test for a trailing
separator looks for a backslash only; the name is built into a 260-byte buffer without a bound.

**RWE.** `encodePcx24` (`pcx.cpp`) and `saveScreenshot` (`scene/Screenshot.cpp`).

- **File format: DIVERGENCE (documented).** RWE writes a **24-bit, three-plane** PCX (`TOTALA-EXE.md`
  section 88, `TOTALA-EXE-KEYBOARD.md` section 77) because it renders in true colour. The run
  coding matches: runs of at most 63, a literal byte with its top two bits set takes a run header,
  no run crosses a row or a plane. RWE rounds bytes-per-line up to an even number (the PCX
  convention); the original writes the width as is, which the reader tolerates.
- **Overwriting versus appending: DIFFERS in RWE's favour.** RWE truncates the destination; the original
  appends to whatever is at the path. The next-number rule makes a collision unlikely either way.
- **Name format: MATCHES the register.** `SHOT` + at least four digits + `.pcx`, taken from the highest
  existing number plus one.
- **First number: UNCLEAR, now with a lead.** The register says the original's start is "not
  decoded" and RWE's first is `SHOT0000` (`nextScreenshotNumber` returns 0 when none exists). The
  chapter's wording, **the highest positive number, starting from zero**, means a file numbered 0
  does not count and an empty directory has "highest" 0. If the next name is highest plus one the
  first screenshot is **`SHOT0001`**; if the next name is the highest itself it is `SHOT0000` and
  every later shot overwrites the last. The chapter does not say which, but the second reading
  would make the feature useless, so one is the likelier first number. A spec-room read of the
  format call settles it (CONFLICT with the register's open point, `BT-NEW-CONFLICTS.md` batch 9).
- **Which names count: DIFFERS.** RWE accepts only twelve-character names of the exact form
  `SHOT` + four digits + `.pcx` (case-folded); the original parses the number after the prefix with
  `atoi` whatever follows. Past `SHOT9999` RWE writes `SHOT10000.pcx`, which its own scan then
  ignores (13 characters), so every later shot writes the same name. Irrelevant below ten thousand
  shots.
- **Source of the pixels and the cursor: DIVERGENCE (documented).** RWE reads the back buffer before
  the cursor is drawn (`-KEYBOARD.md` section 77); the original uses the 8-bit override image.
- **Directory and enumeration failures: MATCHES in effect.** RWE creates the directory (the original
  does not); a failure returns no path.

## 5. The shared 3DO helpers

**Reference (sections 5, 6).** `Load3do` loads the whole file and `RelocateObject` converts every
offset (vertex array, primitive array, each primitive's index list, siblings, children) to a pointer in
place, **with no null-offset test, no bounds, no cycle check, no version check**; it then sorts each
object's primitives. `SortPrimitives` moves the **designated special face to entry 0**, sets the
designation to 0, and then bubble-sorts entries **1 to n-1 by ascending integer average vertex Y**
(entry 0 untouched; a primitive count of zero divides by zero when an average is asked for).
`MirrorObject` negates every vertex X and Z and every node's X and Z origin, recursively. `GetObjectHeight`
starts at 0 and takes the maximum over each sibling's vertex Y plus the node's Y offset and each child's
recursive height plus the current node's Y, so it is never below 0 and an empty child with a positive
offset still counts. `GetObjectBounds` starts both corners at 0 and expands only with vertices of a
node with **more than two vertices**.

**RWE.** `parse3doObjects` (`_3do.cpp`) builds a structural object tree (no relocation); `findHighestVertex`
(`vertex_height.cpp`) gives the model height; `selectionMeshFrom3do` (`mesh_util.cpp` about 358) takes the
plate.

- **Selection plate in entry 0: DIVERGENCE (documented).** RWE does not reorder; it keeps the file
  order and skips `prims[selectionPrimitiveIndex]` when drawing (`TOTALA-EXE-RENDER.md` near 1493 and
  1538 says this is the same outcome as the swap). The plate is read by its index. MATCHES in effect.
- **Sort by average height: not applicable.** The original sorts for its painter-ordered software
  rasteriser; RWE uses a depth buffer. `TOTALA-EXE-SHADING.md` part two covers where the original's
  ordering matters for the pixel result.
- **Height: MATCHES on all 610 retail models.** Computing the chapter's rule (zero floor; vertex Y
  plus the node's offset; child height plus the parent's offset, in the 3DO's native units) and RWE's
  rule (highest vertex of the piece and its children, plus the piece's offset) over every distinct
  `.3do` in the retail archives gives **the same number for all 610** (both readings of whether an
  empty subtree contributes its parent's offset). The two differ only for a model whose every
  vertex is below its origin: the original returns 0, RWE a negative number. No shipped model does
  that; a mod could, and `modelHeightOf` feeds the eye height, the sea-level tests and the
  water-line checks (`GameSimulation_visibility.cpp` about 247, 358, 383, 522). Root siblings: none
  in the retail set (the chapter's routine also walks them; RWE asserts one root).
- **Bounds with the more-than-two-vertices gate: MISSING.** RWE has no bounds routine; the register
  names only one consumer, the target bracket drawn for a Shift-held order (`0x438C00`,
  `TOTALA-EXE-RENDER.md` near 520), which RWE does not draw. Not re-raised.
- **Mirror: UNCLEAR.** The chapter negates X and Z of shared geometry in place and does not say
  who calls it. RWE has no mirror step; whether its model-to-world mapping already includes the
  equivalent is a question for the model-render audit's coordinates (`vertexToVector`).
- **Magic number: DIFFERS in strictness.** RWE requires the object magic to be 1 on every node
  (`_3do.cpp` about 62); the original checks nothing. All 610 models pass. A mod model with another
  value is refused by RWE.
- **Faults in the original's loader** (null offsets, no bounds, cycles, a zero-primitive divide, a sort
  that mis-orders when the special index is -1): **not applicable**; RWE's loader is the hardened one
  (rule 7).

## 6. The BMP writer and the exception adapter (not applicable)

**Reference (sections 7, 8, 9).** A 256-colour BMP writer (header, palette in BGR, rows bottom-up with
the width rounded to four, size fields left zero, partial files kept on failure) and a display-cleanup
filter around exception reporting.

**RWE: not applicable.** The BMP writer's callers are the developer movie-capture keys, gated by the
registry value `Games` (`-KEYBOARD.md` section 77), which RWE does not port. The exception adapter's
role belongs to `util/CrashHandler.*` and `tools/crash-catch.cmd`. The chapter's writer faults
(a row stride read from the source, no padding generated) are irrelevant to a writer RWE does not have.

## 7. Malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") applies to every picture and
model a mod supplies. `malformed_input.test.cpp` covers PCX (three cases: an overlong last run, a file
that cannot describe itself, padded rows) and 3DO (three: a self-referencing tree, a face naming a
missing vertex, piece-name loops).

- **PCX decoder: no new fault found.** Dimensions are capped (8,192 x 8,192 of decoded bytes), the
  header and palette lengths are checked, every run is clamped, and a short body throws. The palette
  index assert in `getUnitPic` cannot fail (the palette always has 256 entries).
- **3DO parser: no new fault found.** Depth 64, 4,096 objects, per-object vertex and primitive caps,
  vertex indices checked, each read checked against the stream. A negative selection index other
  than -1 becomes a huge unsigned index that matches no primitive.
- **Exceptions are not caught at the call sites.** `PcxException` (and the 3DO `runtime_error`)
  propagate through `getUnitPic`, `getBitmapInternal` and the unit loader to `main`; one bad picture or model
  in a mod aborts the launch. The same pattern as `BT-GRAPHICS-GAF-PARITY.md` rule 8.
- **Screenshot path.** `saveScreenshot` creates only the screenshots directory under the local data
  path and writes a fixed name pattern; no external path reaches it.

## 8. Gaps to fix

Ranked by what a player or a modder would hit.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A malformed `.pcx` or `.3do` in a mod aborts the launch** (`getUnitPic` and the model loader do not catch). | fault | code: catch per asset, log, use the placeholder; add loader-level cases |
| 2 | **First screenshot number** (`SHOT0000` in RWE; the chapter's "highest positive from zero" suggests `SHOT0001`). | UNCLEAR, CONFLICT with the register's open point | spec-room read of the number formatting call |
| 3 | **Model height below zero for an all-negative mod model**; the original floors at 0 and RWE does not. | DIFFERS (mods only) | code: floor `findHighestVertex` at 0 where the height is used |
| 4 | **Palette marker required** and **header bytes unchecked**: RWE is stricter on the marker and looser on the signature than the original. | DIFFERS (no retail data) | none |
| 5 | **`SHOT10000` wraps onto itself**; the 12-character name test. | DIFFERS (edge) | code: match any digit count |
| 6 | **Mirror (X and Z negation) and the bounds routine's consumer** have no RWE counterpart named. | UNCLEAR | spec-room read of the callers |
| 7 | 24-bit output, painter-order sort, back-buffer capture without cursor. | DIVERGENCE (documented) | none |

No source file was changed.

## 2026-10-22 clarification for issue #646

See [Model-height floors](BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS.md#height-a-floor-at-every-recursively-measured-list) for the direct static byte-tactics check. It supersedes the final-only floor recommendation above and clarifies empty-leaf versus empty-child-list contributions. The retail census above was not rerun.
