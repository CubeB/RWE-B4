# GUI contexts, layers, gadget state, rendering and input parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-CONTROL-PANEL-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one chapter of the
reference, `gui-gui` (the runtime half of the interface toolkit, `0x49F8C0` to `0x4AA8F0`: the context, the
layer stack, name lookup, focus, active and greyed state, every gadget family's drawing and input, quick keys,
`RenderLayer`, `CloseTopScreen`, `HandleGuiCommand` and the per-frame `UpdateMenu`). This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The record parser is `BT-GUI-GUI-FILE-PARITY.md`, the list skin is `BT-GUI-LISTBOX-PARITY.md`, the dialog
constructors and text editing are `BT-GUI-DIALOGS-PARITY.md`, and blinking words are in
`BT-GUI-FILE-LIST-PARITY.md`. Input production (keyboard, mouse) is not audited here. Rules that the register
already holds are not repeated: the order panel (`TOTALA-EXE-INTERFACE.md` section 19 and
`BT-INGAME-CONTROL-PANEL-PARITY.md`), the caption shadow, the quick-key underline and the three colour tricks
(section 99), the options screens (sections 62 to 69 and `BT-FRONTEND-OPTIONS-PARITY.md`) and the exit flow
(section 63).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\gui-gui.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row concerns the toolkit
  (BT-103 and BT-105, the build-button greying, are about the order panel and are not re-raised).
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the chapter says no
  interaction was performed. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner
  picked.
- **No executed observation of the original was used.** Several findings are about what a player sees; the Gaps
  table says which want a render check (`ui_probe --panel NAME --render <data>`) or an original-game look.
- **The RWE half** is a read-only audit of `ui/UiFactory.cpp`, `ui/UiPanel.*`, `ui/UiComponent.*`,
  `ui/UiLabel.cpp`, `ui/UiStagedButton.cpp`, `ui/UiScrollBar.cpp`, `ui/UiSlider.cpp`, `ui/UiSurface.*`,
  `io/gui/gui.*`, and the panel-building sites in `MainMenuScene*.cpp` and `game/GameScene_menu.cpp` at the
  working tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in the
  tree, so every line number is "about". **Every statement about what RWE draws is a reading of the code, not
  a screenshot.**
- **A census of the shipped GUI files** (the seven archives of the retail install through `tools/hpi.py`'s
  reader, throw-away scratch scripts, later archives overriding earlier ones by path): **186 distinct `.gui`
  paths** (102 from `totala1.hpi`, 7 from `ccdata.ccx`, 12 from `btdata.ccx`, 65 from `rev31.gp3`), holding 186
  panels, 1938 buttons, 40 list boxes, 17 text inputs, 63 sliders, 218 labels, 56 hotspots, 105 font records
  and 10 picture boxes.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Contexts, the layer stack, resource paths and the modal loop (not applicable)
3. Name lookup, duplicate names and the associations
4. Which gadgets are alive: `active`, greyed, outside the dialog
5. Buttons: attribute bits, stages and quick keys
6. Labels: alignment, links and who takes a click
7. Fonts, colours and texture numbers
8. Placement, default buttons, initial focus and keyboard navigation
9. Sliders, scroll bars and list boxes (the dispatch half)
10. Bounds on a malformed `.gui`
11. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| context, paths, modal loop, dirty flags | none (the toolkit is not in `TOTALA-EXE*.md` outside sections 61 to 69 and 99) | rule 2, not applicable |
| name lookup, associations | none | rule 3 |
| active, greyed, outside the surface | `UiFactory.cpp` comment on ARMOPT's caption (hide outside the dialog) | rule 4 |
| button attributes, stages, quick keys | section 99 (caption shadow, underline), section 19 (order buttons) | rule 5 |
| labels | none | rule 6 |
| fonts, colours | section 99 ("three colour tricks") | rule 7 |
| `RenderLayer` placement, `HandleGuiCommand`, `UpdateMenu` | sections 61 to 63 (specific screens) | rule 8 |
| slider, list box input | none | rule 9 |

## 2. Contexts, the layer stack, resource paths and the modal loop (not applicable)

**Reference.** One global context holds the top layer, the font and glyph slots, the focus, hot and hover
indices, the key-command flag and the three resource paths (fonts, GUI, animations; at most 256 bytes each with
a backslash appended and no room check). A layer is a stack node with its gadget array, a click handler, flags,
dirty state and a "keyboard input" mode. Layers draw oldest first; a layer with a "save under" copy restores
what it covered when closed; `CloseTopScreen` clears hover, hot and focus, calls the layer's handler (which
therefore sees hot index minus one), cleans up and repaints what was underneath. A modal helper loops while a
named layer is on top. A frame (`UpdateMenu`) pops one key, runs the generic command table, scans the active
gadgets in order and stops at the first activation, calls the click handler, **and closes the top screen
automatically if the hot index is still set afterwards** (a handler keeps a dialog open by clearing it).

