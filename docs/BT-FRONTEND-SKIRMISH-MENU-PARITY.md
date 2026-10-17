# Skirmish setup, player rows, map selection and launch checks parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SETTINGS-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`, it allocates no section number in the main register. It
covers one chapter of the reference, `frontend-skirmish-menu`: the 24-byte setup slot, the
dynamically built player rows, the controller, side, allegiance, colour and resource controls,
the rule buttons, the map selector, the checks the Start button makes, how the setup is applied
to the live players, and the typed slot-count shortcuts. This is the finding that implementation
work cites for any of them, per `AGENTS.md` and ADR-0004.

What the settings audits already hold is not repeated: how the setup is persisted and its
defaults are `BT-GAME-SETTINGS-PARITY.md` rules 6 to 8 (Mapped against Unmapped, the player
count, "not remembered"); the commander-death button's third stage is
`BT-GAME-PLAYERS-PARITY.md` rule 7; the disc prompt is `BT-GAME-CD-CHECK-PARITY.md`. The
campaign screens are `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` (the chapter's setup-slot queries
live there and are audited here, where RWE uses them). The multiplayer screens reuse this
screen's table (`MainMenuScene_multiplayer.cpp`) and are out of scope.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-skirmish-menu.md`,
  with the setup-slot query table of `frontend-campaign-menu.md`). Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were followed. Addresses
  are the ones the chapter names; rules are restated in prose. The spec-room discrepancy register
  was read, read-only; no row concerns this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play. Several findings
  are about what a player sees on a screen; the Gaps table says which need an original check.
- **The RWE half** is a read-only audit of `src/rwe/MainMenuScene.cpp` (`goToPlayerSetupMenu`,
  the player rows, `togglePlayer`, the colour, team and resource functions,
  `prepareGameParameters`, `startPositionProblem`, `openMapSelectionDialog`),
  `MainMenuModel.*`, `MainMenuScene_multiplayer.cpp` (`cycleComputerSeat`, `startBlocker`),
  `vfs/CompositeVirtualFileSystem.cpp` (`getFileNames`), `game/GameScene_replay.cpp` (the win test)
  and `game/GameScene_audio.cpp` (`updateDefeatNotifications`) at the working tree of 2026-10-17.
  Nothing was built, run or changed. Another agent had uncommitted edits in the tree, so every
  line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, sections 88 and 91),
  `TOTALA-EXE-INTERFACE.md`, `TOTALA-EXE-DATA.md` section 105 and `docs/adr/`.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The setup table and the row layout
3. Controllers: the cycle and the limit of one human
4. Side, allegiance group and colour
5. The rule buttons and the resource amounts
6. The map selector
7. Start: the checks, and what RWE lets through
8. Applying the setup to the live players
9. Typed slot counts and the destruction message
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| setup table, screen lifetime | `BT-GAME-SETTINGS-PARITY.md` rules 6, 7, 8 (persistence, defaults, player count) | not repeated |
| row construction, layout | none | rule 2 |
| controller, side, allegiance, colour | none | rules 3 and 4 |
| rule stages and resources | settings audit rule 7 (defaults) | **MATCHES** (rule 5) |
| map selector | none | **MATCHES** in effect (rule 6) |
| launch validation | none | **DIFFERS**, with a consequence (rule 7) |
| applying the setup | `BT-GAME-PLAYERS-PARITY.md` (alliance as team id) | rule 8 |
| typed slot counts, forces destroyed | settings audit ("`NumSkirmishPlayers`: not applicable") | rule 9 |

## 2. The setup table and the row layout

**Reference.** Each setup slot is 24 bytes: controller (zero open, one the local human, two a
computer), side, allegiance group (zero to five, five meaning none), starting metal and energy
and colour index. The table also holds the commander-death, mapping, line-of-sight, start
location, map name, base gadget count, the row being edited and the difficulty. The screen is
SKIRMISH.GUI over the `Skirmsetup4x` background. The rows are built dynamically with six
controls per configured slot: the player button at x 45 (112 by 20), side at 163, colour at 214
(20 by 20), allies at 241 (40 by 20), metal at 286 (45 by 20) and energy at 337, with a vertical
spacing of **200 divided by the configured count** centred in the panel, and each control's
width and height taken from the sprite sequence bound to it (a missing sequence keeps the
supplied size). The player count is 3 to 10, default 4, and the Spanish quick key and the typed
shortcuts are in rule 9.

