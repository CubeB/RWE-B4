# Camera centering, bookmarks and position persistence parity audit

Spec-room finding, 2026-10-17. Like `BT-INGAME-CONTROL-PANEL-PARITY.md`, `BT-MAP-RADAR-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `ingame-camera` (eight services from `0x41D0F0` to `0x41D4C0`): centring
the view on a radar click or on the mission start, the save-bank record of the camera position,
the four camera bookmarks and the two launch-flag setters. Continuous scrolling, follow, the snap
countdown, shake and the clamp belong to the control-panel chapter and are
`BT-INGAME-CONTROL-PANEL-PARITY.md` rules 12 and 13; they are not repeated. This is the finding
that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-camera.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only; no row concerns the camera services and none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no camera interaction, save load or CD probe was performed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `game/GameScene_input.cpp` (the bookmark and jump keys),
  `game/GameScene.cpp` (the minimap drag, tracking and `setCameraPosition`),
  `game/GameScene_commands.cpp` (`relocateCamera`, `nudgeCamera`), `LoadingScene.cpp` (the opening
  camera), `game/GameScene_menu.cpp` and `game/SaveFile.*` (the saved position) at the working tree
  of 2026-10-17. Nothing was built, run or changed; line numbers are "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Radar-click centring
3. Mission start centring
4. The saved camera position
5. The four bookmarks and the validity check
6. The launch-flag adapters (not applicable)
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| radar-click centring | `BT-MAP-RADAR-PARITY.md` (the minimap click and drag) | the camera half only; rule 2 |
| mission start centring | `BT-GAME-MAIN-LOOP-PARITY.md` rule 9 (commander creation and the camera) | the mission-record variant is new; rule 3 |
| save-bank position | `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 3 (load order) | the Camera account is new; rule 4 |
| bookmarks | `TOTALA-EXE-KEYBOARD.md` section 73 (F5 to F8, Ctrl+F5 to F8) | adds the validity quirk, the sound and the follow reset; rule 5 |
| flag adapters | `BT-GAME-CD-CHECK-PARITY.md` | not applicable; rule 6 |

## 2. Radar-click centring

**Reference.** The click snapshot, less the minimap gadget origin, is scaled by the map pixel size
over the gadget size and has half the visible view size subtracted, which puts the clicked map
location at the view's centre. The result is written to the actual scroll, the dirty flag is set
to two, the shared clamp runs, the clamped actual position is **copied to the desired position**
(so an old follow target cannot pull the view back), map flag 8 is cleared, the snap countdown is
set to zero and **both follow pointers are cleared**. There is no zero-divisor guard and no
inside-the-minimap test; the caller establishes both.

**RWE: MATCHES.** While a minimap drag is in progress, `GameScene::update` (about 695-703) maps the
mouse through `minimapToWorldMatrix` every frame and calls `relocateCamera`
(`GameScene_commands.cpp` about 3624), which leaves a tracking camera for the free one, clamps to
the map and sets the position directly. The "drag" is repeated every frame rather than once per
click, which is the way a held button feels in the original too (the minimap handler is called per
mouse event). Tracking is cancelled, as the chapter requires; there is no desired position to
synchronise, so the "copy to desired" has no counterpart and needs none. The centre-by-half-view
step is the camera constraint's: RWE's camera position is already the view centre.

## 3. Mission start centring

**Reference.** The mission-start service scans the mission entry array in order and accepts the
first entry whose type is one and ID zero; it centres on that entry's signed-short X and Z less
half the view dimensions, marks dirty, clamps, copies actual to desired and clears flag 8. With no
such entry the camera is **left alone and no error is raised**. Unlike the radar click it does
**not** clear the snap countdown or the follow pointers. It does not choose the local player's
numbered skirmish start (the commander spawn centres separately) and creates nothing.

**RWE: DIFFERS in source, UNCLEAR in whether it matters.** There is no mission-record scan.
`LoadingScene::createGameScene` (about 330-361 and 407-410, 491) opens a campaign mission on the
**first unit** the mission spawns, preferring one that belongs to the local player, and a skirmish
on the local commander's start position (`BT-GAME-MAIN-LOOP-PARITY.md` rule 9). A mission whose
type-one, ID-zero record is not at its first local unit (a base with the commander off to one side,
an air mission that begins over empty sea) opens somewhere else in the original. The chapter does
not say what type one and ID zero *means* in the mission file (it says only that the entry array
comes from map metadata), so a spec-room read of the loader is needed before a port. A saved game
is different and correct: it returns early with the saved camera (`LoadingScene.cpp` about
389-399, `applyLoadedGame`, `GameScene_menu.cpp` about 252-257).

## 4. The saved camera position

**Reference.** The write service selects the HapiBank account **Camera** and stores two integer
items, **X Position** and **Z Position**, from the **actual** scroll. It writes neither the desired
scroll nor a follow target, a snap counter, a shake phase or the bookmarks. The read service
selects the same account, reads Z then X with the **current scroll as each item's default**, writes
the actual scroll, marks dirty, clamps, copies actual to desired and clears flag 8. It does not
clear the follow pointers or the snap countdown; the caller's order must have reset them. Both are
void: a missing account or a failed write is silent, and account selection moves the bank's cursor.

