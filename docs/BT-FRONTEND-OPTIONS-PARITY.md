# Options pages, live preferences, undo and defaults, and the in-game menu and exit dialogs parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SETTINGS-PARITY.md`, `BT-INGAME-DIALOGS-PARITY.md`
and `BT-FRONTEND-FRONTEND-PARITY.md`, it allocates no section number in the main register. It
covers one chapter of the reference, `frontend-options` (70 services from `0x45b800` to
`0x460e20`): the Options shell and its four pages (sound, music, visual, speed and interface),
the immediate side effects of each control, the snapshot behind Undo and Cancel, the per-page
defaults, the read-only game-settings, briefing and help dialogs, and the in-game menu with its
exit, restart and surrender dialogs. RWE's save format is its own, so the **UI behaviour** is
what is compared. This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

Not repeated: which registry values persist and their defaults (`BT-GAME-SETTINGS-PARITY.md`
rules 4 and 6, where the unit-speech thresholds, volume defaults and disabled gadgets are
recorded); the music selection rules (`TOTALA-EXE-MUSIC.md`, `TOTALA-EXE-INTERFACE.md` section
68); the restart request's mission handling (`BT-INGAME-BUILD-PLACEMENT-PARITY.md`, the
register's section 63); the YESORNO dialog's role in the CD check
(`BT-FRONTEND-FRONTEND-PARITY.md`). The save and load screens that the in-game menu opens are
`BT-GAME-SAVEGAME-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-options.md`).
  Nothing else under that tree was opened beyond the sibling chapters named in these audits: no
  source, no decompilation, and none of its source links were followed. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only; BT-175 (keys while a menu is open) touches the same menu and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked. Every
  statement about what a player sees is untested.
- **The RWE half** is a read-only audit of `game/GameScene_menu.cpp` (the in-game menu,
  options pages, exit and confirm dialogs, `setMenuPause`), `MainMenuScene.cpp` (the front-end
  options pages and their handler), `GlobalConfig.h` (`GameOptions` and its defaults) and
  `game/GameScene_ownclock.cpp` (`canSave`), at the working tree of 2026-10-17. Nothing was
  built, run or changed. Another agent had uncommitted edits in the tree, so line numbers are
  "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The two contexts and the pause policy
3. The undo snapshot, Cancel, OK, Undo and Default
4. Defaults
5. Sliders, conversions and immediate effects
6. The sound page
7. The visual page and the video-mode list
8. The speed and interface page
9. The music page
10. The read-only dialogs: game settings, briefing, help
11. The in-game menu, restart, surrender and quit
12. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| contexts, pause | `TOTALA-EXE-INTERFACE.md` (the PREFS sidebar at y 126, section 70 pause) | rule 2 |
| snapshot and undo | none | rule 3 |
| defaults | `BT-GAME-SETTINGS-PARITY.md` rules 4 and 6 (registry defaults) | rule 4 gives the **button** defaults |
| sliders | `TOTALA-EXE-INTERFACE.md` near 547 (`TXTSCROL`, `MAXLINES`, `UNITCHAT`) | rule 5 |
| sound, visual, speed pages | `BT-GAME-SETTINGS-PARITY.md` rule 4; `BT-SOUND-PARITY.md` rule 7 | rules 6 to 8 |
| music page | `TOTALA-EXE-MUSIC.md`, `-INTERFACE.md` section 68 | rule 9 (pointer) |
| game settings, briefing, help | none | rule 10 |
| in-game menu, exit, restart, surrender | `-INTERFACE.md` section 63; `BT-INGAME-BUILD-PLACEMENT-PARITY.md` | rule 11 |

## 2. The two contexts and the pause policy

**Reference.** One flag distinguishes a battle from the front end. The options layout is
`PREFS.GUI` in a battle and `STARTOPT.GUI` outside it. In a battle it also sets the orders-panel
loaded bit, grays the music tab according to the sound object's device, enables the GUI key
commands and **pauses a non-multiplayer battle** by setting pause bit zero; a multiplayer game
is not paused ("local UI policy, not pausing every connected peer"). Closing the in-game menu
clears pause bit zero for an active non-multiplayer battle and un-pauses the CD player; it
**restores no earlier pause state**, it writes a fixed policy. Opening the in-game menu also
pauses CD audio for every mode.