**RWE: MATCHES for the geometry at ten rows, DIVERGENCE (documented) for the count.**

- `attachPlayerSelectionComponents` (`MainMenuScene.cpp` about 2063) builds **ten** rows at a
  fixed `tableStart` of 78 and a step of 20, which is the chapter's formula at a count of ten
  (200/10). The column x values 45, 163, 214, 241, 286 and 337 are the chapter's
  (`attachDetailedPlayerSelectionComponents`, about 2134). The widths are 44, 19, 38 and 46 where
  the chapter lists 45, 20, 40 and 45 or "the sequence's own"; the code's sizes are the
  sprites' (the colour button resizes the logo frames to 19 by 19, about 2165) and the
  difference is a pixel. **UNCLEAR, no effect.**
- **The player count: DIVERGENCE (documented).** `MainMenuModel::players` is a fixed array of
  ten (`MainMenuModel.h` about 121) with seat 0 Human and seat 1 Computer and the rest Open;
  there is no `NumSkirmishPlayers` and no screen that shows fewer rows.
  `BT-GAME-SETTINGS-PARITY.md` rule 8 records this. The default screen therefore shows eight
  open rows where the original shows two open rows of a four-row table.
- **Screen lifetime: MATCHES in effect.** The model lives on `MainMenuScene` and is not rebuilt
  when the screen is reopened inside one session; the panel is. It is not persisted between
  sessions (`BT-GAME-SETTINGS-PARITY.md` Gaps rank 1).

## 3. Controllers: the cycle and the limit of one human

**Reference.** A click on the player button cycles: **open to computer**; **human to open**;
**computer to human only when no other human exists, otherwise computer to open**. The row's
controls become active or inactive with the type; a newly occupied row whose colour conflicts
with another active slot receives the first colour no slot uses (the free-colour search tests
**every configured slot, open ones included**, while the conflict test looks at active slots
only). If none is free `-1` is stored. If every slot is empty the screen activates slot zero as
human and slot one as computer. At most one human is a rule of the controls, not of the data.

**RWE: DIFFERS in the order.** `togglePlayer` (about 1661) and `cycleComputerSeat`
(`MainMenuScene_multiplayer.cpp` about 498) cycle:

- **Open to Human when there is none, otherwise to Computer**; Human to Computer; Computer to
  Open (on the multiplayer screen, Computer to Network to Open). The original goes open to
  computer first and treats the human as a rung that follows the computer.
- **RWE inserts two stages for the computer**: "Original TA" and "RWE AI" (`seatCaption`, about
  517), because a seat chooses its engine (ADR-0005). A click on a computer seat moves from the
  first to the second before it becomes Open. This is RWE's own addition (`purpose:rwe-original`)
  and has no counterpart; the cost is one extra click to empty a computer seat.
- **One human at most: MATCHES.** The Open branch tests for an existing Human before offering
  it (about 1685-1695).
- **The free-colour search: DIFFERS slightly.** `isColorInUse` (`MainMenuModel.cpp` about 41)
  counts only **non-open** seats; the original's search counts open ones too. With the default
  colours (seat *i* wears colour *i*) the result is identical until the player has changed a
  colour onto an open seat's colour; then RWE hands the open seat's colour to a newly occupied
  seat and the original skips it. No gameplay effect. **UNCLEAR whether intentional**; it makes
  the unoccupied seats' colours free for use, which is at least as friendly.
- **The empty-table rule: not applicable.** RWE's table is never empty (seat 0 is Human, seat 1
  Computer by default) and a user can empty it entirely; Start then does what rule 7 says.

## 4. Side, allegiance group and colour

**Reference.** The side button advances modulo the configured side count; the allegiance
button advances modulo six (five meaning none); the icon frame is ten for a team of no
members, `2 * team + 1` for exactly one member and `2 * team` for several (team five takes part
in the count). The colour button advances (or reverses, by the click) through the logos sheet
and repeats while another active row uses the colour. A fully used palette can loop for ever.