**RWE: not applicable, one rule worth keeping.** RWE keeps a stack of `UiPanel` objects per scene
(`MainMenuScene::panelStack`, the game menu panels), redraws everything every frame, owns no save-under
surfaces and no modal loop, and finds its files through the virtual file system (`pathMapping`). A click's
closing behaviour is each scene's own message handling (`MainMenuScene*.cpp`, `GameScene_menu.cpp`), not a generic "close unless the handler objects". That is a
structural difference with no player-visible effect so long as each handler pops what the original would have
closed; it is the reason a new screen cannot rely on a default. The chapter's path and surface faults do not
exist here.

## 3. Name lookup, duplicate names and the associations

**Reference.** Name searches compare the **first sixteen bytes, case-sensitively**, over records one to the
count, **first match wins**, and most ignore the gadget type (the "checked" variants only add a fatal error on
a miss). The associations (`assoc`, a byte) group gadgets: a slider moves an equal-association list, a list
mirrors its selection to equal-association lists, the radio-button clear acts on **nonzero** associations
only, and the arrow-button helpers find "the first gadget of a type with this association" returning **zero
on a miss**, which several callers then test against minus one.

**RWE: MATCHES for the shipped data, with two differences in semantics.**

- **Lookup: MATCHES.** `UiPanel::find<T>` returns the first child, in file order, whose type is `T` and whose
  full name is equal (`UiPanel.h` about 150); `setFocusByName` and `removeChildrenNamed` are also exact. No
  shipped gadget name is over sixteen bytes (census: none), so the truncation rule has nothing to act on.
- **Duplicate names: MATCHES in effect.** 39 shipped files repeat a name (the ARM and CORE builder pages repeat
  `igpatch`, `ALLIES.GUI` repeats `allyx` and `teamiconsx`, `ANYMSN.GUI` repeats `knob` and `text`). Both take
  the first. `removeChildrenNamed` removes **all** of them where the original's renaming touches the one it
  found; no RWE call site relies on the difference.
- **Type filter: DIFFERS, harmless.** RWE's lookup is typed; the original's is not, so a name that exists with
  another type is found by the original and missed by RWE. The scenes ask for the type the data has.
- **Association zero: DIFFERS in meaning.** The original's "no association" is zero. RWE's group is simply the
  number (`elem->setGroup(entry.common.assoc)`, `UiFactory.cpp` about 114) and a list box accepts a scroll
  message from any control whose group **equals its own, including zero** (`UiListBox::uiMessage`, about 321).
  Census: four panels hold a slider or list with association zero (`SELVMODE` `VIDSLDR`, `TIMEOUT` `OUTPUT`,
  `VISUALRT` and `VISUALS` `GAMMA`) and none holds two of the kinds that would answer one another, so nothing
  is cross-wired today. A new screen that put a scroll bar and a list both at zero would be.
- **Radio clearing: not applicable as a generic rule.** 110 (file, association) radio groups exist in the
  shipped data; RWE's `BehaviorMode::Radio` sets only its own state and the scenes clear the others by name.

## 4. Which gadgets are alive: `active`, greyed, outside the dialog

**Reference.** A gadget's `active` byte decides whether it is drawn and dispatched: `RenderLayer` and
`UpdateMenu` visit **active gadgets only**; `SetGadgetActive` also propagates to a slider's arrow buttons and a
disabled list disables its slider. Greyed (`grayedout`) is a separate state with a per-type implementation
(button low bit, list attribute `0x100`, slider lock plus arrow bits, label low bit, frame flag) and the chapter
says dimmed, locked and inactive are three different states.

**RWE: DIFFERS. `active` is read and never used.**

- **`active=0`: MISSING.** `GuiCommon::active` is parsed (`io/gui/gui.cpp` about 95) and nothing in `ui/` reads
  it. **Census: 86 gadgets in 19 files carry `active=0`**: 51 buttons, 25 labels, 3 sliders, 3 lists, 3
  hotspots and a picture box. Several are screens RWE builds: `RESTART.GUI` (`HEADING`, the label reading
  "Restart"), `HELP.GUI` (`TEXT`, "Keyboard Commands"), `BLANK.GUI` (`TEXT`, "Blank GUI"), `MAINMENU.GUI`
  (`DebugString`, "Debug Build"), `SELPROV.GUI` (`SERVICEX`, the "BUTTON NAME" template button),
  `SINGLE.GUI` (`AnyMsn`), `TABMENU.GUI` and `SCORE.GUI` (`CANCEL`), `EXITMENU.GUI` (`RESTART`), the lobby's
  `MEMx`, `PINGx`, `RESx`, `SIDEx`, `ALLYx`, `PLAYERx` templates, and the campaign screens' `Side0` to
  `Side4`, `Campaign`/`Missions` lists and knobs. The original draws and clicks none of these. In RWE a few are
  handled by name where a scene noticed (the main menu **reuses** `DebugString` for the revival title,
  `MainMenuScene.cpp` about 735; SELPROV removes `SERVICEX`, `MainMenuScene_multiplayer.cpp` about 1321; the
  lobby hides its templates), and the rest are drawn and clickable. `RESTART.GUI`'s "Restart" heading is the
  easy one to see.
