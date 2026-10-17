# Player slots, updates, end-of-game, commander replacement and persistence parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for what a player record holds,
how a slot is reset and set up, what the per-call and once-a-second player updates
do, how the end-of-game countdown runs, what the "commander replacement" rule does,
what happens to a player who is eliminated, the footprint and unit visibility
samples, and what the player block saves, per `AGENTS.md` and ADR-0004. Already
covered and only cross-referenced: the economy gate, the settle cadence, starting
balances and resource sharing (`TA-SYSTEMS-PARITY.md` rules 11, 29 and 30; the
transfer functions are `TOTALA-EXE-COMMANDS.md` section 124 and
`src/rwe/sim/ResourceSharing.h`), the unit-visibility predicate
(`TOTALA-EXE-VISION.md`, `0x465AC0`), the campaign countdown
(`TOTALA-EXE-DATA.md` section 113) and the commander-death wipe
(`TOTALA-EXE-WRECKS.md`, "The game-end wipe").

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-players.md`,
  sections 1 to 12). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/game` and
  `src/rwe/ai` at the working tree of 2026-10-16 (HEAD `404f3c4d`, branch
  `bt-sim-conformance`). Nothing was built, run or changed, and tests were not run;
  a MATCHES below means the code reads as the rule says. Another agent had
  uncommitted edits in `GameSimulation.cpp`, `GameSimulation.h` and
  `UnitBehaviorService.cpp` while this was written, so line numbers in those files
  may have moved.
- Where the byte-tactics battleroom option names appear (Continues, Game Ends,
  Deathmatch) they come from `docs/TA-NETWORK.md`'s capture of the retail options
  byte, which is the register's, not the chapter's.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Player and player-info state; controller roles
2. Resetting and configuring slots
3. Per-player runtime services
4. Starting balances, resource accessors and the two transfer channels
5. The per-call update and the thirty-tick deadline
6. Victory, defeat and the end-of-game countdown
7. Commander replacement (the Deathmatch rule)
8. Watching after elimination
9. Footprint and unit visibility samples
10. Saving and loading the player block
11. Blink timing
12. Gaps to fix

---

## 1. Player and player-info state; controller roles

**Reference (section 1).** Ordinary loops run over **ten** 0x14b-byte player records.
A record holds an active flag, identity and timing fields, a pointer to the lobby's
player-info, short and full names, an inclusive unit range and ids, a controller type,
AI and squad pointers, the explored-grid storage, resources and statistics, an account
pointer, update deadlines, **five eleven-entry relationship arrays**, and slot, start
and rank flags. The player-info holds lobby and map identity, side and colour, sharing
flags, watching, mapping and line-of-sight settings, initial resources, maximum units,
version and map checksum. Controller roles: **0 unused, 1 local human, 2 local computer,
3 remotely controlled**; a nonzero type alone does not qualify a player for every
economy or AI action.