**RWE: MATCHES the pause rule; DIFFERS in two small ways.**

- **Which games pause: MATCHES.** `setMenuPause` pauses only a game with no remote peers and
  not in own-clock mode ("F2 pauses only single player", `GameScene_menu.cpp` about 275-298),
  which is the chapter's non-multiplayer test.
- **Restoring the pause: DIFFERS, in RWE's favour.** RWE remembers whether the menu paused the
  game (`menuPausedGame`) and un-pauses only then, so a game the player had already paused is
  still paused when the menu closes; the original un-pauses it. A player-visible difference of
  one frame of surprise.
- **CD audio pause: not applicable.** RWE plays files, not a CD; its menu leaves the music
  running (`BT-GAME-SETTINGS-PARITY.md`).
- **The music tab gray by device: not applicable.**

## 3. The undo snapshot, Cancel, OK, Undo and Default

**Reference.** Opening the options panel takes one **shared** in-memory snapshot: 83 bytes of
settings, the two line-of-sight bits, the game-speed control, the scroll speed, the locked CD
track and the hundred track categories. It is not nested per page, and opening another shell can
replace it. Each page has **its own** Undo and Default buttons that restore or default **that
page's subset** and rebuild the page: sound, music, UI (speed and interface), visual. The panel's
OK (`PREV`) **saves** the persistent settings and sets a closing flag; **Cancel restores the
snapshot subsets** and sets the same flag. Restores are subsets: the undo does not memcpy the
whole snapshot, some captured fields have no restore, and the sound restore writes the speech
switch from the saved bit **five** (a source defect). The closing flag is written and never
read, so it is not an implemented timed animation.

**RWE: MATCHES at the panel, DIFFERS at the page.**

- **OK and Cancel: MATCHES.** `PREV` saves the staged values to `rwe.cfg` and applies the window
  mode; `CANCEL` restores `gameOptionsUndo`, taken when the panel was opened
  (`GameScene_menu.cpp` about 882-891 and 1095-1109). The front end does the same with
  `optionsUndo` (`MainMenuScene.cpp` about 250, 905-925).
- **Per-page Undo and Default: DIFFERS.** RWE's `UNDO` calls `applyInGameOptions(gameOptionsUndo)`
  and `RESTORE` calls `applyInGameOptions(GameOptions{})` -- **every** setting on **every**
  page -- and then rebuilds the current page. In the original, Default on the sound page leaves
  the visual and speed settings alone. In RWE, pressing Default on the sound page also resets
  gamma, window mode, scroll speed and camera zoom. Kind: UI; a player who resets one page to
  rescue the sound loses their video settings.
- **Subset defects: not reproduced and not needed.** The sound-restore bit-five slip, the unrestored
  fields and the repeated device calls are the original's quirks; RWE's whole-state restore has
  none of them.

## 4. Defaults

**Reference.** The page defaults are:

| Page | Default | Values |
| --- | --- | --- |
| sound | `ApplyDefaultSoundOptions` | effects volume **27**, sound bits four, five and six set, 3D off, mode stage **one**, unit-speech amount **ten** |
| music | `ApplyDefaultMusicOptions` | music volume **32**, CD mode **four**, music on (starts the next track only if it was off); the playback order is not re-applied |
| UI | `ApplyDefaultUiOptions` | message lines **ten**, requested and effective game speed **ten**, scroll speed **32**, interface type zero, unit speech ten, unit-chat text **five** |
| visual | `ApplyDefaultVisualOptions` | visual bits one to five set, gamma **twelve**; outside a battle, 640 by 480 with bit six cleared |