**RWE.**

- **Side: MATCHES for two sides.** `togglePlayerSide` (about 1719) flips Arm and Core. With more
  than two sides in the side data there is no third stage; the enum has two values
  (`MainMenuModel.h`). **DIVERGENCE (documented)** in `BT-GAME-SIDE-DATA-PARITY.md`.
- **Allegiance: MATCHES in effect.** `cyclePlayerTeam` (about 1763) goes none, 0, 1, 2, 3, 4,
  none: six states in the same cyclic order as the chapter (0..4 then five as none), starting
  from none where the original starts from a stored value. The icon: frame ten when none,
  `2 * team` when the team is shared and `2 * team + 1` when it is not
  (`attachDetailedPlayerSelectionComponents`, about 2200-2240), the chapter's rule except for
  **team five**: the original gives a lone team-five seat frame 11 and a group of them
  frame 10; RWE always shows frame 10 for none. **UNCLEAR, cosmetic**; frame 11's art was not
  looked at.
- **Colour: MATCHES.** Forward and reverse cycle through ten, skip colours used by non-open
  seats, and stop after nine tries (`cyclePlayerColor`, about 1733), which removes the
  original's unbounded loop on a full palette.
- **Forward only for side, team and controller: MATCHES.** Only colour and the two resources use
  the reverse click (`Primary` / `Secondary` activation, about 1040-1075), as in the chapter.

## 5. The rule buttons and the resource amounts

**Reference.** Stored values and stages: fixed start locations (non-zero fixed, stage 0; zero
random, stage 1); commander death (non-zero game ends, stage 0; zero continues, stage 1);
mapping (non-zero black-out of unexplored, stage 0; zero visible, stage 1); line of sight
(disabled, stage 0; enabled with type one, stage 1; enabled with any other type, stage 2).
Commander death, start location and mapping toggle by XOR with one; line of sight cycles. The
difficulty cycles 0, 1, 2 and writes the game and the setup. Metal and energy: **increase adds
500 and clamps at 10000, with a result of 700 turned into 500** (so 200 goes to 500 and then
1000, 1500 and so on); **decrease subtracts 500 and clamps at 200** (500 goes to 200).

**RWE: MATCHES.**

- **Stage to rule:** `skirmishStageToCommanderDeathMode`, `...StartLocationMode`,
  `...MappingMode` and `...LineOfSightMode` (`MainMenuScene.cpp` about 1850-1880) take the
  stages in the chapter's order (game ends, fixed, unmapped, permanent; the third line-of-sight
  stage is "Circular"). The button texts come from SKIRMISH.GUI. The simulation implements all
  three line-of-sight modes (`LineOfSightMode::Permanent`, `PlayerVisibility.h`,
  `gameoptions.test.cpp`); the chapter's gloss on stage 0 is "disabled, all mapped terrain
  visible", which is the Permanent mode's behaviour in the code. `TOTALA-EXE.md` section 91's
  last bullet ("Permanent LOS has not been looked at") is **stale**.
- **Resources:** `incrementPlayerMetal` and its three siblings (about 1605-1660) step 200,
  500, 1000, ... up to 10000 and back down to 200 by the same rule. RWE tests for 200
  directly and goes to 500, which is the chapter's "700 becomes 500" written the other way.
- **Defaults** are the settings audit's (RWE starts every seat at 1000 and the rules at
  Unmapped, Fixed, Game ends, True).
- **Difficulty:** `cycleSkirmishOption` (about 1426) cycles three stages, and the stage becomes
  an AI profile (`skirmishDifficultyToAiDifficulty`, about 1829). The chapter writes it to the
  game and to the persisted setup; RWE keeps it on the model (not persisted).

## 6. The map selector

**Reference.** Opening the selector counts maps with an unfiltered list call; zero opens a
"no maps" message. Otherwise it loads SELMAP.GUI, allocates a 32-byte data object, **sorts**
the list, scrolls to the retained map (found by an exact string compare), loads that map's
metadata and shows it. Preview loads the **shared** mission object at once, so cancelling does
**not** restore the previous preview. Accepting copies the packed name into the setup, loads the
mission and ignores the load result; the preview's information is drawn by the multiplayer
module's display function.

