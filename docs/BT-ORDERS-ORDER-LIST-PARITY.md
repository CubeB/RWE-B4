# Order lists: insertion, replacement, deletion and the executor parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how a unit's two order
lists are built, edited, run and cleaned up, per `AGENTS.md` and ADR-0004. The
mission service loop and its return-code table are `TOTALA-EXE-MISSIONS.md`
section 2 and `TOTALA-EXE-ECONOMY.md` section 110; the queue toggle and the
build-count edit are `TOTALA-EXE-INTERFACE.md` sections 136 and 137; what a
cancelled build refunds and which orders survive replacement is
`TOTALA-EXE-ECONOMY.md` section 134; the idle default mission is
`TOTALA-EXE-WEAPONS.md` (the `DefaultMissionType` section). This audit restates
none of them beyond a line, and says where the new chapter agrees.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-order-list.md`,
  sections 1 to 13). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the
  ones the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/game` and `src/rwe/sim` at the
  working tree of 2026-10-16 (HEAD `404f3c4d`, branch `bt-sim-conformance`).
  Nothing was built, run or changed, and tests were not run; a MATCHES below means
  the code reads as the rule says. Another agent had uncommitted edits in
  `GameSimulation.cpp`, `GameSimulation.h` and `UnitBehaviorService.cpp` while this
  was written, so line numbers in those two files may have moved.
- **A census of the 272 shipped FBIs** (retail install through `tools/hpi.py`,
  totala1 to totala4, ccdata and rev31) supplies the "what would this change on
  shipped data" figures. It is a count of keys in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

Not repeated here: the order-type resolution (`BT-ORDERS-ORDER-DISPATCH-PARITY.md`),
the helpers around the queue (build-animation calls, wait gates, goal attachment,
`BECARRIED`) in `BT-ORDERS-ORDER-QUEUE-PARITY.md`, and the two movement helpers
that sit in this chapter's address range (turn clamp and speed cap), which are
`BT-UNITS-UNIT-SCRIPTS-PARITY.md` rules 1 to 3 and `NAVAL-SYSTEMS-PARITY.md`
rules 3 and 4.

## Index

1. Shape: two lists, an order record, a type registry
2. Insertion and replacement policy
3. The queued-order toggle
4. Editing a build count
5. Deletion and the destructor's effects
6. The primary executor, wait masks and return codes
7. The secondary executor
8. Automatic attack and repair orders; the aircraft idle hunt
9. Queries, overlays and the stockpile percentage
10. The order save contract
11. Gaps to fix

---

## 1. Shape: two lists, an order record, a type registry

**Reference (sections 1, 2).** Each unit has a **primary** list and a
**secondary** list; a type whose default flags carry 0x40000 lives on the second.
An order is a 0x56-byte record: type byte, state byte, an event wait mask, a wake
tick, the **owning unit**, a **linked target reference** (the two are different
fields and confusing them reverses guard, attack and build relationships), a world
position, cached points, three handler-specific integers (build id, count and
progress for builds; a duration, a radius, a retry count or a piece for others), a
persistent flag word, a creation tick, a next pointer, pending events and an
optional goal object. The type byte is an index into a registry of 0x19-byte
entries sorted case-insensitively by name, so **the byte means nothing until
registration has finished**. Looking up an unknown name yields type zero rather than
an error.

**RWE.** None of this exists as such. A unit has one `std::deque<UnitOrder>`
(`UnitState.h:630`) of a variant of fifteen order structs (`UnitOrder.h:243`); the
kind of an order is the variant alternative, and the owner is the unit holding it.
The second list's job is spread over `UnitState::buildQueue` (a factory's products,
`UnitState_util.cpp`), `UnitWeapon::queuedRounds` (stockpile weapons,
`GameSimulation.cpp:2769-2790`) and the mission scripts' own list
(`MissionScripts.h`). Saves name the alternative as text (`save_util_orders.cpp`,
the `kind` strings), so the registry's sort order never reaches a file, a packet or
the demo recorder. **DIVERGENCE (structural).** The one consequence worth a line:
an unknown `DefaultMissionType` is read by `parseDefaultMission`
(`LoadingScene_util.cpp:435-478`) as "no default" with a warning for the names it
knows but does not run, where the original stores type zero; whether type zero is a
real mission or an "absent" marker is not stated by the chapter (**UNCLEAR**, mod
data only, no shipped unit names an unknown mission).