**RWE: DIFFERS in the numbers; MATCHES where the scale is the same.** `GameOptions{}`
(`GlobalConfig.h` about 415-440): both volumes **100 percent**, sound mode **Stereo** (stage two;
the original's default is stage **one**), unit speech Full, track mode Custom, gamma **100** (RWE's
scale is percent of the original's: the original's twelve on `0.5 + g/24` is exactly **1.0**, so the
visual default **MATCHES**), scroll speed **100** percent. The volume defaults are
`BT-GAME-SETTINGS-PARITY.md` rule 4's item; the sound-mode stage is new here: the reference's
default is stage one and "3D" only for stage two exactly, while RWE's default is stage two, named
Stereo, and treats stage one and two alike. Whether the stage names match (the register names the
stages Off, Mono and 3D) is **UNCLEAR**, and with the two stages identical in effect it changes
nothing a player hears.

## 5. Sliders, conversions and immediate effects

**Reference.** Slider ranges are: effects and music volume **64**, gamma **20**, game speed
**21**, scroll speed **65**, message lines **30**, text delay **20** seconds. Position
conversion upper-clamps, scales to range minus one and rounds up on a fractional part, with no
lower clamp; the reverse truncates, so the two are quantised rather than exact inverses. The
callbacks apply at once: gamma sets brightness `0.5 + gamma/24` and both volumes together;
effects and music volume re-apply the same combined call; the **game-speed slider** (unless the
local player is an observer) clamps to at least one, stores it and calls the speed setter with the
**broadcast** argument; the scroll slider clamps to at least one; message lines clamp at zero
and show "None" at zero; the text delay shows plain seconds. Opening a page **invokes every
slider callback once**, so opening the speed page can normalise a quantised value and call the
speed setter -- re-broadcasting the speed -- rather than merely display it.

**RWE.**

- **Gamma: MATCHES.** The scroll bar maps 0 to 1 onto gamma 50 to 133, the code comment gives "the
  original's twenty steps of 0.5 + v/24: 0.5x to 1.333x" (`GameScene_menu.cpp` about 645-656).
  Continuous rather than twenty steps; the range is the chapter's.
- **Game speed: MATCHES in range, DIVERGENCE in mechanism.** The slider covers the 21 positions
  (`GameSpeed::MaxIndex`), and a change is a command in the lockstep stream
  (`PlayerSetGameSpeedCommand`), where the original calls the setter with broadcast. The
  observer exclusion is BT-179's subject (any non-watcher may change speed); RWE does not
  restrict by role here. Opening the page does **not** re-broadcast, which is better than the
  original.
- **Scroll speed: DIFFERS.** RWE's slider is 25 to 200 percent of its own base
  (`scrollSpeedSetting`), not the original's 1 to 65.
- **Message lines and text delay: MISSING.** Both gadgets are disabled (`LEFTCLICK`, `UNITCHAT`,
  `TXTSCROL`, `MAXLINES`, about 743-750); `BT-NETWORK-NET-CHAT-PARITY.md` rule 4 shows what the
  delay actually controls.
- **Volumes: DIFFERS in scale only**: RWE's bars are 0 to 1 of the mixer.

## 6. The sound page

**Reference.** `SPEECH` is a stage; its stage is stored both as the "speech enabled" flag
(nonzero) and as `unitChat` equal to **stage times five** (so stages 0, 1, 2 mean 0, 5, 10:
Off, Medium, Full). The `MODE` button copies the low three bits of the sound word; zero **stops
all sounds**; three-dimensional sound is enabled for stage two only; in the front end the
looping background music restarts for stage one. `TEST` plays `sounds/explode.wav` through the
newly applied settings. The page's sliders are all invoked at open (rule 5).

**RWE: MATCHES the stages and the stop; DIFFERS in the test sound.**

- **SPEECH stage to threshold: this confirms the guess in `BT-SOUND-PARITY.md` rule 7.** Stage
  times five gives threshold values 0, 5 and 10, so Medium is five; RWE's `UnitSpeechLevel`
  stages Off, Medium, Full exist (`GameScene_menu.cpp` about 1157-1160) but gate a different set
  of sounds (that rule).
- **MODE: MATCHES.** The button cycles three stages and Off stops effects
  (`setSoundEnabled`, about 1151-1156); stage one and two are alike (`BT-GAME-SETTINGS-PARITY.md`).
