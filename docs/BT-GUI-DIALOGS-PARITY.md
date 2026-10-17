# Dialog layers, text editing, message layouts and palette tools parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-DIALOGS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one chapter of the
reference, `gui-dialogs` (49 services from `0x4AA8F0` to `0x4ACD00` plus one companion body: `LoadGuiLayer` and its
flags, the stack queries, the dynamic gadget additions, the cursor and mouse-event admission, the text-edit buffer
and its key handler, the five named dialog constructors and their handlers, `OpenMessageBox`, word wrapping and
ellipsis truncation, and the palette remap and editor primitives). This is the finding that implementation work
cites for any of them, per `AGENTS.md` and ADR-0004.

Not repeated: the in-game dialogs the chapter's callers build (the talk bar, the share dialog, TABMENU, the unit
information box: `BT-INGAME-DIALOGS-PARITY.md`), the exit and surrender prompts (`TOTALA-EXE-INTERFACE.md`
section 63 and `BT-FRONTEND-OPTIONS-PARITY.md`), the save and load screens (`BT-GAME-SAVEGAME-PARITY.md`), the
default-button inference that `LoadGuiLayer` performs (`BT-GUI-GUI-PARITY.md` rule 8) and blinking words
(`BT-GUI-FILE-LIST-PARITY.md`).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\gui-dialogs.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-176 and BT-177 (chat recipients,
  a `+command` line also sent as chat) touch the talk dialog and are not re-raised.
- **Evidence class.** A **static reading**; the chapter says no build, execution or source listing was used.
  Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `ui/UiTextBox.*`, `ui/UiPanel.cpp`, `ui/UiLabel.cpp`, `ui/UiFactory.cpp`,
  `MainMenuScene.cpp` (`openMessageBox`, `openDialog`, `onKeyDown`), `game/GameScene_menu.cpp`
  (`openConfirmDialog`, the save dialog) and the callers of `openMessageBox` at the working tree of 2026-10-17.
  Nothing was built, run or changed; every line number is "about".
- **Census** (as in `BT-GUI-GUI-PARITY.md`): of the files the chapter's constructors load, **only `MSGBOX.GUI` and
  `YESORNO.GUI` are shipped**; `CONFIRM.GUI`, `NOTEXIST.GUI`, `CHOICE3.GUI`, `INPUT.GUI` and `FILEREQ.GUI` are in
  none of the seven archives.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Loading, stacking and appending a screen