- **Greyed: MATCHES for buttons, MISSING for other types.** `grayedout=1` on a button comes up disabled
  (`buttonFromGuiEntry`, about 728). **93 shipped buttons in 37 files** are greyed from the start (the
  download-menu builder pages carry five or six each). The key is read for no other type; shipped data sets it
  only on buttons (the key appears in the button bodies alone), so nothing else is lost.
- **Hidden outside the dialog: DIVERGENCE (documented in the code, a reasonable reading).**
  `hideIfOutsideDialog` (`UiFactory.cpp` about 51) hides a gadget wholly outside the panel's rectangle; the
  census finds five (the ARMOPT caption `TEXT` at x 161 in a 128-wide dialog, three `FONT` records in
  `BRIEFING.GUI`, and the `HEADER` record of `TALK2.GUI`). The chapter says nothing of this; the original's
  behaviour follows from the dialog surface being its own width and height.

## 5. Buttons: attribute bits, stages and quick keys

**Reference.** Button behaviour is chosen from the attribute word in a fixed order: `0x10` radio, `0x40` toggle
(pressed look while held, flips on a valid release), **`0x08` toggle on release inside**, `0x100` cycle the
status through the art's frame count on a left click, and otherwise a momentary press that, on a valid
release, **advances the stage index** for an ordinary multi-stage button and drives an associated slider for
masks `0x1800`. Arrow-button auto-repeat uses a shared fifteen-tick countdown (`0x2000`). Alignment is `1` left,
`2` centre; `0x20` is the build-button bottom-centred caption; `0x400` removes a button from the arrow-key
neighbour search; `0x4000` marks the stage-button face art; `0x10000` stops the automatic quick-key assignment.
A multi-stage caption is the text split at the pipe into consecutive strings. Button art is found by name in the
screen's GAF, then the common GAF, then `CHECKBOX`, `stagebuttn` plus the stage count (capped) or `BUTTONS0`, the
**nearest face by width and height difference** being taken from the grouped fallback frames, and the gadget's
width and height are **replaced by that frame's**.

**RWE: MATCHES the common cases; DIFFERS on `0x08`, `0x80`, auto-assigned keys and the stage click.**
`buttonFromGuiEntry` (about 678) maps `1`/`2` to alignment, `0x10` radio, `0x20` bottom-centre, `0x40` toggle and
`0x100` cycle; `getDefaultButtonGraphics` (about 904) takes the nearest `BUTTONS0` face within eight pixels of
width and two of height, preferring height (a deliberate approximation of the chapter's nearest-face rule, and
the 95-by-20 and 119-by-21 cases in the comments are real).

- **Attribute `0x08` is a shadow flag only: MISSING as a behaviour.** `GuiButtonAttrib::CaptionShadow` is 8
  (`io/gui/gui.h` about 34), set from section 99. The chapter reads the same value as a **toggle on release**.
  The two readings are compatible (the value is one bit, and shipped data gives it exactly to toggle-like
  buttons), but RWE implements only the drawing half. **Census: 28 buttons**: the thirteen `CHATSEL.GUI`
  recipient buttons, the lobby's `READYx` (both lobby files) and `ALLYx`, `SENDTO` of `TALK.GUI`, and `SENDTO`
  with the ten `PLAYERx` buttons of `TALK2.GUI` (their attributes are `10`, `138`, `16393` and `16394`). RWE builds none
  of `CHATSEL` or `TALK` (the chat bar is its own, `BT-INGAME-DIALOGS-PARITY.md` rule 3) and the lobby toggles
  its own seats, so nothing visible is lost; a screen built from these files would not toggle.
- **`0x80` and `0x4000` and `0x400`: UNCLEAR.** 19 buttons carry `0x80` (the values `192`, `138`), which the
  chapter's ordered list does not name; 20 carry `0x4000` (`WATCHING`, `MAPPING`, `CHEATING`, `GAMEOPEN`,
  `SHUTUP`, `SHARUNIT`, `MAPINFO`, `SENDTO`, the `PLAYERx` of `TALK2`), which the chapter says selects stage
  art and which RWE ignores (its two-stage face comes from the `stages` key); four carry `0x400` (`Credits`,
  `Side0`, `Side1`, `AnyMsn`), which only matters to the arrow-key search RWE does not have (rule 8). No shipped
  button carries `0x800`, `0x1000`, `0x2000` or `0x8000`, so the arrow-repeat masks are dead data.
- **Clicking a multi-stage button: DESIGN difference.** The original advances the stage on a valid release;
  `BehaviorMode::Staged` in RWE does not (`UiStagedButton::activateButton`, about 258) and about thirty places in the
  scenes (a grep for `nextStage`, `setStage` and the cycle mode) advance it themselves (`MainMenuScene_campaign.cpp` about 249 for the campaign
  difficulty). Equivalent where each scene remembered; a new multi-stage button silently does not cycle.
