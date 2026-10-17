# GUI record files, field readers and shared GAF assets parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-UTIL-TDF-PARITY.md` and `TA-SYSTEMS-PARITY.md`,
it allocates no section number in the main register. It covers one chapter of the reference, `gui-gui-file`
(33 services from `0x4ACD00` to `0x4AEFA0`: the text-writer primitives, the per-type field readers and writers,
`ReadGuiFile` and `WriteGuiFile`, and the helpers that load the common GUI GAF and the glyph GAF slots). This is
the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The TDF grammar underneath it (comments, escapes, duplicate keys, how a number is read) is
`BT-UTIL-TDF-PARITY.md` and is not repeated; what the toolkit does with a decoded record is
`BT-GUI-GUI-PARITY.md`. The question this audit answers is the one the task set: **does a shipped `.gui` decode to
the same thing in RWE, and does a malformed one stay bounded.**

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\gui-gui-file.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row concerns the record format
  (BT-174 is the side-data file, not a GUI file).
- **Evidence class.** A **static reading**; the chapter says no file was exported, no backup renamed and no
  asset rendered. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `io/gui/gui.h`, `io/gui/gui.cpp`, `io/gui/gui.test.cpp`,
  `io/tdf/TdfBlock.*` (the typed getters), `vfs/AbstractVirtualFilesSystem.cpp`, the callers of
  `parseGuiFromBytes` and `ui/UiFactory.cpp` at the working tree of 2026-10-17. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so every line number is "about".
- **A census of the shipped GUI files**, as in `BT-GUI-GUI-PARITY.md`: 186 distinct `.gui` paths over the seven
  archives (later overriding earlier), parsed with a throw-away reader that follows the TDF grammar of
  `BT-UTIL-TDF-PARITY.md`. Counts below are of **records** unless stated.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. What a record holds, and which keys RWE reads
3. The common fields: narrowing, defaults and required keys
4. Reading `quickkey`
5. The type-specific bodies
6. Panel metadata: count, version and the default names
7. Whole-file reading: record order, missing parts and the odd shipped files
8. Writing a GUI file (not applicable)
9. The common GUI GAF and the glyph slots
10. Bounds on a malformed file
11. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| text writer primitives, `WriteGuiFile` | none | rule 8, not applicable |
| common field schema | `BT-UTIL-TDF-PARITY.md` rule 6 (typed getters) for the conversions | rule 3 |
| panel metadata | none (`TOTALA-EXE-INTERFACE.md` uses `crdefault`/`escdefault` for named screens) | rule 6 |
| type-specific bodies | `UiFactory.cpp` comments for `maxchars` and `grayedout` | rules 4 and 5 |
| whole-file read | `BT-UTIL-TDF-PARITY.md` rule 3 (value termination: the missing semicolon) | rule 7 |
| GAF slot loading | `BT-GRAPHICS-FONT-PARITY.md`, `BT-GRAPHICS-GAF-PARITY.md` | rule 9 |

## 2. What a record holds, and which keys RWE reads

**Reference.** A gadget record is `0x15B` bytes: common fields (type, association, name, geometry, attributes,
two colours, texture and font numbers, active, common attributes, help, the `gaffile` flag) and a type-specific
body. Record zero also carries the panel fields. The file is a list of top-level records each holding a
`COMMON` block plus the body keys at the record's own level.

**RWE: MATCHES the structure.** `parseGuiEntry` (`io/gui/gui.cpp` about 60) reads the common keys from the
`COMMON` block and the body keys from the record itself, which is how the shipped files are laid out. The keys
RWE reads and the keys it does not, against the census:

