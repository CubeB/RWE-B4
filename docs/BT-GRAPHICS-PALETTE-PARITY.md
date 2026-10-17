# Indexed palettes, color transforms, their cache files and the filename helpers parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers two
chapters of the reference together because they are one pipeline: `graphics-palette` (32 entries,
`0x4ba200` to `0x4bb150`: the display's palette and five transform tables, the colour matchers, the
generators for the alpha, shade, light, gray and blue tables, and five filename helpers) and
`graphics-palette-tables` (five entries, `0x42e140` to `0x42e300`: how the three file-backed tables
are loaded or generated and cached). This is the finding that implementation work cites for any of
them, per `AGENTS.md` and ADR-0004.

The chapters give **recipes** for five lookup tables. That makes this audit different from the
others: the recipes can be **replayed against the shipped files** and the result is a measurement,
not a reading. That was done (below). All differences are **presentation only**; no table reaches
the simulation.

## Source and evidence class

- **Source.** Two prose chapters of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\`, files `graphics-palette.md`
  and `graphics-palette-tables.md`). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-16 (cut versus tint
  below the sea) touches rule 7 and is **not re-raised**.
- **Evidence class.** A **static reading** by that project, **plus a replay**: the chapters' generator
  recipes were written out in a throw-away script (kept in the session scratch folder, not in the
  tree) and run over the shipped `palettes/PALETTE.PAL` of `totala1` (extracted copy), then compared
  byte for byte with the shipped `PALETTE.SHD`, `PALETTE.LHT` and `PALETTE.ALP`. The replay is
  evidence about the recipes, not about the executable; it does not make the chapter's claims about
  *which consumer uses which table* any stronger than static.
- **No executed observation of the original was used.** No original-game play.
- **The RWE half** is a read-only audit of `src/rwe/AlphaTable.*`, `ShadeTable.*`, `atlas_util.cpp`
  (about 360-430), `ui/UiListBox.cpp` (about 40-60), `shaders/unitTexture.frag`, `unitBuild.frag`,
  `mapTerrain.frag`, `basicTexture.frag` and `worldPost.frag`, `MainMenuScene.cpp` (the gamma slider)
  at the working tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted
  edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Display-owned state and buffers (not applicable)
3. Installing the palette and the brightness setting
4. Colour matching and brightness ordering
5. The alpha table
6. The shade and light tables
7. The gray and blue tables
8. Loading, generating and caching the three file tables
9. The filename helpers (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| palette 1 five buffers and flags | `-RENDER.md` section 54 (the files, their sizes and slots `+0xC0` to `+0xD0`) | agrees |
| palette 2 install, brightness | `-INTERFACE.md` section 65/67 (the GAMMA slider, snapshot and restore); the formula itself is only in the `worldPost.frag` comment | agrees on the formula (rule 3) |
| palette 3 matchers | `-SHADING.md` section 09 and `tools/exe/shading/shdgen.py` (the band matcher, byte for byte) | agrees; two other matchers exist (rule 4) |
| palette 4 alpha | `-VISION.md` "The table" (the same recipe, `0x4BA772`) | RWE does not use it (rule 5) |
| palette 5 shade, light, gray, blue | `-SHADING.md` section 09 (shade); light, gray, blue not recipes in the register | rules 6, 7 |
| palette 6 filename helpers | none | rule 9 |
| tables 1-5 loading and cache | `-RENDER.md` section 54 (the files); `atlas_util.cpp` | rule 8 |

## 2. Display-owned state and buffers (not applicable)

**Reference (palette 1).** The display context carries a 256-entry RGB0 palette, a brightness float
and five transform buffers under five availability flags: **alpha** (65536 bytes, 256 by 256),
**shade** (8192, 32 rows of 256), **light** (8192, 32 by 256), **gray** (256) and **blue** (256). The
allocators return 1 with no null check and do not set the flags; the frees do not null pointers or
clear flags; the setters copy the exact byte count only when the flag is on, never retain the caller's
pointer and never validate its size.

**RWE: not applicable.** The tables become textures in `atlas_util.cpp` (`shadeTableTexture`,
`alphaTableTexture`); there is no flag-gated buffer set, no DirectDraw or GDI palette, no handle to
leak. The sizes are enforced on load (rule 8), which the original never does.

## 3. Installing the palette and the brightness setting

**Reference (palette 2).** `SetPaletteColors` multiplies each requested channel by the float at display
`+0x614`, truncates, caps at 255, zeroes the fourth byte; no lower clamp and no range check. Two quirks:
the stored-palette copy advances from source entry zero while the brightness loop reads source entry
`i` from `start`, and a DIB install builds all 256 entries from a temporary of which only the
requested range was set. `GetPaletteColors` writes into destination indices `first` to
`first + count - 1` rather than packing from zero, and returns 1 even when nothing was filled. The
brightness argument is a 32-bit float bit pattern stored through an `int`.

**Register.** `-INTERFACE.md` lists the VISUALRT GAMMA slider and the settings snapshot (value and
ramp) but not the arithmetic. The arithmetic is written only in the `worldPost.frag` comment: the original's
gamma "is not a gamma curve at all: it rewrites the palette as `min(255, c x m)` with `m = 0.5 + v/24` over a
slider of 20 steps", 0.5x to 1.333x, 1.0 untouched. The chapter's multiply-truncate-cap agrees with it, which
is the first independent confirmation of that comment.

**RWE: MATCHES the formula, DIFFERS in two small ways, presentation only.**

- **The multiply and cap: MATCHES.** `worldPost.frag` computes `clamp(color x gamma, 0, 1)` with
  `gamma = percent / 100`, 50 to 133 (`GlobalConfig.h` about 252; the menu slider maps
  `50 + v x 83`, `MainMenuScene.cpp` about 418-424).
- **Steps: DIFFERS (trivially).** The original has **21 values** (`m = 0.5 + v/24`, `v` 0 to 20);
  RWE's slider is continuous. Settings saved by either read into the other's range.
- **Scope: DIFFERS, probably.** The original rewrites the **hardware palette**, so the whole screen
  (interface panels, text, the minimap, menus) brightens with the setting. RWE applies the
  multiplier in the world view's post pass only (`GameScene_render.cpp` about 2069); the chrome pass is
  drawn without it. The shader comment itself says "the one place the whole *world view* passes
  through". Whether the HUD brightens in RWE when the slider moves was not checked by eye.
- **The palette API faults** (partial-update indexing, uninitialised temporaries, the `int`/`float`
  alias) have no counterpart.

## 4. Colour matching and brightness ordering

**Reference (palette 3).** `NearestColor` (`0x4ba880`) is an exhaustive scan by **unweighted squared RGB
distance**, ties to the earliest index. `SortByBrightness` (`0x4ba920`) takes each entry's R+G+B sum,
builds an index array and sorts ascending carrying the indices (an exchange sort, so the order among equal
sums is the sort's, not stable by index). `NearestColorInBand` (`0x4ba9d0`) examines only entries whose sum
is within **40** of the request's sum, first strict minimum wins; with **no candidate examined** it uses
the scan-end index (not the exhaustive search), a break above the band can pick that first out-of-band
entry, and reaching 256 wraps to zero in a byte. **All five generators use the band-limited matcher.**

**Register.** `-SHADING.md` section 09 and `shdgen.py` reproduce the exchange sort and the +-40 window
and regenerate `PALETTE.SHD` byte for byte. `-RENDER.md` section 50 documents a **different** matcher
for the interface colour table (`0x4AC7D0`, sum of absolute differences, first index on ties,
scanning all 256 entries), which is neither of the chapter's two.

**RWE: MATCHES for the shade table, DIFFERS for the alpha fallback.** `generateShadeTable`
(`ShadeTable.cpp` about 25-100) transcribes the exchange sort and the band matcher exactly. The
exhaustive L2 scan of `NearestColor` has **no caller named in either source**, so nothing says which
table uses it: UNCLEAR, and there is no RWE counterpart to compare. The interface colour table is a
third matcher and must not be unified with these.

- **The empty-band edge case never fires on retail data (measured).** Over every request the five
  generators make from `totala1`'s palette (8192 + 8192 + 65280 + 256 + 256), the band holds at least
  one entry every time (**0 empty bands**). So the chapter's "scan-end index" and "wrap to zero"
  sentences, and `shdgen.py`'s "position zero" fallback, give the same tables. They would matter
  only for a mod palette with a very sparse brightness distribution.

## 5. The alpha table

**Reference (palette 4).** `BuildAlphaTable` (`0x4ba750`): row and column are two palette indices; the
**diagonal returns the index itself**; every other cell takes the integer average of the two red, two
green and two blue channels (each sum divided by two, truncating) and **matches that colour through the
band-limited matcher** (rule 4). It is a fixed 50/50 blend; "construction, fog and particle drawing" must
select the consumer.

**Register.** `-VISION.md` ("The table", near 470): "for every pair `(i, j)` it averages the two palette
entries a channel at a time ... `(r_i + r_j) / 2` ... and calls `0x4BA9D0`, a nearest-colour search ...
`i == j` is short-cut to `i` at `0x4BA7A7`." So the **register already has the recipe**, including the
matcher's address.

**RWE: DIFFERS in the fallback, MATCHES when the file is present.**

- The shipped `PALETTE.ALP` is read and used when it is exactly 65536 bytes (`atlas_util.cpp` about
  396-410); the post pass and the cloak blend consume it as the original does.
- `generateAlphaTable` (`AlphaTable.cpp` about 23-60), used only when the file is missing or the wrong
  size, finds the nearest entry by an **exhaustive L2 scan** (first strict minimum over all 256), not by
  the band matcher, and its header says "the routine that built the shipped file was not found in the
  binary" and calls it "an approximation". Both statements are now out of date.
- **Replay result.** Running the chapter's recipe (diagonal identity, truncating channel averages, the
  band matcher) over the shipped palette reproduces the shipped `PALETTE.ALP` with **0 of 65536 bytes
  different**. RWE's generator, run the same way, differs from the shipped file on **4552 of 65536
  (6.9 percent)**. The error is entirely the matcher: where the exhaustive scan finds a closer colour
  than the +-40 window would have, it takes it, and the original did not.
- Consequence: on a normal install nothing changes (the file is there). A data set without the file,
  or a mod palette, gets a blend that is subtly different from what the engine's own table would give.
  The fix is to reuse the matcher that `generateShadeTable` already holds.

## 6. The shade and light tables

**Reference (palette 5).** `BuildLightTable` (`0x4babd0`): 32 rows; row `r` multiplies each channel by
`1 + r/30`, truncating, capped above 255, then matches (rule 4): row 0 unchanged, row 30 doubled, row 31
slightly above double. `BuildShadeTable` (`0x4badf0`): starts at scale 0 and adds 0.06875 per row
(the stored step is -0.06875 and is subtracted), so row `r` is `r x 0.06875` up to 2.13125 at row 31,
each component converted through an unsigned short, capped, then matched. "The name shade does not imply
that every row darkens."

**Register.** `-SHADING.md` section 09: the generator "scale += 0.06875 per row" is exactly what the
file was built from, and the *measured* mean luminance ratios (1.807 at row 31, not 2.131) are the clamp
and the snap, not a different recipe; `-RENDER.md` section 100: `PALETTE.LHT` is "identity at row 0 and
brightens monotonically to 1.77x at row 31".

**Replay result.** The shade recipe reproduces the shipped `PALETTE.SHD` with **0 of 8192 bytes
different** (as `shdgen.py` already shows), and the light recipe (`1 + r/30`, same matcher)
reproduces the shipped `PALETTE.LHT` with **0 of 8192 different**. So the two generators are exactly the
recipes in the chapter, and each shipped file is the unmodified output of its generator on the shipped
palette.

**RWE.** The shade table: **MATCHES** (file, with an exact generator as fallback). The light table:
**MISSING and unneeded on retail data**. RWE never loads `PALETTE.LHT`: the one consumer it knows, the
selected row of a list box ("level 30 of 32", `0x4A1FAE`), is approximated by a white wash at alpha 115
(`UiListBox.cpp` about 44-65), chosen to match the table's median lift of 1.84x. Having the recipe,
the exact lookup is available, but that is a gain in fidelity of one list highlight. A documentation
note: `-RENDER.md` section 54's table row for `PALETTE.SHD` still reads "row 16 ~ identity, row 31 ~
1.55x" and `PALETTE.LHT` "row 31 ~ 1.5x"; the first is superseded by `-SHADING.md` section 09 (rows 14 and
15 are identity, row 31 is 1.807), the second by section 100 (1.77x).

## 7. The gray and blue tables

**Reference (palette 5).** `BuildGrayTable` (`0x4bad30`): every channel becomes the **integer arithmetic
mean** of the source RGB, matched to the nearest palette entry. `BuildBlueTable` (`0x4baf30`): red halved,
green halved, **blue halved plus 50**, matched; an overflow test on "blue half plus 60" would saturate at
255 but is unreachable for an eight-bit channel. They are 256-byte one-row remaps, generated at game start
from the loaded palette; no file backs them. Where they are used is named only for the blue table: the
model renderer's tint of pixels at or below a threshold (`graphics-model-render` section 8).

**Register.** The tables' existence and slots (`+0xCC`, `+0xD0`) are in `-RENDER.md` section 54 and
`-VISION.md` (near 479); **no recipe and no consumer**.

**Replay result** (the shipped palette, the band matcher; nothing to compare against because no file
exists, so these are what the recipe produces):

- **Blue.** Black (index 0) maps to `(0, 0, 55)`; `(123, 151, 255)` to `(51, 75, 215)`; `(127, 7, 0)` to
  `(51, 19, 63)`; green `(0, 255, 0)` to `(15, 119, 63)`; white to `(107, 143, 163)`.
- **Gray.** `(123, 151, 255)` maps to `(171, 171, 171)`; `(155, 131, 47)` to `(107, 107, 107)`;
  `(127, 7, 0)` to `(43, 43, 43)`; green to `(91, 91, 91)`. The mean ignores perceived brightness, so a
  saturated blue maps far lighter than a luma grey would.

**RWE: DIFFERS in the blue tint, UNCLEAR for gray, presentation only.**

- **Blue.** RWE tints a below-sea fragment by multiplying the lit colour by `(0.5, 0.5, 1.0)`
  (`unitTexture.frag` 46 and 126, `unitBuild.frag` 40 and 70). The recipe's result is not a multiplier:
  the offset of 50 on blue lifts black to a deep blue, and the palette snap desaturates bright colours
  towards grey-blue. Averaged over the 256 palette entries the two differ by **19.6 of 255 per channel**,
  and by as much as 128 on one channel. See `BT-GRAPHICS-MODEL-RENDER-PARITY.md` rule 8 for the effect on
  a half-sunk ship. It rests on the chapter's reading that the model tint pass uses this table; the owner
  distinction between tint and cut is **BT-16**, still open.
- **Gray.** The chapter names no consumer. RWE's "remembered ground" grey is
  `0.299 r + 0.587 g + 0.114 b`, times 0.7 (`mapTerrain.frag` 47, `basicTexture.frag` 14,
  `unitTexture.frag` 128), commented "as TA draws it". Against the gray table's mean-and-snap the two
  differ by **27.8 of 255 per channel** on average. Whether the original's remembered-ground dimming
  uses this table, the fog frames of `anims/fog.gaf` alone (`-VISION.md` "Drawing"), or both, is not
  stated by either source. **UNCLEAR** until a spec-room read finds the consumer of the gray table
  (`gfx+0xCC`).

## 8. Loading, generating and caching the three file tables

**Reference (tables 1-4).** Game-state initialisation calls the alpha, shade and light loaders with the
loaded palette, then the gray and blue makers. Each loader builds `palettes/PALETTE.ALP`, `.SHD` or
`.LHT`; **a nonzero file length selects loading** (fatal error if the load returns null; a short non-empty
file is copied at full length unchecked; the file's content is never validated against the palette; the
loader frees its temporary buffer and **returns the freed pointer**). A zero length selects generation
into the display-owned buffer and **writes the buffer back to the file** with the fixed length, ignoring
the write's result. Gray and blue have no file.

**Register.** `-RENDER.md` section 54 lists the three files, sizes and slots, loaded "via the VFS" at
`0x429340`.

**RWE: MATCHES in the order, DIFFERS in being safer.** `atlas_util.cpp` (about 369-410) reads each file
through the virtual file system, **checks the size** (8192 / 65536), logs a warning and **regenerates** on a
wrong size or a missing file, and never writes a cache back. So a truncated file that the original would
copy blindly (reading past a short buffer) is refused here; and a stale file that disagrees with the
palette is trusted by both (the original has no check, RWE has none beyond the size). Neither concern is
reachable on retail data. The returned dangling pointer and the cache write have no counterpart.

## 9. The filename helpers (not applicable)

**Reference (palette 6).** Five byte-string helpers whose ownership follows the address map: `ChangeExtension`
(scans back for a period stopping at a backslash; unbounded copy and append), `HasExtension` (**any**
period anywhere, directory included), `StripExtension` (last period of the whole string, no backslash
stop), `StripFileName` (truncates after the last backslash), `StripPath` (copies the part after the last
backslash). They recognise **backslash only**, take no capacity, and `ChangeExtension` begins from
length minus one even for an empty input.

**RWE: not applicable.** Paths go through the virtual file system with forward slashes and
`std::string` utilities (`rwe/util/`); an archive entry name is looked up case-insensitively. The chapter's
unbounded copies and the directory-blind `HasExtension` are faults of the original's string handling with
no reachable analogue. One place an equivalence could matter: a name with a dot in a directory
(`totala1.hpi` entries use forward and back slashes interchangeably in the original). Not traced.

---

## 10. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Water tint formula**: RWE multiplies by `(0.5, 0.5, 1.0)`; the recipe is a palette remap (R/2, G/2, B/2 + 50, snapped), so black goes to deep blue and brights desaturate. 19.6 of 255 average channel error. Rests on the chapter's consumer claim. | DIFFERS | a look at a half-sunk ship in the original (BT-16's check does both); then a shader change using a table computed once from the palette |
| 2 | **Alpha table fallback is not the original's**: 4552 of 65536 cells differ; the chapter's recipe (diagonal, truncating average, the band matcher) reproduces the shipped file with 0 differences. The header comment in `AlphaTable.h` is stale. | DIFFERS (fallback only), proven by replay | code: reuse the matcher in `generateShadeTable`; add a test that regenerating equals the shipped file when `RWE_SCENARIO_DATA` has it |
| 3 | **Gamma only brightens the world view**; the original rewrites the palette, so the HUD, minimap and menus brighten too. The slider is continuous (original: 21 steps). | DIFFERS, presentation | a look in play at the HUD with the slider raised |
| 4 | **Gray table's consumer unknown**; RWE's remembered-ground grey (luma x 0.7) differs from the table's rule by 27.8 of 255. | UNCLEAR | spec-room read: who reads `gfx+0xCC` |
| 5 | **`PALETTE.LHT` is never loaded**; the list-box highlight is an alpha wash. The recipe is exact and the file is present. | MISSING, cosmetic | none needed |
| 6 | **Stale register text**: `-RENDER.md` section 54's table rows ("SHD row 16 ~ identity, row 31 ~ 1.55x"; "LHT row 31 ~ 1.5x"). | documentation | fix the rows |
| 7 | The chapter's empty-band and wrap rules in the band matcher: measured never to fire on retail. | UNCLEAR, mod palettes only | none |

No source file was changed.