- **Letter quick keys are dropped: DIFFERS (a real bug on one shipped screen).** The `quickkey` key is read
  as text by the original: an alphabetic first byte is stored as its code, anything else goes through `atoi`
  (`BT-GUI-GUI-FILE-PARITY.md` rule 4). RWE reads it with an integer extraction, which **fails on a letter**, so
  the key is absent. **Census: six gadgets write a letter**: `OPTIONS` `O` and `ALLIES` `A` in `ARMMAIN.GUI` and
  `CORMAIN.GUI`, and `OK` `N` ("Next") and `CANCEL` `P` ("Previous") in `NEWMULTI.GUI`. `NEWMULTI` is built by
  `makeNewGamePanel` (`MainMenuScene_multiplayer.cpp` about 1424), and no other site sets a quick key, so **N and
  P do nothing on the New Game screen**. (`ARMMAIN` and `CORMAIN` are not built by name in the tree.)
- **Negative and control quick keys: UNCLEAR.** The census holds `-68` on 26 buttons (`CHATSEL`, `TALK2`,
  `SELPROV` `SERVICEX`), which as a byte is `0xBC` and would be the comma key's code in the original's key
  space; RWE's `convertQuickKeyToSdlk` (`UiFactory.cpp` about 25) leaves it negative and nothing matches it.
  Codes 8, 13, 27 and 32 are Backspace, Return, Escape and Space and **do** equal SDL's keycodes. The
  conversion's "uppercase" window is 65 to 132, which also lowercases `[ \ ] ^ _` and the backquote and braces
  by accident; no shipped value falls there.
- **Automatic quick keys: UNCLEAR.** The original assigns the first unused non-space character of a button's
  text when the text is set at run time (`AssignQuickKey`, with `0x10000` as the opt-out). RWE only honours the
  file's `quickkey`. 711 of 1753 shipped buttons say zero. Whether the original's `RenderLayer` also assigns on
  load for buttons is not stated (it says so for labels), so whether a run-time-labelled RWE button should get
  a key is open.
- **Alt: MISSING.** The original accepts a quick key only without Alt held and with no focused text input; RWE
  ignores Alt (`UiPanel::keyDown` about 219).

## 6. Labels: alignment, links and who takes a click

**Reference.** A label's attribute word chooses **left (1), centre (2) or right (4)** alignment within its
rectangle (`LayoutLabelText` mutates the gadget's X and width from the measured text); an X of minus one centres
a label in its panel; text longer than the width is wrapped; a label with a **link** names another gadget and,
when clicked, activates or focuses it (a text input is selected and initialised); labels are drawn in glyph
slot one and then slot zero is restored. Unlinked labels in `RenderLayer` are disabled by attribute `0x10` or
given a quick key.

**RWE: DIFFERS. Label alignment is not read, and "centred" means something else.**

- **Alignment from the file: MISSING.** `labelFromGuiEntry` (`UiFactory.cpp` about 744) passes only position,
  size and text; `UiLabel` defaults to **left** and nothing in the factory sets another alignment. **Census:
  106 of the 218 shipped labels are centre-aligned (`attribs` bit 1 value 2), 6 are right-aligned (value 4) and
  104 left; 45 files hold a centred label** (the larger counts are `ALLIES` 11, `RESTRICT2` 12, `SPEEDS` 7,
  `VISUALS` 6, then the side pages, the option pages, `RESTART`, `YESORNO`, `MSNBRIEF`, `CREDITS`). The
  right-aligned ones are the lobby's `MEMx` and the `ENERGYTEXT`, `METALTEXT` and `MAXUNITSTEXT` value fields.
  Outside the two sites that call `setAlignment` for their own labels (`MainMenuScene.cpp` about 539 and 738)
  and `createLabel`, every file label draws from its left edge.
- **"Centre" is on a point, not within the width: DIFFERS.** `UiLabel::render`'s `Center` case calls
  `drawTextCentered(posX, posY, ...)`, centring on the label's own X (the comment at `MainMenuScene.cpp` about
  529 says so). The original centres within the rectangle. Fixing the first without the second would put a
  `YESORNO` question half off its box.
- **A verified example.** The shipped `YESORNO.GUI` has four records and **does** carry a `TITLE` label (x 25,
  width 350, attribute 2, text "Title"), where `GameScene::openConfirmDialog` (`GameScene_menu.cpp` about 393)
  says the file has no such gadget and appends its own centred label only when `find("TITLE")` misses. With the
  real file the question is set into `TITLE` and so **drawn left-aligned at x 25** instead of centred across the
  350 pixels. Reading, not a screenshot: `ui_probe --panel YESORNO --render` settles it.
