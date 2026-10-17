# Save and load screens, summary previews and the restore handoff parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`, which is its sibling,
it allocates no section number in the main register. It covers one chapter of the reference,
`game-savegame` (eight services from `0x491ec0` to `0x493340`): the save and load **screens**
-- discovering saved games, the summary preview, the save, delete and load actions, the
admission checks and the handoff that starts the restore. The bank's file encoding, the summary
account's items and the order the serialisers run in are `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`
and are not repeated; the unit and world records are other chapters. RWE's save **format** is
its own design (ADR-0003 and the register); the **UI behaviour** is what is compared. This is
the finding that implementation work cites for any of it, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-savegame.md`). Nothing
  else under that tree was opened beyond the sibling chapters named in these audits: no source,
  no decompilation, and none of its source links were followed. The spec-room discrepancy
  register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row
  concerns the screens.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked. Every
  statement about what a player sees is untested; the original `.SAV` files on the dev machine
  were not opened.
- **The RWE half** is a read-only audit of `game/GameScene_menu.cpp` (the in-game save and load
  dialogs, `wireSaveList`, `saveCurrentGame`, `loadSavedGame`, the `SAVEGAME` and `LOADGAME`
  handlers), `MainMenuScene.cpp` (`goToLoadGameMenu`, `startLoadedGame`, the `LOADGAME`
  handler), `game/SaveFile.cpp` (`listSaveGames`, `saveFileStem`, `savePathForName`),
  `ui/UiTextBox.cpp` and `game/GameScene_ownclock.cpp` (`canSave`), at the working tree of
  2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in the
  tree, so line numbers are "about". Note: `saveFileStem` (folds `/`, `\`, `:` and control
  characters to `_`) is new since `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` was written, and that
  audit's second gap partly stands corrected by it.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Discovering saves
3. The summary preview
4. Opening the save screen, and what may be typed
5. Save, delete and cancel
6. Opening the load screen
7. Load acceptance and the destructive handoff
8. Between-missions saves and invalid saves
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| shared data, ownership | none | not applicable (RWE builds a panel per dialog) |
| discovery, description filter | none | rule 2 |
| summary info, radar preview | `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 4 (the items; the preview picture is MISSING) | rule 3 adds the labels |
| save screen, filter | `TOTALA-EXE-INTERFACE.md` (the text-edit filter, a gadget attribute) | rule 4 |
| save, delete | `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 6 (write result ignored, no atomic write) | rule 5 |
| load screen, admission | `-DATA.md` section 115 (the header's `Thumbs`, `BetweenMissions`) | rules 6 to 8 |

## 2. Discovering saves

**Reference.** The list is built from the configured directory's `*.SAV` pattern: the entries are
counted, buffers of 256 bytes a name and 64 bytes a description are allocated, and **each file
is opened** as a summary bank. A file whose summary has a non-null `Description` (an **empty**
one is accepted) is kept and its description appended; a file with no description is
**dropped**, and the packed names shift over it. The list is not sorted: it is the enumeration
order. When every file is filtered, the allocations still exist but the function returns null.
Repeated calls replace the global pointers without freeing the old buffers (a leak on every
Delete). A long description overflows both its 64-byte slot and a 256-byte scratch path.

**RWE: DIFFERS in two visible ways.**

- **Unreadable files are listed.** `listSaveGames` lists every `*.rwesave` by extension and
  never opens one (`SaveFile.cpp` about 210-226); a damaged or foreign file appears, and
  selecting it blanks the labels (`wireSaveList`, about 100-106) or, in the front end, nothing at
  all. The original shows only files whose summary opened. Kind: UI.
- **Order: DIFFERS, in RWE's favour.** RWE sorts by last write time, newest first; the original
  lists in directory order.
- **The original's `.SAV` files are not listed** (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` gap 5).
  **The 64-byte description and 256-byte path limits, the leaks and the filter-shift:** not
  applicable.
- **Name is the description.** The original copies the description into the name field on
  selection and, on save, uses the typed text as both the file name and the description. RWE's
  list shows the file stem and copies it into the field (`wireSaveList` about 90-98), the same
  one-name model.

## 3. The summary preview