3. Dynamic additions, cursor and mouse admission (not applicable)
4. Text-edit buffers and key handling
5. The named dialog constructors
6. The message box
7. Wrapping and ellipsis truncation
8. Palette remapping and the editor primitives (not applicable)
9. Bounds
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| `LoadGuiLayer` flags, append mode, fade | `TOTALA-EXE-INTERFACE.md` section 63 (the `0x800` fade named in passing) | rule 2 |
| stack queries, `Add*Gadget`, cursor, mouse admission | none | rule 3, not applicable |
| `CommitTextEdit`, `HandleTextEditKey` | `TOTALA-EXE-INTERFACE.md` (text-edit filter for the save name), `BT-GAME-SAVEGAME-PARITY.md` rule 4 | rule 4 |
| the five named dialogs | section 63 (`CHOICE1`/`CHOICE2` under `YESORNO`) | rule 5 (a naming **CONFLICT**) |
| `OpenMessageBox` | none | rule 6 |
| `WordWrapText`, `TruncateTextWithEllipsis` | `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5 (briefing wrap) | rule 7 |
| palette remap and editor | `BT-GRAPHICS-PALETTE-PARITY.md` | rule 8 |

## 2. Loading, stacking and appending a screen

**Reference.** `LoadGuiLayer` reads `<gui path>/<name>.GUI` through the archive layer and builds a stack layer
holding the parsed records. Flags: **`0x200` append** (the parsed controls are added to the *current* layer: if
a control named exactly `PANEL` exists it is deactivated, marked `0x20`, and the imported controls are **centred
within its rectangle**, otherwise their coordinates are translated by the imported screen's own position; the
count is summed and the records copied down over their temporary header with an overlapping copy and no capacity
check); **`0x400`** skips the immediate `RenderLayer`; **`0x800` fades the covered area by minus `0x18` before
loading**, and the bit is kept in the layer's flags (so the area is repainted on close); `0x80` is kept for a new
layer only. The layer is zeroed storage for **200 records plus a `0x3F`-byte prefix**, and it does not grow.
Default accept and cancel names are inferred (`BT-GUI-GUI-PARITY.md` rule 8) and an explicit `focusName` is
searched case-sensitively. Failure handling is unsafe: a zero length leaves the local pointer unset, a failed read
frees the storage and then continues, and an immediate-render result other than one frees storage without
unwinding the stack.

**RWE: DIFFERS in mechanism (a stack of panels), MISSING for append and the fade.**

- **Stacking: MATCHES in effect.** `MainMenuScene::openDialog` (`MainMenuScene.cpp` about 655) pushes a panel
  onto `dialogStack` and routes clicks, keys and text to the top one (`topPanel`); `GameScene` has its own game
  menu panel stack. The layer storage, the 200-record limit, the failure handling and the immediate render do
  not exist.
- **Append mode: MISSING, and not needed so far.** RWE builds each screen as its own panel and adds children
  from code; nothing loads a `.gui` *into* a live panel. The original uses it to compose one panel out of several
  files (the order panel's pages, `TOTALA-EXE-INTERFACE.md` section 19). The `PANEL` centring rule is what a
  port that wanted append would need.
- **The fade: MISSING.** No dialog dims what it covers. The chapter names `YESORNO`'s and `MSGBOX`'s loads as the
  faded ones, and the register lists the options menu as "with fade" (`BT-FRONTEND-OPTIONS-PARITY.md` about 302).
  A search of the scene code finds no darkening of the world or the menu behind a dialog. Presentation only; the
  amount is a shade offset of `0x18` (24), and what that is in true colour (which table, how many steps) is not
  settled by the chapter; the simplest equivalent is a translucent black quad under the dialog, sized by a look at
  the original.
- **Explicit focus name: MATCHES.** `setFocusByName` ignores a name it cannot find (`UiPanel.cpp` about 391).

## 3. Dynamic additions, cursor and mouse admission (not applicable)

**Reference.** `AddTextGadget`, `AddButtonGadget`, `AddHotspotGadget` and `AddBarGadget` append a record of the
given type, copying a fixed number of bytes from a template (`0x13E`, `0xCC`, `0xD6`), with **no capacity check
beyond equality with 200** and no growth; names are copied with an unbounded `strcpy` into a sixteen-byte field
(the chapter notes that `Player%dController` is too long). The cursor helpers choose static or animated frames
and a hover frame. `UpdateCursorAndMouse` **consumes** a queued mouse event only when its point lies inside the
top layer's rectangle **or its key flags are zero**; a button event outside stays queued, and without a layer
nothing is consumed. The mouse-message predicates test the left branch first when both mask bits are given.

**RWE: not applicable.** Components are created by `UiFactory` and `appendChild` with no limit, and names are
`std::string`. RWE draws its own cursor and delivers a click to the top panel only, so a click outside a modal
dialog is simply lost where the original leaves it queued for the layer below (a modeless panel such as the
in-game side panel; the in-game panels are not a dialog stack and are covered by
`BT-INGAME-CONTROL-PANEL-PARITY.md`). **UNCLEAR** only for a modal dialog over the battlefield: whether a click
outside the in-game menu reaches the world in the original is the mouse chapter's question, not audited here.

## 4. Text-edit buffers and key handling

**Reference.** The cursor is **one shared index in the global context**, not per control. `CommitTextEdit` puts it
at the end of the existing string, or empties the string when it is longer than the maximum. `HandleTextEditKey`
edits the focused text input in place: Home and End, Left and Right, Backspace (shifting later bytes left),
Delete, **paste** (Windows clipboard `CF_TEXT`; two key codes), Escape (stop and return Escape **without
restoring the old text**), and printable codes `0x20` to `0x7F` (insertion at the cursor). The insertion rules:
rejected when the **length equals the status** (the capacity, not "greater than or equal"); attribute bit two
restricts to alphanumerics, underscore, space and apostrophe; and the existing text's pixel width plus the new
character's must **fit the gadget width minus four**. Paste: clears 128 bytes, copies the smaller of the clipboard
size and `status - 1`, trims trailing characters until the string fits the **full** gadget width, and **replaces the
whole content**, without the character filter and without moving the cursor (which can stay past the new end).
Any nonzero initial code marks the control changed, even one that cannot alter text, and the handler drains every
queued key whatever mode it started in.

**RWE: MATCHES for typing and Backspace; MISSING for everything that moves a caret; DIFFERS for fit and paste.**

- **Typing: MATCHES.** Composed text arrives as SDL text input and is filtered to printable ASCII, one byte a
  glyph (`UiTextBox::textInput`, `UiTextBox.cpp` about 86); a full box ignores more (`size() >= maxLength`, the
  stricter of the two comparisons).
- **Caret, Home, End, Left, Right, Delete, and insertion in the middle: MISSING.** `UiTextBox::keyDown` (about
  69) handles Backspace only, removing the last character; text is only ever appended. The caret is drawn at the
  end of the text (about 52). So a typo in the middle of a nickname, a save name or an address is repaired by
  deleting back to it. The shipped boxes are short (the longest `maxchars` is 63) which limits the harm.
- **The pixel-fit rule: DIFFERS.** The original refuses a character once the text would reach the box width minus
  four, so a box holds what shows. RWE keeps typing to `maxLength` and **scrolls the view to the end**
  (`render` about 41 to 48). The shipped boxes where it matters: `NEWMULTI`'s nickname and password (112 pixels,
  `maxchars` 10, so ten capitals can exceed the box in the original and stop at about nine) and the 230-pixel
  save-name boxes (20).
- **Attribute-bit-two filter: not applicable on retail data.** The census of text inputs finds attributes 0 (14)
  and 1 (3, the chat bars); no shipped box sets bit two. The save-name rule is in
  `BT-GAME-SAVEGAME-PARITY.md` rule 4.
- **Paste: DIFFERS.** Ctrl+V is handled in the **front end only** (`MainMenuScene::onKeyDown`, about 697): it
  strips line breaks and **appends** the clipboard through the same filter and limit. The original replaces the
  whole content, unfiltered. In a game (the save-name box, the chat bar, `GameScene_input.cpp`) there is no paste.
  The front-end choice was deliberate (the 13-character invite code, comment at about 697) and is better than the
  original for an invite code pasted into an empty box; the difference shows when the box is not empty.
- **Escape: MATCHES in effect.** Escape is the panel's cancel default where enabled; the box keeps its text, as
  the original does.
- **Return: DIFFERS.** The original's panel handler clears focus and reports activation on Return and, on
  Escape, **empties the text** and activates; RWE's Return presses the panel's accept default where enabled
  (`BT-GUI-GUI-PARITY.md` rule 8) and leaves the text. The empty-on-Escape half matters only to a dialog that
  keeps the box after cancelling.
- **`CommitTextEdit`'s truncation: MATCHES in effect.** A value longer than the limit is cut when set
  (`setMaxLength` cuts, about 116; `setText` does not, an unbounded set from code).

## 5. The named dialog constructors

**Reference.** Five constructors, each loading one file and copying caller strings into inline fields with
no length check, all asynchronous (they return success, not an answer): `OpenConfirmDialog` (`CONFIRM.GUI`, fills
`TITL`), `OpenYesNoDialog` (`YESORNO.GUI`, fade flag, fills `CHC1`, `CHC2` and `TITL`; its parameter order supplies
`CHC2` before `CHC1`; installs a handler), `OpenNotExistDialog` (`NOTEXIST.GUI`, fills `NAME`), `OpenChoice3Dialog`
(`CHOICE3.GUI`, runs `RenderLayer` initialisation a second time, fills three choices and a title) and
`OpenInputDialog` (`INPUT.GUI`, fills a text input `INPT`, a title and two choices; its last parameter is unused
and it does not call `CommitTextEdit`). The handlers only call name tests and **discard the results**; none sets
an answer, clears the hot gadget or sends anything, so the generic frame closes the dialog after activation
unless another owner intervenes, and consumers install their own callbacks.

**RWE: MATCHES where used (`YESORNO`), not applicable for the other four, one naming CONFLICT.**

- **`YESORNO`: MATCHES.** `GameScene::openConfirmDialog` (`GameScene_menu.cpp` about 387) loads the file,
  writes the question into the label and routes `CHOICE1`/`CHOICE2` to its own callbacks (about 1049 to 1061).
  The label alignment and size are `BT-GUI-GUI-PARITY.md` rule 6.
- **CONFLICT: the gadget names.** The chapter's constructor fills **`CHC1`, `CHC2` and `TITL`**. The shipped
  `YESORNO.GUI` (the only copy in the install) names the records **`CHOICE1`, `CHOICE2` and `TITLE`**, which
  `TOTALA-EXE-INTERFACE.md` section 63 also uses and RWE routes on. Either the chapter abbreviates (the likely
  reading: the prose shortens names throughout), or its constructor uses a substring search that would not find
  these, or its data differs from the retail file. Neither side is chosen; a spec-room read of the
  constructor's lookup strings settles it.
- **`CONFIRM`, `NOTEXIST`, `CHOICE3`, `INPUT`: not applicable.** The four files are not shipped, so on a
  retail install the constructors fail at the load. RWE builds none. (Whether the original's menus ever reach
  them is the callers' question, not this chapter's.)
- **The auto-close after an activation: DIFFERS in mechanism, MATCHES where each handler pops.** See
  `BT-GUI-GUI-PARITY.md` rule 2.

## 6. The message box

**Reference.** `OpenMessageBox` loads `MSGBOX.GUI` (with the fade), translates the text into a 256-byte buffer,
**word-wraps it to the given width**, empties the layer's earlier controls, and appends **one `TEXT` label per
line** (empty lines skipped, a carriage return from the wrap left on each token), the first at y 20 and each
next one **font height plus five** lower. The dialog is then **sized to the text**: the parameter named
`autoHeight` actually selects width (nonzero: the widest line plus twenty; zero: the supplied wrap width) and the
height is always **line count times 25 plus the OK control's height plus forty**; the rectangle is centred on the
screen, every label is given the screen's width and attribute two (centre), and the OK button is moved near the
lower right. A second parameter, `centre`, decides whether OK is available: nonzero finds it, places it and makes
it both the accept and the cancel default; zero **deactivates** it. The call finishes by enabling keyboard input,
installing the handler, marking changed and running **one `UpdateMenu` immediately**, which can already close
the box before the call returns.

**RWE: DIFFERS. A fixed plate, one centred line, no wrap.**

- **Size: DIFFERS.** `MainMenuScene::openMessageBox` (about 520) builds `MSGBOX.GUI` as shipped (a 372 by 272
  plate at (116, 82) with the OK button, per the code's comment) and puts **one** `Center`-aligned label in it.
  The original's box for a one-line message is far smaller (roughly 25 + button + 40 pixels high) and sized to
  the text.
- **No wrap: DIFFERS, and visible.** The label is a single line (`drawTextCentered`, centred on the plate's
  middle, `UiLabel.cpp` about 22) with no wrapping or width limit. Many of the strings sent to it are long:
  `"Could not read " + mapName + ": " + e.what()` (`MainMenuScene.cpp` about 1572, and
  `MainMenuScene_campaign.cpp` about 310, where `e.what()` is an exception message from the file that failed),
  `"That game is version <x> and this is <y>"` (`MainMenuScene_multiplayer.cpp` about 2133), `"You do not have the
  map " + game.map` (about 2152, a map name from the network), a problem string from the skirmish checks (about
  1953 and 2005) and the multiplayer start message. Any of them wider than 372 pixels runs past both edges of
  the plate. The original wraps at spaces, hyphens and line feeds to the wrap width.