- **Links: MISSING, and visible on the skirmish screen.** 71 label records in 14 files carry a `link` key and
  **15 of them are non-empty: ten in `LOUNGE2.GUI`** (the captions of `METAL`, `ENERGY`, `MAXUNITS`,
  `COMMANDER`, `FIXEDLOC`, `MAPPING`, `LOSTYPE`, `CHEATING`, `WATCHING`, `GAMEOPEN`) **and five in
  `SKIRMISH.GUI`**: "Location" names `StartLocation`, "Commander" `CommanderDeath`, "Mapping" `Mapping`,
  "Line of Sight" `LineOfSight` and "Difficulty" `Difficulty`, each with attributes `17`. The original makes
  the caption clickable (a click on the word activates the option button beside it). RWE reads no `link`
  (`BT-GUI-GUI-FILE-PARITY.md` rule 5), so **clicking "Location", "Commander", "Mapping", "Line of Sight" or
  "Difficulty" on the skirmish setup screen does nothing**, and the lobby's ten captions likewise.
  `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` does not mention it.
- **Labels take clicks: DIFFERS.** `UiPanel::mouseDown` (about 148) gives the click to the first visible child
  whose rectangle contains the point, **a label included** (only `UiSurface` overrides `contains`); the original
  dispatches to a label only when it has a link. A label drawn before a button and overlapping it therefore
  swallows the click. **Census: four overlaps in three files**, each two to four rows deep: `RESTART.GUI`
  `Adjust` over `Difficulty`, `SKIRMISH.GUI` `HELPTEXT` over `Difficulty`, `VISUALRT.GUI` `TEXT` over `SHADING`
  and over `BSHADOWS`. A few pixels of the top edge of the skirmish Difficulty button are dead.
- **Wrap and clip: DIFFERS.** `UiLabel` wraps left-aligned text (or ellipsises a single line, `fittedToWidth`);
  the original wraps at spaces and carriage returns only, treats a line feed as ordinary, and stops at the
  vertical space. The ellipsis is RWE's own.

## 7. Fonts, colours and texture numbers

**Reference.** Every text gadget selects its font from the layout: `SelectFontForEntry` takes the
`fontnumber`-th **type-7 record** of the screen and installs the font it loaded (from the configured font
directory plus the record's name and `.FNT`), else the global default; `colorf` and `colorb` are palette indices
used by the primitive-drawing and bitmap-font paths; `texturenumber` is an index into the type-8 records
(`FindBackgroundCell`, which in this build **always returns zero**, so the background-cell branches are dead).

**RWE: DIFFERS in method, MATCHES in the shipped result for text; UNCLEAR for colour.**

- **One face for everything.** Every label, button, text box and list is built with `hattfont12` entry
  "Haettenschweiler (120)" (`UiFactory.cpp` about 337, 372, 627, 738, 746, 822). Type-7 records become an inert
  1-by-1 `UiComponent` (`componentFromGuiEntry`, about 674). **Census: 105 type-7 records**, their `filename`
  values `armbutt` and `armfont`-style names on the side builder pages; **15 gadgets carry a non-zero
  `fontnumber`** (and one panel record the debug fill) (value 9 on six font records, two buttons and two hotspots; minus 124 on four hotspots; 1 on
  one label). So the builder pages' per-side font is not used (the build pictures carry their own cost text
  elsewhere, `BT-INGAME-CONTROL-PANEL-PARITY.md`), and a label that asks for font 1 gets the default.
- **Which cut: UNCLEAR.** The chapter has labels draw in glyph slot one and buttons, lists and inputs in slot
  zero, and the application loads `hattfont12` and `hattfont11` into the two slots (`BT-GUI-GUI-FILE-PARITY.md`
  rule 9) without saying which is which. The register shows the original uses both cuts (the Space strip,
  `TOTALA-EXE-INTERFACE.md` section 108) and a play-test named "the beige outlined font the gui labels use"
  without a size. If the label slot is the smaller cut, every RWE label is one size too large. A side-by-side
  render of a label and a button on one panel settles it.
- **`colorf` and `colorb`: UNCLEAR.** Read (`io/gui/gui.h`), unused. 1740 buttons, all 186 label records
  (`colorf` 15 on 186, 0 on 32) and 63 sliders carry a non-zero colour; six records hold `0xCDCDCDCD`
  (minus 842150451) in both, the debug-heap fill byte. Whether the colour index matters for glyph-font text
  (the shipped font is a GAF) is not settled by the chapter.
- **`texturenumber`: not applicable.** The background-cell path is dead in the original; 36 gadgets say minus
  127 and others 97 to 103, none acted on.
- **Text that does not fit.** The original truncates a button's text destructively to the width minus six; RWE
  draws it whole (`UiStagedButton::render`, about 82) and only ellipsises single-line labels.

## 8. Placement, default buttons, initial focus and keyboard navigation