**Reference.** Selecting a save with a non-empty description and an openable summary copies the
description into the name field, frees the old preview and loads the new **radar image** into the
frame gadget, and shows: **Players/Gametype** (a nonzero player count with type one gives
"Single"; another type with players gives "Skirmish" with the count; zero players gives "???"),
**Campaign** and **Mission** (campaign type; the labels are enabled only when the campaign string
exists), **Map** for other types (shown as the mission text), **Game Time** as hours, minutes and
seconds from thirty-hertz ticks (divisors 108000, 1800, 30), **Side** as an index into a packed
side list, and **Difficulty** as Easy, Medium or Hard. Optional strings that are missing leave the
previous values on screen; if selection or opening fails the text is cleared but the **old
picture and the campaign labels stay** (a stale image beside cleared text). Index and string
copies are unchecked.

**RWE: MATCHES the fields, DIFFERS in three places, and the front end shows none.**

- **In the in-game load and save dialogs** the five labels `GAMETYPE`, `SIDE`, `MISSION`,
  `DIFF` and `TIME` are filled from the header on selection (`wireSaveList`, about 66-142),
  and cleared if the header will not read, which is sounder than the original's stale-picture
  case. The time is `h:mm:ss`; the chapter gives the divisors, not the padding (**UNCLEAR**).
- **The front-end load screen: DIFFERS inside RWE.** `goToLoadGameMenu` clears the same labels and
  its selection handler copies only the name (`MainMenuScene.cpp` about 556-583); **no metadata
  is ever shown**. The same files show a description in one place and nothing in the other.
- **Game type wording: DIFFERS.** RWE says "Campaign", "Network" or "Skirmish" and gives no
  player count; the original says "Single" or "Skirmish" and the player count. (A "Network"
  save does not exist in the original, which grays saving in multiplayer.)
- **The map name appears under MISSION** for every type; the original shows the campaign and
  mission names for a campaign and the map for the rest. RWE's campaign saves therefore show the
  map file, not the mission name.
- **The radar image: MISSING** (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 4: nothing is saved,
  nothing is drawn in the frame).

## 4. Opening the save screen, and what may be typed

**Reference.** The save screen loads `LOADGAME.GUI` with flags `0x880`, attaches the `DSAVEGAME2`
background, titles it "Save Game", **grays Delete when the list is empty**, applies **input
attribute two** to the name field, builds the side-name list, refreshes the preview and **begins
editing** the name field. The text-edit filter for attribute two admits **letters, digits,
underscore, space and apostrophe** on typed input only (a pasted string follows different
rules), which the chapter calls "not complete filename validation or a filesystem sandbox".

**RWE: MATCHES the dialog, DIFFERS on what can be typed.**

- **The screen: MATCHES.** The dialog is built from the same `LOADGAME.GUI` with the `DSavegame2`
  background and the name field is focused at open (`openSaveDialog`, about 144-175); RWE
  pre-fills the field with `savegame`, where the original opens with whatever the field starts
  as (the chapter does not say) and begins editing.
