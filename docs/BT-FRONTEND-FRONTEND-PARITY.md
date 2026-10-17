# Frontend coordination, main menu, movies and picture cache parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-MAIN-LOOP-PARITY.md`, `BT-GRAPHICS-MOVIES-PARITY.md`
and `BT-GAME-GAME-STATE-PARITY.md`, it allocates no section number in the main register. It
covers one chapter of the reference, `frontend-frontend`: the two-level menu state machine, its
transition table (intro, main menu, credits, endings, single player, skirmish, options, the
briefing contexts, provider selection, session selection, the battle room, the address dialog),
the main menu's construction and activation, the Windows CD-player prompt, the 100-spark pixel
animation, movie playback and its loop flag, the developer warp shortcuts, the ten-entry picture
cache and the empty hooks. This is the finding that implementation work cites for any of them,
per `AGENTS.md` and ADR-0004.

Most of this module is plumbing for screens that other chapters own. The screens' contents are
`BT-FRONTEND-SKIRMISH-MENU-PARITY.md`, `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md`,
`BT-FRONTEND-ENDGAME-PARITY.md` and `BT-GAME-SETTINGS-PARITY.md` (options); the films are
`BT-GRAPHICS-MOVIES-PARITY.md`; the disc checks are `BT-GAME-CD-CHECK-PARITY.md`. Only what the
state machine itself decides is audited here.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-frontend.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register was read, read-only; no row concerns this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play.
- **The RWE half** is a read-only audit of `src/rwe/MainMenuScene.cpp` (`init`, `goToMainMenu`,
  `messageNow`, `goToPreviousMenu`, `playMovie`, `onKeyDown`), `MainMenuScene_campaign.cpp`,
  `GameLaunch.cpp`'s scene loop and the movie scene as the earlier movie audit read them, at the
  working tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted
  edits in the tree, so every line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, sections 88 and 91),
  `TOTALA-EXE-INTERFACE.md`, `TOTALA-EXE-MUSIC.md` (the category-four rows) and `TA-NETWORK.md`.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. State layers and the transition table
3. Provider, session and battle-room transitions
4. Main-menu construction and activation
5. Menu sparks
6. Movies, the loop flag and the warp shortcuts
7. The Windows CD-player prompt and the startup notices
8. The picture cache, hooks and stubs (not applicable)
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| state layers, frame integration | `BT-GAME-MAIN-LOOP-PARITY.md`, `BT-GAME-GAME-STATE-PARITY.md` rule 1 (the eight front-end modes) | agrees (rule 2) |
| transition table | `TOTALA-EXE-DATA.md` section 115 (the campaign path only) | rule 2 |
| provider, session, battle room | `TA-NETWORK.md` (the wire), `net/ta/` | not traced here (rule 3) |
| main-menu construction and clicks | `BT-GRAPHICS-MOVIES-PARITY.md` rules 1 and 5 (Credits, Intro) | rule 4 |
| sparks | none | **MISSING** (rule 5) |
| movies, loop flag, warp | `BT-GRAPHICS-MOVIES-PARITY.md` rule 5 | rule 6 |
| CD-player prompt, notices | none | not applicable (rule 7) |
| picture cache, hooks | none | not applicable (rule 8) |

## 2. State layers and the transition table

**Reference.** Three byte fields drive a menu: the menu state, the current substate and the
requested substate; a wider frontend state separates "menu" from "battle" and is not
interchangeable. The machine applies a pending substate change, handles **one** state and
returns; changing state does not run the new branch in the same call. The code-checksum check
always passes in this revision. The table of the states that a single player meets:

- **State 0** plays movie slot one (fullscreen only, when the movie option is set) and goes to
  1, or to 2 when skipped; **1** plays slot two; **2** is the main menu (substate 0 tries a
  lobbied connection, else opens MAINMENU.GUI; 5 goes to the single-player root; 6 sets the
  multiplayer mission type; 8 quits; 9 goes to credits); **3** credits (slot five);
  **4 and 5** the endings (slots three and four, then five);