**Reference.** `RenderLayer` places record zero: flag `0x100` centres it, `0x1000` centres it in the battle
region right of x 128, an X of minus one or two has the same meaning, an extent off the right or bottom edge
**triggers recentring**, and a layout larger than the physical screen is rejected. After loading, the default
accept and cancel names are **inferred when the file's strings are empty**: the first button whose name begins
`OK` or `NEXT` (case-insensitive) is accept, the first beginning `PREV` or `Cancel` is cancel. An explicit
`defaultfocus` is searched by name; otherwise the current gadget starts at zero and a spatial search picks the
first candidate. `HandleGuiCommand` then provides Tab (Shift reverses), **Escape = the cancel default, Enter =
the accept default or, failing that, the current control**, Space on the current control, and arrow-key
navigation (a spatial search with a column tolerance of ten units that skips inactive, `0x400`, greyed and
vertical-slider gadgets), list up/down and slider left/right.

**RWE: DIFFERS. Placement is a clamp; defaults and navigation are screen by screen.**

- **Placement: DIFFERS in method, UNCLEAR in result.** `createPanel` (`UiFactory.cpp` about 258) slides a
  panel left or up by the amount it overhangs the screen (640 by 480 in the older overload, about 84 to 96; the
  scene's size otherwise). The original **recentres**. **Census: seven panels extend past 640 by 480 or start
  above the top**; five are the sliding bars (`CONTROL`, `SHARE`, `TABMENU`, `TALK`, `TALK2`, negative Y by
  design, so only the sliding code moves them), and **two are plain boxes that overhang**:
  `MULTIMSG.GUI` and `SKIRMISHMSG.GUI` (x 147, y 115, 375 by 375: bottom edge at 490). RWE would draw them 10
  pixels higher; recentring would put them 5 higher. Whether the shipped two are ever shown is not traced.
- **Default buttons: DIFFERS.** `UiPanel` holds `crDefault` and `escDefault` straight from the file and honours
  them **only when a scene calls `setDefaultKeysEnabled(true)`** (five sites, all in
  `MainMenuScene_multiplayer.cpp`). There is **no inference**. Census (non-empty values, 185 parsed files): 33 give `crdefault`, 31
  give `escdefault`, five misspell the key (`crtdefault`: `BLANK`, `ENERGY`, `METAL`, `MOREINFO`, `SELCAMP`,
  which both readers ignore, so they depend on inference), and counting buttons by the prefix rule **11 more
  files would infer an accept button and 24 a cancel button**. The per-screen patches (`SELPROV`'s `SELECT`,
  `TCP`'s `OK`) are the same inference done by hand. In the original those files also name a gadget that does
  not exist: `BLANK.GUI` escdefault `MainMenu` and defaultfocus `Missions`, `TCP.GUI` crdefault `HOST`,
  `VIEWMAP.GUI` escdefault `PREVMENU`; RWE ignores a name it cannot find, as does the original.
- **Enter with no default: DIFFERS.** The original presses the current control; RWE's `UiPanel::keyDown` sends
  Return to the focused child, which a button does not act on (only Space or its quick key, `UiStagedButton`
  about 231) and a text box ignores.
- **Initial focus: MISSING.** Only `defaultfocus` is honoured (`UiFactory.cpp` about 121); 158 of 185 files do
  not give one and the original would pick the first candidate by the spatial search. In RWE nothing is focused
  until a click, so Space does nothing and a text box needs a click before it takes text (the save dialog sets
  its own, `GameScene_menu.cpp` about 165).
- **Tab, Shift-Tab and arrow navigation: MISSING.** `ui/` has no Tab handling and no neighbour search; only
  `UiListBox` answers Up and Down. A pad or keyboard-only player cannot move between buttons, and the
  `0x400` exclusion (rule 5) has nothing to exclude from.
- **Escape on a text input: MATCHES in effect** through the panel default where enabled (`BT-GUI-DIALOGS-PARITY.md`, "Text-edit buffers").

## 9. Sliders, scroll bars and list boxes (the dispatch half)

**Reference.** A slider's knob size and range are **computed from its associated list** (visible-line ratio with
a minimum knob of ten for text rows), the track and the arrow buttons are separate records `RenderLayer`
appends, dragging applies the pointer delta to the saved knob position, a held press on the track steps one unit
per handler call, a step callback fires even when clamping prevented movement, and `SyncAssocGadgets` maps knob
position to the list's first row. A list box handles clicks, a double click (attribute `0x200` rejects `&G`
grey rows, `0x40` activates at once), autoscroll when dragging above or below, and up/down keys that do not
undo a scroll already made.

**RWE: MATCHES the design, with these differences and two faults.**