## 2. Insertion and replacement policy

**Reference (sections 3, 4).** `AddOrder` builds the instance from type, target,
position and arguments. With its **removal/queue control zero** and a type that
lacks flag 0x40 it first **prunes every primary node that lacks the preservation
flag 4**, and for a new primary order also removes the **leading run of automatic
nodes marked 0x4000**. It marks the new node with flag 1, and with the one-shot
announcement flag 0x2000 when control is zero. Placement: a new node with flag 0x20
or 0x40000 goes to the selected list's **head**; every other node goes **after the
primary node marked 0x1000, and the marker moves to the new node**, or to the tail
when no marker exists. Two naming traps: the function called `AppendOrder` inserts
before the head, and the function that really appends is `AppendOrderToTail`.
`EnqueueOrderType` makes a type-only order marked 0x4000, which is how "automatic or
default" nodes are told from requested work.

**RWE.**

- **A plain order replaces the queue: MATCHES in effect.** `issueOrder`
  (`PlayerCommandApplication.cpp:100-138`) interrupts the current task, calls
  `clearOrders` (`UnitState.cpp:455-461`, which also clears the weapon targets) and
  adds the order. The survivors of the original's prune are the nodes with flag 4;
  `TOTALA-EXE-ECONOMY.md` section 134 names them as the factory's own mission, and
  RWE's factory keeps its products in `buildQueue`, which `clearOrders` does not
  touch. The mobile builder's `MobileBuild` has no flag 4 and is destroyed, as RWE
  does. **UNCLEAR** which other types carry the flag by default: the chapter does not
  list the registry's defaults. If a shipped unit type other than the factory
  mission carries it, an order issued to that unit would not clear it in the
  original and would in RWE.
- **Leading automatic nodes: DIFFERS in one path, UNCLEAR whether reachable.**
  RWE has no automatic marker. Orders the unit makes for itself (an auto-engage
  attack at `UnitBehaviorService.cpp:438`, the patrol break-off at `:4943-4947`, the
  repair-patrol job at `:5043-5076`) are ordinary entries in the same deque. A
  **queued** (Shift) order is appended behind them (`enqueueOrder`, `:140-181`); in
  the original a new primary order first strips the leading run of 0x4000 nodes. It
  is not stated whether the attack nodes made by the sighting routine
  (`TOTALA-EXE-MOVEMENT.md` section 86, `0x43B1F0`) carry 0x4000; if they do,
  a unit auto-engaging an enemy that is Shift-ordered to move drops the engagement
  at once in the original and finishes it first in RWE. The play-test is one click
  (Gaps rank 4).
- **The insertion marker: UNCLEAR.** RWE appends queued orders at the back
  (`UnitState::addOrder`, `UnitState.cpp:489-492`), which equals the original's
  "after the marker" only while the marker sits on the last node. It does not when
  head-inserted nodes (flag 0x20, 0x40000) or sighting nodes are present. The
  chapter says insertion order "is policy-dependent, not universally FIFO" and gives
  no table of which types carry 0x20. No shipped case is known.
- **The one-shot announcement flag 0x2000: MATCHES in effect.** The "Attacking"
  and similar lines are announced once per order; the register has the call
  (`TOTALA-EXE-MISSIONS.md` table of weapon-control helpers, `0x438880`), and RWE
  announces at order start. Not traced further.