- **TEST: DIFFERS.** RWE plays the `BUTTON10` UI click on the sound page of both the in-game menu
  and the front end (`GameScene_menu.cpp` about 1204-1210; `MainMenuScene.cpp` about 1008-1013).
  The original plays an **explosion** (`sounds/explode.wav`), which is the point of a test: it is
  the loudest effect, played at the new effects volume. A click tells the player nothing about
  the volume of gunfire. One word to change.

## 7. The visual page and the video-mode list

**Reference.** Three toggles: `ANTI` writes bit one; `BSHADOWS` writes **three** bits (four,
three, two) together; `SHADING` writes bit five. In a loaded battle a change **flushes the unit
bitmap cache** (the models are not rebuilt). Outside a battle the page opens the list of display
modes (sorted by ascending width then height, **entries under 640 by 480 dropped**, no
de-duplication), a slider over the list showing the current width and height, and writes the
chosen size to the configuration; a one-mode list divides by zero at initialisation. In a
battle the page passes null data and **deactivates the controls whose names begin with `MAP` or
`VID`**, so resolution cannot be changed in a battle. The standalone video page (`SELVMODE`) is
a separate shell with an OK button.

**RWE: DIVERGENCE (by design) with one behavioural difference.**

- **No resolution list.** RWE replaces the mode slider with a three-stage **window mode** slider
  (windowed, borderless, fullscreen; the same `VIDSLDR` and `VIDVAL` gadgets,
  `GameScene_menu.cpp` about 701-718) and applies it on OK (`setWindowMode`). RWE's renderer
  scales to the window, so a resolution list has no meaning.
- **Changeable in a battle: DIFFERS.** The original refuses video changes in a battle; RWE
  applies the window mode from the in-game menu. Harmless and a convenience.
- **The three shadow bits: UNCLEAR.** The original keeps three shadow bits behind one button;
  RWE's `BSHADOWS` is one flag (`BT-GAME-SETTINGS-PARITY.md` gap 9 lists a third shadow bit as
  absent). Which of the three the renderer reads was not decided.
- **Extras: RWE-original.** `HALO`, `AAUNITS`, `CAMZOOM`, `UISCALE` are added gadgets
  (`GameScene_menu.cpp` about 516-587) with no counterpart; the page layout is adjusted around
  them.
- **Cache flush: not applicable.** RWE has no unit bitmap cache; shadows and anti-aliasing apply
  on the next draw (the anti-alias toggle recreates the render textures).

## 8. The speed and interface page

**Reference.** `LEFTCLICK` stores its stage directly as the interface type; `UNITCHAT` stores
stage times five as the unit-chat text threshold; the sliders are `GAME`, `SCREEN`, `MAXLINES` and
`TXTSCROL` (rule 5). Undo and Default restore the UI fields and rebuild the page; the speed fields
are restored by direct assignment and the speed setter runs only when the page's open callback
re-invokes it.

**RWE: MISSING for four of six controls.** `LEFTCLICK` (the mouse interface), `UNITCHAT`
(the text threshold), `MAXLINES` and `TXTSCROL` are disabled (about 743-750). The game speed and
scroll sliders work (rule 5). The mouse interface is the consequential one: the original's
two interface types change what left and right click do; RWE implements both (the config key
`interface-mode`, a debug checkbox) but the gadget is disabled and the choice is not written
(`BT-GAME-SETTINGS-PARITY.md` rule 3, "Interface Type").

## 9. The music page

**Reference.** The page opens a nested shell (`MUSICRT.GUI` in a battle, `MUSIC` outside), sets
the volume slider and the tab state and refreshes the controls and track state; **opening it is
not read-only**: in category mode it writes the selected category back to the sound object.
`NOTRAK` copies stage bit zero into the music-enable flag and enables CD audio; `TRACKMODE` stores
stage plus one and sets the playback order (mode three selects the saved locked track, mode four
updates the category state); `TRACKTYPE` changes the category of the selected track, offered
only for music on and mode four; `CDPLAY` plays; `CDNEXT` and `CDPREV` wrap through track one to
the count; `CDSTOP` stops and selects track one. Page cleanup stops CD audio outside a battle or
plays the next track in a battle.