- **7** the single-player root (substate 0 opens SINGLE.GUI; 10 and 14 open the new-game menu;
  11 skirmish; 13 options); **8** the new-game screen (15 and 16 start a briefing; 3 returns to
  7); **9** the skirmish screen (2 sets the start bit); **10** options return; **11 to 14** the
  briefing contexts, whose back paths differ: 11 returns to the new-game menu with argument
  zero, 12 with argument one, 13 to the end-mission screen with the end-game state seven, 14 to
  the single-player root.

**RWE: DIVERGENCE (documented by structure), MATCHES in the paths a player takes.** RWE has no
state number: `MainMenuScene` keeps a **stack of panels** and a stack of dialogs
(`goToMenu`, `openDialog`, `goToPreviousMenu`, about 620-675) and reaches the other scenes by
`SceneManager`. Consequences for the table:

- **Intro, endings, credits.** The film numbers are the chapter's: 2 for the intro button and 5
  for the credits (`messageNow`, about 813-827), 3 and 4 by side for the endings
  (`GameScene_menu.cpp` about 1368). Slot one (the startup logo) is the movies audit's rule 1.
- **Back from a briefing: MATCHES in effect.** The briefing is a panel pushed on the new-game
  panel, so Previous Menu returns to it, which is the chapter's states 11 and 12. State 13's
  return (to the end-mission screen) is `GameScene`'s own ENDMSN chart, not the menu
  (`BT-FRONTEND-ENDGAME-PARITY.md`); state 14's, the single-player root, is the panel below the
  new-game panel.
- **Previous Menu at the root quits: UNCLEAR.** `goToPreviousMenu` ends with `exit()` when only
  the main menu is left (about 637). A Previous Menu message that reaches the main menu ends the
  program. Whether one can (MAINMENU.GUI's cancel gadget, if it has one, and what Escape does to
  the panel) was not checked. The chapter's main-menu
  state handles EXIT, SINGLE, MULTI, INTRO and Credits and clears the selection for anything else
  (rule 4), so the original does not quit on a cancel. **Original check:** Escape at the main
  menu.
- **One key popped per state call: not applicable.** RWE passes events to the top panel as they
  arrive (`onKeyDown`, about 695).
- **Lobbied start and the automatic multiplayer substate: not applicable.** The main menu's
  substate 0 first tries to join a game that a lobby launcher created; RWE's launcher starts
  the engine with arguments (`CLAUDE.md`, `rwe_bridge`) and the game scene directly.
- **The checksum stub: not applicable** (always zero in the reference too).

## 3. Provider, session and battle-room transitions

**Reference.** State 15 fills the provider list, retries a lobbied connection, distinguishes
modem, serial and TCP/IP by 16-byte GUID and opens an address dialog (state 20) for each; other
providers initialise directly. State 16 creates or joins a session in numbered substates (0
enables guaranteed packets and resets players; 17 creates; 18 and 19 join, 19 first setting the
player-info bit six; 20 and 21 follow the score-reporting attempt; 21 sets the host field from
the connection flags, loads the named mission and starts, or enters the battle room). State 17
is the battle room: it broadcasts player info, finishes unit synchronisation, **removes slots of
type four** and sets the start bit; leaving through substate three quits the network game,
reinitialises the packet manager with the arguments `(2, 100)` and shuts the score tables down.

**RWE: not traced here.** The same four screens exist in RWE as the original's own flow
(`goToSelectConnectionMenu` and the SELPROV, TCP, SELGAME and NEWMULTI panels,
`MainMenuScene_multiplayer.cpp`, with `ui_probe --multiplayer-flow` exercising them), over RWE's
own transport (UDP with STUN and invite codes), and the TA-DirectPlay half lives in `net/ta/`
(`TaHostGame`, `TaBattleroom`). Whether the numbered substates of state 16 have counterparts for
the *TA-compatible* host and join paths was not looked at; `TA-NETWORK.md` already holds what a
real TA sends and expects, and the chapter's state numbers add nothing to the wire. Two facts the
chapter supplies that a reader of `net/ta/` may want:

- **The score-reporting step is optional and failure falls through**: entering substate 21 from
  19 skips the reporting attempt, so a join that arrives by the "watching" route (the info flag
  of bit six) is never offered the report dialog. Not applicable to RWE (no ladder,
  `BT-GAME-STATISTICS-PARITY.md`).
