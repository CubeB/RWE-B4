# Save-bank orchestration, the save summary, campaign registry strings and small setting saves parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SETTINGS-PARITY.md`, which is its sibling, it allocates no
section number in the main register. It covers one chapter of the reference, `game-settings-campaign`
(nine services from `0x432520` to before `0x432ba0`): the wrappers that open the saved-game bank, the
**order in which a whole saved game is loaded and written**, the summary account's items, the
between-missions save, the two Arm/Core campaign registry strings and two single-value setting saves.
The bank's file encoding, the save browser's widgets and the individual unit records are other
chapters. This is the finding that implementation work cites for any of them, per `AGENTS.md` and
ADR-0004.

The settings that the sibling chapter owns (`SaveSettings`, `LoadSettings`, `AllMissions`,
`NumSkirmishPlayers`) are `BT-GAME-SETTINGS-PARITY.md` rules 6 and 8 and are not repeated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-settings-campaign.md`). Nothing
  else under that tree was opened: no source, no decompilation, no source links. Rules are restated in
  prose. The spec-room discrepancy register was read, read-only; BT-82 (the end-of-game countdown) is
  the only nearby row and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `src/rwe/game/SaveFile.*`, `game/save_util.*` (header and
  versioning only), `game/GameScene_menu.cpp` (the save and load dialogs, `saveCurrentGame`,
  `loadSavedGame`), `game/GameScene_endgame.cpp` (`saveBetweenMissions`), `io/campaign/campaign.*`,
  `LoadingScene.cpp` (the load path) and `src/main.cpp` (`--load`), with the tests
  `game/savefileheader.test.cpp` and `io/campaign/campaign.test.cpp`, at the working tree of
  2026-10-17. Nothing was built, run or changed; line numbers are "about".
- **Two observations of the dev machine, read-only:** the registry key
  `HKCU\Software\Cavedog Entertainment\Total Annihilation` holds the skirmish values
  (`NumSkirmishPlayers`, `SkirmishMap` and the like) and **no `ArmCamp` or `CoreCamp` value**; and the
  install has a `SAVEGAME` folder of five original `.SAV` files. They were not opened.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The bank wrappers (not applicable)
3. Loading a whole saved game, and the order it restores in
4. The summary account against RWE's save header
5. A live save against a between-missions save
6. The file operation: failure, atomicity, naming
7. The two campaign registry strings
8. Malformed input
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1 bank interface and ownership | `TOTALA-EXE-EXTERNAL.md` near 262 (the container: 34-byte header, accounts, typed items, boxes, no checksum; "RWE's own save format is its own design") | **DIVERGENCE (documented)**, rule 2 |
| 2 load order | `-EXTERNAL.md` near 270 (Players, Camera, Features, Metal, PlayerFeatures, Mapping, Units, Meteor, triggers; no rollback) | the load order agrees with the chapter's list; the chapter's write order is a different one (rules 3, 5) |
| 3, 4 summary items, live versus between missions | `TOTALA-EXE-DATA.md` section 115 ("Progress is kept only in save games"; the header's `Campaign`, `Mission`, `Difficulty`, `Side`, `Thumbs`; the header-only `BetweenMissions=1` save) | the register agrees with the chapter on the header; rules 4, 5 |
| 5 file operation | `-EXTERNAL.md` near 266 (truncate-write, no temp file, no backup; write result ignored) | rule 6 |
| 6 `ArmCamp`/`CoreCamp` registry strings | the same section 115 says **no registry key or file holds the progress** | CONFLICT (rule 7) |
| 7 `AllMissions`, `NumSkirmishPlayers` | `BT-GAME-SETTINGS-PARITY.md` rules 6, 8 | not repeated |

## 2. The bank wrappers (not applicable)

**Reference (section 1).** Two openers allocate a bank object, open the file with the identifier
`Total Annihilation 3.0`, and either select the `Summary` account or leave the account unselected;
neither checks the result of selecting an account (which may create a missing one), and neither guards
a failed allocation. A close helper releases the bank. Pointers into the bank's accounts live only as
long as the bank.

**RWE: not applicable.** A save is a JSON document, `<name>.rwesave`, in the `saves` folder under the
local data path (`SaveFile.cpp`, `CLAUDE.md` "Saved games"); there is no bank, no account, no
identifier string. The original's `.SAV` files are not read: `HpiArchive` refuses a `BANK` version
word ("Unsupported HPI version"), and nothing else opens one. A save made in RWE cannot be loaded by
TA and the reverse. That is the intended design (`-EXTERNAL.md` says so); it is recorded here
because `listSaveGames` lists only `.rwesave`, so the player's TA saves are invisible in RWE's
Load list. The chapter's allocation and ownership faults do not exist.

## 3. Loading a whole saved game, and the order it restores in

**Reference (section 2).** Load selects the summary account, reads `maxunits` if present into the
unit-limit setting (taking the existing value when absent; a wider number is truncated to 16 bits),
then restores in this exact order: players, camera, terrain features, the metal plot map, the
player-feature plot map, the mapping (exploration) data, units, meteors, victory conditions; it then
sets a loaded bit and returns 1. None of the calls reports failure; **there is no rollback**, no null
test on the bank and no cancellation after a partly restored state.

**RWE.** `LoadingScene` (about 357-368) re-runs the loading pipeline for the saved map and players, then
`GameScene::applyLoadedGame` clears the players and calls `loadSimulationFromJson` on the nested
document (`GameScene_menu.cpp` about 250-257); the camera comes from the header.

- **Order: not applicable.** One structured document is restored as a whole (`save_util.cpp`, with a
  `version` field, 1 or 2, refused otherwise); the dependency the chapter spells out (players and map
  data before units, victory conditions after the objects they name) is carried by the document
  format, not a call order.
- **`maxunits`: DIFFERS in kind.** RWE does not save the unit limit; a load takes it from the map's OTA
  again (`GameSimulationLoader.cpp` about 1010). The two agree whenever the map is the same file.
  If a mod edits the OTA's `maxunits` between save and load, the loaded game uses the new one; the
  original uses the saved one.
- **Failure: DIFFERS in a worse direction.** The original loads what it can and carries on; RWE's
  `loadSimulationFromJson` throws on a field it cannot read, and nothing around the call catches it
  (the same for `LoadingScene` when the header will not read: "Could not read save file"). The
  header is read inside a `try` and an unreadable header returns "no save" to the Load list, which is
  right; the body is not, so a hand-edited or damaged save with a good header **ends the process
  through `main`'s "Critical Error" box**. See rule 8.
- **No rollback: DIFFERS in RWE's favour in effect.** A load happens into a freshly built simulation
  (the old game is gone), so a failure cannot leave a half-restored current match.

## 4. The summary account against RWE's save header

**Reference (section 3).** The summary is written as an item list: `maxunits`, `Campaign` (the
mission object's campaign name), `Mission` (the selected mission's name, **after** a possible
advance), `Map` (the same name), `Difficulty`, `Side` (the local player's side), `Players` (count),
`Gametype` (numeric), `Thumbs` (the 25-letter campaign status), an optional `Description`, `Game ID`
(a caller integer), `Game Time` (the tick). The build date and time are written as **two integer items
whose names are the formatted date and time strings and whose values are zero**. For game type 2
(skirmish) it adds `CommanderDeath`, `Location`, `Mapping`, `LineOfSight` and `LineOfSightType` from the
setup options.

| Chapter item | RWE header (`SaveFile.cpp` about 235-285) | Verdict |
| --- | --- | --- |
| `maxunits` | not saved | DIFFERS (rule 3) |
| `Campaign` | `campaign.campaign` | MATCHES |
| `Mission`, `Map` | `campaign.mission` (index) and `mapName` | MATCHES in content; the name is the map file, the index is the position |
| `Difficulty` | `campaign.difficulty` (clamped to 2); `aiDifficulty` for a skirmish | MATCHES for a campaign |
| `Side` | `campaign.side` (clamped to 1) and each player's `side` string | MATCHES |
| `Players` | the `players` array | derived |
| `Gametype` | not saved; the Load list derives "Campaign", "Network" or "Skirmish" | DIFFERS, no consequence |
| `Thumbs` | `campaign.thumbs`; a string that is not 25 letters is replaced by 25 `U` (the original loader's rule at `0x49282C`, per `-DATA.md`) | MATCHES |
| `Description` | not saved; the file name is the only label | MISSING |
| `Game ID` | not saved | UNCLEAR (what the integer identifies is not in the chapter) |
| `Game Time` | `gameTimeSeconds` (seconds, for the list's TIME column only) | MATCHES in use; a save written before the field loads empty |
| build date and time | not saved; no build identity | MISSING |
| skirmish options | `lineOfSight`, `mapping`, `startLocation`, `commanderDeath` (plus `pathfinding`) | MATCHES; the original's names are `Location` and `LineOfSightType` |
| Radar Image box (a picture of the map) | no picture is saved or shown (`LOADGAME.GUI` has a radar frame; `GameScene_menu.cpp` about 153 mentions it and nothing draws in it) | MISSING |

RWE adds: `schemaIndex`, each player's controller, colour, starting resources, team, AI personality and
engine (ADR-0005), `mission`, the camera, and the post-game screens' fields (`glamour`, `glamourSound`,
`noMovie`, `hasNextMission`) that the original keeps in the mission object.

- **Build stamp: MISSING, low.** A save written by one build and loaded by another resumes under the
  newer build's rules. `save_util.cpp` has a document `version` for shape changes and the lobby has
  `BuildIdentity` for network games; neither marks which engine wrote a single-player save.
- **Dates written as item names: not applicable.**

## 5. A live save against a between-missions save

**Reference (section 4).** In the live state the writer opens a `Radar Image` box, saves the screen
into it, and then calls the serialisers in this order: camera, players, units, mapping, terrain
features, the player-feature plot map, the metal plot map, meteors, victory conditions (a different
order from loading). In any other state it first **advances the mission** (so `Mission` and `Map`
name the next one), writes `BetweenMissions = 1`, restores the in-memory selection and writes **no world
at all**. It does not pause the simulation or lock anything.

**Register.** `TOTALA-EXE-DATA.md` section 115: "A save made from ENDMSN is a header-only
`BetweenMissions=1` save that records the **next** mission (`0x432786`) and reloads to its briefing."

- **Between missions: MATCHES the register and the chapter.** `saveBetweenMissions` (`GameScene_endgame.cpp`
  about 438-458) writes the header with the mission index advanced when there is a next one, the next
  mission's map name, the difficulty the end-of-mission screen shows, `betweenMissions = true` and no
  `sim`; loading one opens that briefing (`GameScene_menu.cpp` about 228). It applies "whether the last
  mission was won or lost", as the register says. The `hasNextMission` field stands in for "advance if
  there is a next mission".
- **Live save: DIVERGENCE (documented).** The whole simulation is written as one nested document
  (`saveSimulationToJson`), refused for a game with a player simulated elsewhere or an own-clock game
  (`CLAUDE.md`, ADR-0003). The chapter's list of serialisers and their order has no counterpart.
- **No picture: MISSING** (rule 4).
- **Save point: MATCHES in effect.** RWE saves from the pause/menu state and from the end-of-mission
  screen; it does not freeze anything either, since the menu is the pause.

## 6. The file operation: failure, atomicity, naming

**Reference (section 5).** `SaveBank` receives the path, the identifier, compression on and audit
off; its result is returned and the temporary bank is closed. No per-subsystem result is tested. There
is no temporary file or backup at this level (`-EXTERNAL.md`: truncate-write, no fsync, name truncated
at a dot, write result and delete result ignored by the interface).

**RWE.** `writeSaveFile` (`SaveFile.cpp` about 289-296) opens the path with truncation and writes the
dumped JSON.

- **Atomicity: MATCHES the original.** Truncate-write, no temporary file, no backup: an interruption
  during the write loses the previous save of that name. (Writing to a temporary name and renaming
  would be a free improvement.)
- **Reporting: DIFFERS in a worse direction.** The function returns nothing and never looks at the
  stream after writing; `saveCurrentGame` and `saveBetweenMissions` then print "Game saved: <name>"
  unconditionally. A full disk or a read-only folder is reported as success. The original's
  `SaveBank` result is at least passed back to its caller.
- **Name handling: DIFFERS.** The original truncates a name at the first dot; RWE keeps the whole
  name and appends `.rwesave`. The name comes straight from the dialog's text box into
  `getSaveDirectory() / (name + ".rwesave")` with no filtering, so a name with separators or `..`
  writes outside the saves folder, and an absolute path replaces it. It is the player's own text on
  their own machine (also `--load <name>` on the command line); not a network vector.
- **Listing and ordering: n/a.** Newest first by modification time.

## 7. The two campaign registry strings

**Reference (section 6).** `LoadCampaignRegistryName(side)` reads a **26-byte registry value** named
`CoreCamp` for a non-zero side and `ArmCamp` for zero; on a failed read it supplies 25 letters `U`
and a NUL (a fixed-length campaign **status** string). `SaveCampaignRegistryName` forces byte 25 to NUL
in the caller's buffer and writes it back as a string. No per-mission completion is computed here and
neither routine awards or unlocks anything. These are distinct from the summary's `Thumbs`.

**Register.** `TOTALA-EXE-DATA.md` section 115, "Progress": "At game end `0x41DC20` records the
result ... a 25-byte string `[g+0x391CF]` ... **Progress is kept only in save games. No registry key
or file holds it.** A save's header carries ... `Thumbs`, the 25-character string (`0x432869`)." and
"`Start` resets the progress string to 25 `U`s (`0x41DA30`)".

- **CONFLICT, register versus reference.** The chapter says two registry values, one per side, hold a
  26-byte status string with a default of 25 `U`; the register says nothing in the registry holds
  campaign progress. The two readings of the 25-`U` default coincide, which is why the register's
  reset at `0x41DA30` could be the same string. Neither source says who calls the load and save
  routines. This machine's registry key has neither value, but it also shows no sign that a
  campaign was ever finished on it, so that does not settle it. A spec-room read of the callers of
  `0x432a80` and `0x432b00` does. Recorded as a conflict in `BT-NEW-CONFLICTS.md` batch 9.
- **RWE: MISSING if the chapter is right, MATCHES the register if not.** RWE keeps campaign
  progress in the running game's `CampaignProgress` (25 letters `W`, `L`, `U`; `GameParameters.h`
  about 148-165) and in saves only; nothing is written to a configuration file when a mission ends.
  If the original really restores a per-side status from the registry at campaign start, a
  campaign started fresh in the original would show the previously completed missions marked; RWE's
  list starts all `U` (`startCampaignMission`: "a campaign started from this screen starts with every
  mission untried (0x41DA30)").
- **Twenty-five missions: MATCHES.** A campaign with more than 25 missions cannot record a result for
  the 26th (`GameParameters.h` guards the index; the endgame list shows `U` beyond 25).

## 8. Malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") applies to a save (a file in a
folder players share) and to a campaign TDF (a mod). `savefileheader.test.cpp` has one malformed case
(a header field of the wrong type reads as unreadable); `campaign.test.cpp` has none for malformed
input; `malformed_input.test.cpp` has none for either.

- **Save header: bounded.** The parse is non-throwing (`parse` with exceptions off), every `.at`
  and `.get` sits inside a `try`, the players array is read up to the slot count, the campaign
  fields are clamped (difficulty 2, side 1, thumbs rebuilt to 25), enum names fall back to defaults.
  What is not bounded: the whole file is read into memory before parsing, so a save is as large as the
  file is.
- **Save body: not audited here and uncaught.** `loadSimulationFromJson` reads units, features and the
  command state out of the document; a thrown error is not caught between it and `main`, as rule 3
  says. This is a local file, not a peer's, so the cost is the player's own session, but it is the
  inverse of "a fault in content costs that content".
- **The campaign TDF.** `readCampaign` parses inside a `try` and returns "no campaign"; the mission loop
  runs while `MISSION<n>` blocks exist, so it is bounded by the file. A mission whose map is missing is
  a message box, not a crash (`MainMenuScene_campaign.cpp` about 296-310).
- **The write path** (rule 6): the unchecked stream and the unfiltered name.

## 9. Gaps to fix

Ranked by what a player would hit. None touches the simulation's rules.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **"Game saved" is printed whatever the write did**; a full or read-only disk loses the save silently. | DIFFERS (worse than the original) | code: check the stream, report failure; write to a temporary name and rename |
| 2 | **A save with a good header and a bad body ends the process**; a name with separators writes outside the saves folder. | fault | code: catch around `loadSimulationFromJson` in `LoadingScene` and return to the menu with a message; reject path characters in the name |
| 3 | **Do the registry values `ArmCamp` and `CoreCamp` carry campaign progress between sessions?** The register says nothing in the registry holds it. | CONFLICT (register) | spec-room read of the callers of `0x432a80` and `0x432b00` |
| 4 | No description, no picture, no build stamp in a save; `maxunits` comes from the map, not the save. | MISSING | decision; a picture needs the LOADGAME radar frame wired |
| 5 | The player's original `.SAV` files are invisible to the Load list and unreadable. | DIVERGENCE (documented) | none; say it in `compatibility.md` if it is not already there |
| 6 | The load order and the serialiser list differ from the chapter's. | DIVERGENCE (documented) | none |

No source file was changed.