**RWE: MATCHES in what it keeps, DIFFERS in form.** The save header carries the camera as three
floats (`SaveFile.cpp` about 292 and 410), written from `worldCameraState.position`
(`GameScene_menu.cpp` about 212) and applied on load by `setCameraPosition`, which **clamps** to the
camera constraint as the original's clamp does. Follow is not saved and the loading scene builds a
fresh scene in the free state, so the unreset follow pointers the chapter warns about cannot occur.
Bookmarks are not saved, as in the original. Differences:

- **Zoom is not saved.** The original's view is a fixed 2D scroll; RWE has a zoom and a pixel
  size, and a loaded game opens at the default zoom, not the one the player had. Presentation, and
  not a gap against the original (it has no zoom).
- **A missing item cannot happen**; the save reader throws on a missing field instead of falling
  back to the current coordinate. `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 8 covers the reader.
- **The height component is kept** (the middle float). Harmless, and it is what makes the clamp's
  Y unchanged.

## 5. The four bookmarks and the validity check

**Reference.** Ctrl+F5 to Ctrl+F8 (`0xD2` to `0xD5`) store the current **actual** scroll into the
slot's X and Y arrays and set that slot's validity byte to one, with no clamp and no index check.
F5 to F8 (`0xE6` to `0xE9`) restore. The restore service's guard compares **the validity array
itself** to zero, not the slot's entry, so (the array being embedded, its address never null) the
guard never rejects: an **unsaved slot restores to its zero coordinates** (the clamped top-left)
instead of doing nothing. Restoring clears the snap and follow state, loads the coordinates, marks
dirty, clamps, copies actual to desired and clears flag 8. The key handler plays the **`SelectSquad`
sound** for both the save and the restore. The camera reset at battle start clears the block
including the bookmarks, which are not part of the save record.

**RWE: DIFFERS, one probable fault.** The keys are in `onKeyDown` (about 200-215): with Ctrl the
current `worldCameraState.position` is stored, otherwise a stored slot is recalled through
`setCameraPosition`, which clamps. The bookmarks are scene state, so they start empty each game and
are not saved, as in the original.

- **A recall does not leave a tracking camera: probable fault.** `setCameraPosition`
  (`GameScene.cpp` about 1133) sets the position and nothing else. If a unit is being tracked, the
  tracking branch of `GameScene::update` (about 706-750) moves the camera back toward the unit on
  the next frame, so the recall appears to do nothing or to jerk. The chapter says the restore
  clears the follow and snap state, exactly so that this cannot happen. The same applies to the F3
  jump (`GameScene_input.cpp` about 216-223) and to `n` (about 228-267): both call
  `setCameraPosition` while tracking. `relocateCamera` and `nudgeCamera` do leave tracking; the
  three key paths do not. Not run; the fix is to route them through the same state change.
- **An unsaved slot does nothing in RWE** (the stored optional is empty); the original goes to the
  top-left corner. **DIVERGENCE, an original quirk not reproduced.** The chapter reads it from a
  source-level condition and no play was done; a player who pressed F6 in an untouched game would
  see the view jump to the corner in the original. Not worth reproducing without a play-test.
- **The sound is missing.** RWE plays nothing on a save or a recall. `SELECTSQUAD` is `beep4` in
  the shipped `allsound.tdf` (also the `CREATESQUAD` name). `BT-INGAME-KEYS-PARITY.md` rule 3.
- **Save stores the position only, and recall leaves the zoom alone** (code comment, about
  202-205). Right for RWE, which has a zoom the original lacks.
- **Bookmarks are not clamped on store**, in either; both clamp on use.

## 6. The launch-flag adapters (not applicable)

**Reference.** One setter stores its integer straight into the "L" command-line flag (called with
zero by the parser); the other stores into the CD drive-scan bypass (called with one for "F"). No
normalising, no view update, no saved preference.

**RWE: not applicable.** `BT-GAME-CD-CHECK-PARITY.md` covers the drive scan, which RWE does not
have. The chapter itself says the placement of these two in the camera address range is
incidental.

---

## 7. Gaps to fix

Ranked by what a player would notice. None touches the simulation; the camera is presentation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Recalling a bookmark, F3 and `n` while tracking a unit**: the camera is set and then pulled back to the tracked unit next frame, because `setCameraPosition` does not leave the tracking state. The original clears follow on every one of these. | DIFFERS (probable fault) | code: leave tracking in the three key paths, then a play-test |
| 2 | **No sound on a bookmark save or recall** (`SELECTSQUAD`). | MISSING | code |
| 3 | **Campaign missions open on the first local unit**, not on the mission-start record (type one, ID zero). | DIFFERS, UNCLEAR | spec-room read of what that record is in the mission data |
| 4 | **An unsaved slot recalls to the top-left in the original and to nothing in RWE.** | DIVERGENCE (quirk, undocumented) | play-test; probably leave |
| 5 | **A loaded game opens at the default zoom.** | no original counterpart | decision |

No source file was changed.

## 2026-10-22 refutation for issue #653

[Camera-jump side effects](BT-CATEGORY-CAMERA-KEY-FINDINGS.md#camera-jumps-bookmark-cancellation-and-the-f3n-refutation) confirms cancellation for bookmark recall but refutes the above claim that original F3 and n also cancel follow/snap. Those two paths retain tracking and only assign a clamped target. This supersedes section 5's three-path recommendation and the corresponding rank-1 gap; changing F3/n to cancel tracking would require a documented divergence. No original-game execution was performed.
