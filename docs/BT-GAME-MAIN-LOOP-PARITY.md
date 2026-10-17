# Battle-frame dispatch, setup transitions and starting units parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
module chapter of the reference, `game-main-loop`: how one pass of the owning frame calls and
gates the simulation, the input, the drawing and the movie capture, how the front-end callback
chain hands over to a battle, and the three starting-resource and starting-commander helpers.
This is the finding that implementation work cites for any of them, per `AGENTS.md` and
ADR-0004.

The pacing arithmetic itself (the step budget, the five-step cap, the speed hysteresis, the
remote-lag rule, the tick order) is chapter 02 of the reference and is in `TA-SYSTEMS-PARITY.md`
rules 1 to 6; it is not restated. The module states that it does **not** cover that arithmetic
either ("the actual pacing calculation and per-tick subsystem loop are in a neighbouring
module"). What this audit adds is the frame around it.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-main-loop.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names (`0x496790` to `0x497180`); rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; the rows this
  audit touches are BT-23, BT-24, BT-25 (pacing, already open) and BT-83 (commander-death modes,
  resolved), none re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no timer measurement or game run was made. Weaker than `TOTALA-EXE*.md`.
  Disagreements are marked **CONFLICT**, with no winner picked.
- **No executed observation of the original was used.** The Gaps table says which items an
  original-game check settles.
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene.cpp` (the update loop),
  `GameScene_menu.cpp` (pause), `LoadingScene.cpp` (commander spawn and camera),
  `GameSimulationLoader.cpp` (player set-up), `sim/GameSimulation.cpp` (storage),
  `MainMenuScene_campaign.cpp` and `GameLaunch.cpp` at the working tree of 2026-10-17. Nothing
  was built, run or changed. Another agent had uncommitted edits in the tree, so every line
  number is "about".
- **Reading of existing RWE documents:** `TOTALA-EXE-INTERFACE.md` section 70 (pause),
  `TOTALA-EXE-ECONOMY.md` section 23 (storage), `TOTALA-EXE-DATA.md` sections 105 and 115
  (campaign set-up), `TOTALA-EXE-KEYBOARD.md` (Ctrl+F10 movie mode) and `TA-NETWORK.md`
  (the launch sequence).

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. One battle-frame pass: the two branches and what gates simulation
3. Start-position assignment is retried inside the pass
4. Input, edge scroll, selection refresh and drawing
5. Movie capture
6. The callback chain: initial frame, menu frame, pre-battle frame, campaign set-up
7. Mission option flags
8. Starting storage: the three helpers that write the player's base
9. Commander creation and the camera
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| profiling snapshot, simulation branches | `TA-SYSTEMS-PARITY.md` rules 3 to 5 (pacing, pause); `TOTALA-EXE-INTERFACE.md` section 70 | pause and the multiplayer exception **MATCH**; the interface-flag gate and the local-pause skip are rule 2 |
| input, selection refresh, drawing | none | rule 4 |
| movie capture | `TOTALA-EXE-KEYBOARD.md` (Ctrl+F10) | rule 5; semantics added, decision stands |
| entering and returning to menus | none (the callback chain has no counterpart) | rule 6, not applicable |
| pre-battle and campaign set-up | `TA-NETWORK.md` (launch), `TOTALA-EXE-DATA.md` section 115 | agree; rule 6 |
| mission option flags | `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 6 (the three modes) | rule 7 |
| starting resources, storage bonus | `TOTALA-EXE-ECONOMY.md` section 23 "the per-player base storage ... has not been found" | **the chapter finds the writers** (rule 8) |
| commander creation and camera | none | rule 9 |

## 2. One battle-frame pass: the two branches and what gates simulation

**Reference (module "profiling snapshot and simulation branches").** An application frame, a
battle pass and a simulation tick are three different things. One battle pass first folds nine
profiling accumulators into display values and resets them, then takes one of two branches,
chosen by bit 0 of the application flag at game offset `+0x2a44`, the **network-form** flag:

- **Network form.** The pacer is always called. If steps are pending it runs them (argument
  one), charges profiling bucket 8, and runs three continuation hooks. If no steps are pending
  and the game is paused, queued packets are flushed, incoming packets handled, and a probe sent
  when the timer strictly exceeds the next-probe deadline (then the deadline advances by 60
  timer ticks). If no steps are pending and the game is **not** paused, this path services no
  packets at all (other paths do).
- **Local form.** Simulation runs **only if bit 0 of the interface flag at `+0x37ebe` is clear
  and the game is not paused**. The pacer is called, and steps run if pending (argument zero).
  When paused, or when the interface bit is set, the local branch **skips the pacer entirely**.

The interface flag is the same bit the menus set and clear (`TOTALA-EXE-INTERFACE.md`: the
close handlers "clear `[0x37ebe]&1`"). That is the mechanism behind the register's "menus pause
a single-player game and never a multiplayer one": the gate is in this function, not in the pause
flag.

**RWE.**

- **Single-player menu pause: MATCHES.** `GameScene::setMenuPause` (`GameScene_menu.cpp` about
  281-296) pauses a game with no remote peers and not own-clock, remembers that the menu did it
  (`menuPausedGame`) and unpauses only its own pause; it refuses in a network game. The result
  is the register's (section 70). RWE does it with a pause command in the command stream, where
  the original gates the local simulation on a UI bit; the visible behaviour is the same.
- **Pause itself: MATCHES.** `GameScene.cpp` about 594-606 zeroes the accumulator, including the
  fractional tick, and takes pause from the shared state in a network game; rendering, chat and
  camera input continue.
- **Packet service during pause: not applicable.** RWE's network thread runs regardless.
- **The local path skipping the pacer while paused: UNCLEAR, no RWE counterpart.** In the
  original the pacer's elapsed quantity comes from a timestamp it keeps, and the local paused
  branch does not call it. When the pause ends, the first call sees all the paused wall time as
  elapsed. The pacer's budget is capped at five steps and a capped budget feeds the hysteresis
  counter that lowers the effective speed (`TA-SYSTEMS-PARITY.md` rule 3), so a long single-player
  pause could, if nothing resets the timestamp on resume, cost a speed step. The chapter does not
  say whether something resets it, and the pause toggle is outside this module. RWE discards the
  backlog on pause (`millisecondsBuffer = 0`) and so cannot show the effect. **Settled by** a
  spec-room read of the pause-toggle handler for a timestamp write, or by pausing a local game for
  a minute and watching the speed readout.

## 3. Start-position assignment is retried inside the pass

**Reference.** In the network-form branch, after the pending steps have run, if bit 2 of the
loading flag at `+0x38d75` is set the pass calls `AssignStartPositions` and **clears the bit
only when the call reports success**. A failed attempt is therefore repeated on every following
pass. (What the routine does is outside the chapter.) The chapter draws one conclusion itself:
"start assignment is retried until success".

**RWE: DIVERGENCE (architectural, undocumented).** Start positions are dealt once, at load, by
`GameSimulation::dealStartPositions` from the seeded generator, identically on every peer, and
commanders are spawned in `LoadingScene` before tick 0 (`LoadingScene.cpp` about 425-465). There
is nothing to retry because there is no message to wait for: RWE is lockstep and TA is
owner-authoritative (`TA-SYSTEMS-PARITY.md` rule 3). In the original the positions can land
after the first game steps have already run; in RWE the first tick sees the commanders. Not in
section 88. No player-visible rule depends on it, except for a **recording**: a demo made by the
original may have its first records before the start positions are known (`TA-DEMOS.md`), and
RWE's own `--record-demo` writes the commander at the start. Low.

## 4. Input, edge scroll, selection refresh and drawing

**Reference.** After the simulation, the pass runs `HandleGameKey` and `UpdateEdgeScroll`
**only when bit 0 of the interface flag is clear** (so not while a menu is up). It always
collects the visible unit ids. It then decides how a pending selection refresh is handled: if
the interface word contains `0x800`, or any bit of `0x65`, or a separate three-bit field at
`+0x2bee` is nonzero, a set bit 4 of a flag is *cleared* and the unit index is cleared (no
refresh); otherwise bit 4 is cleared and `RefreshSelectionOrders` is called. `DrawBattleFrame`
is called with both arguments one **whatever the simulation branch did**, so a paused or
step-less pass still draws.

**RWE.**

- **Drawing every pass: MATCHES.** The scene renders each frame, paused or not.
- **Game keys and edge scroll while a modal is up: DIFFERS (presentation, low).** The original
  disables both while the interface bit is set. RWE's edge scroll block (`GameScene.cpp` about
  651-690) is gated on SDL keyboard focus alone and the arrow-key nudge (about 640-650) on
  nothing; neither tests whether the options menu or a dialog is open. With the F2 menu open in a
  paused single-player game, pushing the pointer to the screen edge still scrolls the world behind
  it. The menus are centred, so it needs a deliberate move to the edge. Whether the interface bit
  is set for dialogs other than the options and save/load menus is not in the chapter (the
  register's section 70 lists the menus that set it).
- **The selection-refresh condition: UNCLEAR, no RWE counterpart.** RWE's selection holds unit
  ids and refreshes its order panel from the simulation each frame; there is no deferred "refresh
  later" bit. The condition lists which interface modes (`0x800`, the `0x65` group, the three-bit
  field) suppress the refresh, but the chapter does not name them, and the register's
  `TOTALA-EXE-INTERFACE.md` does not decode `0x2bee`. A player-visible effect would be an order
  panel that stays stale while a build placement or similar mode is armed. Not traced; no
  reported symptom.