- **Move then patrol: not in the chapter.** `UnitState::addOrder` converts a move
  that is immediately followed by a patrol into a patrol node
  (`UnitState.cpp:466-492`). The reference's patrol type is chosen at resolution
  time (`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 5), so the two meet only if a
  queued Move then Patrol is meant to become a route; the register's own reading
  (`TOTALA-EXE-INTERFACE.md` section 136) is silent. Left alone.

## 3. The queued-order toggle

**Reference (section 4).** With a nonzero control, `IssueOrCancelOrder` searches the
**primary list only** for a node of the same type and, when supplied, the same
target and a position within **0x100000 (sixteen world units) on each horizontal
axis independently**. It deletes the first match and returns; no match falls through
to `AddOrder`. The secondary list is not searched.

**RWE. MATCHES the register** (`TOTALA-EXE-INTERFACE.md` section 136) and the
chapter: `queuePositionsMatch` (`OrderDispatch.h:11-14`) is inclusive sixteen on x and
z and ignores height; `ordersMatchForQueueToggle` (`:44-72`) compares destination or
position, else target, else matches on kind alone; `enqueueOrder` removes the first
match and interrupts the work only if it was the head (`:148-163`), which is the
destructor's flag-0x10000 rule below. One refinement and one open point:

- **A queued build's unit type is ignored: MATCHES** (the comment at `:52`).
- **Feature reclaim: UNCLEAR.** The original keeps a feature order as a **position**
  (features are not unit targets), so two queued reclaims whose positions fall within
  sixteen units of each other are "the same" and a Shift-click on the second removes
  the first. RWE compares the `FeatureId` (`queueTargetsMatch`, `:16-37`), so two
  features a few units apart are two orders. Whether the position stored on the
  original's reclaim order is the clicked point or the feature's centre decides
  whether this is reachable; the register does not say.

## 4. Editing a build count

**Reference (section 4).** `AdjustBuildCount` picks the list by type classification.
A positive amount **merges only into the current tail** when kind and build id both
match and otherwise adds a node; a nonpositive amount repeatedly finds the **last**
matching node anywhere, reduces it if it holds enough, or deletes it and carries the
remainder backward through earlier matching nodes. Queue edits are separate from
resources already committed to an active target.

**RWE. MATCHES the register's section 137 and DIFFERS in node count only.**
`UnitState::modifyBuildQueue` (`UnitState.cpp:730-739`) appends a node on every add
(the tail merge is not done, as section 137 records); `removeFromBuildQueue` searches
from the back and carries the remainder. The command handler decides the
"active node was removed" case itself (`PlayerCommandApplication.cpp:356-373`: the
front product's total is at most the amount removed) and cancels the product with the
section 134 refund, which equals the original for every sequence tried by hand
(removing from a list of one type with other types interleaved reaches the same
answer). The node count is saved and hashed state; nothing player-visible turns on
it. Stockpile weapons are a separate case (`GameSimulation.cpp:2769-2790`): one
counter per weapon, clamped at zero, and the part-built round is thrown away when the
count reaches zero. That equals the original's secondary node being deleted with its
progress, and it needs no carry because there is only one node.

## 5. Deletion and the destructor's effects

**Reference (section 5).** `DeleteOrders` with all false deletes the **primary**
nodes lacking flag 4; with all true it deletes both lists. `DeleteOrder` of a node
that is not present does nothing. A deleted node other than the original head gets
flag 0x10000 first. The destructor can send the type's callback event 2; stops marked
build animation (`StopBuilding`); detaches and deletes the order's movement goal;
**releases all weapon control unless 0x10000 is set**; and unlinks the target
reference. So deleting a queued non-head order must not release control the head
holds. `MoveOrderToTail` relinks a known primary node at the end without destroying
it (callback result 6).

**RWE.**

- **Head deletion: MATCHES.** `interruptCurrentTask` stows the arm and ends the
  nano spray (`PlayerCommandApplication.cpp:111`), `clearOrders` releases the weapon
  targets (`UnitState.cpp:455-461`).
- **Non-head deletion keeps the head's work: MATCHES.** The toggle interrupts only
  when the removed order is the front (`:156-160`), which is the 0x10000 rule.
- **Stop leaves the secondary list: MATCHES in effect.** `Stop` (`:377-389`)
  empties `orders` and interrupts; `buildQueue` and `queuedRounds` are untouched,
  which is `DeleteOrders` with all false (`TOTALA-EXE-INTERFACE.md` section 137,
  "Stop does not clear the queue"). A mission script's remaining steps are erased as
  well (`:380-383`), which is RWE's own list sitting above the unit's.
- **Death clears both lists: UNCLEAR whether RWE refunds.** Section 134's table says
  the builder's death runs the destructor on the product before the type index is
  cleared. RWE does not clear a dying unit's orders at all: a dead unit's update
  returns before its orders run (`UnitBehaviorService.cpp:223-226`). Whether the
  unfinished frame is refunded or left to decay is that section's own "inferred" row
  and was not re-audited.
- **Result 6, move to tail: MATCHES.** Patrol re-queues itself at the back
  (`UnitBehaviorService.cpp:4990,5001,5030`).

## 6. The primary executor, wait masks and return codes

**Reference (section 6).** `SetDeadlineTicks` enables wait bit 1 and sets the wake
tick; `RunOrders` rereads the head after every callback. A due deadline sets pending
bit 1 and resets the wake tick. **Pending input is the order's pending events OR the
unit's network-dirty bits, ANDed with the wait mask**; a nonzero mask with no
matching input returns; otherwise the consumed bits clear in both places and the mask
clears before the callback runs; pending 0x10000 also clears all three weapon
targets. Callback result: 0 reset the state byte, 1 increment it, **2 or 4 keep**,
3 wake at the current tick plus 30 plus random below 15, 5 or 8 delete this node,
6 move to the tail, **7 or any unrecognized value delete all orders and return**,
9 set flag 0x800000 and, if another node follows, delete this one, else reset the
state and wait 30 plus random below 30. A keep result does not force a return: the
head is reread and may run again in the same pass, and **there is no budget on that
loop**. When the list empties, an active local controller role 1 or 2 may receive the
definition's nonzero default mission as an automatic marked node, inserted after the
loop so it does not run in the same call.

**RWE. DIVERGENCE (structural), agreeing with the register where it overlaps.**
`UnitBehaviorService::update` runs **one** `handleOrder` on the front order per tick
and pops it when the handler answers "finished" (`UnitBehaviorService.cpp:370-383`).
There is no wake mask, no pending-event word, no state byte on the order and no
same-tick re-run; each handler polls what it needs every tick and keeps its progress
in `behaviourState` or on the order (capture progress, the unload park time). The
register already has the loop, its code table and the same-tick re-run
(`TOTALA-EXE-MISSIONS.md` section 2; `TOTALA-EXE-ECONOMY.md` section 110, where the
re-run is the reason a build's creation tick carries one extra increment). Points
this chapter adds or sharpens:

- **Result 4 and "unrecognized": a wording difference only.** The register's table
  has no row for 4 and none for unrecognized values; the chapter says 4 keeps the
  node and an unrecognized value flushes the primary list. No shipped handler is
  known to return either.
- **Pending input merges the order's events with the unit's dirty bits.** This is
  what makes **any** COB `set` (supported or not) wake an order waiting on bit 2
  (`TOTALA-EXE-ECONOMY.md` section 112 and the chapter on script calls,
  `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 4). RWE's polling sees the new value
  one tick later at worst, and the corpus timings (`tools/tad-buildtime.py`) are
  what pin the creation-tick increment.