- **"Type four" slots are removed when the battle room starts.** The chapter does not say what a
  type-four player is (the audit of `game-players` lists the controller kinds). **UNCLEAR.**

## 4. Main-menu construction and activation

**Reference.** `OpenMainMenu` closes every layer, blanks the screen, forces 640 by 480, loads
MAINMENU.GUI with `FrontendX` as the background and the menu-sparks callback as the periodic
one, starts looping **BGM**, selects the CD music category four, loads the GUI palette and
remaps it against the background, sets the font "Comix", writes the literal **`v3.1`** into the
DebugString control and **centres it by half its measured width**, and allocates the spark table.
The clicks: **SINGLE** (sound, cursor, request substate five); **MULTI** (registers archives,
needs the multiplayer data file, else a disc prompt); **INTRO** (requires fullscreen and a
disc, sets the loop flag from the Shift key and goes to state 1); **EXIT**; **Credits** (the
same gates, substate nine); anything else clears the selection so the layer does not close.

**RWE.**

- **Layout and background: MATCHES.** `goToMainMenu` (about 720) loads MAINMENU.GUI over
  `FrontendX`; the version label is the DebugString control with centred alignment
  (about 735-739).
- **The label's text: DIVERGENCE (identity).** RWE writes `RevivalTitle` (the project's title
  and version) and not `v3.1`, which would be wrong for a different engine.