- **Profiling snapshot: not applicable.** RWE's per-phase timers are the opt-in
  `RWE_ENABLE_SIMPROF` and `RWE_ENABLE_RENDERPROF` builds (`docs/PROFILING.md`), not part of
  the pass.

## 5. Movie capture

**Reference.** When the capture index is positive and `nextMovieFrameTick` is no greater than
`gameTick`, the pass calls `SaveScreenshot` with the configured text buffer and the prefix
`FRAM`, advances the capture deadline by **integer division of 30 by the output rate**, and
resets `lastSimBudgetTick` to the current timer ticks (so the capture interval is not later
counted as elapsed simulation budget). There is **no catch-up loop**: a late pass captures at
most one image. A rate of zero divides by zero; a rate above 30 gives a zero increment and so a
capture on every pass.

**Register.** `TOTALA-EXE-KEYBOARD.md` (the Ctrl+F10 row and the "Not worth porting" paragraph)
records the registry-gated movie mode: `MOVIE%03i` numbering, `FRAM` frames at the "Movie
Output Rate" registry rate into "Image Output Directory".

**RWE: MISSING, by documented decision.** No movie-frame recorder. The chapter adds three facts
to the register's line: the deadline increment is `30 / rate` in integer arithmetic, counted in
simulation ticks (so every rate from 16 to 30 gives an increment of 1, an image every tick, and
a rate above 30 gives 0, an image on every pass), one image at most per pass, and the **side
effect on the pacer's timestamp**. The last is the only part that touches the simulation: the original's
capture mode changes how its own speed governor sees time, so a game being filmed is paced
differently. Nothing in RWE depends on it. Not worth porting; worth one clause in the keyboard
document.