- **Orientation: MATCHES.** Attribute 1 is horizontal and 2 vertical; census: 40 horizontal, 23 vertical.
  `scrollBarFromGuiEntry` (about 866) builds a `UiSlider` for the first and a `UiScrollBar` for the second. The
  `range`, `thick`, `knobpos` and `knobsize` keys that every one of the 63 sliders carries are **not read**
  (rule 4 of `BT-GUI-GUI-FILE-PARITY.md`); the knob is sized from the art and from the viewport message
  (`getEffectiveScrollBarPercent`, minimum 14 pixels against the original's ten).
- **The associated pair: DESIGN difference, MATCHES in effect.** RWE's scroll bar and list exchange percentages
  by group message rather than the original's direct row mapping. Every shipped vertical slider has a list of
  the same association on its panel (census: no orphan).
- **Mouse wheel and arrow buttons on a short list: a probable code fault (traced by reading, not run).**
  `UiListBox::scrollDown` (`UiListBox.cpp` about 348) tests the position against **`maxScroll - 3` in unsigned
  arithmetic**. When the list overflows by fewer than three rows (including a list that fits entirely, where
  the maximum is zero) the subtraction wraps, the test is true, and the position becomes **three**, past the end.
  `render` (about 20) then reads `items[position + row]` for up to `min(visible lines, item count)` rows with
  no bound on the sum, so it reads **past the end of the item vector**. It is reachable from the wheel on a
  focused list, from a scroll bar's down arrow or wheel (the scroll bar posts a `ScrollDownMessage` the list
  answers with `scrollDown`), and so from any short list on any screen. The original's equivalent clamps to the
  maximum first row.
- **A zero-height list: a related fault.** `numberOfLines` returns `height / 12 - 1` unsigned, so a list
  box under 12 pixels tall (or a zero) has about 4.29 billion visible lines; `render` is then bounded only by
  the item count. No shipped list is that short (the smallest is 31 pixels).
- **Row height and lines: UNCLEAR.** The original rounds a list's height down to a multiple of line height
  plus two and sets the pitch to at least the font height plus one; RWE draws on a fixed **12-pixel** pitch and
  shows `height / 12 - 1` rows ("listboxes defined in TA guis tend to be too long", `UiListBox.cpp` about 211).
  Only two shipped lists give an `itemheight` and both say zero. Whether 12 and the minus one reproduce the
  original's row count for the 40 lists is a measurement (`ui_probe --render` on `LOADGAME`, `SELMAP`,
  `SELGAME`).
- **List alignment and grey rows: MISSING.** See `BT-GUI-LISTBOX-PARITY.md` rule 2.
- **Dragging a scroll bar with nothing to scroll: a minor fault.** `UiScrollBar::mouseMove` (about 130)
  divides the pointer delta by `boxInfo.range`, which is **zero when the viewport fills the track**; a grab
  without vertical movement is 0/0, a NaN percentage, and the list's `static_cast<unsigned int>` of a NaN times
  the maximum (`UiListBox.h` about 27) is undefined (zero on the targets in use).

## 10. Bounds on a malformed `.gui`

**Reference.** The chapter lists, for the original, unchecked path and name copies, an unbounded `strcpy` of
text into gadgets, a 128-byte translation buffer, an off-by-record `SetQuickKeyByName`, record-zero-on-a-miss
for every association search, unguarded divisions in the list and progress drawing and a `TruncateGadgetText`
that can loop forever on a gadget narrower than six. None applies to RWE's string handling (it uses
`std::string` throughout). The faults RWE has are different:

- **Zero records, or a first record that is not a panel.** `panelFromGuiFile` takes `entries[0]` after an
  `assert` (`UiFactory.cpp` about 71 and 157); a release build has no check, so a `.gui` that parses to **zero
  records** (an empty file, a file of only comments, or a binary file such as the shipped `ENDGAME.GUI`, a 4352-byte
  dump that begins with a zero byte and the word HEADER and is filled with `0xCD`; RWE never loads it, and
  whether the TDF reader returns no records or throws on it was not run) reads the first element of an empty vector. `readGuiOrThrow` returns the empty list as success. Callers
  that receive a parsed list directly (`GameScene_menu.cpp` about 157 for `LOADGAME.GUI`,
  `MainMenuScene.cpp` about 266 and 278) pass it on unchecked; `GameScene_endgame.cpp` about 349 does check
  `entries->empty()`. A mod that overrides `LOADGAME.GUI` with an empty file crashes the save dialog.
- **Parse exceptions where the contract is `nullopt`.** `parseGui` is declared to return no value on failure,
  but `parseGuiEntry` and the typed getters **throw** (`GuiParseException`, `TdfValueException`) for a missing
  `COMMON`, `id` or `name`, an unknown gadget type, and an incomplete `VERSION` block. The callers' `!parsedGui`
  branches are dead for these. The throw is uncaught on the game-load path: `loadBuilderGui`
  (`GameSimulationLoader.cpp` about 297) parses `<unit>1.GUI`, `<unit>2.GUI` ... for every unit it loads, so a
  malformed builder page in a mod stops **every peer's** game from loading; the front-end paths are caught at
  the scene level (`MainMenuScene.cpp` about 1569).
