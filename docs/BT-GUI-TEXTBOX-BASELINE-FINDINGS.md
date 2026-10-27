# GUI text-input baseline: where the original puts a text box's text, and what RWE draws

Spec-room finding for **issue #903**, "Battleroom message-box text is vertically
misaligned; it sits too far up". Date: 2026-10-27.

**Evidence class: static reference reading.** The reference rule below was read
from the decompilation, and the font metrics and GUI records were read from the
shipped files as data. **Nothing was executed**: no `TotalA.exe` run, no RWE
build, no RWE render and no screenshot. Statements about what a player sees are
therefore arithmetic from the code and the assets, not observations. The RWE
half is a read-only audit of the working tree named in §1. No source file was
changed.

## Answer in brief

- **A text input's text is placed at a fixed offset from the gadget's top: pen
  Y = gadget Y + 3.** The gadget's height and the font's height are not used to
  choose it. This is routine `0x4a4d70`, the type-3 text-input draw.
- Font GAFs are normalised when they are loaded into the two glyph slots: every
  frame's stored vertical hotspot is reduced by the **height of frame `0x49`**
  (the letter `I`), routine `0x4aedd0`. This is a height, not the `I` frame's
  own stored hotspot; the two differ by one in the shipped faces.
- With `hattfont12` frame `0x49` 12 rows high, `I`'s stored hotspot 11 becomes
  −1. A glyph drawn at pen Y therefore has its cap-cell top at Y + 1, its cell
  bottom at Y + 12, and descenders down to Y + 14.
- For LOUNGE2's MESSAGE box (21, 441, 294 × 20), pen Y is **444**; the cap ink
  occupies rows **445–456**, the baseline row is **456**, and the box has four
  blank rows above and four below the cap cell. The 20-row box therefore *looks*
  vertically centred, but the rule that produces that is fixed, not centring.
- RWE (`UiTextBox::render`) passes gadget Y + 12 to a GAF text drawer whose
  sprites still carry their **raw** hotspots. The same `I` therefore lands on
  rows **442–453**: **three rows too high**.
- Because both rules are independent of the box height, **there is no box
  height at which the current RWE formula reproduces the reference** for
  `hattfont12`. The match is a constant three-row error at every height.
- Labels already match: a label's pen Y is the gadget Y, and RWE's label path
  adds the `I` frame's height (`hattfont11`: 10) as its baseline, which lands on
  the same rows as the reference. The text box is the one that adds the wrong
  constant.

## 1. Evidence and limits

- **Reference revision.** `D:\bytetactics`, commit
  `e89721c46c3fa06e59558419b54fe26061fafb73` (2026-10-20), a matching
  decompilation of the reference executable.
- **Executable identity.** `D:\bytetactics\orig\TotalA.exe`: **1,178,624
  bytes**, MD5 `8E74A1DFFA1F5988624C52048F5B20CD`, SHA-256
  `3B9C0FADABF3DC67ED5F05A70F1E1505A0C65DEADD1A3C930ADFE30E2A84995E`.
- **Static reading.** The type-3 draw (`0x4a4d70`), the GAF-frame drawers
  (`0x4b7f90`, `0x4b8310`), the font-slot loader (`0x4aedd0`), the clipped text
  routine (`0x4a50e0`), the line-height routine (`0x4a50b0`), the label draw
  (`0x4a56b0`), the list draw (`0x4a1b40`), the list configuration routine
  (`0x4a32a0`), the render/placement routine (`0x4a81e0`), the line wrapper and
  filler (`0x4be950`, `0x4cc7ab`), and the display bitmap-font pointer
  (`0x4c1420`) were read. Rules are restated in prose; no decompiled source,
  identifiers or control flow is reproduced here.