| Key (record type) | Records carrying it | RWE |
| --- | --- | --- |
| `id`, `assoc`, `name`, `xpos`, `ypos`, `width`, `height`, `attribs`, `colorf`, `colorb`, `texturenumber`, `fontnumber`, `active`, `commonattribs`, `help` | all | read |
| `gaffile` | 0 | not read |
| `totalgadgets`, `panel`, `crdefault`, `escdefault`, `defaultfocus`, `VERSION` (0) | 186 panels | read |
| `status` (1) | 1509 buttons (four set to 1) | read, **never used** |
| `text` (1, 3, 5) | 1511 buttons, 17 text inputs, 216 labels | read (inputs deliberately start empty) |
| `quickkey` (1) | 1753 buttons | read, **fails on a letter** (rule 4) |
| `grayedout`, `stages` (1) | 857, 242 | read |
| `maxchars` (3) | 17 | read |
| `link` (5) | 71 labels, 15 non-empty | **not read** |
| `range`, `thick`, `knobpos`, `knobsize` (4) | 63 sliders | **not read** |
| `itemheight` (2) | 2 lists, both zero | **not read** |
| `hotornot` (6) | 56 hotspots | **not read** |
| `filename` (7) | 105 font records | **not read** |
| `crtdefault` (0) | 5 | not read (and not read by the original either, rule 6) |

## 3. The common fields: narrowing, defaults and required keys

**Reference.** The readers narrow rather than clamp: id and association to an unsigned byte, geometry to a
signed short, colours to an unsigned short, texture, font, active and common attributes to a byte (read back as
signed), the string `name` to sixteen bytes and `help` to 128; every missing key takes a default of zero or the
empty string, and a missing `COMMON` block leaves the record's earlier bytes in place. `gaffile` contributes only
its low bit. The `help` value is read and then **zeroed before it is translated**, so loaded help text is
discarded in this build.

**RWE: MATCHES for every shipped value, DIFFERS in method and in strictness.**

- **Narrowing: DIFFERS in method, MATCHES on retail data.** RWE keeps every numeric as a full `int`
  (`GuiCommon`, `io/gui/gui.h` about 67). Census, against the narrow ranges: **no position, size, id or
  association is outside its original range; the only out-of-range values are `colorf` and `colorb` of six
  records, which hold `-842150451` (`0xCDCDCDCD`, the debug-heap fill) and which the original reads as `0xCDCD`**.
  `texturenumber` and `fontnumber` carry `-127` (36), `-124` (4) and `-51` (the fill) as signed bytes, and
  RWE's integers hold the same signed values. So nothing shipped is read differently; a mod that wrote a
  width above 32767 would be wrapped by the original and honoured by RWE (rule 10).
- **Defaults: MATCHES for the optional keys, DIFFERS for the required ones.** `xpos`, `ypos`, `width`,
  `height`, `assoc`, `attribs`, `colorf`, `colorb`, `texturenumber`, `fontnumber`, `active` and `commonattribs`
  default to zero (the code comment explains `attribs` and `colorf`); **`id`, `name` and the `COMMON` block are
  required** (`expectGuiElementType`, `expectString`, `findBlock`, about 64 to 72), and a record missing one
  makes the parser throw. The original defaults the name to empty and the record to its previous contents. No
  shipped record misses any of them (census: none).
- **`help`: MATCHES in effect.** RWE reads the value into `GuiCommon::help` and no code uses it; the original
  discards it. 11 shipped gadgets write one (`SELGAME.GUI`'s columns, `SKIRMISH.GUI`'s `Difficulty`) and none
  would show. (The runtime help line of `UpdateHelpText` is `BT-GUI-GUI-PARITY.md`; with every loaded string
  empty it has nothing to show from a file.)
- **`active`: read, never used.** See `BT-GUI-GUI-PARITY.md` rule 4 (86 shipped gadgets say zero).
- **`gaffile`: not applicable on retail data.** No shipped record sets bit zero, so the per-gadget
  `<name>_gadget.GAF` lookup is never taken. A mod that used it would be ignored.