**RWE: MATCHES in structure; the details are other audits.** The same controls exist
(`NOTRAK`, `TRACKMODE` with Play All, Random, Repeat and Custom, `TRACKTYPE` offered only while
music is on and the mode is Custom, `CDPLAY`, `CDSTOP`, `CDNEXT`, `CDPREV`;
`GameScene_menu.cpp` about 1120-1203), and `TRACKTYPE` carries the same offer condition. The
rules for choosing the next track are `TOTALA-EXE-MUSIC.md`. Two differences from the chapter:
`CDSTOP` in RWE only stops, with no "select track one" (`stopMusic`), and RWE's `CDNEXT/CDPREV`
store a step and stop the current track so the rotation picks the next, where the original
wraps an explicit index. Neither affects a player noticeably.

## 10. The read-only dialogs: game settings, briefing, help

**Reference.**

- **Game settings** (`GAMEOPTIONS.GUI`, flags `0x1881`): label pairs for commander death (Game
  Continues, Game Ends, Deathmatch), start positions (Random or Fixed), mapping (Mapped or
  Unmapped), line of sight (True, Circular, Permanent), in multiplayer the cheat and watching
  permissions, otherwise the **difficulty**, the map name, starting metal and energy and the
  unit limit. Multiplayer resource shorts are shown scaled by one hundred. It reads the host's
  player record or the local setup; its OK only plays a sound and closes; it changes nothing.
  The in-game menu's `MISSION` button opens it for skirmish and multiplayer (the button is
  relabelled "Settings") and the **briefing** for a campaign.
- **Briefing** (`BRIEFING.GUI`): text and pagination controls, page advance on the text region or
  `MOREBAR`, narration set up by the campaign briefing code; the handler frees its blink slots but
  not the wrapped text buffer (a leak on repeated opens).
- **Help** (`HELP.GUI`): 17 rows a page from `gamedata/help.TDF`, each row a "left | right" pair
  split at a pipe and drawn as two labels at x 40 and 125 from y 50 in 18-pixel steps, the page
  button cycles pages; a row with a leading pipe shows two blanks; a row with no pipe runs the
  scan past its buffer.

**RWE: MISSING, all three.** The in-game menu grays `MISSION` and `HELP` ("the briefing and help
do not exist in RWE yet; the original greys what does not apply", `GameScene_menu.cpp` about
339-344), and nothing shows the game settings. The data for all three is already held: the
skirmish and lobby rules are in `GameParameters` (commander death, location, mapping, line of
sight, starting resources, map, difficulty) and the help pages are shipped data
(`gamedata/help.TDF`, whose 35th line is the F3 text quoted in `TOTALA-EXE-KEYBOARD.md`). The
settings dialog is the cheapest and the most useful in a network game, where it is the only way
to see the host's rules after the lobby is gone. RWE's own F1 overlay is a help screen of its own
(`TOTALA-EXE-KEYBOARD.md` 390, "different").

## 11. The in-game menu, restart, surrender and quit

**Reference.**

- **In-game menu** (`ARMOPT.GUI`, with fade): `SAVEGAME` and `LOADGAME` are **grayed for
  multiplayer**, `MISSION` becomes "Settings" for skirmish and multiplayer. It routes `LOADGAME`
  and `SAVEGAME` to the save and load screens, `PREFS` to the options shell, `HELP` to help,
  `MISSION` to the briefing or the game settings, `EXIT` to the exit menu, and `OK` closes. Keys
  are enabled while it is open; cleanup applies the fixed pause policy of rule 2.
- **Exit menu** (`EXITMENU.GUI`): `RESTART` is enabled and relabelled for campaign and skirmish
  only; `MAINMENU` is hidden when the lobbied flag is set; the chapter says it does not
  deactivate `RESTART` for other modes and relies on the layout default. `MAINMENU` (intent
  zero) and `EXITGAME` (intent two) open a **surrender prompt**.
