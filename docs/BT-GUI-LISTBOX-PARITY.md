# Listbox skins and bevelled drawing primitives parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md` and `BT-GRAPHICS-PALETTE-PARITY.md`, it allocates no
section number in the main register. It covers one small chapter of the reference, `gui-listbox` (seven services
from `0x4B0090` to `0x4B0610`: the two bevelled-border helpers and their fill wrappers, the raised and sunken box
primitives, and the list-box frame and background skin). This is the finding that implementation work cites for
any of them, per `AGENTS.md` and ADR-0004. What is *inside* the frame (rows, selection, scrolling) is
`BT-GUI-GUI-PARITY.md` rule 9; the sprite and clipping primitives underneath are
`BT-GRAPHICS-GAF-PARITY.md` and the palette work in `BT-GRAPHICS-PALETTE-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\gui-listbox.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row concerns the toolkit.
- **Evidence class.** A **static reading**; the chapter says no widget interaction, rendering or screenshot was
  used. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `ui/UiListBox.*`, `ui/NinePatch.*`, `ui/UiPanel.cpp` (the plate),
  `ui/UiFactory.cpp` (`createPanel`) and `ui/UiTextBox.cpp` at the working tree of 2026-10-17. Nothing was built,
  run or changed; every line number is "about".
- **A census of the shipped GUI files** (as in `BT-GUI-GUI-PARITY.md`): 40 list boxes in 26 files; the screens'
  own `panel=` value is empty in 15 of those files and `NULL` in 11.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. The bevel and box primitives (not applicable)
3. When the list frame is drawn
4. The skin: expansion, the nine frames and the tiling rules
5. What the chapter says is dead
6. Bounds
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| bevel, fill-bevel, raised and sunken box | none | rule 2, not applicable |
| `DrawListboxFrame` when and where | none; `UiPanel.h` comments describe the `BackTile` plate (the same nine-frame layout) | rule 3 |
| the nine-frame tiling | the plate's `layoutNinePatch` (`ui/NinePatch.*`) | rule 4 |
| dead branches | none | rule 5 |

## 2. The bevel and box primitives (not applicable)

**Reference.** The two bevel helpers draw two-pixel top-left and bottom-right edges in two palette colours and
differ only in which argument is light (the "dark-first" variant swaps them, and the fill wrappers forward their
colour arguments unchanged, so the wrapper's parameter names do not describe the callee's). The raised and
sunken boxes draw a **one-pixel** edge and fill with the same colour; the sunken one reverses the edges. The
caller supplies palette indices and decides when raised or sunken applies. None validates the rectangle, installs
a clip region or changes a gadget's state.

**RWE: not applicable.** RWE draws in RGBA through `UiRenderService`; the places the original falls back to
primitives are covered by sprites (the default sprite for a missing button face, `UiFactory.cpp` about 975) and a
translucent plate with a one-pixel outline for a text box (`UiTextBox::render`, about 29, built from
`fillColor` and `drawBoxOutline`, not from the original's bevel). No palette-index argument exists to compare.
Where a visible bevel is wanted it already comes from the `BackTile` frames, which carry their own.

## 3. When the list frame is drawn

**Reference.** `DrawListboxFrame` takes the gadget rectangle from the entry table and, if the caller passes no
explicit sequence, needs a non-null object GAF holding a `Listbox` entry (otherwise it returns without drawing).
The destination is the layer's surface. It asks `FindBackgroundCell` for a background; that routine **always
returns zero** (`BT-GUI-GUI-PARITY.md` rule 7), so the background branch is never taken and the **skin branch is
the only one reached**. The list-drawing code in the generic module calls for the skin when the list has no
background taken from the layer surface **and layer flag `0x80` is clear**, and otherwise copies the background
into the rectangle. There is a final fill-and-bevel branch for a missing bitmap, which control flow cannot reach
(the earlier returns), so a missing `Listbox` entry means **no frame**, not a fallback fill.

**RWE: MISSING as a mechanism, UNCLEAR as a visible difference.** `UiListBox::render` (`UiListBox.cpp` about 20)
draws row text and a selection wash and **never a frame or a background**: there is no `Listbox` lookup, no
nine-frame skin and no fill. A list therefore shows what lies behind it, which is the panel's background art.
Whether that is the same picture as the original's depends on whether the original would have drawn the skin on
top of that art:

- **Dialogs with painted art: no difference expected.** The original's skin is drawn only when the list has no
  surface background; the full-screen menus and the dialogs with a bitmap (`LOADGAME`, `SELMAP`, `SELGAME`,
  `SELPROV` and their kin carry `panel=NULL` and take a `D...` bitmap, `createPanel` about 245) have the list's
  well painted into the art, and RWE's comments say so (`GameScene_endgame.cpp`: "OUTCOME1, whose frame has the
  list's surround ... painted into it").
- **Dialogs without art: a difference to look for.** Where `createPanel` resolves no background and falls to
  `drawSolidPlate`, the original would frame every list with the skin and RWE draws the list bare on the plate.
  Census of list-bearing panels whose `panel=` is empty and which are not full-screen (640 by 480): **`GAMMA.GUI`**
  (333 by 128, the `SCRL` list), **`LOGOSEL.GUI`** (49 by 361), **`SIDESEL.GUI`** (140 by 174), **`SHARE.GUI`**
  (233 by 396, `PLYRLIST`), **`TIMEOUT.GUI`** (391 by 325, `OUTPUT`), **`MODEM.GUI`** and **`SERIAL.GUI`**
  (501 by 154, two lists each). Whether the art search finds a bitmap for each is a render question;
  `ui_probe --panel GAMMA --render <data>` and the same for the others settles it in a minute each.
- **Not traced which of these RWE builds.** `SHARE.GUI` is not built at all (`BT-INGAME-DIALOGS-PARITY.md`
  rule 2); `GAMMA` is the front end's brightness page; `MODEM` and `SERIAL` are the retired transports.

## 4. The skin: expansion, the nine frames and the tiling rules

**Reference.** With no explicit sequence the rectangle is **expanded by three pixels on every side**; an explicit
sequence gets no expansion. A sequence of **more than one frame** takes the grid path: frame zero supplies the tile
width and height; rows use frame offsets **0 (top), 3 (middle), 6 (bottom)** and columns add **0, 1, 2 (left,
middle, right)**; interior tiles repeat and the last row and column are **pinned back to the far edge**
(overlapping the one before). **Gadget index zero tiles from the destination origin; other indices from the
rectangle's top-left.** A sequence of one frame (or none) is drawn once, frame zero, **at the destination origin
(0, 0), not at the rectangle**. Any count above one takes the grid path even if it holds fewer than nine frames,
and the missing cells are not replaced. Frame zero is dereferenced before it is checked, and a zero-sized tile
does not advance the loops.

**RWE: MATCHES the algorithm for the plate; MISSING for the list.** `layoutNinePatch` (`NinePatch.cpp` about 6)
is this layout exactly: the first row `0 1 2`, the rows after it `3 4 5`, the last pinned to the bottom edge `6 7
8`, the last column pinned to the right edge, tiles in drawing order so the pinned tile lands on top. It is used
for the `BackTile` plate under dialogs with no art (`UiPanel::render` about 61), and `UiFactory::createPanel`
(about 301) takes the nine-frame path only when the sprite series holds at least nine frames.

- **Frames 0 to 8 and the pinning: MATCHES.** (`NinePatch.test.cpp` pins the layout.)
- **The three-pixel expansion: not implemented**, because the list skin is not (rule 3). Reuse is direct: the
  rectangle expanded by three, tiles from `layoutNinePatch`, drawn behind the rows.
- **Fewer than nine frames: DIFFERS, harmlessly.** RWE falls to a single tile for fewer than nine frames
  (`UiFactory.cpp` about 308) where the original draws the grid with missing cells. No shipped data is in that
  state that the audit knows of (the `Listbox` entry itself was not examined: `commongui.gaf` was not opened
  here, which is a gap in this audit's census).
- **The origin quirk: UNCLEAR, a possible trap.** The chapter says gadget zero and the one-frame case use the
  destination origin and not the rectangle. The one-frame case is only reached for an explicit sequence and a
  one-frame file, and index zero is the panel record, which is never a list; neither is reachable from a normal
  list. A port should draw at the rectangle and not carry the quirk over.
- **Zero-sized tiles: MATCHES (hardening).** `layoutNinePatch` returns nothing for a non-positive width, height
  or step (about 8); the plate render uses `std::max(1, ...)` for the step (`UiPanel.cpp` about 66).

## 5. What the chapter says is dead

The chapter's own conclusion is that the **background-cell branch and the final fill-plus-bevel branch are
unreachable**, and that the "generic fallback" the source overview describes is broader than what runs. Two
consequences for RWE's records:

- A claim elsewhere that a list "falls back to a fill" when its art is missing would be wrong. The original
  draws nothing in that case; RWE draws nothing either, so on that point they agree.
- The type-8 gadget (surface filename) and the `texturenumber` field exist to feed the dead branch; nothing in
  the shipped data needs them (`BT-GUI-GUI-PARITY.md` rule 7).

## 6. Bounds

**Reference.** No helper validates ordering or minimum dimensions of a rectangle; small or inverted rectangles
overlap or run past the interior; destination clipping belongs to the primitive; a nine-frame skin with missing
cells or a zero tile size can misbehave.

**RWE: bounded.** The plate path guards non-positive sizes and steps (rule 4) and builds a vector of tiles. Its
one open bound is in the size of that vector: `layoutNinePatch` creates one tile per step in each direction on
every frame, so an oversized or sign-wrapped panel size (`BT-GUI-GUI-PARITY.md` rule 10) multiplies into millions
of tiles per frame. The list renderer has the related fault described in `BT-GUI-GUI-PARITY.md` rule 9 (a list
taller than its item count is safe; one scrolled past the end is not).

---

## 7. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **No list frame**: a list on a panel with no painted well is drawn bare on the plate where the original frames it with the `Listbox` skin expanded by three pixels. Candidates: `GAMMA`, `LOGOSEL`, `SIDESEL`, `TIMEOUT`, `MODEM`, `SERIAL`. | MISSING / UNCLEAR | render check first (`ui_probe --panel NAME --render`); then reuse `layoutNinePatch` with the three-pixel expansion |
| 2 | **The `Listbox` GAF entry was not examined** (its frame count and size decide whether the grid path is real). | audit gap | open `commongui.gaf` through the engine's reader and record the entry |
| 3 | **Do not port the origin quirk** (frame zero at the destination origin for a one-frame skin). | documentation | none |
| 4 | **The per-frame nine-patch vector is unbounded in the panel size.** | malformed-input fault | cap at the parser (`BT-GUI-GUI-FILE-PARITY.md` rule 10) and cache the layout per size |
| 5 | **Bevel and raised/sunken primitives have no counterpart** and need none. | not applicable | none |

No source file was changed.