- **The text is not sanitised for length.** `e.what()` and a peer's map name are unbounded; a long one is drawn
  as one very long line (the label itself is safe: `ensureUtf8` makes it walkable).
- **`OK` as both defaults and its availability: UNCLEAR.** RWE's OK is a plain button; whether Return and Escape
  close the box depends on `setDefaultKeysEnabled`, which `openMessageBox` does not call (it is not among the five
  sites), so **Return does not dismiss a message box** unless the focused control takes it. The original makes OK
  the accept and cancel default when `centre` is nonzero.
- **The immediate update: not applicable.**

## 7. Wrapping and ellipsis truncation

**Reference.** `WordWrapText` estimates the buffer as source length plus three per estimated line plus two (the
estimate divides by the width of the letter `d` and so divides by zero for a font without one) and copies one
character at a time to the NUL or `0xFF`. When the next source character is a **space, a newline or a hyphen**
it measures the accumulated line; at or above the width it backs up to a previous **space or hyphen, removes
that break character**, inserts a carriage return and a line feed and resumes after it. An existing newline
restarts the measurement. A word wider than the line is **never split**; and the backtrack has no start guard, so
a too-wide first word reaching a delimiter reads before the source. `TruncateTextWithEllipsis` leaves text
**strictly narrower** than the limit alone; otherwise it reserves the width of three dots, drops characters from
the end until the rest fits, and appends the dots even if they alone exceed the limit; text of **exactly the limit**
gains dots.

