# End-of-game snapshot, fades, outcome screens and result controls parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`, it allocates no section number in the main register. It
covers one chapter of the reference, `frontend-endgame`: the statistics snapshot taken before the
battle is torn down, the screen and palette fades, the campaign's full-screen outcome picture,
the seven-column results chart, the ENDMSN mission screen with its next-or-retry rule, the
nine-state machine that drives them, and the debris-cache helpers that share the address range.
This is the finding that implementation work cites for any of them, per `AGENTS.md` and
ADR-0004.

What *decides* a game is `BT-GAME-VICTORY-PARITY.md` and `BT-GAME-PLAYERS-PARITY.md` (the
countdown, the commander-death modes, the watching flow: BT-82, BT-83 and BT-89, not re-raised).
The tables that go to the online reporter are `BT-GAME-STATISTICS-PARITY.md`. The movies the
ending plays are `BT-GRAPHICS-MOVIES-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-endgame.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register was read, read-only.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play. Several findings
  are about what a player sees; the Gaps table says which need a play or capture check.
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene_endgame.cpp`,
  `game/EndGameStats.*`, the end-game sites of `game/GameScene_input.cpp` and
  `game/GameScene_menu.cpp` (`playCampaignEnding`, `returnToMainMenu`), `game/GameScene.cpp`
  (`update`, the tick dispatch), `game/GameScene_replay.cpp` (the win test),
  `io/campaign/campaign.cpp` (`campaignMissionListEntry`) and `MainMenuScene_campaign.cpp` at the
  working tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted
  edits in the tree, so every line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, sections 88 and 91),
  `TOTALA-EXE-INTERFACE.md` section 104 (the register's account of this module),
  `TOTALA-EXE-DATA.md` section 115 (the campaign's screens), `TOTALA-EXE-EXTERNAL.md` ("Settled:
  the end-of-mission score divisor") and the ADRs.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. When the sequence starts, and when the statistics are taken
3. The banner and the ten-step screen fade
4. The campaign outcome picture and the ending movies
5. The chart: rows, columns, geometry, run-up and skipping it
6. ENDMSN: the mission list, next-or-retry, Start and the difficulty
7. Branches RWE does not have (multiplayer, disc, errors)
8. The game object, the debris cache and the rest (not applicable)
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| allocation, retained state | none | not applicable (rule 8) |
| statistics before teardown | `TOTALA-EXE-INTERFACE.md` section 104 "The score table" (record layout, floors, score) | agrees on the values; **timing and row inclusion differ in RWE** (rules 2 and 5) |
| screen and palette fades | section 104 ("The fade is ten steps") | agrees on ten steps; **the chapter's comparison is stricter than the register's** (rule 3, **CONFLICT**) |
| result labels, seven columns | section 104 ("Geometry", "The run-up") | **MATCHES** (rule 5) |
| mission-list decoration, next-or-retry | `TOTALA-EXE-DATA.md` section 115 (ENDMSN) | **MATCHES** (rule 6) |
| the state machine | section 104's state table | agrees; two chapter details not in RWE (rules 3 and 4) |
| CD retry, cleanup, debris cache | none | not applicable (rules 7 and 8) |

## 2. When the sequence starts, and when the statistics are taken

**Reference.** The chapter says this module "consumes outcome flags; it does not determine
victory". The statistics routine is called from the shutdown of the in-game systems **before**
units, players and the map are released; it copies the winner bit, records the campaign result
letter for a campaign, and fills ten packed rows from the players at that moment. The chart
later reads the retained rows, not live players. State 0 of the machine then copies the last game
frame (multiplayer only) and moves on.

**RWE.**

- **The sequence starts on the tick the win test answers: DIFFERS from the original only in the
  countdown** (`BT-GAME-PLAYERS-PARITY.md` rule 6; BT-82). `GameScene_replay.cpp` about 817-840
  sets `gameOver` and calls `beginEndGameSequence` (`GameScene_endgame.cpp` about 176).
- **The statistics are computed when the chart opens, not at the decision: DIFFERS.**
  `buildEndGameChart` (about 287) calls `computeEndGameStats(simulation)` when the chart is built,
  after the banner (90 scene ticks), the fade (10) and, for a won campaign mission, however long
  the player looks at the glamour picture. **Nothing stops the simulation at `gameOver`**: the
  tick dispatch in `GameScene::update` (about 541-700) is gated only by pause, the speed and the
  network; `gameOverTime` is written (`GameScene.h` about 388, `GameScene_replay.cpp` about 828)
  and never read; and no `gameOver` test appears in `GameScene.cpp`. Traced by reading, **not
  run**. So units that survive the decision go on fighting and producing under the banner, the
  fade and the chart, and the kills, losses, produced and excess figures, and the score's clock
  term, are read some seconds after the game was decided. In the original they are fixed at the
  shutdown, which is at or before the first frame of the sequence. On the shipped data the clock
  term is zero (`timemul` is 0 in every shipped map and mission), so the visible effect is on
  the four economy columns and on kills and losses in any game where fighting is still going on
  at the decision, which is any game decided while shooting continues.
  `ROADMAP.md` calls the picture under the banner "the frozen battlefield"; it is frozen only in
  the sense that the frame is the last one drawn when the fade starts, and the view does keep
  updating until the fade covers it. **Original check, then a one-line fix:** take
  `computeEndGameStats` (or a copy of the player counters) at `beginEndGameSequence`.
  Hash-neutral (presentation only), but `EndGameStats.test.cpp` will want a case that ticks
  after the decision.
- **The campaign result letter is recorded at the start of the sequence: MATCHES in effect**
  (`beginEndGameSequence`, about 185: `recordResult(localPlayerWon())`). The chapter writes it
  in the statistics routine, which is earlier in the same flow, and says it does not check the
  25-mission bound; RWE's progress holds a bounded string.

## 3. The banner and the ten-step screen fade

**Reference.** State 0 (non-multiplayer) clears the last-frame surface and selects state 2;
state 2 arms a ten-step screen fade with the next step due one tick on; state 3 performs **at
most one step per call**, and only **when the stored deadline is strictly less than the current
tick**, then reschedules to now plus one; the fade level runs from `steps - 29`; at zero steps
it forces 640 by 480 surfaces and selects state 4.

**Register.** Section 104: the fade is ten steps "one a tick", "a third of a second at the
original's thirty ticks a second"; state 1 waits on a message box only.

- **CONFLICT: strict or non-strict deadline.** The register counts one step per tick, ten
  ticks. The chapter's "strictly less than" means that after a step at tick `t` the next is due
  at `t + 1` and runs when the clock reads `t + 2` or later, **one step every two ticks, twenty
  ticks for the fade**, when the main loop runs faster than the tick clock (it can: the chapter
  describes a frame loop, and the register's clock is milliseconds scaled to ticks). The
  palette fade in the same module runs "when now is at least the deadline", which is one per
  tick, so the two fades would differ by a factor of two by design or by accident. **A spec-room
  re-read of the compare in the screen-fade step settles it**; the answer changes the fade from
  a third of a second to two thirds. Kind: register.
- **RWE: MATCHES the register.** `EndGameFadeTicks = 10` (about 70) and `EndGamePhase::Fade`
  (about 475-500, 593-604) step the overlay once per scene tick, reaching full black on the tenth.
- **The banner's duration: UNCLEAR, and RWE's comment disagrees with both sources.**
  `EndGameBannerTicks = 90` (about 84) holds the world with `igvictory` or `igdefeat` over it
  for three seconds before the fade, and the comment says "the original has no timer here: its
  banner is a flag on the world renderer and the player dismisses it". Neither the register's
  table nor the chapter has a state that waits for the player: state 0 selects state 2 at once
  and state 1 waits only for a message box. What the chapters do not say is **how long the
  banner is drawn before state 0 runs**, which depends on how many frames the main loop spends
  between the latch and the switch to the end-game mode. That is not in this chapter. If the
  switch is immediate the original shows the banner for the ten fade steps only (a third of a
  second, or two thirds under the conflict above); RWE shows it for about three seconds plus the
  fade. **Original check:** time the banner after a skirmish win. Kind: design versus the
  register's silence.
- **Cursor and overlay: not applicable.** The chapter disables the cursor overlay for the fade;
  RWE's cursor is the operating system's or a scene sprite, and the fade is an alpha quad.
- **The two beeps: UNCLEAR** (the register's "Not decoded"). The chapter plays nothing by name
  in these states either, which agrees with `beginEndGameSequence` choosing `BEEP6` and saying
  so (about 190-200).

## 4. The campaign outcome picture and the ending movies

**Reference.** State 5 allocates the fade buffers and the optional glamour palette. For a **won**
campaign mission with a glamour name it builds `bitmaps/glamour/<name>.PCX` from the name after
its first byte, and falls back to a literal `glamour/Arm01.PCX` when that is missing. A **won
final mission with no next mission and the no-movie flag clear** chooses the Arm or Core ending
(by player zero's side, when the display bit that allows films is set) or goes to the main menu.
A won campaign with a glamour image starts a **five-step palette fade from black**, draws the
picture and selects state 6. State 6 steps the fade, then after the deadline plays the glamour
sound once, and **a popped key or any pointer or button activity** closes the picture and opens
the results screen; five further tick-rate intervals later "Click to continue" is drawn.

**RWE.**

- **Selection of the picture and the films: MATCHES the register and the chapter.**
  `updateEndGameSequence` (about 484-505) takes the ending films for a won last mission that has
  no `nomovie` flag, otherwise the glamour picture for a won mission that has one, otherwise the
  chart. `playCampaignEnding` (`GameScene_menu.cpp` about 1362-1378) picks film 3 or 4 by the
  campaign side and then film 5 (the credits) and the main menu. The only departure is
  documented there: RWE plays the films in a window as well as full screen.
- **The missing-picture fallback: DIVERGENCE (documented in the code).** `beginGlamour` (about
  217-245) returns false for a name that is not in the data and goes straight to the chart; its
  comment says three shipped missions (`EXP1AC06`, `EXP1AC11`, `EXP1CC11`) name a picture that
  is not there and records that the original's fallback path has no `bitmaps` prefix, so the
  fallback itself cannot load. The chapter agrees on the shape ("keeps a path substring ... falls
  back to glamour/Arm01.PCX"). No change.
- **The five-step palette fade from black: MISSING.** The picture appears at once
  (`renderEndGameSequence` Glamour case, about 612-640). Cosmetic: about a sixth of a second
  at the register's rate, and the picture's own palette is not a concern for RWE's true-colour
  path.
- **Dismissing the picture: DIFFERS.** The chapter's condition is a popped key **or** cursor
  activity after the deadline. `glamourClicked` runs only from a left mouse button
  (`GameScene_input.cpp` about 503); a key press during the picture falls through to the
  ordinary key handler, where **Return opens the chat bar** (about 95-100) over a full-screen
  picture. A keyboard-only player cannot get past it except by clicking.
- **The first second and the prompt: MATCHES.** `EndGameGlamourDeafTicks` (one second) and
  `EndGameGlamourPromptTicks` (six seconds from the start, the chapter's "first interval then
  five more") reproduce the deadline and the "Click to continue." line (about 94-98, 628-636).
- **The glamour sound: MATCHES.** Played once when the picture opens; stopped on the click
  (about 245-258). The chapter starts it after the first deadline rather than at the start of the
  fade; with a missing palette fade the difference is the five-step interval and is not audible.
- **The "last frame" surface: not applicable.** It exists in the original only for multiplayer
  game types and to show the translated rejection reason over the frozen frame.

## 5. The chart: rows, columns, geometry, run-up and skipping it

**Reference.** The snapshot has ten 58-byte rows; a player is included when it is active, of
type 1, 2 or 3, not the sentinel index 10 and not an observer, **or** when its units-created
counter is nonzero, and a nonzero rejection reason suppresses the row. Values are kills, losses,
total energy and metal produced, energy and metal wasted, and the score; the column maxima start
at 10 for kills and losses and 100 for the rest and only rise. Rows start at y 93 and step 20;
bars are 67 by 18 at x 112, 186, 260, 334, 408, 482 and 556; the name label is 90 wide; each bar
starts at zero and runs to its value at a per-update step of `max(value / 15, 1)`; columns are
**activated in order, one every ten ticks**, with a sound each and a final one for the score; a
popped key outside multiplayer, or any popped key once the timed condition is entered, activates
every column at once. The completion test omits the last bar.

**RWE.**

- **Columns, geometry, floors and the run-up: MATCHES.** `EndGameStat`, `statFloors`
  (`EndGameStats.cpp` about 14), the column x of 112 and a stride of 74, the 68-pixel bar (the
  gadget is 67; the extra pixel is the cell's own edge, as the code comment says), the row
  origin and step, the ten-tick column delay and the fifteen-step bar all appear
  (`GameScene_endgame.cpp` about 40-90, 520-545). `computeEndGameStats` truncates each of the
  score's two terms separately and clamps at zero (about 40-50), as the chapter and the
  register do.
- **Row inclusion: DIFFERS.** RWE makes a row for every player in the simulation
  (`EndGameStats.cpp` about 28-55) and caps at ten when the chart is built. It has no observer
  type (spectators exist only in playback), no sentinel slot, no rejection reason and no
  `unitsCreated` counter (`GamePlayerInfo` holds `unitsKilled` and `unitsLost`, about 120). For
  every RWE game the two sets are the same: each seat is a participant. Not observable.
- **Score clock term: DIFFERS in the divisor, a code fault against the corrected register.**
  `EndGameStats.cpp` about 30 divides the game time by `SimTicksPerSecond` (30) and multiplies by
  the map's `timemul`. The chapter, `BT-GAME-STATISTICS-PARITY.md` and the **corrected** section
  104 (`TOTALA-EXE-INTERFACE.md`, and `TOTALA-EXE-EXTERNAL.md` "Settled: the end-of-mission score
  divisor") all say the divisor is **sixty**, taken before the multiplication and truncated.
  Section 104 was corrected on 2026-09-24; the code, its comment ("seconds * timemul") and
  `EndGameStats.test.cpp` (about 105-125, which asserts 90 seconds by 2) were not. Nothing
  shipped sets `timemul` above zero, so it is invisible on retail data and wrong for any map
  that sets it, doubling the clock term. This is rank 1 of the Gaps because the number is now
  known.
- **Skipping the run-up: DIFFERS in the input and in the multiplayer exception.** A left click
  anywhere that is not a control fills every bar (`GameScene_input.cpp` about 520-546); a key
  does nothing (Return opens the chat bar, or Starts the next mission on the ENDMSN chart). The
  chapter's trigger is a popped key; its register account says a click (`0x4C1AB0` tested at
  `0x41FFD9`), which is the source of RWE's click-only reading. The chapter's key and the
  register's click may both be true (the popped-key test may include a pointer button); the
  chapter itself says "cursorKeyFlags" for the picture. **UNCLEAR**; a key press should do
  what a click does in either case.
- **Bars set to their targets, not just enabled: MATCHES.** The chapter says the skip "enables
  bars rather than setting all current values to their targets"; RWE's `finishEndGameBars`
  (about 272) sets every fill to its target at once. The difference is one interval of
  animation and is not observable.
- **The completion test that omits the last bar: not applicable.** The original's use of the
  completion test is only to move to state 8, which changes the cursor and the GUI input
  mode. RWE has no equivalent state and no test.
- **Name text and colour swatch: MATCHES.** Name in the 90-pixel cell, seven pixels of player
  colour from the logos sheet; the name falls back to "Player n" where the original uses the
  player record's name (about 38 of `EndGameStats.cpp`).
- **Multiplayer economy broadcasts during the run-up: not applicable.** The original sends the
  local player's economy twice per activation step so remote machines can fill the same table.
  RWE is lockstep, and every machine already holds every player's counters.

## 6. ENDMSN: the mission list, next-or-retry, Start and the difficulty

**Reference.** `ShouldShowNextMission` is true for a campaign when the mission was **lost** or
a next mission exists; a **lost final mission** therefore still offers retry. In that case the
ENDMSN screen loads over `outcome1` with Start as the default, selects the completed mission
(or the next one after a win), prefixes each mission with a thumb byte (`0xff` lost, `0xfe` won,
`0xfd` untried) and a space, and enables Start, LoadGame, SaveGame, the knob, the list, the
difficulty buttons and MainMenu. Otherwise it attaches `outcome0`, moves MainMenu to y 416 and
focuses it. The win or loss badge is drawn by the local player's observing state, which is not
the global winner. Start checks the campaign disc but does **not** return on failure; it then
selects the mission and returns through the briefing. Difficulty cycles 0, 1, 2 in the game and
the saved setup.

**RWE: MATCHES.**

- **The rule** (`campaignContinues`, `GameScene_endgame.cpp` about 211) is the same expression,
  including the lost-final-mission retry.
- **The list** (`buildCampaignEndPanel`, about 340-400) puts `outcome1` under ENDMSN, fills one
  entry per mission with the thumb letter (`campaignMissionListEntry`, `campaign.cpp` about
  92: U+00FD for untried, U+00FE won, U+00FF lost, then a space), and selects the next mission
  after a win and the same one again after a loss (about 378).
- **Controls:** Start, Difficulty, SaveGame, LoadGame and MainMenu are handled
  (`campaignEndMessage`, about 410-440), Return is Start between missions
  (`GameScene_input.cpp` about 88) and any mission on the list can be chosen, won or not.
- **The save between missions** points at the next mission whether this one was won or lost, at
  the difficulty the screen shows (`saveBetweenMissions`, about 445-465), as the register's
  section 115 says.
- **Otherwise OUTCOME0 and one button** (`buildEndGameChart`, about 305-335): MATCHES; the button
  is placed from the artwork's inset (y 416), not from ENDMSN.GUI's own y of 395, which is the
  chapter's "moves its y to 416".
- **The win and loss badge: UNCLEAR, not traced.** The chapter draws badge sequence A for a won
  game with the local player not observing and B otherwise. RWE has no observing local player
  on this screen; whether the ENDMSN gadget set has a badge that RWE leaves unfilled was not
  looked at (`UiFactory::panelFromGuiFile` builds what the GUI lists). Low value.
- **The difficulty writes the saved setup as well as the game: DIFFERS in a harmless way.** RWE
  keeps the choice in `endGameCampaignDifficulty` and carries it in the campaign progress; the
  original also writes it to the retained setup, which is the next new campaign's default. RWE's
  next new campaign starts at whatever `MainMenuScene` had (`campaignDifficulty`, which is
  re-seeded from the progress when a campaign continues).
- **The disc check on Start: not applicable** (`BT-GAME-CD-CHECK-PARITY.md` rule 5). The chapter
  notes the original does not return after the message, so the message is advisory in that
  routine as well.
- **The scroll-item helpers' unbounded copies and unused index: not applicable.** RWE uses a
  list box that owns its strings.

## 7. Branches RWE does not have (multiplayer, disc, errors)

**Reference.** State 0 for the multiplayer game type allocates the last-frame surface, reports
game event seven and, for a nonzero rejection code other than two, opens the translated reason.
State 1 draws the frozen frame behind that message box. State 4 opens the CD-check dialog for
a campaign without its disc, whose OK retries state 5. Lobby flag four changes MainMenu's text to
OK and leaves the network game on cleanup. The cleanup notification (index minus one) frees
the retained surfaces, buffers and mission list, selects CD category four and restores the saved
display brightness.

**RWE: not applicable, and nothing is missing that a lockstep game needs.** A desync ends the
game through `sendGameEnded("abandoned"...)` and the desync report (`GameScene_endgame.cpp`
about 118-175), a peer's departure is handled by the network service, and the options screen
owns brightness. The "rejection reason" text the original shows over the last frame (a player
dropped, a version mismatch) has no counterpart; `docs/NETWORK-OVERLAY.md` and the desync report
carry what RWE tells a player about a failed game.

## 8. The game object, the debris cache and the rest (not applicable)

**Reference.** `CreateGameObject` allocates the game plus a random interior offset and zeroes
it; the endgame state byte is separate from the wider mode; `InitMissionStatus` fills 25 results
with `U` and a terminator at index 25; the debris memory cache registers its free at exit.

**RWE: not applicable.** The simulation and scene are ordinary objects. The `U` fill is the
default of the campaign progress string (the audit
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 8 covers the 26th-index guard).

---

## 9. Gaps to fix

Ranked by what a player would notice, then by confidence. Presentation only; nothing here
touches a tick (the statistics read sim state but nothing reads them back).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The score's clock term divides by 30 and must divide by 60** (and truncate the quotient before multiplying). Invisible on shipped maps (`timemul` 0); doubles the term on any map that sets it. | DIFFERS (a code fault against the corrected register) | code: `EndGameStats.cpp` and its test; the register already has the number |
| 2 | **The statistics are read when the chart opens, with the simulation still ticking beneath the banner, fade and picture.** The original fixes them at the shutdown. | DIFFERS (traced by reading, not run) | a scenario run to confirm the sim ticks after `gameOver`; then take the snapshot in `beginEndGameSequence` |
| 3 | **A key does not dismiss the glamour picture or fill the bars, and Return opens the chat bar over the picture.** The chapter's condition includes a popped key. | DIFFERS | code: route keys through `glamourClicked` and `finishEndGameBars`; `UNCLEAR` whether the original's pointer test already covers it |
| 4 | **How long the banner shows before the fade.** RWE holds it 90 ticks on the claim that the player dismisses it; neither source has a dismissal. | UNCLEAR, **CONFLICT** of comment against sources | original check: time the banner after a skirmish win |
| 5 | **Screen-fade deadline: strict or not** (ten ticks or twenty). | **CONFLICT** (register against chapter) | spec-room re-read of the compare at the fade step |
| 6 | **The glamour picture's five-step fade-in from black is missing.** | MISSING, cosmetic | none unless a play-through objects |
| 7 | **The chart has a row for every simulated player, with no inclusion test** (observers, a rejection reason, units-created). | DIFFERS, unobservable today | none |
| 8 | **The win/loss badge on ENDMSN** was not traced. | UNCLEAR | a visual comparison with a play-through |

No source file was changed.