- **Result 3 and 9 timers: MATCHES where ported.** The unload park
  (`UnitBehaviorService.cpp:3164`, 30 + `randomBelow(30)`) is result 9; the mine
  poll (`:4751`, 30 + `randomBelow(30)`) is the standby poll; both draw from
  `sim->rng` with a modulo, as `SimRandom.h` requires. Result 3's 30 + `rand(15)` is
  the factory-blocked and air-idle polls the register lists (`TOTALA-EXE-ECONOMY.md`
  section 112, `TOTALA-EXE-MOVEMENT.md` sections 13 and 138). **Idle poll cadences stay
  deferred on purpose** (`TOTALA-EXE.md` section 88, section 138).
- **Result 7 (flush) is ported at the sites the register found.** The factory whose
  product dies from outside is the case (`UnitBehaviorService.cpp:5337` comment);
  the generic rule has no counterpart because RWE handlers return a boolean.
- **Default mission on an empty list: DIVERGENCE (documented).** Only
  `Standby_Mine` is run (`UnitBehaviorService.cpp:532-536`); the rest are what an idle
  unit does anyway or are warned about at load (`LoadingScene_util.cpp:471-478`),
  per `TOTALA-EXE-WEAPONS.md` (the `DefaultMissionType` section) and
  `TOTALA-EXE.md` line 731. The chapter adds only that the inserted node is
  **marked automatic (0x4000)**, which is the same unknown as rule 2.

## 7. The secondary executor

**Reference (section 7).** `RunSecondaryOrders` visits every secondary node. A node
runs when its mask is zero or its wake tick is due; it clears the mask and calls the
callback with **event input zero** (no dirty-bit merge). 0 and 1 reset or increment,
2 and 4 keep, 3 schedules 30 plus random below 15, **5, 8 and 9 delete the node, 6 and
7 delete it and return** (neither rotates the queue nor flushes), and an unrecognized
result deletes the node. After a run it restarts at the head; a still-waiting node is
skipped, so another secondary action can run. "The same code does not mean the same
queue operation in both lists."