- **Shipped data.** The 3.1 data set as extracted at `D:\RWE-extract`
  (`rev31` overrides `totala1` by path):
  `rev31\guis\LOUNGE2.GUI`, `rev31\anims\hattfont12.GAF`,
  `rev31\anims\hattfont11.GAF`. The `totala1` copies of all three were also
  read; every value this finding uses is identical in both copies. The GAF
  frames were inspected as file records (width +0, height +2, horizontal
  hotspot +4, vertical hotspot +6, all 16-bit in the frame header), and the
  glyph bitmaps were decoded only to confirm the ink extents quoted below.
- **GUI census used for context.** 17 shipped type-3 text inputs across the
  shipped GUI files (later archives overriding earlier): heights 19 (3), 20
  (5), 21 (1), 22 (1), 24 (3), 25 (1), 26 (1), 27 (1), 28 (1). LOUNGE2 has no
  type-7 font records; its text input therefore uses glyph slot zero.
- **RWE checkout.** `D:\RWE-spec-room-903`, branch
  `903-textbox-baseline-findings`, commit
  `6e6d3aec48632022a2e00c68726dd06607b0618a`. Line numbers are as read on
  2026-10-27. Nothing was built or run.
- **Not done.** No original runtime, no `ui_probe` render, no RWE capture, and
  no test execution. The caret's inclusive end count is a static read of the
  line filler; the exact pixel snapping of RWE's projection was not measured.
- **Cross-references.** `BT-GUI-GUI-PARITY.md` rule 7 establishes slot zero for
  text inputs and slot one for labels. `MAP-DESCRIPTION-FONT-FINDINGS.md`
  (MAP-FONT-02) independently establishes that font loading subtracts the `I`
  frame's height from every frame's stored vertical hotspot and gives the
  label example (hattfont11 `I`: 10 high, hotspot 9 → −1, visible frame starts
  one row below the pen). This finding uses that result and extends it to the
  text input.

## 2. The reference rule

### 2.1 The text input draw (`0x4a4d70`)

The routine is given the entry's rectangle in panel coordinates. For a type-3
entry the rectangle is (gadget X, gadget Y) to (X + width − 1, Y + height − 1).
It first fills the rectangle with GUI colour 0 or copies the panel background
into it, depending on attribute bit 0. Then:

- it adds **3** to the rectangle's top;
- it draws the entry's text with the current glyph font, starting at the
  rectangle's left edge, with the text's pen Y at the shifted top;
- the horizontal limit is `right − left`, i.e. width − 1; a glyph that does not
  fit is not drawn and the string stops there (no partial glyph, no ellipsis).

**The height is used only to build the background rectangle.** It does not
appear in the text Y, in the caret Y or in the caret length. There is no
centring and no vertical padding derived from the font.

So the reference's **pen Y for a text input is `gadget Y + 3`**. For
LOUNGE2 MESSAGE that is 441 + 3 = **444**.

### 2.2 The font-slot load adjustment (`0x4aedd0`)

The routine loads a GAF into a glyph slot and then, before the slot is used:

1. takes frame `0x49` (the letter `I`) and reads its **height** at frame offset
   +2;
2. subtracts that value from the **vertical hotspot at +6 of every frame** in
   the file, including `0x49` itself.

This is the adjustment the issue calls "subtract frame `0x49` height". It is
the *height* (12 for hattfont12), not `I`'s stored hotspot (11), and not a
signed byte.

The two slots are loaded at start-up: `hattfont12` into slot zero and
`hattfont11` into slot one; the active table is then slot zero. LOUNGE2's
type-3 entry has `fontnumber=0` and the file has no type-7 font records, so the
message box draws in slot zero, hattfont12.

A GAF frame is drawn with its cell top at `pen Y − adjusted hotspot`. With
`hattfont12`'s `I` height 12 and stored hotspot 11, `I`'s adjusted hotspot is
**−1**, so:

| Glyph class | Stored hotspot | Adjusted | Cell top | Ink rows at pen Y |
| --- | ---: | ---: | ---: | --- |
| Capitals, digits, most letters (12 high) | 11 | −1 | Y + 1 | Y+1 … Y+12 |
| Lowercase without descender (10 high) | 9 | −3 | Y + 3 | Y+3 … Y+12 |
| Descenders `g p q y` (12 high) | 9 | −3 | Y + 3 | Y+3 … Y+14 |

Every class shares the same **baseline row, Y + 12** (the bottom row of `I`).
Descenders reach two rows below it. Some ASCII punctuation has its own stored
hotspot (for example `,` 4, `.` 3, `_` 0, `"` 12), and follows the same
subtraction.

This is why cap ink begins one row below the pen: the normalisation leaves caps
with a −1 hotspot.

### 2.3 The caret

When the entry has focus, the routine measures the text before the cursor with
the current GAF font's raw frame widths and draws a one-pixel vertical line at
`rectangle left + that width`:

- **top**: the shifted rectangle top, i.e. `gadget Y + 3` (= the pen Y);
- **end coordinate**: top + (`I` height + 2) = top + 14 for hattfont12;
- the line filler includes both end rows, so the painted span is **15 rows**,
  from pen Y through pen Y + 14 (444–458 for MESSAGE);
- colour: GUI palette index 9.

The caret is therefore anchored to the **pen Y**, not to the text's visible cap
top and not to the gadget's middle. Its top is one row above the cap ink and its
bottom is two rows below the `I` bottom.

### 2.4 Per-gadget-type vertical placement

The constant is different per type; only the button derives its Y from the
rectangle's height.

| Type | Text pen Y | Evidence |
| --- | --- | --- |
| 3, text input | `Y + 3` | `0x4a4d70`; height unused |
| 5, label | `Y + 0` | `0x4a56b0`; the label path also switches the glyph table to slot one |
| 2, list text row | `Y + 2 + row × step` | `0x4a1b40`; `step` = the entry's itemheight if non-zero, else line height + 1, where line height = `I` height + 2 |
| 1, button caption | `(bottom − lineHeight − top) / 2 + top`, plus 1 when the button is pressed | `0x4a5f40`; integer division |
| 1, bottom-centred caption (attribute `0x20`) | `bottom − 4 − lineHeight` | `0x4a5f40` |

Two consequences worth stating plainly:

- **A text input is not a centred button.** The button's caption is the one
  text placement in the toolkit that genuinely uses height and font metric
  together. A text input's text does not.
- **The `+3` is not "half of (height − text height)".** It is a literal
  constant. For the shipped 20-high message box, 4 + 12 + 4 happens to be
  symmetric, which is why the fixed rule can be mistaken for centring.

A list's height is also rounded down, on a forced render, to a multiple of
`lineHeight + 2` (16 for hattfont12), and a text list's row pitch is set to
`lineHeight + 1` (15) by the configuration routine `0x4a32a0` when the
application binds a text buffer to it. Neither applies to a text input.

## 3. Worked example: LOUNGE2 MESSAGE

**Data.** `rev31\guis\LOUNGE2.GUI`: entry `MESSAGE`, `id=3`, `xpos=21`,
`ypos=441`, `width=294`, `height=20`, `attribs=0`, `fontnumber=0`,
`maxchars=63`. The panel record is (0, 0, 640 × 480), so panel coordinates are
screen coordinates. The `totala1` copy has the same values.

**Font data.** `rev31\anims\hattfont12.GAF`, entry `Haettenschweiler (120)`:
frame `0x49` is 5 wide, **12 high**, horizontal hotspot 0, vertical hotspot
**11**. The common capital frame is 12 high with hotspot 11; the common
lowercase frame is 10 high with hotspot 9; `g`, `p`, `q`, `y` are 12 high with
hotspot 9.

**Arithmetic.**