## 6. The callback chain: initial frame, menu frame, pre-battle frame, campaign set-up

**Reference.** `InitFrame` and `ReturnToMainMenuFrame` install the front-end state and the menu
callback (state 2) and the leave-network close handler. `MenuFrame` refreshes unit information,
runs the front-end state machine and tests four transitions in order:

| Condition | Installed state and callback |
| --- | --- |
| application bit 2 set, mission game type 1 | state 4, campaign set-up |
| application bit 2 set, game type 2 | state 5, loading screen |
| front-end menu state `0x11`, application bit 2 set | state 3, pre-battle |
| front-end state `0x10`, bit 4 at `+0x2b4c` set, substate `0x12` or `0x13` | state 3, pre-battle |

Every transition hides the software cursor; a callback change does not call the new callback in
the same body. `PreBattleFrame` uses the local player: if its flags byte `+0x97` has bit 0 set
it **broadcasts the one-byte value 8** with that player's id; otherwise, with bit 4 of `+0x2b4c`
set and the heartbeat deadline strictly in the past, it advances the deadline 60 timer ticks and
sends a heartbeat. Every path then installs the loading screen: **there is no acknowledgement
wait**. `CampaignSetupFrame` sets two players, slot 0 of kind 1 and slot 1 of kind 2.

**RWE.**

