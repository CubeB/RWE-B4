# In-game control panel, build menus and camera parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for what the
original's order panel, build menus, construction-progress helpers and camera services
do, per `AGENTS.md` and ADR-0004: which button is lit and why, what a click on a panel
gadget does, how pages are stepped, the arithmetic of the build, decay and repair ticks,
and how the camera follows, shakes and scrolls. What the click on the map does, placement
mode and the frame's input routing are the companion `BT-INGAME-BUILD-PLACEMENT-PARITY.md`;
the order handlers a button finally arms are `BT-ORDERS-UNIT-ORDERS-PARITY.md` and
`BT-ORDERS-ORDER-DISPATCH-PARITY.md`. This audit restates none of them.

The register already holds most of this module's subject, and a large part of the chapter
is corroboration. Where it is, the rule says so in one line and names the section
(`TOTALA-EXE-INTERFACE.md` sections 19, 99, 103, 107, 133, 136 and 137;
`TOTALA-EXE-ECONOMY.md` sections 23, 29, 93, 94 and 134; `TOTALA-EXE-KEYBOARD.md`
sections 72 to 81; `TOTALA-EXE-WEAPONS.md` on the shake). What is new is the page-stepping
rules, the panel-opening policy, a handful of presentation choices where RWE departs
from a decoded routine without saying so, and the camera services, of which the register
holds only the follow and the shake.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-control-panel.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its
  source links were followed. Addresses are the ones the chapter names; rules are restated
  in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that rows
  already resolved there are not raised again; the rows this audit touches are BT-51,
  BT-55, BT-62, BT-66 and BT-70.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was run.
  The Gaps table says which items a play-test or a `ui_probe` render settles.
- **The RWE half** is a read-only audit of `src/rwe/game`, `src/rwe/sim` and `src/rwe/ui`
  at the working tree of 2026-10-16. Nothing was built, run or changed, and tests were not
  run; a MATCHES below means the code reads as the rule says. Another agent had uncommitted
  edits elsewhere in the tree while this was written, so every line number below is "about".
- **No census of the shipped FBIs or GUI files was taken for this audit.** Where a count
  would matter the rule says "not counted".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what is covered elsewhere
2. Command routing and the order-button click
3. Standing settings, on/off and cloak
4. Selection aggregation and the LOAD, UNLOAD and BLAST slot
5. Which panel a selection opens, and what it remembers
6. Build-panel clicks, counts and queue captions
7. Page stepping and the deferred page request
8. Menu fill, unavailable entries and PREV and NEXT
9. Construction progress, health and the unfinished-build refund
10. Unfinished-build decay and completion
11. The repair arithmetic
12. Camera: follow, snap and shake
13. Camera: drag, edge and keyboard scrolling
14. Persistence and coverage boundaries
15. Gaps to fix

---

## 1. Shape and what is covered elsewhere

**Reference (section 1, and the header).** The chapter's functions run from `0x419560` to
before `0x41D0F0` and are three things at once: the order panel and build menus, the
construction and repair arithmetic every builder shares, and the camera. Controls are routed
by **substring matching of the gadget's name**, not by a typed command id, so behaviour
depends on GUI naming conventions. Mode numbers armed by the buttons are the ones in rule 2.

**RWE. DIVERGENCE (structural), documented.** The panel is a `UiPanel` built from the
shipped `*GEN.GUI` and per-builder pages by `UiFactory`; clicks arrive as group messages and
`GameScene::onMessage` (`GameScene_commands.cpp`, about 2455-2680) matches the control name
against the side prefix plus a suffix by equality, not by substring. The register already
records the trap in the original (`TOTALA-EXE-INTERFACE.md` section 19, "`0x49FE60` finds a
control by `strstr`") and the equality match is stricter in the safe direction: it cannot
confuse LOAD and UNLOAD, and it can differ from the original only for a mod whose gadget
names depend on substring luck. The chapter's InitCommands (counters for message types 1 to
44, console and cheat tables, the default command handler) has no panel content; the
counters are the network overlay (`LockstepStats`), not the panel. Nothing to port.

## 2. Command routing and the order-button click

**Reference (section 3).** Nonzero button status arms a numbered mode: Move 2, Attack 3,
Blast 4, Unload 5, Load 6, Defend 7, Repair 8, Patrol 9, Reclaim 12, Capture 13. Zero status
restores the contextual mode 1. Stop restores mode 1 **and sends Stop to the selection at
once**. UNLOAD is tested before LOAD so the shorter name cannot win. **Move, Stop, Attack,
Blast, Defend, Patrol and Load play the `immediateorders` sound; Repair, Reclaim, Capture
and Unload play `specialorders`.** An unknown name does nothing.

**RWE.**

- **Modes and toggling: MATCHES.** Each button arms its cursor mode and a second press of the
  same button disarms it (`onMessage`, `cursorMode.next(NormalCursorMode())` when the mode is
  already that one). Stop disarms and sends Stop to every selected unit
  (`localPlayerStopUnit`). The Move button arms the full default ladder, the register's
  command 2 (`TOTALA-EXE-INTERFACE.md` section 103).