| Step | Value |
| --- | ---: |
| Gadget top | 441 |
| Pen Y = 441 + 3 | **444** |
| Adjustment = `I` height | 12 |
| `I` adjusted hotspot = 11 − 12 | −1 |
| Cap cell top = 444 − (−1) | **445** |
| Cap cell rows (12) | **445–456** |
| Lowercase top = 444 − (9 − 12) | 447 |
| Lowercase rows | 447–456 |
| Descender bottom = 447 + 12 − 1 | 458 |
| Baseline row (bottom row of `I`) | **456** |
| Caret rows (444 through 444 + 14) | **444–458** |
| Box rows | 441–460 |
| Blank rows above the cap cell | 4 (441–444) |
| Blank rows below the cap cell | 4 (457–460); 2 below a descender |

The text lands symmetrically in the 20-row box by coincidence of the numbers,
not by a height calculation.

**Other shipped text-input heights.** Applying the same rule (cap ink at
Y+4 … Y+15, i.e. 12 rows beginning four rows below the gadget top):

| Height | Cap ink rows (relative) | Blank above | Blank below (cap-only) |
| ---: | --- | ---: | ---: |
| 19 | +4 … +15 | 4 | 3 |
| 20 | +4 … +15 | 4 | 4 |
| 21 | +4 … +15 | 4 | 5 |
| 22 | +4 … +15 | 4 | 6 |
| 24 | +4 … +15 | 4 | 8 |
| 25 | +4 … +15 | 4 | 9 |
| 26 | +4 … +15 | 4 | 10 |
| 27 | +4 … +15 | 4 | 11 |
| 28 | +4 … +15 | 4 | 12 |

Descenders reduce the bottom figure by 2. Only the 20-row box is symmetric.
This table is the direct answer to "is it derived from gadget height": it is
not; the top padding is 4 at every height.

## 4. Comparable gadgets in the same file

### 4.1 A label: `RESCTEXT` (510, 12, 41 × 16, `text=Metal`)

- The label draw sets the glyph table to slot one, then draws at **pen Y =
  gadget Y** (no +3). It does not use the file's height for the text Y.
- `hattfont11` frame `0x49` is 3 wide, **10 high**, vertical hotspot **9**;
  after subtracting 10 the adjusted hotspot is −1.
- Pen Y = 12 → cap cell rows **13–22**; lowercase rows 15–22; descenders to 24.
- RWE matches this exactly. `UiFactory::getLabelFont` sets
  `textBaseline = I frame height` (10) and `UiLabel::render` draws at
  `posY + textBaseline`, while RWE's `hattfont11` sprites carry the raw hotspot
  −9: 12 + 10 − 9 = 13 for the cap top, 22 for the bottom. This is the same
  "pen Y + `I` height" relation as the reference, which is why the label is
  correct and the text box is not.

### 4.2 The `OUTPUT` list (21, 342, 414 × 96)

- `id=2`, `itemheight=0`, `attribs=1`. The file alone does not mark it as a text
  list; the lobby setup binds the chat buffer to it by name through `0x4a32a0`,
  which sets the text-row attribute and sets the row pitch to
  `lineHeight + 1` = 14 + 1 = **15**.
- On the first forced render, `0x4a81e0` rounds the height down to a multiple of
  `lineHeight + 2` = 16: 96 is already a multiple, so it stays 96.
- Row pen Y = `342 + 2 + row × 15` = 344, 359, 374, 389, 404, 419.
- Cap ink = pen + 1: rows 345–356, 360–371, 375–386, 390–401, 405–416,
  420–431. Descenders reach two rows lower each time.
- Six rows fit: after the sixth row the remaining height is 6, below the line
  height 14, so the loop stops. The last cap ink ends at row 431 and its
  descender at 433; the box bottom is 437.
- **RWE differs here too**: `UiListBox::render` draws at `posY + 12 + i × 12`
  and `numberOfLines` is `height / 12 − 1` (7 rows for 96). With the raw
  `hattfont12` hotspot that puts the first cap at 343 (2 rows high) and uses a
  12-row pitch against the reference's 15, so the error grows down the list.
  This is adjacent to #903, not caused by it.

## 5. RWE audit (read-only)

