# Bitmap font file, string measuring, fitting and outlined text parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md` and `BT-GRAPHICS-PALETTE-PARITY.md`, it
allocates no section number in the main register. It covers one chapter of the reference,
`graphics-font` (the four services `0x4c1480`, `0x4c14f0`, `0x4c1760` and `0x4c1830`), together with
the glyph file format the chapter describes in its first section: how an `.FNT` file is laid out, how
one line of text is measured, fitted, centred and outlined. This is the finding that implementation
work cites for any of them, per `AGENTS.md` and ADR-0004.

Fonts that are GAF sprite sets (the `hattfont12` glyph series RWE's speech text uses) are GAF
frames and are covered by `BT-GRAPHICS-GAF-PARITY.md`; this audit is the `.FNT` bitmap fonts only.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-font.md`). Nothing else
  under that tree was opened: no source, no decompilation, no source links. Rules are restated in
  prose. The spec-room discrepancy register was read, read-only; no row touches fonts.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `src/rwe/io/fnt/Fnt.*`, `TextureService::getFont`
  (`TextureService.cpp` about 312-357), `UiRenderService.cpp` (the text routines, about 101-226 and
  the helper at the top) and `util/rwe_string.*` at the working tree of 2026-10-17. Nothing was
  built, run or changed; line numbers are "about".
- **A census of the shipped fonts** (every `.fnt` in every archive of the retail install, 39 files
  counting duplicates across archives, through `tools/hpi.py`'s reader; throw-away scripts outside
  the tree), and a scan of the shipped FBI, TDF, GUI and text files for bytes above 0x7f.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The font file
3. Measuring a line
4. Fitting, clipping and the all-or-nothing rule
5. Colours, centring and the outlined string
6. Malformed input
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register | Verdict |
| --- | --- | --- |
| 1 file layout | none for `.FNT` (`ta-gui-backgrounds-and-text-positioning.md`, untracked, only lists which fonts exist) | rule 2 |
| 2 measuring | none | rule 3 |
| 3 string fitting | none | rule 4 |
| 4, 5 glyph pixels, centred and outlined text | `TOTALA-EXE-INTERFACE.md` describes the gadget text colours and the quick-key underline (rules about colours, not about the draw wrapper) | rule 5 |

## 2. The font file

**Reference (section 1).** The file begins with a height byte, further metadata including a signed
vertical offset used by the glyph blitter, and a **first-character byte at offset 3**. An array of
unsigned-short offsets starts at offset 4; character `c` at or above the first character uses entry
`c - first`; **offset zero means no glyph**. A glyph is a width byte then packed bitmap bits (most
significant bit first; a set bit is the foreground colour, a clear bit the background). The table's
extent and each bitmap's length are trusted.

**RWE.** `FntArchive` (`Fnt.cpp` about 6-30) reads a 4-byte header as two 16-bit words (glyph height,
and an unused word), then **exactly 256 offsets**; `extract` seeks to the offset, reads the width byte
and then **twice the width in bytes**. `getFont` turns each into a sprite, bit by bit, most
significant bit first, white for a set bit and transparent for a clear one.

- **Table indexed by the character itself: MATCHES on all 39 shipped fonts.** The first-character byte
  (offset 3) is 0 in every one, the table is always present in full (the first glyph sits at offset
  520 to 543), so indexing by the character and ignoring that byte gives the chapter's result. A mod
  font with a first character other than 0 would be shifted by that amount in RWE.
- **Height: MATCHES as a byte.** RWE reads offsets 0 and 1 together as a 16-bit height; offset 1 is 0
  in all 39 files. Shipped heights are 9 (2 files), 11 (13), 12 (2), 13 (8), 14 (8), 16 (2) and 17 (4).
- **Glyph storage is one continuous bit stream, `ceil(width x height / 8)` bytes: confirmed.** Over the
  seven fonts sampled across archives (`armfont`, `roman12`, `console`, `armbrief`, `corefont`,
  `buttons`, `mscript`), the distance from every glyph to the next in file order equals exactly
  `1 + ceil(width x height / 8)`, never a per-row padded size.
- **Glyph length: DIFFERS for any font taller than 16 rows.** RWE reads `2 x width` bytes, which is
  `16 x width` bits, and then resizes the pixel list to `width x height`. For heights up to 16 the
  needed bits are the first ones of what was read (the extra bytes are the next glyph's and are
  ignored), so the sprite is correct. **For a height of 17 the last row is cut off**: the glyph
  has `17 x width` bits and RWE has read only `16 x width`, so the bottom row is transparent. Four
  shipped files are 17 high: `fonts/armfont.fnt` in `btdata.ccx`, `ccdata.ccx` and `rev31.gp3`, and
  `fonts/hatt14.fnt` in `totala1.hpi`. `MainMenuScene_campaign.cpp` (about 362) loads
  `fonts/ARMFONT.FNT` for the Arm campaign screens, so on an install that has those archives the
  descenders (g, j, p, q, y) and the base of any 17-row glyph lose their last pixel row.
- **Vertical offset: MISSING.** The byte at offset 2 is the offset the chapter says the blitter
  applies to every glyph; shipped values are 1 (18 files), 2 (17) and 3 (4). RWE places every glyph
  with its top at the requested Y. The text lands 1 to 3 pixels higher than the original's for the
  same coordinates unless the GUI layout code already compensates; that was not checked.
- **Offsets and widths: the census sees no glyph that runs past its file.** (Zero glyphs in 39 files.)
- **Glyph count.** 20 shipped fonts hold 94 glyphs (ASCII 32 to 125), 19 hold 221 to 223; characters
  without a glyph have offset zero. RWE builds 256 sprites; a missing glyph is a sprite of width 0.

## 3. Measuring a line

**Reference (section 2).** The width of a line is the sum of the width bytes of its glyphs, up to a
NUL or a newline; a byte below the first character or with a zero offset adds nothing and no
replacement space. Characters are unsigned bytes: no UTF-8, no shaping, no kerning.

**RWE.** `getTextWidth` and `drawText` (`UiRenderService.cpp` about 101-154) sum each sprite's width and
step by it.

- **Missing glyph adds nothing: MATCHES.** Width-0 sprites.
- **Sum of glyph widths, no kerning: MATCHES.**
- **Bytes versus code points: DIFFERS.** RWE walks the string with the **checked UTF-8 iterator**; the
  original treats every byte as a character. Two consequences:
  - A code point above 255 is mapped to glyph 0 (an empty sprite) and a byte pair that is valid
    UTF-8 for a Latin-1 letter (U+00E9, two bytes) shows the glyph for 233, where the original shows
    two glyphs. For ASCII text the two agree.
  - **A byte that is not valid UTF-8 makes the iterator throw** (`utf8::invalid_utf8` from the
    bundled `utfcpp`, a `std::exception`), from inside the render call. Census: **309 of 435 shipped
    FBI files, 10 of 548 TDF files, 7 of 223 GUI files and 82 of 284 text files contain bytes above
    0x7f that are not valid UTF-8** (Windows-1252: `M\xe9cano`, `r\xe9parations`). In the retail set
    these sit in the French, German, Spanish and Italian keys (`FrenchName`, `Frenchdescription`,
    `GermanDescription`, `SpanishName` and the like), which an English game never draws, and in
    the French campaign briefings. **Third-party units and maps that put a single Latin-1 accent in
    the English `Description` or `UnitName` key would throw in the draw call when selected**, taking
    the process down through the "Critical Error" box (no catch on the frame path). I did not run
    this; it follows from the iterator's documented behaviour and from no code on the way
    converting the string.
- **NUL and newline end the line: DIFFERS.** The original stops at the first newline (the measure and
  the draw both); RWE draws a newline as the missing glyph 10 (width 0) and carries on with the
  text after it on the same line. A string with an embedded NUL is whatever the `std::string` holds.
  Whether any shipped string carries a newline into a draw call was not checked.

## 4. Fitting, clipping and the all-or-nothing rule

**Reference (section 3).** `DrawString` takes a maximum width (-1 for none). If the line is wider it
copies at most 299 bytes, chops characters off the **end** until it fits (no ellipsis, no word
wrap), and then draws it. The rectangle `[x, y, x + width, y + height]` must lie **entirely inside**
the surface's clip rectangle or **nothing is drawn**; there is no partial clipping of glyphs. The
routine returns nothing, so the caller cannot know a string was dropped or trimmed. The chapter's
own faults (an unterminated copy, a stale width when the buffer empties, trimming that starts at the
end of a multi-line string) are specific to its buffer.

**RWE: MISSING.**

- **No width limit with trimming.** `drawText` has no maximum width. Where a gadget's text is wider
  than its box RWE draws it past the box; the original (if the gadget passes a width, which the
  chapter does not say) would have chopped the tail.
- **No all-or-nothing clip.** A string partly outside the viewport is drawn partially by the GPU
  clip; the original draws none of it. This is only visible at a screen edge.
- **Wrapped text exists in RWE only** (`drawTextWrapped`, a 15-pixel line step) as RWE's own design.

## 5. Colours, centring and the outlined string

**Reference (sections 4, 5).** Glyph bits pick the foreground or background colour; a colour equal to
the transparent-key colour leaves the destination alone. `DrawStringCentered` centres on the **whole
surface width**, not on a given point (its third parameter is the Y position despite a placeholder
name) and does not shrink or wrap overwide text. `DrawOutlinedString` centres, draws the text four
times at **one pixel left, right, up and down** in the first colour (a cross, no diagonals), then
once in the middle in the second colour; the colours are not restored afterwards; each of the five
draws passes or fails the containment rule on its own, so an edge can lose some of the outline.

**RWE.**

- **Foreground/background colours: DIVERGENCE (documented in kind).** A clear glyph bit is always
  transparent and the text is tinted by one colour (`drawText` with a `Color`), which is what the
  shipped GUI text needs; an opaque background colour (if any shipped screen uses one) has no
  counterpart. `TOTALA-EXE.md` section 88 covers the true-colour pipeline.
- **Centring: DIFFERS in meaning, MATCHES in effect.** RWE centres around a caller-supplied X
  (`drawTextCentered`, `drawTextCenteredX`); the original centres on the surface. Callers that pass
  the surface centre get the same result. `drawTextCentered` adds a hard-coded half height of five
  to Y; the chapter's routine does not.
- **Outlined string: MISSING.** There is no outlined text routine in RWE. The chapter does not say
  what in the original uses it, so whether a player ever sees the outline is UNCLEAR (a spec-room read
  of its callers settles it).
- **The caption drop shadow and quick-key underline** (`ROADMAP.md`, `-INTERFACE.md`) are the
  register's text effects and are not part of this chapter.

## 6. Malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") applies to a mod's `.FNT`.
`malformed_input.test.cpp` has **no FNT case**.

- **What is safe.** The glyph read is capped at 510 bytes into a 512-byte buffer (width byte times
  two), the offset is a 16-bit value so a seek cannot overflow, and a failed seek or read leaves the
  stream failed so every later glyph reads as empty. No out-of-bounds write.
- **A truncated file reads uninitialised bytes.** The constructor fills the table with `readRaw`,
  which checks a short read with `assert` alone and returns an uninitialised value in a release
  build (`io/io_util.h`). A font cut inside its offset table gets garbage offsets for the missing
  entries; each garbage offset seeks (past the end, failing) and reads a garbage width byte, so the
  result is a font of blank or garbage glyphs, not a crash. Same helper as the GAF and HPI readers.
- **No limit on the height.** Offsets 0 and 1 are read as a 16-bit height; a font that sets offset 1
  asks for up to 65,535 rows. Every one of the 256 glyphs is expanded to `width x height`
  four-byte pixels and uploaded as its own texture (`TextureService.cpp` about 345-347): at the
  maximum, 255 pixels wide, that is about 66 MB per glyph and about 17 GB across the font. TA's
  tallest is 17 and the table is a byte in the original.
- **No limit on a bad string.** Rule 3's invalid-UTF-8 throw is the text-side fault.

## 7. Gaps to fix

Ranked by what a player or a modder would hit.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A non-UTF-8 byte in a drawn string throws out of the render call.** Third-party units with a Latin-1 accent in an English key are the likely trigger; every shipped English key is clean. | fault | code: decode as Windows-1252 or replace invalid bytes before drawing (or use the unchecked iterator with a fallback); add a case |
| 2 | **17-row fonts lose their bottom pixel row** (`armfont.fnt` in the three extension archives, `hatt14.fnt`): the glyph read is `2 x width` bytes. | DIFFERS (code) | code: read `ceil(width x height / 8)` bytes |
| 3 | **Vertical offset byte ignored** (1 to 3 pixels). | MISSING | spec-room or listening-style check: place a font beside the original; fix by reading offset 2 |
| 4 | **Text is drawn UTF-8, not bytes**; a newline does not end the line. | DIFFERS | decision; follows from rank 1 |
| 5 | **No trimming to a width, no all-or-nothing clip, no outlined string.** | MISSING | spec-room read of which callers pass a width and which draw an outline |
| 6 | **A FNT with a large height field asks for gigabytes**; truncated files read uninitialised bytes; no FNT malformed-input test. | fault | code: cap the height at 255 (or 64), check the stream, add cases |
| 7 | First-character byte unused (0 in all shipped fonts). | DIFFERS, mods only | code: honour it or reject non-zero |

No source file was changed.