- **Callback chain: not applicable.** RWE's scenes (`MainMenuScene` to `LoadingScene` to
  `GameScene`, through `SceneManager`) replace it; the guard against re-entering the new
  callback in the same body has no counterpart because a scene change is queued
  (`setNextScene`).
- **Campaign set-up: MATCHES.** `MainMenuScene_campaign.cpp` about 330-334 builds two players,
  the human on the chosen side in colour 0 and the computer on the other side in colour 1, and
  `TOTALA-EXE-DATA.md` section 115 records the same colours at `0x477C79` and `0x477C8B`. The
  mission, not the lobby, supplies what each starts with (`GameSimulationLoader.cpp` about
  863-875).
- **Byte 8 and the heartbeat: MATCHES the register where RWE speaks TA.** `TA-NETWORK.md` ("The
  host sends `0x08` (loading started)") is the same packet. RWE's own lobby carries its launch in
  its own protocol; the heartbeat (60 timer ticks) has no counterpart because the lobby has its
  own keep-alive. The "no acknowledgement wait" is consistent with `TA-NETWORK.md`'s ladder of
  fixed 200 ms steps on the host, so a TA joiner that is slow is not waited for in the original
  either; whether RWE's TA-host panel waits (it launches once the peer is in the battleroom)
  was not traced.

## 7. Mission option flags

**Reference.** `ApplyMissionOptionFlags` (`0x496e10`) copies the first integer of a four-integer
settings block into `battleCommanderDeath`, and assigns the other three into **single-bit
fields** at `+0x14281`: input `+0xc` to bit 2, input `+0x4` to bit 0, input `+0x8` to bit 1.
Assigning an integer to a one-bit field keeps only its low bit (2 becomes 0). Callers choose the
single-player or the shared settings block first. The chapter does not name what the three bits
mean.

**Register.** `TOTALA-EXE-VISION.md` (near 373) says bit 1 of the byte at `global+0x14281`
(mask 2) selects the per-player byte grid rather than the shared bitmask for the visibility test.

**RWE: UNCLEAR, and DIFFERS in shape.** RWE keeps the options as three named values
(`GameParameters.h` about 95-130: `LineOfSightMode` with three values Permanent, True, Circular;
`MappingMode` with two; `CommanderDeathMode` with two) and the loader stores them as such
(`GameSimulationLoader.cpp` about 758-760). The original keeps **three bits** where RWE keeps a
three-valued line-of-sight setting plus a two-valued mapping setting. The original's three bits
and RWE's two settings have to be the same information (the skirmish options RWE saves are
`Mapping`, `LineOfSight` and `LineOfSightType`), but which bit is which is not in this chapter.
Retail settings come from a button with two or three states, written as small integers, so the
low-bit truncation could only matter for a value of 2 reaching one of the three bit fields;
nothing here shows one does.

- **Commander death: not truncated.** It is the whole integer, so more than two modes could be
  carried; `BT-83` records the modes and RWE has two plus its documented respawn handling.
- **Remaining uncertainty.** Which of offsets `+0x4`, `+0x8`, `+0xc` is Line of Sight, Mapping
  and the sight type; the vision and line-of-sight audits (`BT-MAP-LINE-OF-SIGHT-PARITY.md` rule
  6) have the consumer side. A spec-room read of the three destination bits' readers closes it.