- **The missing semicolons: MATCHES in effect, with the code comment out of date.** The comment at
  `gui.cpp` about 80 says `ARMGEN.GUI`'s missing semicolon after `attribs` makes the parser take `colorf` as the
  value "making both invalid". RWE's integer reader is a stream extraction, which takes the leading integer and
  ignores the rest (`BT-UTIL-TDF-PARITY.md` rule 6), so `attribs` reads `256` correctly and only `colorf` (15)
  is lost, in both readers. Census: the same fault is in `CORGEN.GUI` (`GADGET10`, the standing move-order
  button, `attribs=256` the cycle bit) **and in `RESTART.GUI` (`GADGET3` and `GADGET4`, `active=1` swallowing
  `commonattribs=0`)**. The first matters (it is the cycle behaviour of the order button, and it survives); the
  second loses nothing because both fields are unused.

## 4. Reading `quickkey`

**Reference.** The reader takes `quickkey` as **text**: an alphabetic first byte is stored as itself, otherwise
the text goes through `atoi`. The writer emits a signed decimal byte.

**RWE: DIFFERS. A letter is not read.** `parseGuiEntry` (about 119) extracts `quickkey` as an integer, which
fails on a non-numeric string, so the field is left empty and `buttonFromGuiEntry` and `stagedButtonFromGuiEntry`
set no key. **Census: six records write a letter**: `OPTIONS` `O` and `ALLIES` `A` in `ARMMAIN.GUI` and
`CORMAIN.GUI`, `OK` `N` and `CANCEL` `P` in `NEWMULTI.GUI`; and one writes nothing (`AnyMsn` in `SINGLE.GUI`,
`quickkey=;`, which is zero in the original and nothing in RWE, the same). On the New Game screen
(`makeNewGamePanel`) Next and Previous therefore have no keys. The numeric majority (1746 records, 711 of them
zero) read the same. A letter should map to its upper-case code (the existing `convertQuickKeyToSdlk` lowers it,
`UiFactory.cpp` about 25), so the fix is at the parser: take the first byte of a non-numeric value.

## 5. The type-specific bodies

**Reference.** Per type: button (status, text, quickkey, grayedout low bit, stages); list box (`itemheight`;
the reader also clears the callback fields); text input (`maxchars`, text; `maxchars` narrowed to a short and
**clamped only above 128**, then the text is translated with an unbounded copy and **not truncated to
`maxchars`**); slider (range, thick, knobpos, knobsize and a text field the writer omits, so a slider's text does
not survive a save and reload); label (text and link, the hotkey cleared); hotspot (`hotornot`, low bit); font
and surface filenames (capacity 32); the provisional type 10 (`nuttin`).

**RWE: MATCHES for buttons, text and list; MISSING for the rest.**

- **Buttons: MATCHES except `status`.** `grayedout` and `stages` are used (`UiFactory.cpp` about 728 and 613).
  `status` is never applied: the **four buttons that start pressed** (`status=1`: `Side0` and `Medium` in
  `NEWCAMP.GUI`, `ARM` and `MEDIUM` in `SELCAMPX.GUI`, all radio buttons) come up unpressed unless the scene sets
  them. The campaign screens set their own selection; whether the two old screens are ever built is not traced.
- **Text input: DIFFERS in the cap.** `maxchars` is honoured (`UiFactory.cpp` about 642) but limited to
  `UiTextBox::largestMaxLength`, **63** (`UiTextBox.h` about 24); the original clamps above 128 and caps the
  runtime status at 127. Census: `maxchars` is 63 on eleven boxes, 20 on four (the save and load names), 10 on two
  (the New Game nickname and password) and **99 on `SAVEGAME.GUI`'s `GAMENAME`**, which RWE would cut to 63; RWE
  builds the save dialog from `LOADGAME.GUI` instead (20), so nothing shows. A non-positive value is ignored
  (the box keeps its default length), where the original's negative value is "not given a safe minimum".
