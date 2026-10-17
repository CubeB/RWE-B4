# Battle-frame input, placement clicks and battle exit parity audit

Spec-room finding, 2026-10-16. Like `BT-INGAME-CONTROL-PANEL-PARITY.md`,
`BT-ORDERS-UNIT-ORDERS-PARITY.md`, `TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it
allocates no section number in the main register. This is the finding that implementation
work cites for what the original does with a mouse click in the battle view, with building
placement as the case that carries state, and for how a finished battle hands over to the
menu or restarts, per `AGENTS.md` and ADR-0004. The panel gadgets that arm placement, the
page stepping and the camera services are `BT-INGAME-CONTROL-PANEL-PARITY.md`; what order a
click becomes is `BT-ORDERS-ORDER-DISPATCH-PARITY.md`; the construction handlers that run
after the click are `BT-ORDERS-UNIT-ORDERS-PARITY.md` rules 7 and 8; the validity rules of the
footprint itself (the yard letters, the support spread, the height) are the resolved row
BT-51 and `BT-MAP-TERRAIN-PARITY.md`. This audit restates none of them.

The chapter is four functions, `0x498F70` to `0x499880`. The register already holds the
click dispatcher almost to the instruction (`TOTALA-EXE-INTERFACE.md` section 103, "The
cursor is the decision"), the queue modifier (section 136) and the end-of-game chart
(section 104), so much of what follows is corroboration with RWE's side added. What is new is
the retained-mode rule, the all-selected-builders fan-out, the selection-drag thresholds'
units, the right-click variants by region, and the restart and exit sequences.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-build-placement.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its
  source links were followed. Addresses are the ones the chapter names; rules are restated in
  prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that rows
  already resolved there are not raised again; the rows this audit touches are BT-51, BT-61,
  BT-62, BT-63, BT-72, BT-82 and BT-89.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements are
  marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was run.
  The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/game` at the working tree of 2026-10-16.
  Nothing was built, run or changed, and tests were not run; a MATCHES below means the code
  reads as the rule says. Another agent had uncommitted edits elsewhere in the tree while this
  was written, so every line number is "about".
- **No census of the shipped FBIs or GUI files was taken.** Where a count would matter the
  rule says "not counted".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shared input state, and the stages that are separate
2. The preview, and which units a placement click reaches
3. Left click: placement, retained mode and the invalid click
4. Right click: cancellation, and the contextual variants by interface type
5. Left click outside placement
6. Frame routing priority
7. The click-or-drag threshold of the selection box
8. The post-tick validity check of the displayed focus
9. The end-of-battle transition
10. The restart transition
11. Ownership and limits
12. Gaps to fix

---

## 1. Shared input state, and the stages that are separate

**Reference (section 1).** The battle frame copies the 24-byte mouse record, updates its world
position, reads the **input-flags byte** and chooses cursor and placement behaviour. Order
mode 1 is contextual and **14 is product placement**. The relevant input bits are 1, 2 and 4
for region and interface routing, 8 for the selection drag, `0x10` for radar dragging, `0x20`
for retained command mode and `0x40` for build validity. Their producer is outside the module,
so they are described by their effects only. Windows messages are 0x201/0x202 (left down and
up) and 0x204/0x205 (right). Cursor mode 15 selects a hover unit, 17 and above take the
no-action branch of the left click and 19 is the neutral cursor. Cursor animation changes only
when the numeric mode changes. **Input, queue insertion, unit movement and construction
rendering are separate stages**: a valid preview is not a reserved factory slot or a funded
build, and another unit can obstruct the site before a builder arrives.

**RWE.**

- **Mode and cursor model: DIVERGENCE (structural), documented.** RWE keeps a variant,
  `cursorMode` (Normal with its dragging and selecting states, Attack, Dgun, Move, Guard,
  Capture, Load, Unload, Patrol, Repair, Reclaim, Build), in `GameScene.h`, and the cursor
  picture is chosen from it each frame (`GameScene.cpp`, about 800-900). There is no input-flag
  byte; the bits are the variant's alternatives and the held keys are SDL state
  (`isShiftDown`, `isCtrlDown`). The register's numeric ids are the original's cursor
  table (`TOTALA-EXE-INTERFACE.md` section 103); RWE's `CursorType` is its own enumeration over
  the same pictures.