**RWE. DIVERGENCE (structural), a superset.** `GamePlayerInfo` (`sim/GamePlayerInfo.h`)
holds name, `GamePlayerType` (Human or Computer), colour, status (Alive or Dead), side,
the resource blocks, `teamId`, and `PlayerSimulation` (Local or Remote), so the four
roles are the product of two enums: Human+Local is role 1, Computer+Local role 2, and
either type Remote is role 3 (a Remote computer is possible, as for a TA host's AI,
and the original has no such combination). There is no ten-slot array in the simulation:
`players` is a vector sized by the lobby. The alliance is the lobby's team id, symmetric
(`arePlayersAllied`, `GameSimulation_visibility.cpp:180-189`), not five directional
eleven-entry arrays; this is the register's documented alliance divergence
(`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 2). Whether the lobby caps at ten is not
traced; the TA host path caps at ten (`net/ta/TaHostSession.h:143`).

## 2. Resetting and configuring slots

**Reference (section 2).** `ResetPlayerSlots` keeps each player-info pointer and zeroes
the record, resets the local and view player to zero, clears the identity array and
rebuilds default names, **self-relationship entries** and info defaults: controller and
active to zero, colour by index, side zero, unit ranges and counts cleared, **slot byte
0xff, team-style byte 10, alliance mode 5**, chat indices reset. **Both reset loops and
`FreePlayers` run indices 0 through 10 inclusive**, one past the ordinary ten; the chapter
says that must not be rewritten as a ten-iteration loop and that it is not evidence of an
eleventh combat player. `SetupPlayerSlot` clears all five relationship arrays, stores the
controller type, **updates the info's kind except for role 3**, enables two self entries,
clears the squad-manager pointer, assigns slot, start and rank defaults, marks the slot
active, resets lobby synchronisation and rejection state, and for roles 1 and 2 records a
display-context memory value. Naming: campaign and skirmish call role 1 "Player" and role
2 "Arm" or "Core" by side; multiplayer names come from elsewhere. A private field at +4 is
called "index" by the setup and "identity" by other views.

**RWE. DIVERGENCE (structural); nothing to port.**

- **The eleventh record: do not port.** It is an array-extent quirk of the original; RWE's
  vector has exactly the lobby's players. The one place a tenth-plus-one index could
  matter is the rule that "10" is the sentinel player number in the transfer functions
  (rule 4).
- **Defaults: MATCHES in effect.** A new `GamePlayerInfo` is built by the loader from
  the lobby (`GameSimulationLoader.cpp`, the players block), with side and colour by
  lobby; the 0xff/10/5 bytes are the original's lobby encoding and have no field.
- **Naming: UNCLEAR.** A computer opponent's display name in RWE comes from the lobby
  (`MainMenuModel` player names); the original's campaign and skirmish names ("Player",
  "Arm", "Core") are a string rule not traced. Cosmetic.

## 3. Per-player runtime services

**Reference (section 3).** `InitPlayerSlot` stamps the update, win/lose and display
timestamps with the current tick, clears balances, income, usage, capacity, lifetime
produced, consumed and wasted totals, share thresholds and the kill and loss statistics,
creates an extra unit-resources account if absent and resets it for the slot, clears the
starting-storage flag. The **explored grid is half the terrain width by half the height,
one byte per cell**, its allocation size rounded up to a multiple of eight bytes and zeroed
(it is a byte grid, not a bit grid). It builds the ten squads. **Unless the record is both
active and role 3** it allocates the squad manager and creates the player AI services, a
broader condition than role 2 alone. `InitPlayers` initialises each nonzero-type slot and
then loads the default AI command text: the mission path's slot 7 file, falling back to
`ai/default.txt`, then the downloadable AI weight scripts, and reparses limits and weights
for active role-2 players; having neither file does not stop the later per-AI passes.
`RebuildAIFeatureCells` calls the feature-grid rebuild for slots with a squad manager.
`FreePlayers` releases storage, squads, per-player AI, manager timers and the extra account.

**RWE.**

- **Timestamps, totals and the kill and loss counters: MATCHES in effect.** The totals are
  fields of `GamePlayerInfo` with initialisers (`unitsKilled`, `unitsLost`, `metalProduced`,
  `energyProduced`, `metalExcess`, `energyExcess` and the buffers, `GamePlayerInfo.h`),
  which is also the hash-safety rule in `CLAUDE.md` (every member the hash reads has an
  initialiser). The end-of-game chart's four middle columns read these four fields.
- **The explored grid: UNCLEAR.** The register's vision section has the original's cell
  size (a 32-world-unit cell in the fog test, `TOTALA-EXE-VISION.md`, the `lit` sketch for
  `0x465AC0`); the chapter's "half the terrain width and height" agrees with a 16-pixel
  height cell halved. RWE's visibility grids are `PlayerVisibility` (`sim/PlayerVisibility.h`)
  at the sim's own resolution; not compared cell for cell.
- **AI services: MATCHES the register.** One controller per computer seat, no controller for
  a Remote player (`AiSeat`), and the profile file is `ai/<profile>.txt` then
  `ai/default.txt` (`game/AiSeat.cpp:26-42`), which is `TOTALA-EXE-COMPUTER-PLAYER.md`
  section 123 ("the OTA `aiprofile` key, default `DEFAULT`, then `ai\default.txt`"). The
  chapter's "mission path slot 7" and the register's `aiprofile` key are the same resource
  slot under two names (the register puts `useonlyunits` in slot 6,
  `TOTALA-EXE-DATA.md`). Downloadable AI weights are the register's `aiWeight` text, run by
  `ClassicAiScript` (`ai/classic/ClassicAiScript.cpp`).
- **The squads: DIVERGENCE (documented).** RWE's armies are the AI's own
  `ArmyManager`/classic groups, not ten squads per player (`docs/ai-architecture-proposal.md`,
  ADR-0005).

## 4. Starting balances, resource accessors and the two transfer channels

**Reference (section 4).** `InitPlayerResources` is skipped while a saved store is pending;
otherwise campaign reads the mission's start arrays through the starting-storage helper,
skirmish the slot options, multiplayer the **host's info values times 100** (falling back to
the current slot), over **all ten** slots. Six accessors return stored energy income, usage and
net, metal income, usage and net, without settling anything. The two transfer functions are
**named the wrong way round** for what they move: the one called `TransferMetal` works on
stored energy (`SpendEnergy`, destination `energyMake`) and the one called `TransferEnergy` on
metal. Each rejects either player value 10, optionally clamps to the source's stock, ignores
exactly zero, and credits the destination's production field; with the flag set it also spends
from the source and sends the share message carrying the float's raw bits. **An active role-2
destination scales the credit to 0.5 at difficulty 0 and 0.7 at difficulty 1.** The body does not
clamp negative amounts, test alliance or clamp to the destination's storage.

**RWE.**

- **Starting balances: see `TA-SYSTEMS-PARITY.md` rule 29**, including its open CONFLICT on
  whether a computer player's starting stock is handicap-scaled. The chapter's statement
  here (starting balances are initialisation, not production) is the reference side of that
  conflict; nothing new.
- **Transfers: MATCHES the register and the chapter.** `ResourceSharing.cpp` names its
  resource enum by what it moves, with a comment that "the original names its two transfer
  routines the wrong way round" (`:13-19`); a gift needs no alliance (`:171-204`); the sharing
  toggles and reserves are as `TOTALA-EXE-COMMANDS.md` section 124. The handicap is applied by
  the settle on the whole production buffer (`ResourceSharing.h`, last paragraph of the comment
  on `applyPlayerShareCommand`), which is the same factor the chapter applies at the credit
  and is the economy's rule (`TOTALA-EXE-ECONOMY.md` section 23).
- **Negative amounts: DIVERGENCE (documented, hardening).** The original does not clamp and a
  negative amount reverses the transfer; RWE refuses a non-positive amount (`:190-196`;
  `ResourceSharing.h`, "Two deliberate departures"). Correct under `docs/SECURITY-AUDIT.md`.
- **The six accessors: MATCHES in effect.** The HUD reads the settled buffers
  (`previousMetalProductionBuffer` and its relatives), not a fresh calculation.

## 5. The per-call update and the thirty-tick deadline

**Reference (section 5).** `UpdatePlayers` first runs pending pathfinder searches, then visits
active role-1, 2 and 3 records whose slot byte is not 10. **Before** the slower deadline gate it
ticks an existing squad manager, calls the AI updater, refreshes line of sight for the player's
live units and draws radar units for the viewing player. Then the gate: a not-yet-due player
skips the rest; a due player **advances its deadline by 30 ticks** (not "now plus 30"), only once
per call, so repeated calls can catch up. The slower phase handles conditions and endgame, the
economy and sensor and report work. **Economy runs for active local roles when the player still
has units or has never created any, the endgame bit is clear and no endgame countdown is
active.** For the viewing player the due phase updates sensor, radar, cloak state and radar
mapping; in multiplayer it increments an economy-report counter and **sends a snapshot every
fourth eligible increment**. `RefreshOwnedUnitsLineOfSight` refreshes every live unit in a range
and does not size the player's grid.

**RWE. MATCHES where the register already audited it, with two points the chapter adds.**
`TA-SYSTEMS-PARITY.md` rules 6, 8, 9 and 11 are the tick order, the player and slot walk, the
periodic cadences and the economy gate. Beyond them:

- **Catch-up of the deadline: UNCLEAR, no effect.** RWE has no player deadline: the settle
  fires when `gameTime % 30 == 0` (`GameSimulation.cpp:4403`), and a tick is never skipped
  (lockstep runs every tick). The original's "advance by 30, once per call" matters only when
  a call is late; RWE cannot be late. Nothing to port.
- **The economy's "never created a unit" and "endgame countdown" gates: DIFFERS for the
  second.** The first is `TA-SYSTEMS-PARITY.md` rule 11's open point. The second means that
  while an end-of-game countdown runs, no player's economy settles. RWE does this only for a
  mission (`missionRules->economyFrozen()`, `GameSimulation.cpp:4397`; `MissionRules.h:175`).
  For a skirmish RWE has no countdown at all (rule 6), so the economy keeps settling until
  the scene leaves.
- **Line of sight and radar each call: MATCHES in effect.** RWE's visibility is updated in the
  sim pass (`PlayerVisibility`) for every player, where the original refreshes line of sight for
  every player in the call and radar only for the viewing player (`TOTALA-EXE-VISION.md`
  section 17, which settles the radar picture being recomputed for one player per tick, and why
  RWE's decisions use `canSeeUnit`, not the radar picture, `TOTALA-EXE.md` section 88).
- **The report counter: MATCHES the register** (`TA-SYSTEMS-PARITY.md` rule 31).

## 6. Victory, defeat and the end-of-game countdown

**Reference (section 6).** For the local campaign player victory is checked before defeat. A
detected outcome initialises a **negative shared countdown to 4**; later qualifying observations
decrement it until it falls **below zero**, then set the endgame and result flags. The blocks do
not reset the countdown when a later check is false. Non-campaign local processing also checks
defeat, unless the watching or exclusion info flag suppresses it; victory can enter the same
countdown through an alternate path. These checks live in the player's due phase, so a step is an
observation on that schedule, not a rendered frame. **A separate multiplayer fallback after the
player loop checks for zero combat players when the commander-death mode differs from 2** and
advances the same countdown outside the per-player gate, once per `UpdatePlayers` call. "A blanket
explanation that all endgame countdowns mean a fixed number of seconds would therefore be
incorrect."

**RWE.**

- **Campaign: MATCHES the register** (`MissionRules`, `countdown{-1}`, `MissionRules.h:104-126`;
  `TOTALA-EXE-DATA.md` section 113: set to 4 the first second, then five more, never reset).
  The chapter's "initialised to 4, falls below zero" is the same word as the register's.
- **Skirmish and multiplayer: DIFFERS, or UNCLEAR whether the original has the delay.** RWE
  decides at once. `computeWinStatus` (`GameSimulation.cpp:2554-2620`) is asked every tick by the
  scene (`GameScene_replay.cpp:817-837`); the first tick it returns Won or Draw sets `gameOver`,
  starts the banner (`GameScene_endgame.cpp:176-199`), and reports the result. There is no
  countdown between the last enemy commander's death and the banner. The chapter says the
  non-campaign path reaches the same shared countdown; the register's section 113 puts it under
  "In a campaign game" and for skirmish says only that defeat is checked first (`0x465167`);
  the Nanolathe summary says "a condition that becomes true arms a countdown and only fires the
  ending latch about 150 ticks later" (`TOTALA-EXE-EXTERNAL.md` lines 284-296, not verified
  here). If the delay is real for a skirmish it is about five seconds of play after the
  deciding death, with the economy frozen, before the banner. Gaps rank 2.
- **Draw: UNCLEAR.** RWE returns `WinStatusDraw` when nobody is left alive; neither the
  chapter nor the register says what the original shows if both last units die in the same
  second. Not a shipped-data question.
- **Victory test shape in a team game: DIVERGENCE (documented in the code).** RWE ends the game
  when every living player is on one team (`computeWinStatus` comment, `:2572-2583`), which the
  chapter does not cover (it treats the zero-combat-players fallback only).
- **The copy-protection branch: do not port** (`TOTALA-EXE-DATA.md` section 113).

## 7. Commander replacement (the Deathmatch rule)

**Reference (section 7).** When the local defeat reaches the countdown threshold **and the
commander-death mode equals 2**, the updater creates a replacement commander. It resolves the
local side's commander definition and draws candidate positions inside a central rectangle with
**one-tenth-of-the-map margins**, up to **9999 draws**. Each draw applies a **three-by-three
arrangement of footprint-placement probes** around the candidate (offsets derived from terrain
width and height; "implementation-specific, not a general nearest-valid-cell search"), requires
no feature at the candidate and, on lava worlds, sampled mean ground above sea level. After the
loop it creates the unit **at the last candidate drawn**, sets starting storage from the host
settings, credits initial energy and metal through the new unit's account with the computer
difficulty scaling, recalculates sight and focuses the commander. The returned pointer is not
tested before the account is touched, and exhausting the attempts is not handled by a safe
fallback.

**RWE. MISSING.** `CommanderDeathMode` has two values, `GameEnds` and `GameContinues`
(`game/GameParameters.h:124-130`), and the lobby offers two stages for the button
(`MainMenuScene.cpp:1430`, `skirmishStageToCommanderDeathMode` at `:1871-1874`, text
"Game ends|Continues"; the multiplayer option table has the same two, `MainMenuScene_multiplayer.cpp:50`).
The original's battleroom has three: **Continues, Game Ends and Deathmatch** (the options byte
bits 0x08 and 0x10 are "Game Ends" and "Deathmatch", neither bit meaning Continues,
`docs/TA-NETWORK.md`), and the register already reads them as "0 continue / 1 game ends / 2 game
ends with a respawn" (`TOTALA-EXE-WRECKS.md`). In mode 2 the commander's death still wipes the
player's army (the wipe runs for any nonzero rule) and then, after the countdown, a new
commander appears. RWE has neither the mode nor the respawn.

- **What the new commander gets.** Storage from the host settings and the starting stock as a
  credit, scaled by the computer difficulty for a computer player. The scaling is the register's
  handicap (`TOTALA-EXE-ECONOMY.md` section 23). Placement is random in the central 80 percent
  and may land on bad ground: the chapter says the last candidate is used even if every probe
  failed. A port should instead stop at the first candidate that passes and refuse to create
  when none does (never an unchecked pointer; `docs/SECURITY-AUDIT.md`).
- **Determinism.** The draws are sim-stream draws; RWE would take them from `simulation.rng`
  with a modulo (`SimRandom.h`'s `randomBelow`), never `uniform_int_distribution`.
- **The TA battleroom: not traced.** RWE's TA host writes the options byte from its own two-state
  rule, so what a joining TA sees for the commander option, and what RWE does with a joiner's
  Deathmatch, was not looked at (`net/ta/TaHostSession.cpp`, `docs/TA-NETWORK.md`).

## 8. Watching after elimination

**Reference (section 8).** For defeat paths other than rule 7, multiplayer can keep the local
participant **watching** if no rejection reason is set and host configuration permits it or a
hosted computer player remains: it sets the info's watching flag, **clears two map flags**,
recalculates sight and broadcasts the changed player info. Without surviving hosted AI it can
show a yes/no "continue watching" dialog (the YESORNO-style layer, translated Yes, No and
title); with AI it warns that leaving terminates the hosted AI, subject to remaining combat
players. If watching is unavailable it sets the endgame and failure flags directly.
`ContinueWatchingCallback` plays the button sound; choice 1 broadcasts the info, clears the
dialog flag and reports game event 4; choice 2 sets the exit bit. The dialog does not revive
units or reverse statistics.

**RWE. MISSING.** There is no elimination flow for the local player: when the local player's
status becomes Dead the scene prints "side forces have been obliterated" in the fallen colour
(`GameScene_audio.cpp:266-284`) and nothing else; there is no dialog, no watching flag in
`GamePlayerInfo`, no change to the fog (the "two map flags" are presumably the fog and
line-of-sight display switches, but the chapter does not say), and no hosted-AI warning. The
game continues until the win test is met, with the dead player's view whatever its vision gives
(no units, so black outside the last explored area) unless `spectatorMode` (replays and demos)
is on. In a two-player game the win test ends the game on the same tick, so the missing flow
only shows in a game with three or more sides or with a team. **Original check first** (Gaps
rank 3): lose a commander in a three-player skirmish and note what the screen offers.

## 9. Footprint and unit visibility samples

**Reference (sections 9, 10).** `IsFootprintVisible` tests two positions, the supplied cell
origin and the diagonally displaced origin plus dx and dy, converts cell coordinates to 16-unit
world coordinates and projects into the half-resolution information grid with X divided by 32 and
(Z minus half Y) divided by 32. With map flag 2 it reads the supplied player's explored byte map;
otherwise the shared visibility mask with the global viewing-player bit. Out of bounds is false.
**One passing sample returns true**; it does not require every cell, and two samples are not exact
coverage. `IsUnitVisibleToPlayer` returns true first for the player's own unit, then false for a
foreign unit with activation bit 4 (cloak), builds a position from the model minimum X/Z and
maximum Y plus the unit position, and, **without runtime bit 0x200** (the sonar-contact bit),
returns false for a model top below sea level. It then samples four model-bound positions (top
corner, opposite X corner, the Z-extended lower corner, the other X corner at the lower edge; the
fourth is not the first point, despite a provisional source comment); one passing sample makes the
unit visible. It does not decide radar, sonar, cloak detection, weapon choice or hit.

**RWE.**

- **Unit visibility, order and conditions: MATCHES the register** (`TOTALA-EXE-VISION.md`,
  `0x465AC0` in full, with the same four sample corners).
  `canSeeUnit` (`GameSimulation_visibility.cpp:213-259`) tests own, cloak, the sonar veto lifted by
  a contact, then the fog.
- **Four samples against one: DIFFERS (known).** The code says so: "the fog grid, asked of each
  corner of the unit's footprint in turn. RWE asks it once, of the unit's position"
  (`:256-258`). A building whose centre is in the dark but whose edge is lit is seen by the
  original and not by RWE; for a large unit at the edge of a vision circle the original sees it a
  few cells sooner. The register states the four-corner probe as fact
  (`TOTALA-EXE-VISION.md`, "the footprint is probed four times") and this chapter confirms it with
  the order of the corners. A cheap, deterministic change that also moves what the weapon scan and
  the computer player can target (`canSeeUnit` is the sim's one visibility question), so it needs
  the hash-oracle treatment and a play-test (Gaps rank 4).
- **Sonar-contact bit: MATCHES.** RWE asks the player's `sonarContacts` set rather than a unit bit
  (`:247-253`).
- **Footprint visibility test: MISSING.** RWE has no `IsFootprintVisible`: `nodrawundergray` is not
  read (`BT-MAP-FEATURES-PARITY.md` rule on forced no-draw-under-gray) and no other caller of the
  rectangle test is known. The register marked `0x4658E0` "inferred, not followed into"
  (`TOTALA-EXE-WRECKS.md`, "Loose ends"); the chapter now supplies the rule (two samples, any one
  passes, same two backends as the unit test), which turns that inference into a reading.

## 10. Saving and loading the player block

**Reference (section 11).** `SavePlayers` writes a Players account with "Human Player" and a
0x1c-byte clock box, and for each nonzero-controller slot a per-player account with balances,
lifetime production, use and waste, storage bonuses and the enable flag, kills and losses, the
update, win/lose and display timestamps, Controller, Logo, Side and an **eleven-byte Alliances
box**. `LoadPlayerControllers` is a separate preliminary pass reading Controller (zero when the
account is absent) into setup options and the runtime type. `LoadPlayers` restores Human Player
with default 10 into the viewing player, then **requires exactly 0x1c clock bytes**; a short read
returns early after the index assignments, before the per-slot restore. Alliances are read only
when the box size is exactly 11 and the self entry is forced to 1; missing accounts clear the
controller. The serializer **does not save** every field or relationship array: commander-specific
kill and loss counters, ranks, share thresholds, explored bytes, unit objects and AI internals are
not all reconstructed here.

**RWE. DIVERGENCE (structural), and wider.** The player block is part of the simulation JSON
(`game/save_util.cpp:262-330`), hashed (`GameHash_util.cpp:67-82`) and dumped. It saves what the
chapter says the original does not: the sharing flags and reserves "steer the simulation, so a
lockstep resume or a replay keyframe that forgot them would diverge" (`GamePlayerInfo.h`, the
sharing comment), and the air-base rebuild timestamp (`airBasesRebuiltAt`). Loading validates
types and ranges and throws on a bad file (`loadEnum`, `j.at`). The original's short-clock early
return, the exact-size alliance box and the default-10 "no human player" are file-format quirks
with no counterpart. `TA-SYSTEMS-PARITY.md` rule 31 covers the per-unit side. No gap.

## 11. Blink timing

**Reference (section 12).** `UpdateBlink` decrements a positive minimap blink counter, otherwise
resets it to 7 and toggles the blink flag, so successive toggles are **eight calls apart**. The
rate follows the caller.

**RWE. MATCHES the register** (`TOTALA-EXE-INTERFACE.md` line 976: the blink bit flips every
eight passes of the main loop). The scene derives it from scene time:
`sceneTime / 8 % 2` (`GameScene_render.cpp:964-966`), an eight-tick parity rather than a counter
that is reset, with the same period. Only the dashed order lines read it
(`TOTALA-EXE-VISION.md` line 874); the minimap dots do not blink in RWE
(`TOTALA-EXE-VISION.md` line 736).

## 12. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture run
on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | The Deathmatch rule: commander-death mode 2, the respawn with the central random placement and the starting credit (rule 7) | `src/rwe/game/GameParameters.h:124-130` (a third mode), `MainMenuScene.cpp:1430,1871` and `MainMenuScene_multiplayer.cpp:50` (three stages), `GameSimulation.cpp:4296-4372` (`processVictoryCondition`), `ReplayFile.cpp:135`, `SaveFile.cpp:185` (names), `save_util`/`GameHash_util` if new state | **Yes.** Host a three-state battleroom game in the original and kill a commander: how long before the new one, where it lands, what stock it has. A hashed, replay-visible change: needs a command-version gate. |
| 2 | Is there an end-of-game countdown in skirmish, and does it freeze the economy (rule 6)? | `src/rwe/game/GameScene_replay.cpp:817-837`, `GameScene_endgame.cpp:176-199`, `GameSimulation.cpp:4397` (the freeze) | **Yes, a stopwatch.** Kill the last enemy commander in a skirmish and time the banner; watch whether income stops. If the countdown is real the match should not end the same tick. |
| 3 | The eliminated-player flow: watching flag, the continue-watching dialog, the hosted-AI warning (rule 8) | `GamePlayerInfo.h` (a watching flag), `GameScene` (a dialog), `GameScene_endgame.cpp` | **Yes.** A three-player skirmish and a LAN game, losing as the second player to fall. |
| 4 | Four footprint samples in `canSeeUnit` (rule 9) | `src/rwe/sim/GameSimulation_visibility.cpp:256-258` | **Yes**, and the hash oracle: it changes which units the weapon scan and the AI can see. `TOTALA-EXE-VISION.md`'s four-corner reading is already the register's; the chapter confirms the corner order. |
| 5 | `IsFootprintVisible` for `nodrawundergray` features (rule 9) | `src/rwe/sim/GameSimulation_visibility.cpp` (a rectangle test), `BT-MAP-FEATURES-PARITY.md` rule 6 | No for the rule; yes for the one-wreck play-test the feature audit asks for. |
| 6 | Mission start for an invalid player and for an empty unit list (`BT-UNITS-UNIT-COMMANDS-PARITY.md` rule 3) | `GameSimulationLoader.cpp` | Spec-room: classify `0x4B6290`. |

### Documented divergences this audit leaves alone

Two enums instead of four controller roles; the lobby's team id for alliance; no eleventh record;
no player deadline; the army managers in place of ten squads; the economy settle on a modulo; the
wider save and hash of the player block; the hardening of negative gifts.

### Conflicts with existing RWE findings, in one place

1. **Commander-death modes.** Register (`TOTALA-EXE-WRECKS.md`): three modes, 0 continue, 1 game
   ends, 2 game ends with a respawn. RWE: two (`GameEnds` = 0, `GameContinues` = 1, in the order
   of the GUI's text, so the numbers are RWE's own and not the original's). The chapter's mode-2
   rule agrees with the register's. Not a disagreement about the original; recorded because the
   enum's numbering is easy to mistake for the wire value.
2. **Where the non-campaign countdown lives.** Register (`TOTALA-EXE-DATA.md` section 113): the
   once-a-second, then-five-more countdown is written under "In a campaign game", with skirmish
   noted only as checking defeat first. Reference: the same shared word is used for the
   non-campaign path and for a multiplayer fallback. RWE: no skirmish countdown. Open until a
   stopwatch run.
3. **Four samples against one in the unit visibility predicate.** Register and reference agree
   (four corners); RWE's code comment records the one-sample shortcut. Listed so it is not
   mistaken for a register disagreement.