- **Sound classes: DIFFERS.** RWE plays `specialOrders` for Attack, Blast, Move, Defend,
  Patrol, Load, Reclaim, Repair, Capture and Unload, and `immediateOrders` for Stop, On/Off,
  Cloak and RWE's own Repeat toggle (`onMessage`, about 2460-2680, each branch's
  `sounds.specialOrders` or `sounds.immediateOrders`). The chapter puts **six** of the ten
  armed buttons (Attack, Blast, Move, Defend, Patrol, Load) in the immediate class. The
  register has no finding on which sound goes with which button (`grep` of the docs for either
  sound name finds nothing outside this audit). Presentation only, not hashed; a
  change is six lines. Which sound each class holds is `SOUND.TDF` data and was not read.
- **Stop with an empty selection:** the chapter's Stop restores mode 1 unconditionally; RWE
  disarms inside its per-unit loop. An armed mode cannot outlive the selection
  (`onSelectedUnitsChanged` disarms on empty), so the two agree.
- **The unchecked local pointer** in the chapter (a recognised branch reads a gadget pointer
  initialised only for an entry of type 1) is an interface precondition in the original and
  is not ported; RWE's equality match has no such hole.

## 3. Standing settings, on/off and cloak

**Reference (section 3).** The orders-panel click cycles Move and Fire orders 0 to 1 to 2 to
0, and a mixed state 3 resets to 0. On/off 0 activates, 1 deactivates, mixed 2 activates.
Cloak toggles on whether the aggregate field is nonzero. Each dispatches an order through the
normal issuer rather than writing to every unit. The chapter notes the naming disagreement of
the two standing-field pairs elsewhere; the panel establishes the labels and the cycle.

**RWE. MATCHES the register on every point.** `TOTALA-EXE-INTERFACE.md` section 19, "What a
click on a toggle does", decoded the same four handlers (`0x41A4F0`, `0x41A5EF`, `0x41A7DB`,
`0x41A743`), including that mixed goes to hold position or hold fire and that cloak cannot
tell "all on" from "mixed". RWE's `fireOrdersAfterClick`, `moveOrdersAfterClick`,
`onOffAfterClick` and `cloakAfterClick` (`OrderButtons.cpp`, about 160-195) carry it, issued
once to every unit that offers the button; the on/off and cloak loops filter on `onOffable`
and `cloakable` in the panel because the simulation does not (the class finding is
`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 10 and `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 2).
The faces for the disagreement state are in the register (the fourth frame of the fire and
move art, the third of on/off and cloak) and `toggleFace` draws them. Nothing new.

## 4. Selection aggregation and the LOAD, UNLOAD and BLAST slot

**Reference (sections 4).** The refresh scans the selected live local units, or uses an
existing focus unit alone. Fire and move modes start at sentinel 4 ("unsupported") and
differing supported values become mixed 3; on/off starts at 3 and mixed is 2; **cloak adopts
the first supported request and then reports 2 for every further cloak-capable unit without
comparing**, so a multi-cloaker selection can show mixed when all agree. The capability flags
(move, stop, attack, guard, patrol, load, repair, reclaim, capture, blast) accumulate with an
**OR**, so a lit button does not mean every unit can execute it. The repair flag is flags2
bit 9, a copy of the reclaim flag made by the loader. With no load capability the layout
**hides LOAD, greys UNLOAD and greys BLAST if unavailable**; with load capability it **hides
BLAST instead**.

**RWE.**

- **The OR, the sentinels, the repair copy and the cloak accumulator: MATCHES / DIVERGENCE
  (documented).** Register `TOTALA-EXE-INTERFACE.md` section 19 ("A mixed selection: ANY, not
  ALL", ten bits at `0x41B49F` to `0x41B524`, the cloak bug at `0x41B485` "deliberately not
  ported"). `unitOffersOrderButton` and `selectionOffersOrderButton` (`OrderButtons.cpp`,
  about 90-150) implement it, with repair keyed on `canReclamate`. RWE does not copy the cloak
  disagreement bug, and says so.
- **BLAST with load capability: MATCHES.** `selectionOffersOrderButton` withdraws BLAST when
  any selected unit can load, and a test pins it (`OrderButtons.test.cpp`, about 164-178).
- **BLAST with no load capability and no D-gun: DIFFERS.** The chapter and the register
  (`0x41A409`, "BLAST is greyed unless `candgun`") agree that in the no-load case the shared
  slot holds a **greyed BLAST**. RWE's `applyOrderButtonGating` (`GameScene_commands.cpp`,
  about 983-1042) removes the child outright for both LOAD and BLAST whenever the selection
  does not offer them, so a selection with neither capability (the usual one: a squad of
  Peewees, a solar collector) shows an **empty slot** where the original shows a dimmed BLAST
  face. The 2026-09-02 port note in the register ("hides only LOAD and BLAST, which is exactly
  the split above") is true of LOAD and of BLAST-when-loading, and not of BLAST-when-nothing-
  loads. Presentation only. **Needs an original look** (a `ui_probe --panel` render or one
  frame of the original with a Peewee selected) because the register's own words are the only
  evidence that the dimmed face really appears.
- **UNLOAD greyed in the no-load case: MATCHES** (it is greyed through the ordinary path).
- **"The focused-ID branch does not rebuild the aggregate":** an original quirk (prior panel
  state and refresh order matter). RWE derives every gather from the live selection each time
  (`gatherCloak` and siblings), so there is nothing stale to match. Not ported, by design.

## 5. Which panel a selection opens, and what it remembers

**Reference (section 4).** No selection closes back to the base layout. One selected unit
with build pages **can open its unit-specific page when the stored page is positive or
definition bit 31 requests that path**; otherwise a side-specific generic panel opens, with or
without a unit focus. The stored page lives in the unit's packed flags (a three-bit page and a
build-page bit), and the chapter says those flags are retained through unit serialization.
`GetBuildMenuFocusUnit` returns a nonzero-ID live slot or nothing; marking the orders dirty
requests a refresh only for a selected local unit.