### 5.1 `UiTextBox::render`

File `src/rwe/ui/UiTextBox.cpp`, function `UiTextBox::render` (about 17–57):

- `textX = posX + 4` (about 35). The reference draws at the gadget's left edge
  with no inset.
- `baseline = posY + 12` (about 36), a hardcoded constant, independent of
  `sizeY`.
- `room = sizeX − 8` (about 42), and leading characters are dropped until the
  rest fits; then the text is drawn at `(textX, baseline)`. The reference clips
  at width − 1 from the start and never drops leading characters.
- The caret is `fillColor(caretX, baseline − 10, 1, 12, ...)` (about 55), i.e.
  rows `posY + 2 … posY + 13`, twelve rows.

`UiTextBox::render`'s own comment says the original's caret runs "from the
text's own top down two pixels past the font's height". The position and
length above do not implement that: the reference caret starts at the pen Y
(gadget Y + 3) and paints 15 rows to gadget Y + 17.

The font is `anims/hattfont12.gaf`, entry `Haettenschweiler (120)`, chosen in
`UiFactory::componentFromGuiEntry` (about 666–688). No scene calls
`setDrawPlate(false)`, so every text box also paints its own plate; the shipped
MESSAGE has `attribs=0` and the original blits the panel background instead.
That is a separate visual difference.

### 5.2 Why the numbers differ

`UiRenderService::drawText` (`src/rwe/UiRenderService.cpp`, about 153–179)
draws each glyph at `y − font.verticalOffset` (`SpriteSeries.h` about 19).
`verticalOffset` is set only for bitmap `.FNT` fonts (`TextureService.cpp`
about 407); it is 0 for GAF fonts. RWE's GAF sprites are built with their bounds
top at `−stored vertical hotspot` (`TextureService.cpp`, frame end about 64–78),
i.e. RWE **does not perform the load normalisation**. So:

- the raw `hattfont12` `I` has bounds top −11;
- `drawText(x, 453)` paints `I` on rows 442–453;
- the reference paints it on 445–456.

`UiRenderService::drawText` therefore takes a Y whose meaning is "the frame's
origin" for a GAF font and "the first text row" for a `.FNT` font. The text box
treats the Y as a baseline and adds 12; that is 3 short of the reference's
`+3 pen + 12 normalisation`.

### 5.3 Labels and lists

- `UiLabel::render` (`src/rwe/ui/UiLabel.cpp`, about 10–45) draws at
  `posY + font->textBaseline`; `UiFactory::getLabelFont` (about 781–791) sets
  `textBaseline` to the `hattfont11` `I` frame height (10). With the raw
  hotspot −9 that gives cap rows `posY + 1 … posY + 10`, exactly the
  reference's label rows. **Labels match.**
- `UiListBox::render` (`src/rwe/ui/UiListBox.cpp`, about 20–72) uses a fixed
  `y = 12 + i × 12` and draws at `posY + y`; `numberOfLines` (about 215–224) is
  `height / 12 − 1`. For OUTPUT that is first cap at 343, pitch 12, 7 rows,
  against the reference's 345, pitch 15, 6 rows.

### 5.4 What the audit establishes

The current text-box Y is not a height-derived centring rule and not a GAF
hotspot rule. It is the font's nominal height (12) used as if it were the
whole offset. It differs from the reference by exactly the reference's `+3`
inset. The label path shows the correct convention already exists in RWE: the
GAF text Y must be the pen Y plus the `I` frame's height.

## 6. Implementation contract

These are behavioural requirements, not code.

**A conforming type-3 text input** at (X, Y, W × H) drawn in glyph slot zero
must:

1. use **pen Y = Y + 3** for the text and for the caret anchor; the gadget
   height must not enter the calculation;