- **Unbounded sizes.** `width`, `height`, `stages` and positions are plain `int` and are taken as `unsigned`
  by the component constructors. A **negative width or height becomes about four billion**; a very large one is
  accepted. Three consumers loop on it: the plate fill (`UiPanel::render`, about 110, a row-by-column draw
  count from `size / tile`, and `layoutNinePatch`, which builds a vector of that many tiles **every frame**,
  `NinePatch.cpp` about 15), the scroll bar's art loops (`UiScrollBar.cpp` about 56 and 97, a step per middle
  frame down the gadget height) and the staged-button factory (`stages` of two billion allocates a label and a
  sprite per stage, `UiFactory.cpp` about 768 and 810). The original narrows to a short and refuses a layout
  larger than the screen. No shipped gadget has a negative size (census: none; 102 have zero).
- **Art assumptions.** `UiScrollBar` indexes frames 0 to 9 of the `SLIDERS` entry with no count check
  (`UiScrollBar.cpp` about 52 to 60, 95 to 110) and loops forever on a zero-height middle frame; `createPanel`
  and the picture-box path take `.sprites.at(0)` of whatever series they find (about 205, 231 and 668), which
  throws on a frame-less one. The original has the same reliance (the chapter says so of `DrawGafBar` and
  `DrawSliderBar`); RWE's input is the same community GAF files.
- **Overflow.** `hideIfOutsideDialog` and the screen clamp add a position and a size in `int` (about 61, 260);
  a position near the limit overflows. Narrowing at the parser (rule 3 of `BT-GUI-GUI-FILE-PARITY.md`) would
  remove all of the above except the art assumptions.

None of these has a case in `src/rwe/io/malformed_input.test.cpp` (it has no GUI case) and `gui.test.cpp` has
one well-formed file.

---

## 11. Gaps to fix

Ranked by what a player or a mod author would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A list scrolled down when it overflows by fewer than three rows (wheel, or the scroll bar's down arrow) reads past the end of its items**, from any short list. | probable code fault | code: compare against `maxScroll` without the unsigned subtraction and bound `render`'s row index by the item count; add a `UiListBox` test |
| 2 | **Label alignment is ignored and "centre" is on a point**: 106 centred and 6 right-aligned shipped labels draw from their left edge; the real `YESORNO.GUI` carries a `TITLE` label RWE's code says does not exist, so its question is left-aligned. | DIFFERS | code: read the attribute, centre within the rectangle; render check with `ui_probe --panel YESORNO --render` |
| 3 | **Clicking a skirmish option's caption does nothing**: five linked captions on `SKIRMISH.GUI` and ten on `LOUNGE2.GUI` are clickable in the original. | MISSING | code: honour `link` for a label (read the key, forward the activation) |
| 4 | **`active=0` is never honoured**: 86 gadgets in 19 files are drawn and clickable (the visible ones include `RESTART`'s heading, `HELP`'s caption, the lobby templates). | DIFFERS | code: hide and deactivate in `componentFromGuiEntry`; then drop the by-name patches that compensate |
| 5 | **A zero-record or non-panel `.gui` is read as valid**, and parse errors throw through a contract that says `nullopt`; a malformed builder page stops every peer's load. | malformed-input fault | code: check emptiness and the first type at `readGuiOrThrow`, catch per file in `loadBuilderGui`, add `malformed_input.test.cpp` cases |
| 6 | **Letter quick keys are dropped**: N and P on the New Game screen (and O, A on `ARMMAIN`/`CORMAIN`); `-68` (comma) on 26 buttons is unmatched. | DIFFERS | code: read the key as text (see `BT-GUI-GUI-FILE-PARITY.md` rule 4) |
| 7 | **No default-button inference, no initial focus, no Tab or arrow navigation**; defaults honoured on five screens only. | MISSING | code, per screen first (add the inference to `UiFactory`, default off for the in-game panels as now) |
| 8 | **Sizes are unbounded and sign-wrapped** (plate fill, nine-patch vector, scroll-bar art loops, `stages`). | malformed-input fault | code: narrow `width`, `height`, `xpos`, `ypos` to 16 bits and cap `stages` at the parser; refuse a panel larger than the screen |
| 9 | **Attribute `0x08` toggles, `0x80` and `0x4000` are not acted on**; stage cycling is the caller's job. | MISSING / UNCLEAR | none until a screen needs them; record in the factory |
| 10 | **Labels take the click ahead of a button they overlap** (four overlaps, three files, 2 to 4 pixels). | DIFFERS, minor | code: let a label without a link return false from `contains` |
| 11 | **Type-7 font records and `fontnumber` are ignored**; `colorf`/`colorb` read and unused. | DIFFERS / UNCLEAR | render check on a builder page and a label with font 1 |
| 12 | **Panel placement slides instead of recentring** (`MULTIMSG`, `SKIRMISHMSG` end at y 490). | UNCLEAR | a look at the original; or accept |
| 13 | **Association zero is a group**; a scroll bar and list both at zero would be cross-wired. | latent | none today; make zero mean "none" in `setGroup` |

No source file was changed.