**RWE.**

- **No selection: MATCHES.** `onSelectedUnitsChanged` (`GameScene_commands.cpp`, about
  2863-2910) loads the side's `MAIN2` base panel.
- **One builder: DIFFERS in policy, equal in what a player sees on the commonly selected
  units.** RWE keeps a `UnitGuiInfo` (section and current page) per unit
  (`getGuiInfo`, about 2913-2935), defaulting a builder to the **Build** section on first
  selection and any other unit to Orders, and rebuilds the panel from it. The original's rule
  is "stored page positive or bit 31". What definition field bit 31 of the flag word is, and
  what page a freshly created builder is stamped with, is not stated by the chapter and is
  not in the register's table of the flag word (`TOTALA-EXE-INTERFACE.md` section 19 stops
  at the three-bit countdown, bits 20 to 22, and `REVERSE-ENGINEERING-PRIORITIES.md` lists to
  bit 30). **UNCLEAR.** The cases that could differ are a factory or constructor whose page
  was never set and a non-builder that has pages (an immobile launcher: the stockpile page is
  reached through the unit's own GUI, `TOTALA-EXE-ECONOMY.md` section 29).
- **Where the state lives: DIVERGENCE (architectural), not documented.** The original keeps
  the page and section in the unit's own flags, so a save carries them; RWE keeps them in
  `GameScene::unitGuiInfos`, which is scene-local, is never written by `save_util`, and is
  cleared on a replay seek (`GameScene_replay.cpp:348`). A loaded game therefore reopens every
  builder on its default page. Not hashed (it is not simulation state), so it is no desync
  hazard; a player-visible nuisance only.
- **Focus pop on a dead unit: MATCHES in effect.** The unit-died event calls
  `onSelectedUnitsChanged` and the panel is rebuilt; the code guards the case of several
  self-destructs in one tick (`tryGetUnit`). The chapter's "post-tick validity check of the
  displayed focus" is therefore a per-event refresh here, not a per-frame test.
- **An unfinished focus unit** (`OpenBuildMenuGui` refuses it): see rule 8.

## 6. Build-panel clicks, counts and queue captions

**Reference (section 5).** A product is resolved from the gadget name truncated to **16
characters**. PREV, NEXT, ORDERS and BUILD set deferred flags (rule 7). A resolved product
whose definition field `+0x22F` (the register's `bmcode`, 1 = mobile, 0 = building,
`TOTALA-EXE-DATA.md` line 654) is **zero** selects placement mode 14. Other entries try the
standing-setting handlers, then the order-button handlers, then **production count
adjustment for a selected focus unit**; **Shift (key `0xF9`) makes the step five instead of
one**, a left click adds and any other click subtracts. After a change the counts refresh
for a factory (runtime bit 29) or a stockpile weapon (flag `0x10000000`). Count captions:
`SetBuildCountText` shows `+count` or blank; attribute 4 means a named product whose queued
count is summed; attribute 8 means primary-slot stockpile followed by queued ID-zero count.
A `MAKENUKE` or `MAKEANTI` name adjusts the weapon count for ID zero; other names resolve a
product (a missing id is ignored). **Presence of the producer's motion pointer** selects
mobile versus building count adjustment in that direct helper.

**RWE.**

- **Shift-for-five and left-adds, other-subtracts: MATCHES** (`onMessage`, about 2640-2672:
  `(isShiftDown() ? 5 : 1) * (Primary ? 1 : -1)`; the register says "x5 factory clicks: works",
  `TOTALA-EXE-KEYBOARD.md` section 80; queue merge and remove semantics are
  `TOTALA-EXE-INTERFACE.md` section 137).
- **Captions: MATCHES.** The `+N` badge and the stockpile caption `N +M` (a space and a
  plus; `stockpileButtonLabel`, `OrderButtons.cpp` about 70-87) are the register's
  (`TOTALA-EXE-ECONOMY.md` section 29; ROADMAP: "rebuilds the caption ... counting the
  outstanding orders on demand").
- **The placement-mode test: DIFFERS in form.** RWE enters placement mode when the **selected
  builder** is mobile (`unitDefinition.isMobile` in `onMessage`, about 2660) and queues
  otherwise. The chapter keys the choice on the **product's** `bmcode`. The two agree whenever
  a mobile builder makes only buildings and an immobile one only mobiles, which is the whole
  retail catalogue as far as is known (not counted); they differ for a mod with a mobile
  builder that makes a mobile unit (the original would queue it, RWE would ask for a site)
  or an immobile builder that makes a building (the original would ask for a site, RWE would
  queue it). The chapter's second discriminator, "motion pointer of the producer", applies
  only to the direct queue helper (`QueueBuildOrder` at `0x419B00`), which RWE's
  `localPlayerModifyBuildQueue` also keys on the builder; so RWE has the helper's rule and
  not the panel's. Hash-visible only through which command a click produces.
- **Name length:** the 16-character truncation is the original's buffer size; RWE compares
  full names, so a unit name longer than 16 characters is possible in a mod and works here.
  Not ported, harmless.
- **Selected focus unit only:** both act on one unit. RWE requires `getSingleSelectedUnit`.
  MATCHES.

## 7. Page stepping and the deferred page request

**Reference (section 6).** `GetBuildMenuPage` returns the stored page only when build-page
bit 22 is set, else zero; pages are a **three-bit** packed field. **Forward step**
(`0x41BDE0`) has a *toggle* mode and a *non-toggle* mode. Toggle, from closed: opens
**page 1**; at the last page: **closes**; otherwise increments. Non-toggle: wraps last to 1
and **keeps the build menu open**. **Backward step** (`0x41BF10`) is the reverse: toggle
opening backward chooses `pageCount - 1`, toggling backward from the first page closes,
non-toggle wraps to the last. Both mark the panel dirty for a valid focus and **play
`nextbuildmenu` even with no focus**. `OpenBuildMenuPage` (`0x41C060`) accepts a page strictly
below the definition's page count, turns the build-page bit off for zero and on otherwise,
stores a nonzero page, marks dirty and plays the sound, with **no explicit negative-page
rejection**. The deferred dispatcher (`0x41C180`) handles **at most one request per call**,
priority next, previous, build, orders; it clears the consumed request and, for build and
orders, changes bit 22 on a live focus.

The register already has the keys: `,` calls `0x41BF10` with argument 1 and `.` calls
`0x41BDE0` with argument 1, both with the `nextbuildmenu` sound
(`TOTALA-EXE-KEYBOARD.md`, the dispatch table). **Neither source says whether argument 1 is
the toggle flag.** The digits `1` to `9` select a page by default and Alt+digit selects a
squad (`TOTALA-EXE-KEYBOARD.md` section 75), which is `OpenBuildMenuPage`'s caller.

**RWE.**

- **Wrap versus toggle: UNCLEAR, and the behaviour a player sees depends on it.** RWE's
  NEXT, PREV, `.` and `,` always **wrap** (`GameScene_commands.cpp`, about 2549-2599:
  `(page + 1) % pages`; `GameScene_input.cpp`, about 175-198, with `pages - 1` as the
  backward step). If the keys and buttons pass the toggle flag, then in the original `.` on
  the last page **closes the build menu to the orders panel** and `.` from the orders panel
  **opens page 1**, where RWE goes to page `current + 1` of the build section. If they pass
  the non-toggle flag RWE matches. No source here says which. A spec-room read of the three
  callers' pushed arguments settles it (a button click, the two keys).
- **Sound with no focus:** the original plays `nextbuildmenu` even when no unit is focused;
  RWE plays it only inside the single-selection branch. Presentation only.
- **Page-field width:** three bits give pages 0 to 7 (seven build pages). RWE has no limit.
  No shipped builder has more than four pages (the commander's; the register's table of GUI
  pages), so this only matters to a mod.
- **Orders and Build tabs: MATCHES** in effect (`BUILD` and `ORDERS` rebuild the panel
  from the stored section, `onMessage` about 2600-2635, with the `#342` pressed-state fix).
- **One request per call and the priority order:** RWE has no deferred flag word; a click
  becomes one group message, handled in arrival order, and the panel swap itself is deferred
  (`setNextPanel`, applied by the frame). Two simultaneous requests cannot be made through
  the interface, so the priority order cannot be observed. Equivalent.
- **`OpenBuildMenuPage` with a negative or too-large page:** RWE's index arithmetic is
  modular, so it cannot go out of range; the digit keys are not ported (KEYBOARD section 80
  lists Alt+digit squads and digit paging as a gap, and ROADMAP records the decision that
  digits stay control groups, which is the original's non-default `SwitchAlt` arrangement).

## 8. Menu fill, unavailable entries and PREV and NEXT

**Reference (section 5).** `OpenBuildMenuGui` (`0x41ACE0`) **refuses an unfinished focus
unit**, looks for the requested GUI asset and, if the file length is zero, falls back to the
side name plus `DL`. Download entries matching the producer's definition id and the page
minus one populate the configured slots **plus four**, replacing name, attribute and
visibility. `DisableUnavailableBuildMenuEntries` (`0x41AC90`) **greys** unit-name entries
whose definition cannot be resolved. `SetPrevNextGadgetNames` (`0x41A920`) **disables the
side PREV and NEXT when the page count is below two**. `OpenGeneratorDialog` (`0x41B0F0`) loads
the local side's `GEN.GUI`, installs the same handler and records the optional unit ids; a
load failure leaves the setup incomplete with no new fallback layer.

**RWE.**

- **Download entries: MATCHES the register** (`TOTALA-EXE-INTERFACE.md` section 107,
  `DownloadMenus.cpp`): page equals menu minus one, button index into the 2x3 grid, an empty
  slot is a gadget named `IGPATCH` or one carrying attribute 32. The chapter's "slots plus
  four" is the original's offset into the gadget table, not a different layout.
- **The generator panel: MATCHES** (`createOrdersPanel` loads `<side>GEN`).
- **Unavailable entries: DIFFERS, and the register says why.** The original greys an entry
  whose definition cannot be found. A definition is missing in the original when a mission's
  unit list compacted it out (`TOTALA-EXE-DATA.md` lines 1050-1070), so the original shows
  the button **greyed in place**. RWE, since issue 381, **erases the gadget** from the
  builder's page (`GameSimulationLoader.cpp`, about 1388-1401), and the register's own
  paragraph says "their buttons come off every build menu". The chapter is the reference for
  the greyed rendering. Presentation only, but the choice leaves a hole in the grid where the
  original leaves a grey face, and it also moves nothing else (the grid is fixed). **CONFLICT
  of the register's port note with the chapter**; see the conflicts list.
- **PREV and NEXT below two pages: DIFFERS.** RWE guards only the zero-page case
  (`pages == 0`, about 2562 and 2587) and leaves the buttons live on a one-page menu, so a
  click rebuilds the same page and plays the sound. The original disables both. Shipped
  factories and most lower-tier builders have a single page (not counted), so the dead
  buttons are visible on common selections. Presentation only.
- **An unfinished focus unit refused:** RWE has no analogue. A nanoframe is not selectable
  (`isSelectableBy` requires `!isBeingBuilt`, `UnitState.cpp:618`), so the case cannot arise
  from the interface; the original reaches it because it keeps the frame as a focus candidate.
  Equivalent.

## 9. Construction progress, health and the unfinished-build refund

**Reference (section 7).** `AddBuildProgress` (`0x41BA60`) returns zero at once for a
completed target. A **nonnegative** amount sets the activity bit (`0x80` at `+0xBB`) even when
the amount is zero or the funding later fails. The remaining fraction is `previous - amount /
buildtime` clamped to 0 to 1; the **fraction actually applied** (not the request) scales the
energy and metal charges. Health changes by the difference of truncated `fraction x maxHealth`
across the step, capped at maximum. A positive step advances only if the builder's paired
request accepts both costs; remaining fraction and the visual-change bit `0x2000` update; the
helper returns one. A newly zero fraction calls `FinishConstruction`. A **negative** amount
raises the fraction, lowers health with a floor of zero, sets `0x2000` and **credits metal
into the target unit's own account**; an active computer player of type 2 gets **half at
difficulty 0, 70 percent at difficulty 1, otherwise full**, other owners full; **energy is not
refunded**. A fully unbuilt fraction of one gives the unit **30000 of kind 9 against itself**.
The return stays zero while dismantling. Build time is an **unguarded divisor**, and a
nominal small worker time (the actions pass the integer `workerTime / 30`) can request zero
work.

**RWE. MATCHES the register on every point it covers.**

- The rate, the integer `workerTime / 30`, the clamp, the truncated-and-differenced health
  and the "throttle acts on resources, not progress" are `TOTALA-EXE-ECONOMY.md` section 23
  ("Build rate, `0x41BA60`") and are ported (`UnitState::addBuildProgress`, about 167-190;
  the register's note that RWE is one tick fast where `BuildTime` is an exact multiple of the
  rate stands).
- The activity claim before the economy call is the register's "event bit 15 is set ... before
  the economy is consulted" (`TOTALA-EXE-ECONOMY.md` section 93); RWE stakes
  `nanoframeWorkedOn` in both builder paths (`UnitBehaviorService.cpp`, about 5462 and 6132),
  ahead of the progress call, so a refused builder still holds the decay off.
- The negative branch's metal credit, the 0.5 and 0.7 factors, untruncated for decay, energy
  not refunded, the silent kill with no wreck and no loss count: section 134 and the decay
  loop in `GameSimulation.cpp` (about 1440-1475, citing section 134). The factor is applied
  at the ordinary settle rather than in the helper; same total.
- The chapter's "credit lands on the target unit's account" is the register's "the frame's
  own accumulator" (`unit+0xD4`, section 134). The same thing said twice.
- **The divisor: DIFFERS, and the original is the weaker model here.** RWE's
  `addBuildProgress` and `removeBuildProgress` divide by `buildTime` and are reached only
  when a frame is being built, which already implies `buildTime > 0`
  (`isBeingBuilt` is `completed < buildTime`). But
  **`UnitState::getBuildPercentLeft` divides by `buildTime` with no guard** (`UnitState.cpp`,
  about 117-120), and it is reached from the COB `BUILD_PERCENT_LEFT` getters
  (`cob.cpp`, about 397 and 473) and from the AI's frame logging
  (`BuildManager.cpp`, about 2326 and 5896). A unit whose FBI names `BuildTime=0` (or whose
  key is missing and the default is zero; the loader's default was not traced) would trap
  on an integer divide by zero in the simulation, **on every peer at once**, the first time a
  script asks. The original has the same hole, and `docs/SECURITY-AUDIT.md` is explicit that
  a fault in content must cost that content and not every peer. This is the one finding in
  this chapter that is a crash rather than a look; see Gaps rank 2.

## 10. Unfinished-build decay and completion

**Reference (sections 7 and 8).** `ApplyUnfinishedBuildDecay` (`0x41BCD0`) hands the build
helper a negative amount of `buildtime x ticks / energyCost` against the unit itself, so the
fraction grows by `ticks / energyCost` before clamping; the divisor is the **energy cost**,
and a zero energy cost is not repaired locally. The decay is driven by the frame's own
`GetBuilt` waits, not by a scan. `FinishConstruction` (`0x41B8D0`) needs both units live
(runtime `0x10000000`) and a non-null builder-list pointer on the source definition; it resets
the target's draw frame and remaining fraction, sets `0x2000` and, for role 1 or 2 ownership,
either marks `BUILDER.GUI` dirty (runtime bit 29) or **detaches a carried non-factory
product** with flag 1. `activatewhenbuilt` (flags1 bit 18) activates selector 1. A focused
source refreshes its counts. **An `isfeature` target (flags1 bit 24) receives last damage type
7 and runtime `0x4000`.** Role 1 or 2 ownership broadcasts the builder link; a selection of
either unit marks the panel dirty. The function **does not itself copy rally orders**;
`GetBuilt` on the product does.

**RWE.**

- **Decay: MATCHES** (`TOTALA-EXE-ECONOMY.md` section 93, `updateNanoframeDecay` and
  `nanoframedecay.test.cpp`; one energy-point of build cost per tick, first loss at tick 330,
  then every 11 ticks). The zero-energy-cost case is handled (the frame goes in one step,
  `GameSimulation.cpp`, about 1431-1445), which is the original's divide-by-zero outcome made
  safe.
- **Completion and `activatewhenbuilt`: MATCHES** (`activateWhenBuilt` is applied at
  completion in both builder paths, `UnitBehaviorService.cpp`, about 5483 and 6153).
- **Rally copy at the product:** the register's section 133 and `PORTING-125-138.md`; the
  chapter agrees that completion does not copy it.
- **The `isfeature` completion mark: UNCLEAR, probably covered.** The register has the other
  half (an `IsFeature=1` unit always leaves the intact wreck, death cause 7,
  `TOTALA-EXE-WRECKS.md`). That the **completion** routine itself writes last-damage type 7 on
  such a target, and sets runtime `0x4000`, is not in the register. Runtime `0x4000` is also
  the "recently completed" bit the register has not named; the chapter does not say what
  reads it. Mod-visible only (the only shipped `IsFeature=1` units are the floating dragon's
  teeth, which are never constructed).
- **Detach of a carried non-factory product:** this is the carried-build case (a transport
  or builder carrying a frame) with no shipped example; RWE detaches only through its own
  transport paths. Not counted.

## 11. The repair arithmetic

**Reference (section 9).** `AddRepairProgress` (`0x41BD10`) returns zero at full health. The
health and energy amounts are `(max x work - 1) / buildtime + 1` in integer arithmetic, and
**each value that is at least one is set to one**: an **upper** cap, not a floor. It requests
only energy from the builder's account, then restores through the damage helper with **kind
10**. Accepted returns one. No metal, no particles here (the callers decide; air repair asks
for particles unconditionally after the call). Negative or malformed work and a zero build
time are unguarded.

**RWE. MATCHES the register, ported, with the fix recorded.** `TOTALA-EXE-ECONOMY.md` section
94, "`SELFREPAIR`, and the repair tick every repairer in the game shares", read the same
routine at the same addresses and reached the same conclusion (one hit point and one energy a
tick for any non-zero worker time; the single-resource energy request `0x401180`; refusal means
no hit points; cause 10 bypasses armour). RWE's `deployRepairArm` and
`GameSimulation::addEnergyRequest` carry it and `sim/repair.test.cpp` pins it. The chapter's
"each value at least one is set to one" and the register's "both clamps are upper bounds" are
the same sentence. Nothing new.

## 12. Camera: follow, snap and shake

**Reference (sections 10 and 11).** `ResetCameraState` clears a `0x5C`-byte range and keeps
`scrollSpeed`. `CycleCameraFollow` finds the next selected unit. `FindLocalCommander` scans the
local slots for the Commander category and **keeps the last match; no match leaves the previous
pointer**. `UpdateCameraFollow` (`0x41CA10`) prefers a pending **snap position while its
countdown decrements**, then a tracked object, then a followed unit with live bit
`0x10000000`; a dead followed unit clears the follow. It projects, centres and clamps the
target and approaches it by **half the remaining difference per axis, at most 320 a call**,
integer, so a one-pixel residual can remain. Centering on a world point subtracts half the
viewport; centering on a map position projects the vertical coordinate as **`Z - Y/2`** and
truncates to signed short. All positioning clears map flag 8. Clamps use map size minus
viewport and do not normalise a negative maximum. **Shake:** `StartScreenShake` is disabled by
UI option `0x10` and replaces amplitudes only for nonzero arguments; `AccumulateScreenShake`
zeroes the amplitudes if inactive, **averages the old and new duration and adds the
amplitudes**; `UpdateScreenShake` scales the amplitude by remaining over initial duration,
picks a signed random offset around half the span, adds it to the scroll and counts down.

**RWE.**

- **Follow: MATCHES in rule, DIVERGENCE (documented in the code) in cadence.** The
  halving and the 320 cap are in `GameScene.cpp` (about 700-760, with a comment citing the OTA
  behaviour): the per-axis decay factor is `1 - 0.5^(0.03 x ms)` and the cap is
  `9.6 x ms` pixels, so it reproduces half-per-30th-of-a-second at any frame rate. The
  original's integer half-step leaves a one-pixel residual; RWE's float does not. Not
  visible. The register has not recorded this (the cap is not in `TOTALA-EXE*.md`); the
  chapter and the code agree.
- **Snap countdown: MISSING.** The chapter's snap position with a decrementing countdown has
  no counterpart. The F5 to F8 bookmarks set the camera directly
  (`GameScene_input.cpp`, about 198-212, "recalling one leaves the zoom alone") and the 'n'
  and F3 jumps are the only other relocations. What sets a snap in the original is not in
  this chapter (the setter is elsewhere). Presentation only.
- **Tracking a dead unit clears the follow: MATCHES** (`GameScene.cpp:711`, the unit lookup
  falls through when the unit is gone, and T with an empty selection stops tracking).
- **`FindLocalCommander` keeping the last match: MATCHES.** The Ctrl+C path selects every
  own commander and tracks only the last one found, with a comment that the original does
  not cycle on repeated presses (`GameScene_input.cpp`, about 272-296). The chapter's "no
  match leaves the previous pointer" has no counterpart because RWE tracks nothing when
  there is no commander.
- **Y/2 projection and the signed-short truncation:** RWE's camera is a 3D projection with a
  zoom, not a 2D scroll, so the projection is its renderer's, not a port of this arithmetic.
  `TOTALA-EXE-RENDER.md` holds the original's. Not applicable.
- **Shake: MATCHES the register** (`TOTALA-EXE-WEAPONS.md`, "no falloff with distance",
  accumulate by averaging the durations and adding the magnitudes). The chapter adds the
  **UI option `0x10` that disables a start**; the register's version of the same bit is the
  `NoShake` console command (`TOTALA-EXE-WEAPONS.md`, bit 4 of `WORD [globals+0x37F2F]`, read
  at `0x41C5E6` and `0x41C646`). Those are two descriptions of one option; no conflict.
  `UpdateScreenShake` is stepped on the simulation tick in RWE by design (the register says
  so), a fixed-rate port of a per-call decrement.

## 13. Camera: drag, edge and keyboard scrolling

**Reference (section 12).** `BeginMouseScroll` (`0x41CC60`) clears follow and snap, hides the
software cursor, saves the mouse position and the scroll **divided by 16**, anchors the
cursor at the screen centre and warps the OS cursor there. `UpdateMouseScroll` (`0x41CD50`)
takes **one quarter of the mouse's displacement from the centre**, adds it to the saved scroll
in 16-pixel units, multiplies by 16, clamps, and recentres the cursor; releasing the button
(key flag bit 1) ends it and restores the cursor. `UpdateEdgeScroll` (`0x41CE90`) multiplies
`scrollSpeed` by the simulation-budget delta, **caps the product at 128**, returns on zero,
and reconciles the cursor near window edges by the display flags and focus. Arrow keys
`0xF4` to `0xF7` move left, up, right, down **unless `TALK.GUI` is open**; mouse-edge scrolling
stays available during the chat check; **left and up win over right and down on each axis**.
Any move clamps, copies the desired position, marks the view dirty, clears map flag 8 and
cancels following. A negative speed is not validated.

**RWE.**

- **Right-button drag scroll: MISSING.** RWE has a **middle**-button pan only
  (`onMouseDown`, `CameraControlStateMiddleMousePan`, `GameScene_input.cpp`, about 973-976
  and 1249-1262), which moves the camera by the cursor delta one for one. The original's
  scheme is a right-button drag in Interface Type 0 (the shipped default, "Left Click"),
  warping the cursor to the screen centre and scrolling by a quarter of the offset with a
  16-pixel quantum. Whether the quarter-and-quantum feel matters is a taste question; that the
  right button does it is documented (`TOTALA-EXE-INTERFACE.md` line 1209 and 1544). The
  **modifier** is the open point: see rule 4 of the companion placement audit and the
  conflicts list (the chapter says the drag needs a key flag, the register says Type 0 always
  drags).
- **Edge scroll: DIFFERS in rate, UNCLEAR in shape.** RWE scrolls at a constant
  `1000 px/s x (scrollSpeed / 100)` while the pointer is on the window's border and the
  window has input focus (`GameScene.cpp`, about 665-690, `nudgeCamera` about 3591-3620).
  The original's rate is `scrollSpeed x delta`, where "delta" is the simulation-budget delta
  (the number of ticks, or a time fraction, it was given), capped at 128 per call; the
  chapter does not give its unit, and RWE's own tracking comment says the original's scroll
  speed "followed game speed". So at game speed 10 the original's edge scroll presumably goes
  faster and RWE's does not. **UNCLEAR**; settled by one play-test at two game speeds, or a
  spec-room read of the delta's source.
- **Arrow keys during chat: MATCHES in effect.** RWE's message bar owns the keyboard ahead
  of everything (`onKeyDown`, about 57-62), so no arrow reaches the camera, and the edge
  scroll does not look at the bar. A key held down when the bar opened could leave the
  `up`/`down`/`left`/`right` flag set; `onKeyUp` is not gated, so it clears on release.
- **Axis priority** (left and up beat right and down): RWE sums the directions
  (`directionX = right - left`), so holding both cancels. Original: the left key wins.
  Presentation only, and only with two opposite arrows held.
- **The clamp, the copy, "clears map flag 8" and "cancels following":** RWE's `nudgeCamera`
  leaves tracking as the chapter does (a nudge cancels it), clamps to the map, and has no
  map flag 8 (the flag's meaning is not in this chapter). Equivalent.
- **Negative scroll speed not validated:** RWE clamps the setting to 25 to 200 in the slider
  (`GameScene_menu.cpp`, about 680). A `ScrollSpeed` console command value is the original's
  open hole; RWE has the slider only. No hole here.

## 14. Persistence and coverage boundaries

**Reference (section 13).** The panel and camera define no save record. Paging and standing
settings persist in unit flags; orders, construction fraction and resources persist in their
own components; GUI assets and current focus are presentation state rebuilt on load.

**RWE.** Standing move and fire orders, on/off and cloak are sim state and are saved and
hashed. The build page and section are not (rule 5). The camera and the shake are not sim
state and are not saved; neither is the original's. Equivalent except for the page.

---

## 15. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation, or a `ui_probe` render, before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Page stepping: does `.`/`,`/NEXT/PREV toggle (close at the last page, open page 1) or wrap? RWE always wraps and reopens at `current + 1`. Rule 7 | `src/rwe/game/GameScene_input.cpp` (the `,` and `.` branch), `GameScene_commands.cpp` (`NEXT`, `PREV`) | **Yes.** Spec-room read of the pushed argument at the three callers, or one play: press `.` on the commander's last page |
| 2 | `BUILD_PERCENT_LEFT` divides by an unguarded `buildTime`; a unit with `BuildTime=0` crashes every peer. Rule 9 | `src/rwe/sim/UnitState.cpp` (`getBuildPercentLeft`), `cob.cpp`, `io/fbi/io.cpp` (default and floor), `io/malformed_input.test.cpp` | No. A floor of one at load, or a guard; add a malformed-input case per `SECURITY-AUDIT.md`. Needs the loader default read first |
| 3 | BLAST hidden where the original greys it (no load capability, no D-gun): an empty slot instead of a dimmed face on every ordinary selection. Rule 4 | `GameScene_commands.cpp` (`applyOrderButtonGating`, the `doomed` set), `OrderButtons.cpp` | Yes, a look: one frame of the original with a Peewee selected (the register's `0x41A409` text is the only evidence that the dimmed face appears) |
| 4 | Right-button drag scroll missing in the Left Click scheme; the register and the chapter disagree on whether a modifier is needed. Rule 13 and the conflicts list | `GameScene_input.cpp` (`onMouseDown`, `onMouseMove`), `GameScene.h` (`CameraControlState`) | **Yes.** Which key flag the drag needs is unidentified; spec-room read, then one play |
| 5 | Order-button sound classes: six buttons use `specialorders` where the original uses `immediateorders`. Rule 2 | `GameScene_commands.cpp` (`onMessage`, six branches) | No for the mapping (static); the sound files are data |
| 6 | PREV and NEXT stay live on a one-page menu. Rule 8 | `GameScene_commands.cpp` (`createBuildPanel`, disable at `pages < 2`) | No |
| 7 | Mission-excluded entries are removed from a build page; the original greys them. Rule 8 | `GameSimulationLoader.cpp` (the erase at about 1388), `createBuildPanel` | No for the shape; it reverses a deliberate port (issue 381), so the maintainer decides |
| 8 | Placement mode is chosen by the builder's mobility; the panel's rule is the product's `bmcode`. Rule 6 | `GameScene_commands.cpp` (`onMessage`, the `isMobile` test) | No for the rule; confirm with a modded mobile builder |
| 9 | Edge-scroll rate and its dependence on game speed; the quantised right-drag feel. Rule 13 | `GameScene.cpp`, `GameScene_commands.cpp` (`nudgeCamera`) | Yes, two game speeds |
| 10 | Build page and section are scene-local and lost on save and load; the original keeps them on the unit. Rule 5 | `GameScene.h` (`unitGuiInfos`), `SaveFile.*` | No. Presentation state, optional |

### Documented divergences this audit leaves alone

The substring-versus-equality control match; the cloak accumulator's disagreement bug
(not ported); the integer-versus-float follow step; the shake stepped on the simulation tick;
the build progress kept as integer work units (the register's one-tick note); the metal
refund applied at the settle rather than in the helper.

### Conflicts with existing RWE findings, in one place

1. **Right button in Interface Type 0.** Register (`TOTALA-EXE-INTERFACE.md` lines 1209
   and 1544): an armed command is cancelled, otherwise the right button starts the screen
   drag-scroll. Reference (`ingame-build-placement`, section 3): the drag starts only with a
   key-flag modifier; without it the same click clears the selection. RWE clears the
   selection and has no right-drag. Open.
2. **Mission-excluded build entries.** Register port note (`TOTALA-EXE-DATA.md`, after line
   1060): "their buttons come off every build menu". Reference: they are greyed in place
   because the definition cannot be resolved. Open (a presentation choice, not a decoded
   routine).
3. **Page-step argument.** Register (`TOTALA-EXE-KEYBOARD.md`): `,` and `.` call the
   backward and forward step with argument 1. Reference: the step has a toggle mode and a
   non-toggle mode but the chapter does not say which a caller uses. Open until the
   callers are read.
4. **BLAST in the no-load case** is not a conflict with the reference (both say greyed) but
   a place RWE departs from the register's own text; it is listed here so it is not read as
   settled by the 2026-09-02 port note.