2. place the text with the cap cell at rows **Y+4 … Y+15** for `hattfont12`.
   Equivalently, because RWE's GAF sprites carry the raw hotspot, the Y passed
   to the GAF text drawer must be the pen Y plus the `I` frame height:
   **(Y + 3) + 12 = Y + 15** for `hattfont12`. The general form is
   `Y + 3 + I-frame height`;
3. paint the caret from row **Y + 3** through row **Y + 17** inclusive (the
   pen Y plus `I` height + 2, both ends included, 15 rows for `hattfont12`),
   not from the GAF drawer's Y minus 10 and not 12 rows long;
4. keep the caret's X at the measured width of the text before the cursor.

**The same relation applies to any other gadget that draws GAF text**: the
drawer's Y is the reference pen Y plus the `I` frame's height. Labels already
do this. A list row's reference pen Y is `Y + 2 + row × 15` for
`hattfont12`, so its drawer Y would be `Y + 14 + row × 15`, but the list is a
separate change with its own row pitch and row-count questions.

**Must not change under #903**:

- the gadget rectangle, its hit test and focus behaviour;
- the horizontal text limit (the reference stops at width − 1 and never draws a
  partial glyph) and the current leading-character trimming — the latter is a
  separate divergence and must not be altered as a side effect;
- the background/plate choice (attribute bit 0 in the reference; RWE's
  `drawPlate`) and the caret colour;
- the label path, which already matches;
- the list's row pitch and line count until a separate finding covers them.

**What remains uncertain for a conforming draw** is listed in §7: fonts other
than `hattfont12` in slot zero, type-7 font records, multi-line input, and the
exact snapping of RWE's projection.

## 7. Remaining uncertainty

Static analysis versus executed checks:

- **No executed check was performed.** The rule, the row numbers and the
  three-row error are read from the decompilation and from the shipped GAF/GUI
  bytes. No original-game screenshot, no `ui_probe --render`, no RWE run and no
  test run exists for them. A render comparison is still the acceptance test.
- **The caret's 15 rows depend on the line filler including both end points.**
  That was read statically from the matching line-filler code; if a different
  build or a clipping path shortened the last row, the caret would be 14 rows.
  The top row (Y + 3) is not in doubt.
- **RWE's pixel snapping is unmeasured.** Its UI projection and float
  coordinates could shift the whole draw by one row; the arithmetic above
  assumes integer placement.
- **Slot zero is established statically, not observed.** The start-up code
  loads hattfont12 into slot zero and hattfont11 into slot one and makes slot
  zero current; the label and button draws restore slot zero at their tails.
  LOUNGE2 also has no type-7 font records. A mod GUI that adds one, or a screen
  that leaves the table on slot one, would change the face and therefore the
  numbers.
- **A type-7 font record selects a bitmap `.FNT` path.** That path places text
  with the display font's own height and RWE models it with
  `SpriteSeries::verticalOffset`; the `+3 + I` contract above is for the GAF
  slot-zero case only. No shipped LOUNGE2 entry exercises it.
- **The shipped art may leave its own inset.** The MESSAGE entry has
  `attribs=0` and the original copies the panel background, so the visible
  field border is painted art. Whether the field's inner area begins at the
  gadget's left/top or a few rows in is a visual question; the text rule itself
  is fixed at +3 regardless.
- **Other text gadgets are not settled here.** Buttons centre by height;
  labels use +0; lists use +2 and a 15-row pitch. A single shared "text
  baseline" helper would not reproduce all three and is not implied by this
  finding.
- **Multi-line text is not a text-input feature in this build.** The entry has
  a single line and a `maxchars` limit; the drawing routine skips bytes below
  space rather than breaking a line. Newline behaviour in a text input was not
  exercised.
- **The binary/data pairing.** The GUI and GAF values are from the 3.1 data
  set (`rev31`), read as files; the executable identity is the reference
  binary above. That the installed 3.1 build pairs exactly these files was not
  re-verified by launching it.

No source file was changed. No code is proposed here; the contract above is the
prose an implementation session may work from, and the horizontal trimming,
left inset and plate remain separate questions.