**RWE: DIFFERS in the rules, MATCHES in spirit.**

- **Wrapping: UNCLEAR.** `UiLabel` left-aligned labels call `drawTextWrapped` (`UiLabel.cpp` about 19), the
  briefing uses `UiTextRegion` (`BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5 holds that comparison). Neither
  treats a hyphen as a break that is removed; both break only between words. Whether the shipped strings contain
  hyphens at a wrap point is the data's question; the observable difference is a hyphenated word or "Stand-by"
  style term that the original splits at the hyphen and RWE keeps whole. No census was taken.
- **Ellipsis: MATCHES with one edge.** `UiLabel::fittedToWidth` (about 28) drops whole characters (never a stray
  UTF-8 continuation byte) until the rest and "..." fit, and appends the dots at the end. Text of exactly the
  limit is left alone in RWE (`<=`) and gains dots in the original (`<`); one pixel in one case in a million.
- **The bounds faults: not applicable.** The divide by zero and the read before the buffer are `std::string`
  impossibilities here.

## 8. Palette remapping and the editor primitives (not applicable)

**Reference.** `BuildColorRemapTable` maps each of 256 source colours to the nearest of 256 destination colours
by the **sum of absolute RGB differences** (not Euclidean, not weighted), a strictly smaller distance replacing
the winner so **ties keep the first index**, flags ignored; `RemapPaletteToClosestIndices` copies a 1024-byte
palette into the context and fills the 256-entry remap; `CopyPaletteEntries` returns it; the colour grid, cell
outline, cell-at-point (clamped to 0..15 on each axis), RGB display on `RED`, `GREN`, `BLUE` sliders, slider to
palette entry, and `LerpPaletteRange` form a small palette editor. `LerpPaletteRange` copies **1024 bytes into a
768-byte local** in both directions, an overflow on every call. `WriteTabs` writes indentation one byte at a time.

**RWE: not applicable.** The interface is drawn in true colour from decoded sprites; there is no runtime palette
to remap or edit, and no `RED`/`GREN`/`BLUE` screen is shipped (the census finds no gadget so named). If a
nearest-palette conversion is ever needed (a mod that supplies an indexed image), the metric is the sum of
absolute differences with the first index winning ties. `BT-GRAPHICS-PALETTE-PARITY.md` holds the palette rules
that do apply.

## 9. Bounds

**Reference.** Unbounded copies throughout (names, titles, clipboard, token buffers), the 200-record layer and the
palette overflow above.

**RWE: bounded for strings; one input-sized hazard.** All text is `std::string`. The one place where a
caller-supplied string sets a size is the message box, which draws a single line of any width (rule 6), and
`UiTextBox::setText`, which sets any length from code (the shipped callers pass short names). The GUI-file
bounds are `BT-GUI-GUI-PARITY.md` rule 10 and `BT-GUI-GUI-FILE-PARITY.md` rule 10.

---

## 10. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A long message-box text runs off the plate**: no wrap and no sizing, and several callers pass an exception message or a peer's map name. | DIFFERS | code: wrap to the plate width in `openMessageBox` (the original wraps at spaces, hyphens and line feeds to a width and sizes the box to the lines) |
| 2 | **Return does not dismiss a message box** (`openMessageBox` never enables the panel's default keys). | DIFFERS | code: `setDefaultButtons("OK", "OK")` and `setDefaultKeysEnabled(true)` as the original makes OK both defaults |
| 3 | **Text boxes have no caret movement, Delete or mid-string insertion**; a typo is repaired by deleting back to it. | MISSING | code: a per-control caret in `UiTextBox` (the original's is a global; RWE's can be per control) |
| 4 | **Paste replaces nothing and exists in the front end only**; the original replaces the whole content, unfiltered, in every text box. | DIFFERS (deliberate in part) | decision; at least add Ctrl+V to the in-game boxes |
| 5 | **No dim behind a dialog**: the original fades the covered area for `YESORNO`, the message box and the options menu. | MISSING (presentation) | code, once the amount is known from a look at the original |
| 6 | **CONFLICT: `CHC1`/`CHC2`/`TITL` against the shipped `CHOICE1`/`CHOICE2`/`TITLE`.** | CONFLICT (documentation) | spec-room read of `OpenYesNoDialog` |
| 7 | **The pixel-fit rule**: the original stops typing when the text fills the box; RWE scrolls. | DIFFERS, minor | none unless a screen needs the original's cap |
| 8 | **Hyphens are break points in the original's wrap and are removed**; RWE's wrap does not split at them. | UNCLEAR | a census of shipped dialog and briefing strings with hyphens |
| 9 | **`CONFIRM`, `NOTEXIST`, `CHOICE3`, `INPUT` and `FILEREQ.GUI` are not shipped**; the constructors are dead on retail data. | documentation | none |

No source file was changed.