**RWE. UNCLEAR, and one correction to the register's wording.** The register says the
secondary pass puts "each through the same wake test and return-code table"
(`TOTALA-EXE-MOVEMENT.md` section 132 stage 7). The chapter says the wake test is the
same but the **return table is not** (6 and 7 mean "delete and return", not "rotate"
and "flush"), and the dirty bits are not merged. Recorded as a CONFLICT of wording
(see the list at the end). It matters only to the `BuildWeapon` order, the one
secondary type in the shipped data, whose handler is a loop returning keep or delete.
RWE's stockpile is advanced from the weapon update, not from an order
(`TA-SYSTEMS-PARITY.md` rule 27 for the cost and rate), so there is no
secondary-list executor to compare.

## 8. Automatic attack and repair orders; the aircraft idle hunt

**Reference (section 8).** `IssueAttackOrder` rejects self-target and, **unless
overridden, requires nonzero standing move and nonzero standing fire settings**. It
resolves the attack mode and rejects type zero. With a limited-movement setting it
inserts a return-position movement node and a target attack node carrying the
maneuver leash and the start X/Z; other accepted cases insert a direct node.
`IssueRepairOrder` resolves the repair mode first and then branches on the standing
move state: states zero and one insert a return-position plus repair pair with
different range and leash, state two inserts a direct repair, other states return
zero; its third argument is not the attack helper's force-success override.
`FindBestTargetIfFireAtWill` runs the weapon target search **only when the standing
fire mode equals 2**.

**RWE. MATCHES the register** (`TOTALA-EXE-MOVEMENT.md` sections 13 and 86 and
`TOTALA-EXE-ECONOMY.md` section 131, which decoded the same routines to the
instruction) **with one DIFFERS.**

- **Patrol and mines: MATCHES.** The patrol break-off requires Fire At Will exactly
  and a movement mode other than Hold Position (`UnitBehaviorService.cpp:4925-4926`),
  pushes the return move first for Maneuver and the leashed attack in front
  (`:4936-4947`); the mine poll requires Fire At Will (`:4737`).
- **The aircraft idle hunt gate is wider than the search: DIFFERS.** An idle
  aircraft looks for a fight when the unit can attack, its fire setting is **not
  Hold Fire**, and its move setting is not Hold Position
  (`UnitBehaviorService.cpp:412-413`). The search the original runs there
  (`VTOL_Standby` state 1, `TOTALA-EXE-MOVEMENT.md` lines 287 and 243) is the one that
  returns nothing unless the fire setting is exactly 2. So an aircraft on **Return
  Fire** is sent after targets by RWE and stays put in the original. The ground
  patrol and the mine already use the exact test; only this branch uses the weaker
  one. Shipped aircraft default to Fire At Will, so it shows only after the player
  presses the fire-order button.
- **Repair helper: MATCHES.** The repair patrol's three arms
  (`UnitBehaviorService.cpp:5043-5076`) are the direct, return-position and leashed
  forms.

## 9. Queries, overlays and the stockpile percentage

**Reference (section 9).** `GetOrderTarget` returns the head's target, or zero.
`GetOrderTargetIfFlagged` requires head flag 8. `GetOrderName` returns the head type's
status text, or type zero's text when the list is empty. `FindOrderByType` searches
the list selected by the type's default classification and returns the first match.
`SumQueuedBuildCount` sums counts with flag 0x100 over both lists.
`GetBuildWeaponPercent` finds the first secondary node marked 0x80000 and divides its
progress times 100 by the selected weapon's timing field, **without guarding a zero
denominator**. `DrawOrderOverlays` walks primary nodes and intersects the caller's
mask with each type's draw mask (footprints, path animation, order range ring, weapon
coverage, unit range rings drawn once), with a running position that connects queued
destinations.

**RWE.**

- **Queries: DIVERGENCE (structural).** The panel and the status line read the
  front order directly (`GameScene_util.cpp:1935` is the activity mapping, and
  `GameScene_render.cpp:1397-1474` the overlay and cursor walks over the deque).
  Nothing in the sim reads a "name" or a flag-8 target; the roles are distinct code
  paths.