- **Surrender prompt** (`YESORNO.GUI`): Yes and No, with **both the accept and the cancel
  defaults set to the No choice**, so Enter and Escape both mean No. Intent zero asks to return to
  the main menu, intent two to exit (with a different title when lobbied). Yes shuts the in-game
  systems down and either returns to the front end or sets the end-game flag and quits.
- **Restart dialog** (`RESTART.GUI`): the mission name wrapped to two lines, difficulty buttons;
  OK mounts the archives, copies the difficulty and sets the **restart request** (it does not
  reload directly); unsupported mission types return without setting it.

**RWE: MATCHES the structure; DIFFERS for multiplayer.**

- **The routes: MATCHES.** The same buttons and topics exist (`gameMenuMessageNow`,
  `GameScene_menu.cpp` about 866-1060) and the prompt texts are the register's ("Surrender this
  battle and return to main menu?", "... exit to Windows?", section 63).
- **Save and load in a network game: DIFFERS.** The original grays both buttons for any
  multiplayer game. RWE grays neither; saving is refused only for an own-clock game and only
  when the player presses (`openSaveDialog`, `canSave`), and **loading is available in a
  lockstep game**: choosing a save leaves the scene (`loadSavedGame` -> `leaveFor`), i.e. one
  player abandons a live game without warning the others. What the other peers then see (a
  disconnect, a stall) was not traced. Kind: UI rule; gray the buttons when
  `hasRemotePeers()` or own-clock.
- **Restart in a network game: DIFFERS.** `openGameExitMenu` enables and labels `RESTART`
  unconditionally; the comment says RWE "has no multiplayer game in progress to gate it on",
  which is stale (lockstep and own-clock games exist). `restartGame` leaves the scene for a fresh
  `LoadingScene` over the same parameters, which in a lockstep game is a one-sided restart.
  The original enables it for campaign and skirmish only.
- **`MISSION` relabel and the Settings dialog: MISSING** (rule 10).
- **Surrender defaults: UNCLEAR.** RWE reuses `YESORNO` through `openConfirmDialog`, and the
  shipped GUI file's accept and cancel names are what the panel honours (`setDefaultButtons`
  appears in the multiplayer menu code, about line 1346); whether Enter in the surrender prompt
  means No as in the original was not traced.
- **Restart difficulty buttons: not traced** (`openRestartMenu` builds `RESTART.GUI` as is).

---

## 12. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Restart and Load are available in a network game** and leave the scene for one player only. The original enables Restart for campaign and skirmish only and grays Save and Load for multiplayer. | DIFFERS | code: gate the three buttons on `hasRemotePeers()` and own-clock; update the stale comment |
| 2 | **Default and Undo on one options page reset every page.** | DIFFERS (UI) | code: restore only the page's own fields (sound, music, UI, visual) |
| 3 | **No game-settings dialog (and no briefing, no help).** `MISSION` and `HELP` are grayed though the data exists. | MISSING | code: a read-only panel from `GameParameters` for `MISSION`; `help.TDF` for `HELP` |
| 4 | **The sound page's `TEST` plays a button click, not an explosion.** | DIFFERS | one-word change; the sound name is the explosion effect, not a UI one |
| 5 | **Sound-mode default is stage two, the original's is stage one**, and volume defaults are 100 percent against 27 of 64 and 32 of 64 (`BT-GAME-SETTINGS-PARITY.md` rule 4). | DIFFERS, low | decision |
| 6 | **Four controls are disabled**: the mouse interface, unit-chat text, message lines and text delay. | MISSING | decision per control; the interface type is the consequential one |
| 7 | **Menu close un-pauses only what the menu paused**; the original un-pauses unconditionally. | DIFFERS, in RWE's favour | none |
| 8 | **No resolution list; video changes allowed in battle.** | DIVERGENCE (documented) | none |
| 9 | **Whether Enter in the surrender prompt means No.** | UNCLEAR | trace `openConfirmDialog`'s default buttons |

No source file was changed.
