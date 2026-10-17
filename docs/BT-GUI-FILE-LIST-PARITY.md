# Directory lists, the file requester and blinking text slots parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and `TA-SYSTEMS-PARITY.md`,
it allocates no section number in the main register. It covers one chapter of the reference, `gui-file-list`
(fourteen services from `0x4AEFA0` to `0x4B0090`): the parallel-list sort, the directory scan, the file requester
dialog, and the nine services that manage timed blinking words. This is the finding that implementation work cites
for any of them, per `AGENTS.md` and ADR-0004.

Most of it has no counterpart in RWE, which has no file requester and no directory-scan routine; the part that
matters is the **blinking words**, which the briefing screen uses (`BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5
already records that RWE does not blink and gives the briefing's parameters; this audit adds the timing model).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\gui-file-list.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-156 (GAF durations and the loop
  flag, "per-tick list = ticks, cursor and interface gadgets = wall time in 1/30 s") is the nearest row and is
  not re-raised.
- **Evidence class.** A **static reading**; the chapter says no navigation, file opening or blink rendering was
  executed. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `game/SaveFile.cpp` (`listSaveGames`), the map-list code in
  `MainMenuScene*.cpp`, `ui/UiTextRegion.*` and a search for any blink or file-requester counterpart, at the
  working tree of 2026-10-17. Nothing was built, run or changed; every line number is "about".
- **Census:** `FILEREQ.GUI` is in none of the seven archives, so the requester cannot be opened on a retail
  install (`BT-GUI-DIALOGS-PARITY.md`, census).

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Sorting parallel lists (not applicable)
3. Scanning a directory view (not applicable)
4. The file requester (not applicable)
5. Blinking words: slots, timing and drawing
6. Bounds
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| `SortFileList` | `BT-GAME-SAVEGAME-PARITY.md` rule 2 (the save list order), `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule on the map list | rule 2 |
| `ScanDirectory` | `BT-UTIL-HPI-PARITY.md` (attributes `0x10`/`0x11`) | rule 3 |
| the file requester | none | rule 4 |
| blinking words | `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5 (the briefing's use); BT-156 (clocks) | rule 5 |

## 2. Sorting parallel lists (not applicable)

**Reference.** `SortFileList` sorts consecutive-NUL-string lists plus an optional parallel second list and an
optional integer key array. It finds the **last item that begins with a backslash** (a directory marker) and sorts
two regions apart: the first (through that item) by **ascending key** if keys exist else ascending
case-insensitive text, the second by **descending key** if keys exist else ascending text; equal items keep their
order; every swap also swaps the parallel pointer and key. It does **not** partition mixed input into directories
and files first. It allocates two 96,000-byte output buffers and two 12,000-byte pointer arrays whatever the count
and indexes beyond 3000 slots without a check; key comparison is by subtraction and can overflow.

**RWE: not applicable.** There is no generic sorter. The two lists RWE shows are ordered by their own rules and
those audits stand: saved games newest first (`listSaveGames`, `SaveFile.cpp` about 209 to 229, a deliberate
improvement on the original's enumeration order, `BT-GAME-SAVEGAME-PARITY.md`) and the map list, where the
collation question is `BT-FRONTEND-SKIRMISH-MENU-PARITY.md`'s. The fixed capacity (3000 items, 96,000 bytes) is a
limit of the original's file requester and does not exist here.

## 3. Scanning a directory view (not applicable)

**Reference.** `ScanDirectory` allocates a 12,000-byte time array, optionally enumerates directories first (mode
one: pattern `*.`, accepting a non-dot name only when the **whole attribute value equals `0x10`**, prefixing a
backslash, and recording `<DIR>` as the size text), then always searches the given mask for files (non-dot names
whose attribute is **not** `0x10`), copying names, optionally stripping extensions, recording modification times
and optionally a decimal size queried by the **leaf name** (so the process directory, not the mask's, is used).
The attribute test is an equality, so **archive directories, which the archive layer reports as `0x11`, fall
into the file branch**. Sorting by alphabet is mode two; other modes sort by time; the size list is **not** sorted
with it, and directory entries advance the count without filling a time slot, so a time sort of a mixed result
uses misaligned keys. There is no capacity argument and no cancellation.

**RWE: not applicable.** Directory content comes from the virtual file system's own listing
(`vfs/`), which has its own rules (`BT-UTIL-HPI-PARITY.md`); RWE never reproduces the exact-attribute test, the
size lookup by leaf name or the misaligned time keys, and does not need to.

## 4. The file requester (not applicable)

**Reference.** `OpenFileRequester` loads `FILEREQ.GUI` and, if it loads, allocates a `0x24C`-byte state object,
copies the caller's selection into it and strips the filename (the open **already mutates the caller's string**),
resolves `NAME`, `MASK`, `PATH`, `TITL` and `SLID`, allocates a 96,000-byte name list and a 60,000-byte size list
filled with `0xFF`, saves the current drive and directory, and refreshes the list from the **process's actual
current directory** (so the dialog does not open in the directory it was given). Its handler descends into a
directory on `LOAD` or a list double click, goes up on `PATH`, accepts `NAME`, and changes drive on `ADRV`,
`BDRV`, `CDRV`, `DDRV` and `VDRV` (**the last changes to drive R**, whatever its label says). It **changes the
process-wide drive and directory** while navigating and restores them only if the layer's close handler runs.
Neither the name nor the size list is freed by the handler.

**RWE: not applicable.** RWE has no file requester: saved games, maps and replays are chosen from list boxes
over known folders (`BT-GAME-SAVEGAME-PARITY.md`) or from the command line. `FILEREQ.GUI` is not shipped, so
the original cannot show it on a retail install either; the chapter's process-wide directory changes are the sort
of thing RWE's rule that nothing outside the data folders is touched already excludes.

## 5. Blinking words: slots, timing and drawing

**Reference.** A blink word is **164 bytes**: 128 text bytes, an X and Y, **colour A and colour B**, **duration A
and duration B** (floats), a phase, a next-toggle time (a float) and a final value. A dialog owns an array pointer,
a count and an "active" flag. The first word's final value doubles as a **shared font selector** for the whole
array. The services:

- `AllocBlinkWords(n)` frees any earlier array, allocates `n` records, clears each first text byte, enables drawing
  and sets word zero's selector to minus one. It does not clear any other field; a count of zero still writes
  word zero; negative counts and allocation failure are unchecked.
- `FreeBlinkWords` frees and disables, leaving the count. `SetBlinkGadget` writes word zero's selector, unchecked.
  `EnableBlinkWords` and `DisableBlinkWords` act only when the array exists.
- `AddBlinkWord` finds the **first slot with an empty first text byte**, copies up to 128 text bytes **without
  forcing a terminator**, stores position, colours and durations, sets **phase zero** and the first deadline to
  **now plus the tick rate times duration A**, and returns **one** (not the slot index; zero for no array or no free
  slot). Empty text leaves the slot looking free.
- `ClearBlinkWord(i)` clears the first text byte when `i <= count` (so **`i == count` and negative values write
  outside** the array); `ClearBlinkWords` clears each in range and keeps the other fields.
- `DrawBlinkWords` returns when inactive. Otherwise it reads the clock, selects the font named by the first
  word's selector when it is not minus one, and visits every word with text. A word whose **deadline is strictly
  earlier than now toggles once per draw call**: to phase one with deadline `now + duration B x rate`, back to
  phase zero with `now + duration A x rate`. The deadline is computed from the **current draw time**, not the
  previous deadline, so skipped frames do not make it catch up. The **phase picks colour A or colour B** for the
  text, and the current text-key colour is the other argument; the string is drawn at the word's X and Y on the
  current surface with width minus one. The previous font and colours are **not restored**. Durations are not
  validated; a large time loses float precision. Enabling and disabling do not reset deadlines.
- These are **presentation**: not simulation particles, not chat flags, not mission timers.

**Register and the neighbouring audit.** `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5 says spans coloured `R`, `Y`
or `G` are drawn through a blink word "registered with **period 0x5e, duration 1.0 and phase 0.25**" with at most
fifteen per briefing and a per-side colour table at offsets three, two and one. Read against this chapter's field
names, a plausible mapping is **colour B = 0x5E, duration A = 1.0 s, duration B = 0.25 s**: the span shows in its
side colour for a second and flashes the other colour for a quarter of a second. The briefing chapter's words
("period", "phase") do not match the fields here, and neither chapter lists the argument order of `AddBlinkWord`,
so the mapping is an inference. Marked **UNCLEAR**; a spec-room read of the call in the briefing code (or a look
at the original briefing) settles which of the two colours is held longer.

**RWE: MISSING.** There is no blink word, no timer and no second colour. `UiTextRegion::parseMarkup`
(`ui/UiTextRegion.cpp` about 39 to 70) paints a span in one fixed colour for good (`BT-FRONTEND-CAMPAIGN-MENU-
PARITY.md` rule 5). A grep for blink code finds only the minimap contacts and damage flashes and unrelated mentions, none
of it a text slot. What a port needs from this chapter, beyond that audit:

- **Two colours and two durations per span, a phase, and the deadline rule.** The toggle happens at most once per
  draw and re-bases on the draw time, so the visible rhythm is the frame time rounded up to whichever duration
  is current. At 60 frames a second the difference from an exact schedule is invisible; at 5 it is not.
- **A presentation clock, never the simulation's.** The briefing runs before a game exists; in a game the same
  rule applies as everywhere (`CLAUDE.md`, "Nothing a frame knows may reach the simulation"): drive the phase
  from the scene's own clock (`sceneTime`/`effectsRng` family), never from a tick. The original reads the wall
  clock in ticks per second, consistent with BT-156's "interface gadgets = wall time".
- **The array semantics are irrelevant.** The first-free-slot search, the 128-byte cap, the shared font selector
  in word zero and the return value are artefacts of the fixed array. RWE should store a vector of spans per
  page, redraw all of them each frame and cap them (the briefing's fifteen is a sensible bound for a malformed
  brief).
- **Disabled and hidden pages.** Disabling the array stops drawing but not the deadlines; since RWE draws only
  the visible page, nothing to port.

## 6. Bounds

**Reference.** The file lists and requester have fixed buffers with no capacity arguments; the blink array
trusts its count and index, with the `i == count` write above and an unterminated 128-byte copy.

**RWE: none of this exists**, which means none of the faults do. If blink words are added the bounds that matter
are the ones the briefing already has: a cap on spans per page and an `ensureUtf8` on the text, because a briefing
is a file from a mod or a map (`BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5 notes the unchecked copy lengths of
the original).

---

## 7. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Briefing spans do not blink**; the timing model is two colours, two durations and a re-based deadline, once per draw. | MISSING (presentation) | code, after the parameter mapping is settled |
| 2 | **Which colour is held for how long** (colour B = `0x5E`, durations 1.0 and 0.25?) is an inference across two chapters. | UNCLEAR | spec-room read of the briefing's `AddBlinkWord` call, or a look at the original briefing |
| 3 | **Nothing else in this chapter applies**: no generic sorter, directory scan or file requester exists in RWE, and `FILEREQ.GUI` is not shipped. | not applicable | none |

No source file was changed.