- **The typed-character filter: DIFFERS (a source of silent failure).** `UiTextBox::textInput`
  admits **any printable ASCII** (`0x20` to `0x7E`, `UiTextBox.cpp` about 86-110). The path step
  folds only `/`, `\`, `:` and control characters to `_` (`saveFileStem`); the characters
  `*`, `?`, `"`, `<`, `>` and `|` pass through, are not legal in a Windows file name, and make
  the write fail -- which `saveCurrentGame` does not check and reports as "Game saved"
  (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` gap 1). The original's narrower filter makes that
  impossible by construction. A trailing space or dot and the reserved device names
  (`CON`, `NUL`) also pass. The fix is the original's: restrict the field to the five classes, or
  fold every illegal character, and check the write.
- **Delete grayed on an empty list: MISSING.** RWE's Delete is always live; with nothing
  selected it re-opens the dialog (`DELETE` handler, about 905-930).

## 5. Save, delete and cancel

**Reference.** `CANCEL` disables the key commands if the game is in progress, plays the
"previous" sound and lets the dialog close. `DELETE` plays a click, builds the directory and the
selected file name, **removes the file with no confirmation and no result check**, rebuilds
the lists without freeing the old ones, clears the selection and refreshes the preview;
there is no check that anything is selected. `GAMES`, `LOAD` or `GAMENAME` activation is the save:
it plays a click and, **for a non-empty name only**, builds the `.SAV` path, uses the same text
as the description, and calls the file writer with the current time as the game id. **An empty
name does nothing** (and does not clear the activation, so the dialog may close as if cancelled).
There is **no overwrite prompt**, no success or failure message, no temporary file and no
rollback; the writer's result is ignored.

**RWE: DIVERGENCE (safer) with one difference and one known gap.**

- **Delete asks first: DIVERGENCE.** RWE puts a "Delete the saved game?" confirmation in front of
  `DELETE`, in the save and in-game load dialogs (about 905-925 and 965-990); the front-end
  load screen's Delete has none (`MainMenuScene.cpp` about 861-872). A reasonable improvement
  that is inconsistent between the three places.
- **Overwrite asks first: DIVERGENCE.** An existing name raises "Overwrite the saved game?"
  (about 940-955); the original overwrites silently.
- **Empty name: DIFFERS.** RWE substitutes `savegame` and saves (about 933-942); the original
  saves nothing. A player who clears the field and presses OK overwrites (after the prompt) a
  file they never named.
- **The result is ignored and "Game saved" is printed regardless**: the same fault as
  `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` gap 1, and the original has it too. Not repeated.
- **Cancel: MATCHES.** `CANCEL` closes the dialog (`closeSaveDialog`) and returns to the menu root
  or closes the menu when the end-game chart is up.
- **Game id (the current time) and the key-command bit: not applicable.**

## 6. Opening the load screen

**Reference.** `LOADGAME.GUI` with flags `0x980`, the `DLOADGAME2` background. If the list is
empty the new screen is closed and a "no saved games" message opened (its cleanup does not
release the filtered lists). Otherwise it builds the side list, configures the list, **disables
Delete and the name field**, installs the selection callback, refreshes the preview, enables
keys, and disables the surrounding Save entry by name. It validates no saved-unit state and
needs no disc to show the list.

**RWE: MATCHES the empty case in the front end, DIFFERS elsewhere.**

- **No saves: DIFFERS between the two places.** The front end shows "There are no saved games to
  load from" (`goToLoadGameMenu`, about 549-553), the original's behaviour; the in-game load
  dialog opens an empty list (`openLoadDialog`, no check).
- **Delete and the name field are live in the load screen: DIFFERS.** The original disables both
  so the load screen can only load; RWE allows Delete (with a prompt in-game, without one in the
  front end) and a typed name, so a typed name that is not a file reports "Could not read save"
  or silently does nothing.
- **Save and load in a multiplayer game: see `BT-FRONTEND-OPTIONS-PARITY.md` rule 11**
  (the original grays both for multiplayer; RWE does not, and loading leaves the scene).

## 7. Load acceptance and the destructive handoff

**Reference.** Activation of `LOAD` or `GAMES` builds the selected name and probes its summary.
**Only game types one and two are admitted**: a campaign needs the campaign disc, a skirmish the
skirmish disc (a missing disc shows its message, clears the activation and returns, unlike the
end-game and skirmish start handlers); any other type, or a summary that will not open, takes the
**invalid-save path**. The summary is freed and the full bank opened. Then, **before the full
bank or the mission is validated**, it mounts the archives, blanks the screen, shows the
hourglass, disables the key commands for a battle, plays a click, records the save name and, if a
battle is running, **shuts the battle's systems down** (the world is gone, the statistics captured
and the flags changed with no rollback copy). It then sets the end-game bit, opens the full bank
into the pending store, selects the summary account, restores the mission type, optional
campaign, side and difficulty (campaign side zero assigns the first two players sides zero and
one, otherwise one and zero), requires a non-empty `Mission` and a successful mission load (the
full bank's `Mission` is required even for a skirmish, where the preview reads `Map`), copies the
25-byte `Thumbs` into the mission status without a null guard (a length other than 25
re-initialises the mission results), and for a skirmish restores the player count (a short) and
the five rule values with defaults of one, **without range checks**. The success handoff sets
the start and front-end state bits, installs the menu-frame handler, frees the lists and
unwinds the menu layouts, and **keeps the pending store** for the loader; "merely returning from
the UI is not completed unit restoration".

**RWE: DIFFERS in the admission checks, MATCHES in the point of no return.**

- **Admission: DIFFERS.** `loadSavedGame` and `startLoadedGame` accept any save whose file
  parses (`readSaveFile`). There is no disc (not applicable), and no type filter: a save of a
  lockstep network game (`PlayerControllerTypeNetwork` players) is not refused, and what a
  load of one does (a human seat with no peer) was not traced (**UNCLEAR**). The
  original's refusal of every type but campaign and skirmish has no counterpart.
- **Validation order: MATCHES in effect.** The header is parsed before anything is torn down
  (RWE reads the file first, so an unreadable file reports "Could not read save" and leaves the
  game running -- better than the original). Past that the scene is left and the loading scene
  applies the body; a body that will not apply ends the process
  (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 8, gap 2), which is the original's
  "no rollback" in a worse form: the original returns to the front end with a message.
- **Campaign side and difficulty restore: MATCHES** in outcome (the header carries the campaign
  block with side and difficulty, `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 4; the side
  assignment rule is the register's `0x49282C` area).
- **Thumbs length rule and the skirmish rule restore: MATCHES in the header** (a string that is not
  25 letters is replaced; the rule fields are read with defaults); RWE range-checks by enum
  name.
- **Handoff: MATCHES.** The loading scene is given the save path and applies the state instead of
  spawning the starting commanders (`parameters.loadFromSaveFile`, `applyLoadedGame`).

## 8. Between-missions saves and invalid saves

**Reference.** For a campaign save carrying a `BetweenMissions` item (presence is tested, not
its value) the handler sets the campaign bit, closes and frees the pending bank, clears the
start bit, restores the menu-frame handler and selects front-end state fourteen, which goes
to the briefing. The **invalid** path closes and frees any pending store, opens "Invalid savegame
file" and clears the selection; it does not restore the mission, side and difficulty fields,
undo the archive mounts or recreate a battle that was shut down. A smaller helper shows the
translated invalid-save message at width 320.

**RWE: MATCHES the between-missions case, DIFFERS on the message.**

- **Between missions: MATCHES.** `betweenMissions` opens the briefing for the next mission
  (`continueCampaign` in game, `openCampaignMission` from the front end,
  `GameScene_menu.cpp` about 232-239 and `MainMenuScene.cpp` about 596-604).
- **Invalid save: DIFFERS.** There is no "Invalid savegame file" dialog. In-game a console
  line reads "Could not read save: name"; from the front end `startLoadedGame` **returns without
  any message** (about 586-590), so choosing a damaged save appears to do nothing.
- **Pending-bank hygiene (an old bank not freed before a new open):** not applicable.

---

## 9. Gaps to fix

Ranked by what a player would hit. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A save name with `* ? " < > |` fails to write and is reported as saved.** The original's typed-character filter (letters, digits, underscore, space, apostrophe) makes this impossible. | DIFFERS (silent loss) | code: restrict the field or fold the characters; check the write (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` gap 1) |
| 2 | **Choosing a damaged save from the front end does nothing and says nothing**; in game it prints a console line. | DIFFERS | code: one "Invalid savegame file" box, the original's behaviour |
| 3 | **Load can start in a multiplayer game and Save/Load are not grayed.** | DIFFERS | see `BT-FRONTEND-OPTIONS-PARITY.md` gap 1 |
| 4 | **An empty name silently saves as `savegame`.** The original saves nothing. | DIFFERS | code: refuse an empty name |
| 5 | **The front-end load screen shows no metadata and has no Delete prompt**; the in-game one shows both. | DIFFERS (inconsistent) | code: share `wireSaveList` and the prompt |
| 6 | **Unreadable files are listed**; the original hides any file whose summary will not open. | DIFFERS | code: filter on a header read, cache it |
| 7 | **Load screen keeps Delete and the name field live**; the original disables both. | DIFFERS | decision |
| 8 | **Game type wording and the campaign mission name**: "Campaign/Network/Skirmish" and the map under MISSION, against "Single/Skirmish N" and the mission name. | DIFFERS, low | decision |
| 9 | **A lockstep-network save is not refused at load**, and what loading one does is untraced. | UNCLEAR | trace, then refuse types the original refuses |

No source file was changed.