- **Stockpile percentage: UNCLEAR, and the zero denominator is not to be ported.**
  RWE shows stockpile progress from `UnitWeapon::stockpileProgress` against the
  weapon definition; the reference formula divides by a weapon timing field that is
  zero for a weapon with no build time. RWE never divides by it
  (`docs/SECURITY-AUDIT.md`: a divisor from content is checked before use).
- **Overlay mask and the running position: UNCLEAR.** The queued-destination
  connecting line exists in RWE's overlay; whether it starts from the unit and
  threads orders in the original's order was not traced. Presentation only.

## 10. The order save contract

**Reference (sections 10, 11).** A saved order is a raw 0x3a-byte descriptor plus a
name key (name plus `_name`) and, for the three build types, a unit-type key written
as decimal width four; a goal is saved under name plus `g`. On load the kind is
resolved from the stored name when present, otherwise through a **legacy ordinal scan
that skips registry entries with the relevant high flag**; build ids are remapped
through the type key, with a legacy table-position mapping over non-downloadable
definitions as the fallback. The creation tick is not restored (it becomes the load
tick). Early validation failures leave a partly built object and the constructor
returns no status.

**RWE. DIVERGENCE (structural), and stricter.** Orders are saved as JSON with a
`kind` string and typed fields (`save_util_orders.cpp`); a bad kind throws
(`:38-47`), which the loader reports as a failed load. Nothing is positional, nothing
depends on registry order, and `UnitState` has no creation tick. The saved
**capture progress, the resurrection countdown and the unload park time ride on the
order** exactly as the original's progress does (`UnitOrder.h:120-240`), and the
order queue is hashed (`GameHash_util.test.cpp:190-209` lists every alternative), so
the failure mode the reference warns about (a restored action of the wrong type) has
no counterpart. No gap.

## 11. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or
capture run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | The idle aircraft hunt must use the exact Fire At Will test, as the patrol and the mine do (rule 8) | `src/rwe/sim/UnitBehaviorService.cpp:412-413` (and its test in `UnitBehaviorService_air.test.cpp` / `aircraftidle.test.cpp`) | No: `TOTALA-EXE-MOVEMENT.md` line 287 and `0x43B700`'s compare are already in the register; a one-click confirm is a Return Fire fighter idling next to an enemy. |
| 2 | Decide whether a Shift-queued order drops a unit's self-made engagement (the 0x4000 leading run) (rule 2) | `src/rwe/game/PlayerCommandApplication.cpp:140-181`, `UnitState.h` (a marker on self-made attack, return-move and repair-patrol nodes) | **Yes.** Send a fighter or a Peewee at an idle enemy, Shift-click a move while it is engaged, watch whether it keeps shooting first. |
| 3 | Position-based match for queued reclaim of features (rule 3) | `src/rwe/game/OrderDispatch.h:16-37`, `PlayerCommand_patrol.test.cpp:114` | **Yes.** Shift-click two features within sixteen units and see if the first order disappears. |
| 4 | List the registry's default flags and make "which types survive replacement" a table in the register (rule 2) | `docs/TOTALA-EXE-INTERFACE.md` section 136/137 follow-up; no code unless a second flagged type is found | Spec-room: the registry defaults are the missing input. |
| 5 | Insertion marker policy for head-inserted nodes (rule 2) | `UnitState.cpp:489-492` | Spec-room first; no shipped case known. |
| 6 | Tail-merge of build-count adds (rule 4) | `UnitState.cpp:730-739`, `save_util`, `GameHash_util.cpp` | Not recommended (`TOTALA-EXE-INTERFACE.md` section 137): node count is saved and hashed state with no gameplay value. |

### Documented divergences this audit leaves alone

One deque instead of two lists and a registry; no wake masks or same-tick re-run
(register section 110 is the compensation); the default mission being implicit; the
mission script list above the queue; name-keyed JSON saves; the stockpile counter
instead of a secondary node.

### Conflicts with existing RWE findings, in one place

1. **Secondary executor return codes.** Register (`TOTALA-EXE-MOVEMENT.md`
   section 132 stage 7): every secondary mission runs "through the same wake test and
   return-code table". Reference (`orders-order-list` section 7): same wake test,
   different table (6 and 7 delete the node and return; unrecognized deletes the
   node) and no dirty-bit merge. Wording; the one shipped secondary type is
   unaffected.
2. **Callback result 4.** Register (`TOTALA-EXE-MISSIONS.md` section 2): no row for 4.
   Reference: 2 or 4 keep the node. Wording only.