- **List box: `itemheight` MISSING, harmless.** Two lists write it, both zero.
- **Slider: MISSING.** All 63 write the four numbers (a few values recur: range 106 or 112,
  thick 22 or 34 and knob size 10 for the horizontal ones; thick 56 with knob size 175 or 138 for the vertical ones). RWE builds a slider from the
  SLIDERS art and the viewport message instead (`BT-GUI-GUI-PARITY.md` rule 9), so the file's knob size and range
  are not used. Whether the shipped knob sizes (175, 138) match the art-derived knob is a render question.
- **Label `link`: MISSING.** 71 records, 15 non-empty (ten in `LOUNGE2.GUI`, five in `SKIRMISH.GUI`); see
  `BT-GUI-GUI-PARITY.md` rule 6 for the consequence (a caption is not clickable).
- **Hotspot `hotornot`: MISSING, 56 records** (ten each in `ALLIES.GUI`, `SCORE.GUI` and `TALK2.GUI`, nine in `CONTROL.GUI`, the
  rest scattered, among them `battlestart`, `SOLARSYSTEM` and `junk`). `componentFromGuiEntry` makes every type 6 a `UiSurface`
  that takes no click, so no hotspot answers (the lobby draws its own).
- **Font `filename`: MISSING, 105 records.** See `BT-GUI-GUI-PARITY.md` rule 7.
- **Surface filename (8) and type 10: not applicable.** No shipped record has either.

## 6. Panel metadata: count, version and the default names

**Reference.** Record zero writes `totalgadgets`, a nested `VERSION` block (major, minor, revision, read only
when the block exists; the bytes are read as signed), and the names `panel`, `crdefault`, `escdefault` and
`defaultfocus`. **Neither the version nor the names is validated**; `ReadGuiFile` overwrites the count with the
actual number of top-level records minus one.

**RWE: MATCHES for what is read; one strictness difference.**

- **`totalgadgets`: MATCHES in effect.** RWE reads it and never uses it; the panel is built from the actual
  records (`UiFactory.cpp` about 107). **Census: it is wrong in ten files** (`ALLIES` 40 against 43,
  `ARMGEN` and `CORGEN` 30 against 18, `BRIEFING` 5 against 6, `LOUNGE2` 42 against 43, `NEWCAMP` 13 against 10,
  `NEWGAME` 17 against 13, `SCORE` 27 against 31, `SSIDESEL` and `SSIDESEL1` 2 against 3), which is why
  trusting it would drop or invent gadgets.
- **`VERSION`: DIFFERS in strictness.** A `VERSION` block is parsed with three required integers (about
  106 to 115); one missing makes the parser throw, where the original leaves the bytes it did not read. The
  value is never compared. Census: 185 of 185 parsed files carry a block; the triples are `1.0.0` (110 files),
  `-51.-51.-51` (63, the uninitialised fill), `0.0.0` (10) and `1.0.1` (2).
- **`crdefault`, `escdefault`, `defaultfocus`: MATCHES.** Read as strings, held by the panel, resolved by name
  at use (`BT-GUI-GUI-PARITY.md` rule 8 for the use and the missing inference). Census of the written values
  (non-empty, 185 parsed files): 33 files give a `crdefault`, 31 an `escdefault`, 27 a `defaultfocus`; **five misspell the key `crtdefault`**
  (`BLANK`, `ENERGY`, `METAL`, `MOREINFO`, `SELCAMP`), which the original does not read either; and four name a
  gadget the file lacks (`BLANK.GUI` `escdefault=MainMenu` and `defaultfocus=Missions`, `TCP.GUI`
  `crdefault=HOST`, `VIEWMAP.GUI` `escdefault=PREVMENU`). `panel=` is read and used as the background name
  (`createPanel`, `UiFactory.cpp` about 201: `NULL` or absent falls back to the other searches).
- **Record zero's own type: UNCLEAR, harmless.** The census finds record zero typed 0 in 185 files (the 186th
  is the binary one, rule 7). RWE never checks it.

## 7. Whole-file reading: record order, missing parts and the odd shipped files