- **BGM: MATCHES.** `startBgm` (about 122) loops the `[BGM]` entry of the sound table. The CD
  music is **not** selected: `TOTALA-EXE-MUSIC.md` records category four ("menus, frontend
  screens, and both endgame paths") as a CD category and concludes the original plays nothing
  there; RWE plays none either (`MainMenuScene::init` comment). The chapter's "selects CD music
  category four" is consistent with the register and with RWE.
- **Buttons: MATCHES in effect.** `messageNow` (about 795-828) handles EXIT, SINGLE, MULTI, INTRO
  and Credits; anything else falls through to the generic path. **MULTI** goes to RWE's own
  connection chooser (no data-file test, no disc). **INTRO** and **Credits** play films 2 and 5
  in a window as well as fullscreen and never loop; both facts are the movies audit's.
- **The activation sounds and cursor modes: not traced.** The original plays a big, small or
  exit button sound per control and picks the busy cursor on a transition. RWE's buttons play
  their face's sound by the GUI's own `sound` fields (`UiStagedButton`), and the cursor is the
  scene's.
- **The GUI palette remap and the missing-asset unwind: not applicable.** True colour, and
  `goToMainMenu` throws a readable error for a missing GUI.
- **Quit confirmation: none in either.**

## 5. Menu sparks

**Reference.** A 100-record decoration, separate from battle particles, that works on **pixels
of the background artwork**. Each 13-byte record has a position, an active byte, a signed
velocity, a life and a linear pixel index. Per call, per active spark: put back the pixel that
was under it from the original background; move by the velocity; **die** when it leaves the
640 by 480 image or when its life was already zero; otherwise decrement life, compute the new
index with a hard-coded 640-byte row, and **die** if the new pixel's low nibble is below 13,
else paint it with palette index `0xaa`. When fuel runs out the motion switches from horizontal
to vertical or back, with the sign chosen by parity and a magnitude of three, and fuel reloads to
1 to 16. An inactive spark picks x in 0 to 639 and y in 0 to 219 and **starts only if the pixel
there has a low nibble above 12**, with a life of a random byte plus one (which wraps to zero
sometimes) and fuel of 1 to 32. The layer is marked changed after every call whenever a
background exists.

**RWE: MISSING.** `MainMenuScene` draws the panel and nothing else; there is no spark table, no
background-pixel access and no per-frame redraw of the menu (`render`, about 87; a grep for
"spark" finds only the simulation's wreck and exhaust effects). Cosmetic. A port would run on
the GPU rather than in the background's pixels: the bright regions are the pixels whose palette
index has a low nibble above 12 in `FrontendX`, so the placement rule needs the **indexed**
bitmap (RWE converts the picture to a true-colour sprite on load, so the index would have to
be kept or re-derived), and the colour `0xaa` is a palette entry to be looked up in the same
palette. The random draws are presentation (the
scene's own generator, never the simulation's). **Not worth doing before the rest of rules 2 and 4
are settled**; recorded so the shape is known.

## 6. Movies, the loop flag and the warp shortcuts

**Reference.** `PlayMovie` stops all sounds, resolves the film path through the disc helper,
checks the length and, when it exists, blanks the screen, hides the cursor overlay and
**repeatedly** allocates a player with parameters `(0, 600000, 1, 2000000, 1)`, plays and frees it
**while the loop flag is set**. It drains the key queue afterwards. The movie code pumps window
messages; a character message clears the loop flag, Alt-F4 also posts quit. The loop flag is
set from the **Shift key** at the INTRO click. `LoadWarpLevel` reads a developer file
(`Warp.ini`, section WARPLEVELS, keys `warpNcampaign` and `warpNmission`) to start a mission
directly; a debug key handler maps A and C to side zero and one, E, M and H to difficulty, digits
to the warp, anything else back to the main menu.

**RWE.**

- **Looping: UNCLEAR, as in the movies audit.** `BT-GRAPHICS-MOVIES-PARITY.md` rule 5 says RWE
  plays each film once and records that nothing in that chapter says what sets the loop flag.
  **This chapter does:** the INTRO button's click sets it from the **Shift key**; a plain click
  plays once. RWE's INTRO plays once in every case, which therefore matches a click without Shift
  and misses a click with it. The skip rule (a key ends the loop) is the film player's. Close
  that row: the loop is a Shift-click feature.
- **No sound stop and no key drain: not applicable.** The film scene owns its audio and its
  events.
- **The warp shortcuts and `Warp.ini`: MISSING, deliberately.** They are a developer aid; RWE
  has `--map`, the scenario runner and `battle_test`. No change.

## 7. The Windows CD-player prompt and the startup notices

**Reference.** A YESORNO dialog offers to close an external Windows CD-player window that would
hold the drive; CHOICE1 closes it, CHOICE2 leaves it; it never closes the game. Startup
notices are latched separately: the CD-player prompt, a DirectX version check (set before the
check, so it is not retried), a no-sound-driver message and a general-protection check that runs
once.

**RWE: not applicable.** No CD, no external player, no DirectX version and no driver message of
that kind; a missing audio device is reported by the audio service
(`GameLaunch.cpp`).

## 8. The picture cache, hooks and stubs (not applicable)

**Reference.** A ten-entry cache of surface, 1024-byte palette and 32-byte name, searched by
case-sensitive compare; a hit moves to the front; a miss loads, evicts entry nine and inserts at
the front, except **in the wider frontend state six where the load is not cached and the caller
owns it**. A null name detaches the background. Failed loads can leave stranded palettes and
duplicate records. `ClearPictureCache` zeroes without freeing. A series of empty hooks and
"continue" stubs, and a label-attribute helper, complete the range.

**RWE: not applicable.** Textures are cached by `TextureService` for the process
(`animCache` for GAF entries and the like), with no eviction, no palette and no ownership
hand-off. The
stub hooks do nothing in the reference either.

---

## 9. Gaps to fix

Ranked by what a player would notice. All presentation or flow; none touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Previous Menu at the main menu quits the program.** The original's main-menu state ignores anything but its five buttons. | UNCLEAR, probable difference | original check: Escape at the main menu; then `goToPreviousMenu` should do nothing at the root |
| 2 | **Menu sparks are missing.** | MISSING, cosmetic | a presentation task; needs the indexed background and the palette entry |
| 3 | **INTRO loops on a Shift-click in the original**; RWE never loops. | MISSING, trivial | record in the movies audit; implement only if wanted |
| 4 | **The numbered substates of the session screens** have not been matched with the TA-compatible host and join paths. | UNCLEAR, not traced | a follow-up read of `net/ta/` against `TA-NETWORK.md` |
| 5 | **Version label** shows the project title where the original writes `v3.1`. | DIVERGENCE (identity) | none |

No source file was changed.