**RWE: MATCHES in effect, with three differences.**

- **Candidate and committed selection: DIVERGENCE, an improvement.** `setCandidateSelectedMap`
  (about 1550) reads the OTA for the preview only; `commitSelectedMap` (about 1595) is the
  accept and `resetCandidateSelectedMap` the cancel, so the previous choice stands after a
  cancel. The original keeps a stale shared preview but the **setup name** is not changed by a
  cancel either, so the launch uses the right map in both.
- **The map list: UNCLEAR.** `getMapNames` (about 2280) keeps only maps that have a multiplayer
  schema (`hasMultiplayerSchema`, about 2298); the chapter's call has "zero filters". What the
  original's filters are (and whether a campaign-only map with no multiplayer schema appears)
  is in the multiplayer module, not here. RWE's filter exists so the loader never meets a map
  with no start position for a seat (upstream #49).
- **Order: UNCLEAR.** RWE lists the files in an ordered `std::set`
  (`CompositeVirtualFileSystem.cpp` about 60-72), so the order is **byte-wise**: every upper-case
  name sorts before every lower-case one. The chapter says only "sorts". The collation (case
  folding, locale) is not stated.
- **No maps: MISSING.** With no map `selectDefaultMap` (about 1279) selects nothing; the
  selector opens with an empty list and Start returns without a message
  (`prepareGameParameters`, first lines). The original opens a message. Not reachable with a
  retail install.
- **The default map is "Coast To Coast"** (`selectDefaultMap`), RWE's choice; the original's is
  the retained one, with the first mission name as the fallback.

## 7. Start: the checks, and what RWE lets through

**Reference.** Start plays a button sound, then: if the multiplayer disc is missing, shows a
message but **does not return** (the later checks still run); registers the archives; counts the
computers and sets the provisional player count to computers plus one; **loads the map and
returns with a message if that fails**; **requires at least one computer and at least one human**
(a separate message each); compares the provisional count with the map's number of start
positions; **rejects the setup when every active slot is in one allegiance group** (a group
other than five; all active slots on five do not trigger it, and a mix with a five-slot does
not either); recounts the real humans and computers and stores the total; applies the slots;
saves the settings and requests the next frontend state. The group test's helper takes the first
active slot with a non-five team and fails on any difference.

**RWE: DIFFERS, with a visible consequence.** `startGame` (about 1903) calls
`prepareGameParameters` (about 1929), which for a skirmish:

- **checks that a map is selected** (silently returns otherwise), reads the options, and builds
  the parameters from every non-open seat;
- **checks that the map has a start position for every occupied seat's slot number**
  (`startPositionProblem`, about 2012): "This map has no start position for slot *n* (it has
  *k*)". This is **stricter than the chapter's count comparison**: the original compares a
  total (computers plus one) with the number of positions; RWE requires the *n*-th position for
  the *n*-th seat. A game of two seats in slots 0 and 5 on a four-position map is accepted by
  the original's count and rejected by RWE. **UNCLEAR whether the original's dealing needs the
  slot number to be a valid position** (Batch 11 notes that the start-slot deal uses the occupied
  slot numbers as the position set), in which case RWE's check is the right guard and the
  chapter's count is what lets the original read a slot past the table. Original check: seats 0
  and 5 on a four-position map.
- **does not require one human and one computer.** Nothing in the skirmish path counts them.
  An all-computer table, an all-human table and a lone human all start. `GameScene` accepts the
  lone human by design (a game "needs an opponent before it can be decided", about 819 of
  `GameScene_replay.cpp`, so a one-seat game runs undecided), and the AI-only table is how the
  arena harness runs. The original refuses all three with a message. Not a fault, but a
  difference a player can reach.
- **does not reject one allegiance group.** The helper has no counterpart. **Consequence, by
  reading:** two seats, both put on the same team, start a game that `computeWinStatus`
  (`GameSimulation.cpp` about 2615) decides on the first tick the win test runs — "every living
  player is on one team" is true at once — so the game ends in a victory banner at tick one.
  The original refuses to start. This is the one launch check whose absence produces a
  broken-looking game rather than a different one. **Not run**; the guard against a lone player
  (`players.size() >= 2`) does not apply.