**Reference.** `ReadGuiFile` enumerates every top-level record by index until selection fails; it does not
require `GADGETn` names; there is no output size argument, no type-range check and no clearing of the
destination; a missing `COMMON` keeps the earlier type; the count is recomputed; an empty file succeeds with a
count of minus one; structural TDF errors keep the TDF reader's own fatal behaviour.

**RWE: MATCHES on order, DIFFERS on failure.**

- **Order and names: MATCHES.** `parseGui` walks the list adapter's blocks in file order whatever they are
  called (`gui.cpp` about 127); `ListTdfAdapter` returns them as a list.
- **Failure: DIVERGENCE (a hardening, with a hole).** The original carries on with stale bytes; RWE throws
  `GuiParseException` for a missing `COMMON`, `id`, `name` or a short `VERSION`, and for **any gadget type
  outside 0 to 7 and 12** (`toGuiElementType`, about 19). Census: every shipped record is in that set (types 0 to
  7 and 12 only; the original also has 8, 10, 11 and 13, used by no retail file). The throw escapes a function
  whose contract is a missing value (`parseGui` returns an optional, and its callers test it), so the callers'
  failure branches are dead and the exception reaches a `catch` at the scene level or none
  (`BT-GUI-GUI-PARITY.md` rule 10).
- **The empty result: the other hole.** An empty list is a success in RWE as in the original (which returns a
  count of minus one), and `readGuiOrThrow` hands it on (`AbstractVirtualFilesSystem.cpp` about 16).
  `panelFromGuiFile` then indexes record zero (`BT-GUI-GUI-PARITY.md` rule 10).
- **The odd shipped files.** `ENDGAME.GUI` (4352 bytes, `totala1.hpi`) is **not a TDF file**: it begins with a
  zero byte, `0xCD` and the word HEADER and is mostly `0xCD`. Nothing in RWE loads it, and the original does not
  appear to either (the chapter does not name it). Seven files hold bytes above 127; RWE's list adapter falls
  back to Latin-1 when a file is not valid UTF-8 (`tdf.cpp` about 30). One record (`single.gui` `AnyMsn`) writes
  `quickkey=;`.
- **Encoding: MATCHES.** The original reads bytes; RWE converts to UTF-8 and the label widths are measured in
  the font's byte-indexed glyphs after `ensureUtf8` (`UiLabel` about 54). No shipped GUI string is wider than
  one byte per glyph except through that route.

## 8. Writing a GUI file (not applicable)

**Reference.** `WriteGuiFile` renames an existing `.gui` to `.BGU`, ignoring failure, writes a new loose file
record by record, and **mutates the panel's `ypos`** (subtracting 480 when a global is set and the value is
non-negative) so that repeated saves keep moving it. The writers use 100-byte line buffers with no escaping, and
discard write results. The slider writer omits its text.

**RWE: not applicable.** RWE has no editor and no writer for `.gui`; `GuiEntry` equality exists for the tests
(`gui.test.cpp`). The chapter's data-loss and mutation faults do not exist.

## 9. The common GUI GAF and the glyph slots

**Reference.** `LoadGafIntoSlot` loads `<animations path>/<name>.GAF` into one of three slots and, for the first
sequence, **subtracts the height of frame `0x49` (the letter `I`) from every frame's Y hotspot**, a baseline
adjustment, then makes that slot the active font; a zero length clears the slot but leaves the active font
pointing at an older one; there is no slot range check and no free of a replaced pointer. `FreeGafSlot` and
`FreeCommonGuiGaf` clear. The application normally loads `commongui` and two glyph GAFs (`hattfont12`,
`hattfont11`) at start.

**RWE: MATCHES in the loaded set; the baseline is handled elsewhere.** `TextureService` loads the GAF entries
by name on demand (`getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)")`) and the renderer places text
from a baseline of `y + 12` with the `I` measured for the quick-key underline (`UiStagedButton.cpp` about 97),
not by rewriting hotspots on load. `hattfont11` is used only by the Space tab strip
(`GameScene_render.cpp` about 573). The chapter's slot faults do not exist (there are no slots), and
`BT-GRAPHICS-FONT-PARITY.md` and `BT-GRAPHICS-GAF-PARITY.md` hold the glyph rules. **Which cut a gadget uses is
`BT-GUI-GUI-PARITY.md` rule 7.**