- **The stages are separate: MATCHES.** The preview is `hoverBuildInfo` (`GameScene.cpp`,
  about 764-790), a presentation structure rebuilt every update and never sent anywhere; the
  click produces a `BuildOrder` through the command buffer; the site is tested again when the
  builder arrives (`createNewUnit`, `UnitBehaviorService.cpp`, about 5840 onward, and the
  blocked-site ladder `TOTALA-EXE-ECONOMY.md` section 112). A preview is therefore never a
  reservation here either.
- **Cursor animation on change only:** RWE's cursor service changes the picture when the
  requested type changes; not examined further, presentation only.

## 2. The preview, and which units a placement click reaches

**Reference (control-panel chapter section 2, restated by this chapter's header).**
`UpdatePlacementGhostValidity` (`0x4197D0`) converts the world point to a footprint-relative
cell, writes the preview rectangle in world units and sets input bit `0x40` from the
placement test for the **local player**; a valid site takes the build-site height, an invalid
one the plain footprint height. **`IssueMobileBuildOrders` (`0x419670`) snaps the point to the
product footprint, uses the preview height, and visits every selected local unit that has the
builder flag (`0x40`), issuing MobileBuild or VTOL_MobileBuild according to each unit's own
`canfly` bit.** One key flag (bit 2) supplies the queue-or-remove control of the issuing
routine.

**RWE.**

- **The test is asked as the local player sees the world: MATCHES.** `hoverBuildInfo` uses
  `canBeBuiltAtAsSeenBy(..., localPlayerId)` so an undiscovered enemy building does not turn
  the preview red (`GameScene.cpp`, about 777-785, with a comment saying exactly that). The
  footprint rules themselves are BT-51 (hash-visible, resolved, unported in part).
- **The order carries a terrain height from the cursor, not the build-site height:
  UNCLEAR.** The click builds a position from the footprint rectangle's centre and
  `terrain.getHeightAt(x, z)` (`GameScene_input.cpp`, about 839-844). The original uses the
  preview height, which BT-51 resolves as the minimum low bound of the support cells or sea
  level minus the waterline. Whether the simulation reads the order's y (it re-derives the
  frame's height when it places it, `UnitBehaviorService.cpp` about 5440-5455) was not traced;
  if it does not, this is cosmetic.
- **Which units the click reaches: DIFFERS.** RWE acts only when **exactly one** unit is
  selected (`getSingleSelectedUnit`, `GameScene_input.cpp` about 833) and otherwise ignores the
  click **without a sound**. The original fans the order out to **every selected builder**. The
  two agree on everything reachable from the panel, because the build panel itself is offered
  only for a single selection (`BT-INGAME-CONTROL-PANEL-PARITY.md` rule 5). They differ when the
  selection changes **after** the mode is armed: RWE disarms only on an empty selection
  (`onSelectedUnitsChanged`, `GameScene_commands.cpp` about 2865-2870), so with two
  constructors Ctrl-selected (or a group recalled) while an armed Build cursor is up, the
  original sends both to the site and RWE does nothing and says nothing. What the original does
  to the armed mode when the selection changes is not in this chapter. **UNCLEAR on the
  trigger, DIFFERS on the fan-out**; mod-visible and expert-visible, not hashed (it decides
  which commands exist).
- **The builder flag only, not the menu:** the original does not test the unit's build menu
  at the click, and neither does RWE (`refusalOf` has no menu gate, BT-70 resolved: no human
  build order is checked against the buildable list). A selected unit that is a builder but
  does not offer the armed product is therefore ordered to build it in both.
- **VTOL versus ground:** chosen by the sim from the builder's own definition in RWE, per unit
  by `canfly` in the original. Equivalent.
- **Queue-or-remove modifier:** the register, `TOTALA-EXE-INTERFACE.md` section 136 (claim A:
  a queued order that is already there is removed). RWE's Shift-click on a planned building
  cancels it (`plannedBuildOrderAt`, `localPlayerCancelBuildOrder`, `GameScene_input.cpp` about
  845-874). MATCHES in effect.
- **The product test for entering placement mode** is in the panel audit, rule 6.

## 3. Left click: placement, retained mode and the invalid click

**Reference (section 2).** In placement mode 14, **a valid site** (bit `0x40`) issues the
mobile-build orders and plays `oktobuild`. If **key flag 4** is held (the register's `0xF9`,
which is Shift) the retained-mode bit `0x20` is set and the handler returns, so further
placements can follow. Without the key the mode is restored to contextual 1, the retained bit
is cleared and the STOP gadget's group status is cleared if the gadget exists. **An invalid
site only plays `notoktobuild`**: it creates no unfinished object and **does not leave
placement mode**. The ordinary command path has the same retained-key rule (restore mode 1,
clear `0x20`, reset the STOP group state without the key); selection-only and no-action
branches return before that reset. Section 4 of the chapter adds that **retained intent
depends on the key staying down**: each frame, if `0x20` is set and `0xF9` is no longer down,
the mode is restored and the group status reset.

**RWE.**

- **Valid, with Shift: MATCHES.** A valid site with Shift held enqueues the order and keeps
  the Build cursor (`GameScene_input.cpp`, about 845-856); `okToBuild` plays from the shared
  issue path (`GameScene_commands.cpp`, about 325-340).
- **Valid, without Shift: MATCHES.** The order is issued and the cursor returns to Normal
  (about 857-861).
- **Invalid: MATCHES.** `notOkToBuild` plays and the mode stays (about 875-881); no object is
  created, because nothing is sent.
- **Shift release ends retention: MATCHES, with a narrower trigger.** `GameScene::update`
  (`GameScene.cpp`, about 755-759) returns to Normal when `commandWasQueued` is set and Shift
  is not down, and clears the flag. `commandWasQueued` is set by every enqueue and issue
  (`GameScene_commands.cpp:338`). The original's test is "retained bit set and key up", where
  the bit is set only by a click that held the key; RWE's is "a command was just sent and
  the key is up". The two differ in one case: a command sent **without** Shift while the armed
  mode is retained (not reachable, since an unshifted command already disarms) and one sent
  from the panel while a retained mode is up. Equivalent in play.
- **The STOP gadget's group status reset: UNCLEAR.** The chapter clears the pressed state of
  the STOP gadget when it returns to contextual mode (the order buttons are a radio group in
  which STOP is the "none pressed" member, `TOTALA-EXE-KEYBOARD.md`, Escape: "pressing the
  STOP gadget of the main panel"). RWE's order buttons are toggles that follow the cursor mode
  through `refreshToggleButtons` and `UnitGuiInfo`; whether a button is left pressed after a
  placement was not exercised. Presentation only.
- **Shift-click on a blocked-by-plan site removes the plan:** an RWE behaviour (about
  863-874) that the original's queue-or-remove control also gives for a valid site; it is the
  same rule extended to the red box that the player's own plan caused. DIVERGENCE (benign).

## 4. Right click: cancellation, and the contextual variants by interface type

**Reference (section 3).** `HandleRightClick` (`0x499100`) **first cancels any non-contextual
intent**: restore mode 1, clear the retained bit and reset the STOP group state. **It does not
send a Stop order.** In contextual mode with **interface type 0**: input bit 2 together with
key flag 8 **starts mouse scrolling**; without that modifier the same bit **clears the
selection and pops the layout**; input bit 1 starts radar dragging, sets `0x10` and switches to
cursor 19. For **other interface types**, input bit 4 instead issues the order with mode 1 and
the current position, with a default-constructed MissionType whose index is zero (the same
contextual dispatch as the left click). The right click is therefore not one unconditional
move or attack.

**RWE.**

- **Cancel without Stop: MATCHES.** Every armed mode, Build included, goes back to Normal on
  a right press and sends nothing (`GameScene_input.cpp`, about 911-943).
- **Left Click scheme over the world: MATCHES the clear half, MISSING the scroll half.** With
  `leftClickMode()` set (the register's Interface Type 0, the shipped default), a right click
  over the world clears the selection (about 945-955). There is **no right-button drag
  scroll** at all; the pan is on the middle button (`BT-INGAME-CONTROL-PANEL-PARITY.md` rule
  13). The register says Interface Type 0 simply "starts the screen drag-scroll"
  (`TOTALA-EXE-INTERFACE.md` lines 1209 and 1544), without a modifier and without the clear.
  **CONFLICT** between the register and this chapter on whether the scroll needs a key flag (the
  chapter's key flag 8 is not identified; the register's `0xF9` is Shift, a different value),
  and on what the unmodified click does. RWE implements the chapter's unmodified half. Which is
  right decides whether a plain right click deselects in the original's default scheme.
- **Radar: MATCHES.** A right press over the minimap in the Left Click scheme starts the
  minimap drag (about 947-950). **DIFFERS in a detail:** RWE ends the drag on **either**
  button's release (`onMouseUp`, the Left and Right branches both clear
  `DraggingMinimapState`, about 1080 and 1097); the chapter ends it on the matching button
  only (right-up for type 0, left-up for others). Not observable in normal use.
- **Right Click scheme: MATCHES.** `issueDefaultAction(..., RightClickDefault)` for every
  selected unit (about 956-970), the longer ladder of `TOTALA-EXE-INTERFACE.md` section 103
  (0x4991D5). The mission-type index of zero is the register's "explicit zero type index".
- **Clearing pops the layout:** RWE rebuilds the base panel through `onSelectedUnitsChanged`
  (`MAIN2`), the same visible result.

## 5. Left click outside placement

**Reference (section 2).** Cursor mode 15 delegates to the hover-unit click (select it).
Cursor mode 17 and above issue no action; **interface type 1 in contextual mode additionally
clears the selection and pops to the base layout**. Any other mode calls the order issuer with
the current intent, the current world point and an explicit zero type index, so normal
contextual dispatch happens. The ordinary command path restores mode 1 as in rule 3.

**RWE. MATCHES the register's dispatcher** (`TOTALA-EXE-INTERFACE.md` section 103, "The cursor
is the decision"; the register has the same three arms and the "if not shift, unarm and
un-press" tail).

- **Armed modes act on the press; the default ladder acts on the release.** In RWE an armed
  Attack, Move, Guard and so on issues from `onMouseDown` (about 591-885), and an ordinary
  click is resolved in `onMouseUp` after the drag test (about 1025-1089). That is the register's
  split: the down-event with a command armed (`0x4993B6`), the ended short drag otherwise
  (`0x4995AE`). MATCHES.
- **Interface Type 1 clears the selection on an empty click: MATCHES, with a Shift exemption.**
  `onMouseUp` clears the selection on a short click on nothing in the Right Click scheme, but
  skips the clear when Shift is down (about 1065-1071). The original's arm has no shift test
  that either source mentions, so with Shift held it clears. Benign, kept as a convenience.
- **Armed-mode eligibility per unit** (`isFriendly`, `isEnemy`, the Capture arm's `isEnemy`
  test) is `BT-72` (resolved: an ally may be captured from the armed button; RWE's capture arm
  at `GameScene_input.cpp` about 710 requires an enemy, so RWE **refuses** the ally the
  original allows). That row's resolution says "RWE matches", which was written for the
  default-action path (`GameScene_input.cpp:699` at the time); the armed Capture cursor reads
  `isEnemy(*hoveredUnit)` today. **DIFFERS, small.** Worth one line on the BT-72 row.
- **No-action for cursor id 17 and above:** RWE's armed modes act only when the hover target
  qualifies and otherwise do nothing, which is the same outcome for the same cases.

## 6. Frame routing priority

**Reference (section 4).** `BattleFrame` (`0x499200`): recompute placement validity when input
bit 2 and mode 14 are both active; with **neither bit 2 nor bit 1** set the neutral cursor 19;
otherwise pick the hover unit and resolve a cursor, then **dispatch one deferred panel-page
request**. The retained-key test follows (rule 3). The remaining input path, in priority order:
radar drag (right-up ends it for type 0, left-up for others, otherwise the camera centres on
the radar position); an active mouse scroll; right-down runs the right-click handler; a
non-contextual intent takes left-down through the left-click handler; an active selection drag
handles left-up or updates its end; and an unhandled left-down starts a selection drag, starts a
radar drag for interface type 1, or issues the contextual left-click action for the other
type. A drag starts only on input bit 2, records a timestamp and all three whole world
coordinates as start and end, sets bit 8, and uses cursor 19. After the input path **the world
tick runs regardless**.

**RWE.**

- **The ordering is not a port; the outcomes agree.** RWE receives discrete SDL events
  (`onMouseDown`, `onMouseUp`, `onMouseMove`) and a once-a-frame `update`; there is no single
  routing function. The cases:
  - Minimap versus world: a press over the minimap in the Left Click scheme issues the default
    action, in the other starts a minimap drag; over the world it starts the selecting state
    (`onMouseDown`, `NormalCursorMode` branch, about 885-905). That is the chapter's "start a
    radar drag for interface type 1, or issue the contextual action for the other type",
    with the types named by RWE's own flag. MATCHES.
  - Radar centring: while dragging, `update` relocates the camera to the minimap point each
    frame (`GameScene.cpp`, about 692-702). MATCHES.
  - Selection drag records its start in the camera plane, not three world coordinates
    (`SelectingState`, start time and `worldViewportToCameraPlane`). A selection drag is a
    screen rectangle in RWE, and a world-coordinate pair in the original; see rule 7.
- **The deferred panel-page request:** panel swaps are deferred through `setNextPanel` and
  applied by the frame (`BT-INGAME-CONTROL-PANEL-PARITY.md` rule 7). Equivalent.
- **The tick always follows input:** RWE's tick scheduling is independent of input. Equivalent.

## 7. The click-or-drag threshold of the selection box

**Reference (section 4).** On release the drag is a **click** if it lasted at most **25 clock
units** and the horizontal extents are **both less than 32 world units** (strict comparisons on
X and Z); anything else calls `SelectUnitsInBox`, and **a zero result clears the selection and
pops the layout**. The chapter calls the clock **wall-clock `GetTicks`** and the thresholds
"strict size and time comparisons, not a screen-pixel-only rectangle threshold".

**RWE.** `onMouseUp` (`GameScene_input.cpp`, about 1032-1076).

- **Time: DIFFERS from both sources.** RWE compares **simulation ticks**
  (`sceneTime - state.startTime < SceneTime(30)`): the limit is **30**, not 25; the unit is the
  tick counter, which stops while the game is paused and runs at the chosen game speed.
  The register reads the same test as "under 32 pixels in under `0x19` ticks"
  (`TOTALA-EXE-INTERFACE.md`, "The cursor is the decision"), so the **register and the
  chapter agree on 25 and disagree on whether the unit is "ticks" or "wall-clock"**, and RWE
  agrees with neither on the number. What `GetTicks` returns is not given: if it is the
  Windows millisecond clock, 25 would make every real click a box drag, which cannot be the
  behaviour, so it is almost certainly TA's own tick counter at 30 a second (25 is 0.83 s).
  **UNCLEAR** until read. RWE's 30 ticks is one second at the normal speed and 3 seconds at a
  tenth of it.
- **Distance: DIFFERS in unit.** RWE tests `maxSingleDimensionDistance < 32` between the start
  and end in **world-viewport pixels** (the start is held in the camera plane and projected
  back, `cameraPlaneToWorldViewport`). The original compares **world units on X and Z**.
  At RWE's default zoom of one the two coincide on both axes; at any other zoom or UI scale
  the pixel test is a different physical distance. Both are strict on both axes, so the shape
  agrees.
- **A zero result clears the selection: MATCHES.** `selectUnitsInBandbox` ends in
  `replaceUnitSelection(units)` with an empty set, which clears (about 2735-2742); the panel
  falls to `MAIN2`. Shift toggles instead (RWE's addition; the chapter does not mention a
  modifier for the box).
- **The box is a screen rectangle of projected unit positions** in RWE
  (`selectUnitsInBandbox`: a unit is in if its viewport position is inside), where the original's
  start and end are three world coordinates and the selection routine tests in the world.
  The register has the box routine's address (`0x48C390`) and nothing about what it tests.
  **UNCLEAR**; for a 2D top-down view the two coincide and RWE's camera is tilted, so a tall
  model's origin versus its drawn body can fall on different sides of the box edge.

## 8. The post-tick validity check of the displayed focus

**Reference (section 4).** After the input path the tick runs; then the displayed focus id is
checked and **a slot whose definition id became zero causes the panel layout to pop**. This
ordering lets the simulation destroy a focused unit before the check.

**RWE. MATCHES in effect, DIFFERS in mechanism.** The unit-died event removes the unit from the
selection and calls `onSelectedUnitsChanged` (`GameScene_commands.cpp`, about 2863-2910),
which falls back to `MAIN2` for an empty selection. There is no per-frame test of a displayed
focus because there is no displayed-focus variable separate from the selection. A unit
captured away from the local player (the other way a focus can stop being ours) was not
traced here; the owner change is `BT-73`'s territory.

## 9. The end-of-battle transition

**Reference (section 5).** After the tick, end-game bits 2 or 4 **request exit**. Non-type-3
missions proceed at once; **type 3 (multiplayer) requires the pending-view-state broadcast to
return nonzero** before teardown, a network gate. The transition sets the neutral cursor, pops
and closes screens and, for type 3, resets the chat HUD indices and runs **one extra tick**. It
then shuts down the in-game systems, clears the key queue, selects sound-track category 4, sets
frontend state 7 and installs the end-game frame, with the leave-network-game callback as the
close handler and the end-game screen state reset to zero. The end-game frame is only a
trampoline to the end-game state; the victory condition and the scores are evaluated elsewhere.

**RWE.** Largely **covered**: the countdown and latch are BT-82 (resolved: set to 4 on the
first observation, five more 30-tick observations, latch at 150 ticks), the continue-watching
branch BT-89, the chart and its bars `TOTALA-EXE-INTERFACE.md` section 104 and `GameScene_endgame.cpp`.

- **The shape: DIVERGENCE (structural), documented.** RWE does not tear down and install a
  frame; the scene stays and passes through phases (`EndGamePhase`, `gameOver`; `Glamour`, the
  chart, the optional campaign panel), and leaves for the main menu through
  `returnToMainMenu` / `leaveFor` (`GameScene_menu.cpp`, about 839-860 and 1380). `onMouseDown`
  returns early for the whole of that state (`GameScene_input.cpp`, about 497-550) so the world
  stops taking orders, which is what the original's frame swap achieves.
- **The multiplayer gate: not applicable to lockstep.** The type-3 wait for the pending view
  state and the one extra tick exist because TA's multiplayer is owner-authoritative; RWE's
  lockstep ends on the same tick on every peer by construction (`CLAUDE.md`, Determinism).
  The own-clock mode (`NetMode::OwnClock`, ADR-0003) has its own end path. Not ported, by
  design.
- **Sound-track category 4 and the key-queue clear:** RWE selects the end-game music in its own
  music path and has no key queue to clear. Presentation.

## 10. The restart transition

**Reference (section 6).** The restart request is tested **after** the end-game block and has
**no else-exclusion against it**: if both are set, the restart branch can run later state and
handler changes in the same invocation. The body does not clear the restart request; the
surrounding lifecycle does. **Game type 1 (campaign):** shut down, close screens, blank the
display, remember the mission index and campaign name, reload the campaign and reselect the
mission; a successful selection sets network-game bits 3 and 2; frontend state 2, the menu
frame and the leave callback are installed. **Other game types** keep the player count across
the shutdown, close and blank, set cursor 20, restore the count and load the mission named in
the options at `+0x11C`, apply slot data to the game players, set network-game bit 2 and
install the same state and handler. Both select sound-track category 4. **Load results in the
non-campaign branch are not checked through a return status.**

**RWE.**

- **Skirmish and single mission: MATCHES in effect.** `restartGame`
  (`GameScene_menu.cpp`, about 374-385) builds a fresh `LoadingScene` over the game's own
  `gameParameters` and leaves for it, reloading the same map, players and options; the
  menu is RESTART.GUI (`openRestartMenu`), the register's section 63. The starting commanders
  are spawned afresh by the load.
- **Campaign restart: UNCLEAR.** The chapter reselects the campaign mission by index and name.
  RWE's campaign flow (`GameScene_menu.cpp`, `continueCampaign`, about 1351-1380, and
  `CampaignProgress`) was not traced for whether a restart from the in-game menu preserves the
  campaign context (the mission's position in the chain and the progress record) or comes back
  as a plain skirmish load. The in-game restart dialog also offers a difficulty adjustment in
  the original (`TOTALA-EXE-INTERFACE.md` section 63: "Adjust Difficulty"); whether RWE's
  restart applies a changed difficulty was not checked.
- **Both requests at once:** no counterpart; RWE has a single scene transition at a time
  (`leaveFor`). The chapter's overlap is an artefact of a single frame function testing two
  flags in sequence. Not ported.
- **Unchecked load result:** RWE's loading scene reports a failure to the player; the
  original's non-campaign branch does not. A divergence in the safe direction.
- **Multiplayer:** the register disables RESTART in multiplayer (section 63: "in multiplayer
  it stays blank and dead"). RWE shows it from `gameMenu` for single-player scenes; the
  multiplayer and own-clock cases were not traced here.

## 11. Ownership and limits

**Reference (section 7).** The module owns no serializer, no preview model renderer, no unit
allocator and no resource debit. Placement commands flow to selection dispatch and the
construction handlers; terrain, range and resource state can change between preview, click and
work. GUI layout, world cursor picking, radar position conversion, simulation scheduling and
shutdown are separate dependencies.

**RWE. MATCHES.** The same ownership: input in `GameScene_input.cpp`, the order in
`localPlayerIssueUnitOrder`, the site test in the simulation, the resource debit in the
economy settle. A placement click has no save record (a pending plan lives in the order list and
in `localBuildGhosts`, the latter presentation only).

---

## 12. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | An armed Build cursor reaches only a **single** selected unit; the original sends every selected builder, and RWE says nothing when the selection is not exactly one. Rule 2 | `src/rwe/game/GameScene_input.cpp` (`BuildCursorMode` arm), `GameScene_commands.cpp` (`onSelectedUnitsChanged`) | Partly. The fan-out is stated; what the original does to the armed mode when the selection changes is not. One play: arm a build, then recall a two-constructor group |
| 2 | Right button in the Left Click scheme: does a plain click clear the selection (the chapter) or start a drag-scroll (the register)? RWE clears and has no right-drag. Rule 4 | `GameScene_input.cpp` (`onMouseDown`, `onMouseMove`) | **Yes.** One play in the shipped default scheme; also identifies the modifier |
| 3 | Click-or-drag: RWE uses 30 simulation ticks and 32 viewport pixels; both sources say 25 and world units, and disagree on whether the clock is ticks or wall time. Rule 7 | `GameScene_input.cpp` (`onMouseUp`), `GameScene.h` (`SelectingState`) | Spec-room read of `GetTicks`, then no play needed |
| 4 | An armed Capture cursor refuses an ally that the resolved row BT-72 says the original accepts. Rule 5 | `GameScene_input.cpp` (`CaptureCursorMode`, the `isEnemy` test), `DefaultAction.cpp` | No: BT-72 is resolved; confirm the armed button against the default ladder |
| 5 | The box select tests projected unit origins on screen; the original's box is in world coordinates. Rule 7 | `GameScene_commands.cpp` (`selectUnitsInBandbox`) | Spec-room read of the box routine `0x48C390` |
| 6 | Campaign restart and the restart difficulty option: not traced. Rule 10 | `GameScene_menu.cpp` (`restartGame`, `openRestartMenu`), `LoadingScene` | Yes, in a campaign |
| 7 | The order's y is a cursor terrain height; the original uses the build-site height (BT-51). Rule 2 | `GameScene_input.cpp` (the `BuildCursorMode` arm), `UnitBehaviorService.cpp` | Trace first whether the sim reads the order's y at all |
| 8 | Radar drag ends on either button; STOP group reset after placement. Rules 3 and 4 | `GameScene_input.cpp` | No. Presentation only |

### Documented divergences this audit leaves alone

The cursor-mode variant in place of the input-flag byte; the discrete-event handlers in place
of one routing function; the scene that stays up through the end-of-game phases rather than
installing frames; the lockstep end-of-battle that needs no broadcast gate; Shift toggling the
box selection; Shift keeping the selection on an empty click; the player's own plan being
removable from a red box.

### Conflicts with existing RWE findings, in one place

1. **Right button in Interface Type 0.** Register (`TOTALA-EXE-INTERFACE.md` lines 1209 and
   1544): the right button starts the screen drag-scroll. Reference: the drag needs a key flag
   (8, unidentified); without it the click clears the selection and pops the layout. RWE
   follows the reference's unmodified half and has no right-drag. Open.
2. **The click-or-drag clock.** Register: "under `0x19` ticks". Reference: "25 clock units of
   wall-clock `GetTicks`". RWE: 30 simulation ticks. The two sources give the same number and
   a different unit, and RWE gives a different number. Open until the clock is read.
3. **Which unit the placement mode keys on.** The reference's panel test is the product's
   building code; RWE's is the builder's mobility. No register finding states either
   (`TOTALA-EXE-DATA.md` gives only the field's meaning). Design; see the panel audit rule 6.
4. **BT-72's "RWE matches".** True of the default-action path; the armed Capture cursor
   refuses an ally. A stale clause in the spec-room row rather than a reference conflict.