- **The disc prompt: not applicable** (`BT-GAME-CD-CHECK-PARITY.md`).

## 8. Applying the setup to the live players

**Reference.** For every configured slot: a human or computer slot copies colour and side into
the live player info and sets the controller kind (one or two); humans also set the local
player and the player index (the last human wins if there are several); other slots are set to
kind zero. For each active human or computer it marks the allied byte for every active member of
its team when the team is not five, and always itself (a separate fallback). It does not clear
the other allied bytes (player initialisation owns that) and does not copy starting resources
(the loader does, from the retained setup). `FindNextAllySlot` returns the first active same-team
non-five slot or the slot itself, without wrapping.

**RWE: DIVERGENCE (documented).** `prepareGameParameters` builds `PlayerInfo` records with the
team index as an optional integer (`teamId`; none means no team) and the loader and simulation
read it (`GamePlayerInfo.h` about 63; `BT-GAME-PLAYERS-PARITY.md` rule 4 and its closing list).
Self is always allied, team none is allied with nobody, and the starting resources travel in
`PlayerInfo`. The set of results is the same for a game started from the screen.

## 9. Typed slot counts and the destruction message

**Reference.** Typing `*III` to `*X` into the screen (compared at fixed offsets in the key
history) saves the settings, sets the number of players to three to ten, saves, reloads, rebuilds
the rows and plays a sound. `AnnounceForcesDestroyed` formats the side's name (Arm for side
zero, **Core for every other side**) with one of **three randomly chosen** translated phrases and
posts it as a category-four message for that player.

**RWE.**

- **The slot-count shortcuts: not applicable** (rule 2).
- **The message: DIFFERS, cosmetic.** `updateDefeatNotifications` (`GameScene_audio.cpp` about
  266-290) prints one phrase, "*Side* forces have been obliterated", in the fallen player's
  colour, once per dead player, using the player's side string lower-cased after its first
  letter. The comment names two others ("forces have gone to a better place", "vermin have been
  exterminated") as the original's. The choice of phrase is a presentation draw and would use
  the scene's `effectsRng`, never `simulation.rng`. The side label is the player's own side name
  rather than "Core for every other side", which agrees for the two shipped sides.
  `AnnounceForcesDestroyed` does not decide defeat; neither does RWE's function (it reads the
  status the simulation set).

---

## 10. Gaps to fix

Ranked by what a player would meet. Nothing here touches the simulation except through
`GameParameters`.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Two or more seats on one team start a game that ends in victory on the first tick.** The original refuses to start when every active slot is in one allegiance group other than "none". | DIFFERS (traced by reading, not run) | code: a launch check in `prepareGameParameters` with the original's rule (all active seats share a non-none team), and a one-line message; test beside the team cases of `computeWinStatus` |
| 2 | **No "one human and one computer" check.** The original refuses a table with none of either. RWE starts a lone-human, all-human or all-computer game. | DIFFERS, partly by design (arena, sandbox) | decision: keep for the arena and for `--map`, require it only when the screen starts a skirmish |
| 3 | **Start position test by slot number against the original's count.** | UNCLEAR | original check (seats 0 and 5, four positions); then keep or loosen `startPositionProblem` |
| 4 | **Controller cycle order** (open, computer, human, open) differs from RWE's (open, human, computer(s), open), plus RWE's engine stages. | DIFFERS (RWE addition, ADR-0005) | none; note in `docs/compatibility.md` if the order matters to anyone |
| 5 | **Free-colour search ignores open seats' colours** (the original counts them). | DIFFERS, cosmetic | none |
| 6 | **Map list order and filter**: byte-wise order; only maps with a multiplayer schema. | UNCLEAR | multiplayer-module chapter and a listing of the retail map set |
| 7 | **No "no maps" message** and a silent Start without a map. | MISSING, not reachable on retail | none |
| 8 | **One "forces have been obliterated" phrase of three.** | DIFFERS, cosmetic | presentation-side draw from `effectsRng` if wanted |
| 9 | **`TOTALA-EXE.md` section 91's "Permanent LOS has not been looked at" is stale.** | documentation | edit the register |

No source file was changed.