## 10. Bounds on a malformed file

**Reference.** The reader has no destination size, so a file with more records than the caller allocated
overruns it; the 128-byte text copies and 100-byte line buffers are unbounded in both directions.

**RWE: bounded in memory, not in value.** `std::string` and `std::vector` remove the buffer faults. What is not
checked is the **value** of anything numeric, and no case covers it (`io/malformed_input.test.cpp` has no GUI
case; `gui.test.cpp` has one well-formed file):

- **Sizes, positions and `stages` are unchecked `int`s** passed on as unsigned (`BT-GUI-GUI-PARITY.md` rule 10:
  the plate fill, the nine-patch vector built per frame, the scroll bar's loops, a staged button's per-stage
  allocation). The fix belongs here: narrow `xpos`, `ypos`, `width`, `height` to 16 bits as the original does,
  `assoc` and `stages` to a byte, and refuse a record zero larger than the screen.
- **The number of records is bounded only by the file cap.** A file of 16 MB of `[G]{[COMMON]{id=1;name=a;}}`
  would build millions of components. The original has a 200-record allocation. A cap at the loader (the
  original's 200 is a safe choice; the largest shipped file has 44) costs nothing.
- **Every parse throws instead of returning a value** (rule 7), so a loader that wants "this file costs this
  file" must catch per file; `loadBuilderGui` does not.
- **Required keys, an unknown type and a short `VERSION` abort the whole file.** For a builder page that is one
  unit's menu in the original (nothing more is read from it) and every peer's game in RWE.

---

## 11. Gaps to fix

Ranked by what a player or mod author would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A malformed or empty `.gui` is not contained**: required keys, an unknown type or a short `VERSION` throw through an optional-returning API; an empty list is accepted and then indexed; a malformed builder page fails every peer's load. | malformed-input fault | code: return `nullopt` or an error from `parseGui`, reject an empty list in `readGuiOrThrow`, catch per file in `loadBuilderGui`, add `malformed_input.test.cpp` cases |
| 2 | **Numeric fields are not narrowed or capped**: sizes, positions and `stages` reach the factories as `int` (sign-wrapped to unsigned), and the record count is unbounded. | malformed-input fault | code: narrow at the parser as the original does, cap records, refuse an oversize record zero |
| 3 | **A letter `quickkey` is dropped** (six records; N and P on the New Game screen). | DIFFERS | code: read the key as text |
| 4 | **`link` is not read** (15 non-empty: the five skirmish captions and the lobby's ten). | MISSING | code (with the label click in `BT-GUI-GUI-PARITY.md` rule 6) |
| 5 | **`status` is not applied** (four radio buttons start pressed in `NEWCAMP` and `SELCAMPX`). | MISSING | code, if those screens are built |
| 6 | **The text-input cap is 63, the original's is 127 or 128** (`SAVEGAME.GUI` says 99). | DIFFERS, no shipped consequence | none until a screen needs more |
| 7 | **`VERSION` is required in full; the original reads what is there.** | DIVERGENCE (hardening) | none; document in the parser |
| 8 | **Slider `range`/`thick`/`knobpos`/`knobsize`, hotspot `hotornot`, font `filename` are dropped**; font and knob are re-derived. | MISSING / UNCLEAR | render comparison of a slider and a builder page |
| 9 | **Code comment at `gui.cpp` about 80 is out of date** (the leading integer is read; only `colorf` is lost) and names one file of three (`CORGEN` and `RESTART` share the fault). | documentation | fix the comment |
| 10 | **`ENDGAME.GUI` is a binary file, not TDF.** | documentation | note in `TOTALA-EXE-DATA.md`; nothing loads it |

No source file was changed.