## 8. Starting storage: the three helpers that write the player's base

**Reference.** Three routines write the same pair of float fields of the player record, with a
**minimum of 200** each, and mark the player's start flag:

- `SetStartingStorageBonus` (`0x496e90`): stores two arguments, the first at `+0xdc`, the second
  at `+0xe0`, each floored at 200.
- `SpawnCommanderAtStartPos` (`0x496ee0`): copies the team's side and colour, marks the player
  started and stores the option integers the same way (energy option to `+0xdc`, metal option to
  `+0xe0`, each floored at 200).
- `InitStartingResourcesFromSkirmish` (`0x497080`): walks **ten slots**. A qualifying slot is
  active, has controller type 1, 2 or 3, and has index other than 10. In **one loop** it keeps
  running maxima of the qualifying options' metal and energy values and, immediately after
  updating them, marks the current player and stores the **running maximum, floored at 200,**
  at `+0xdc` (energy) and `+0xe0` (metal).

The chapter spells out the consequence: this is a **prefix maximum, not a two-pass global
maximum**. If an earlier participating player requests 300 and a later one 1000, the earlier
player is not revisited; only later slots see the larger value. The order dependence "follows
the loop directly; the code does not establish that it was intended as a fairness rule".

**Register.** `TOTALA-EXE-ECONOMY.md` section 23 ("Storage and overflow"; "Not ported, and why"):
a per-player base, from `player+0xDC` and `player+0xE0`, added at `0x401988` when `player+0x149`
bit 0 is set, which `0x4661B1` and `0x4661C7` read from a save or scenario as
`PlayerEnergyStorage` and `PlayerMetalStorage`; "where a skirmish gets its own values from has not
been found". RWE gives the commander the side's starting stockpile as storage instead, and the
demo corpus (86 players in thirteen games) opens at exactly 1000 and 1000 with capacity 1000.

**The offsets agree.** The chapter's `+0xdc` (energy) and `+0xe0` (metal) are the register's
`PlayerEnergyStorage` and `PlayerMetalStorage` slots, and "marks the player's start flag" is,
very probably, the register's `player+0x149` bit 0 (the chapter gives no offset for the flag; this
is an inference, labelled as one). If so, **the chapter has found the skirmish writer the register
could not**: the base is the running maximum of the lobby's start values, floored at 200, and
the flag is set from the first frame.

**RWE: DIFFERS in three ways that equal values hide.** Storage for a skirmish is the commander's
contribution (`GameSimulation.cpp` about 4491-4496: a finished commander adds the player's
`startingMetal` and `startingEnergy` while `hasBaseStorage` is clear); `hasBaseStorage` is set
only for a mission (`GameSimulationLoader.cpp` about 877).

- **The floor of 200: DIFFERS, unreachable from the lobby.** RWE's skirmish stepper bottoms out
  at 200 (`MainMenuScene.cpp` about 1609-1629, steps of 500 up to 10000), so the floor never
  binds from the menu. A command-line or saved game can hold a smaller number: RWE then has that
  small storage, the original 200.
- **Prefix maximum: DIFFERS whenever players start with different amounts.** RWE gives each
  player a base equal to its own start value; the original gives a later slot at least the
  largest start value of any earlier qualifying slot. With slot 0 at 1500 and slot 1 at 500,
  slot 1 stores up to 1500 in the original and up to 500 in RWE, so its income overflows (and is
  counted as waste) earlier. Sim-affecting and hashed. With equal values, which is every
  corpus game, the two agree.
- **After the commander dies: DIFFERS, already known.** The register says the same (a skirmish
  player loses its base with the commander in RWE); with the flag set from the start the original
  keeps it. `TOTALA-EXE-ECONOMY.md` section 23 leaves this as a gameplay question.
- **Slots 10 and controller types: UNCLEAR.** "Index other than 10" in a ten-slot walk is a
  sentinel whose meaning the chapter does not give; controller types 1, 2 and 3 are the same
  roles `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 6 left unclear (human, computer, remote).

**Remaining uncertainty and who settles it.** Whether the three writers feed the register's base
at `0x401988` is a spec-room check (the flag's offset). Whether the lobby of the original allows
**different** start values per player (the prefix maximum only matters then) is an
original-game check; RWE's lobby has per-player metal and energy, so it can produce the case.

## 9. Commander creation and the camera

**Reference.** `SpawnCommanderAtStartPos` (`0x496ee0`) takes a team index and a start-position
index. After copying side and colour and storing the start values (rule 8), it asks the mission
for the start position as three 16.16 fixed-point coordinates; on success it looks up the starting
unit type by the selected side's **commander-name entry** and calls `CreateUnit` with team, type,
position and trailing arguments 1, 1, 0. A **missing** start position is a fatal error naming its
number. There is no bounds check on team or side indices and no check on the returned unit id.
For the **local** team only, the camera is positioned afterwards at the start's whole X and Z
(fractions dropped) minus half the current view's size; remote teams do not move the camera.

**RWE.**

- **Commander from the side's commander entry: MATCHES.** `LoadingScene.cpp` about 462-463 looks
  the side up in the side-data map and spawns `sideData.commander`
  (`SideData.h`, key `commander`; the retail values are `ARMCOM` and `CORCOM`).
- **A missing start position ends the launch: MATCHES in effect.** `LoadingScene.cpp` about
  440-450 throws a message naming the map, the slot and how many positions the schema declares,
  which is the chapter's fatal error with more to say. The missing-side case also throws
  (`getSideData`, about 614-621).
- **Only the local team moves the camera: MATCHES.** RWE takes the first start position of the
  local human player, or the first mission unit (`LoadingScene.cpp` about 335-355, 491).
- **Where the camera lands: UNCLEAR, sub-pixel.** The original centres the view on the whole
  part of the position by subtracting half the view size; RWE passes the exact position to
  `setCameraPosition`, which is the view centre in world units. A fractional start coordinate (the
  schema's `StartPos` are integers in map squares, converted to world units) could therefore put
  the first frame at most under one world unit off the original's.
- **The `(1, 1, 0)` creation arguments: UNCLEAR.** The chapter does not say what they select.
  RWE creates the commander already finished (`spawnCompletedUnit`), which is also what the demo
  corpus's commander openings imply; nothing here contradicts it.

---

## 10. Gaps to fix

Ranked by what changes a game. Items 1 and 2 touch the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Starting storage**: the original stores the running maximum of start values (floor 200) as the player's base from the first frame; RWE stores each player's own value, only while a commander stands, with no floor. Same on equal values; different when slots start unequal. | DIFFERS (hashed); the register's "not found" is answered | original-game check (unequal skirmish starts, watch the storage bar), then a spec-room confirmation that the flag is `player+0x149` bit 0 |
| 2 | **Meaning of the three option bits** and the low-bit truncation: RWE's three-valued line of sight and two-valued mapping versus three bits. | UNCLEAR | spec-room read of the readers of `+0x14281` bits 0, 1, 2 |
| 3 | **Edge scroll and arrow keys while a menu is open**: original gated off, RWE not. | DIFFERS (presentation) | code: gate both on the menu-open state |
| 4 | **Single-player pause and the pacer's timestamp**: does a long local pause cost a speed step on resume. | UNCLEAR | spec-room read of the pause toggle, or a one-minute pause on the original |
| 5 | **Start positions assigned after the first steps** in a network game, retried until success. | DIVERGENCE (architectural, undocumented) | none; add to section 88's list when next edited |
| 6 | **Movie capture**: the interval is `30 / rate` in integers, with a pacer side effect. | MISSING (decision stands) | none; one clause in `TOTALA-EXE-KEYBOARD.md` |
| 7 | **Selection-refresh suppression** by interface mode. | UNCLEAR | none unless an order panel is seen stale |
| 8 | **Camera centring** at the start: whole units versus exact. | UNCLEAR (sub-pixel) | none |

No source file was changed.
