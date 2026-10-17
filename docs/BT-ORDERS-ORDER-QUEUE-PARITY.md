# Order helpers: carried state, build-animation calls, wait gates and reclaim pulse parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for the small services the
order machinery calls: installing the carried-state order, starting and stopping
the build animation, the two COB readiness gates, the live-unit reclaim pulse,
status delivery and goal attachment, per `AGENTS.md` and ADR-0004. Where the
register already has a rule (the same-tick re-run and the creation-tick increment
are `TOTALA-EXE-ECONOMY.md` section 110; the reclaim bite is the same file's reclaim
section; the carried state is `TOTALA-EXE-TRANSPORTS.md`) this audit says "agrees"
and moves on. The companion audit for the lists these helpers serve is
`BT-ORDERS-ORDER-LIST-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-order-queue.md`,
  sections 1 to 9). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose.
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
  was written, so line numbers in those files may have moved.
- **A census of the 272 shipped COB scripts** (retail install through `tools/hpi.py`
  and the opcode table of `tools/exe/coblist.py`, same archives as the FBI census of
  the earlier audits) supplies the figures where a rule turns on what scripts do.
  It is a count of bytecode patterns, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Orders and types are separate state
2. Installing the carried-state order
3. Starting and stopping build animation
4. COB readiness gates
5. The reclaim damage pulse
6. Status delivery and speech
7. Goal attachment and ownership
8. Changing an order's type
9. Gaps to fix

---

## 1. Orders and types are separate state

**Reference (section 1).** An order instance (0x56 bytes) is distinct from its type
byte, which selects a registry entry; the two table accessors multiply the type by
0x19 and **do not range-check it**; building a type from a name binary-searches the
registry case-insensitively and stores zero on a miss, never registering a new name.

**RWE. DIVERGENCE (structural).** No registry and no type byte; the kind is the
`UnitOrder` variant alternative (`UnitOrder.h:243`). The unchecked accessors have no
counterpart, which is the right answer under `docs/SECURITY-AUDIT.md`. See
`BT-ORDERS-ORDER-LIST-PARITY.md` rule 1 for the one visible consequence.

## 2. Installing the carried-state order

**Reference (section 2).** `AddBeCarriedOrder` acts only when the unit has a carrier.
It walks the primary list and **deletes every node without preservation flag 4**,
keeping flagged nodes (a deleted non-head node gets 0x10000 first, so the head's
weapon control is not released). It then builds `BECARRIED` targeting the carrier and
inserts it at the head of the list chosen by its own flag 0x40000, inheriting flag
0x4000 from the node it now precedes. This is an order-state change that accompanies
attachment, **not** the physical link. Existing preserved orders and the carried-state
order can coexist: cargo does not mean every queued command was blindly discarded.

**RWE.**

- **Orders cleared on load: MATCHES in effect for shipped data.**
  `GameSimulation::loadUnitIntoTransport` empties the order list, drops the
  navigation state, clears weapon targets and the behaviour state
  (`GameSimulation.cpp:889-904`; the order list and the targets are cleared twice in
  a row, which is harmless). The preserved nodes of the original are the factory's
  own mission (`TOTALA-EXE-ECONOMY.md` section 134), and a factory cannot be picked
  up, so nothing the original keeps is lost. **UNCLEAR** if a mod gives flag 4 to a
  mobile type.
- **The airbase exemption is the register's, not new.** The original does not run
  this service when the thing holding the unit is an air base
  (`TOTALA-EXE-MOVEMENT.md` line 668, `TOTALA-EXE-TRANSPORTS.md` line 433). RWE's
  pad landing is a separate order and never calls `loadUnitIntoTransport`
  (`LandOnAirBaseOrder`, `UnitOrder.h:219`); agrees.
- **A carried unit has no order: DIFFERS in presentation and in one edge.** The
  original's carried unit holds a `BECARRIED` order at the head of its list, so
  `GetOrderName` returns its status text (`TOTALA-EXE-INTERFACE.md` line 1403 says it
  is "not orderable", installed only by this routine). RWE's carried unit has an empty
  list and `carriedBy` set (`UnitState.h` carried fields), the unit tick returns early
  for it (`UnitBehaviorService.cpp:229-232`), and the selection predicate excludes it
  (`UnitState.cpp:618`). The edge: `refusalOf` does not refuse an order for a carried
  unit (`PlayerCommandApplication.cpp:245-294`), so a modified client's order is queued
  and runs when the unit is set down. The original cannot be asked: the selection that
  builds the command never contains a carried unit. A refusal for `carriedBy` in
  `refusalOf` is the same refusal on every peer.

## 3. Starting and stopping build animation

**Reference (section 3).** `StartBuildingScript` finds `StartBuilding` in the unit's
COB interface, starts it with **one unsigned heading argument**, sends the same call
over the script-call event path and marks order flag 0x400000. `StopBuildingScript`
runs `StopBuilding` **only when that flag is set**: it starts the no-argument call,
sends the no-argument event and clears the flag, so repeating a stop without a start
does nothing. The flag belongs to the order; it coordinates cleanup when the order is
destroyed. Starting the animation creates no construction target, debits nothing and
does not guarantee the build stance.

**RWE.**

- **Once-per-start stop: MATCHES in effect.** The stop is created where the
  behaviour state leaves `UnitBehaviorStateBuilding` (`UnitBehaviorService.cpp:5340,
  5363, 5392, 5499, 6043`), and that state exists only after a start; the same
  property as a per-order flag, held on the unit rather than on the order.
- **The start is queued, not run: MATCHES the register** (`TOTALA-EXE-ECONOMY.md`
  line 1940): RWE creates a COB thread (`:5247`), which runs in the script pass
  of the same unit slot (`TOTALA-EXE-MOVEMENT.md` section 132).
- **The argument list: UNCLEAR.** RWE passes **two** values, the heading and the
  pitch of the line to the target (`:5247`, `:6177`, `:6332`, `:6460`, from
  `computeLineOfSightHeadingAndPitch`). The chapter says the start passes a single
  unsigned heading argument. A COB function reads its parameters as its first locals,
  so a script that reads a second parameter would see whatever the missing argument
  leaves there in the original and the real pitch in RWE. The COB header carries no
  parameter count (`src/rwe/io/cob/Cob.cpp` reads none), so the census could not
  count declared parameters; the register does not state the argument list either
  (`StartBuilding` is only named, `TOTALA-EXE-ECONOMY.md` line 1940). A visible
  consequence, if the original really passes one, is the nano arm of a builder aiming
  at a target above or below it: RWE tilts toward the target, the original would not.
  **Original check first** (Gaps rank 1): build something on a hillside with a
  commander and watch the arm's pitch.
- **The network call: not applicable.** The start and stop are also sent as script
  events to peers because the original is owner-authoritative
  (`TOTALA-EXE-MOVEMENT.md` section 118); RWE is lockstep and every peer runs the
  script.

## 4. COB readiness gates

**Reference (section 4).** `WaitIfNotInBuildStance` tests bit 0 of the unit's COB
state byte; when clear it **replaces the order's wait mask with the supplied mask OR
4** and returns result 2, else returns 1. `WaitIfCobBusy` has the opposite sense for
bit 1 (busy set: install the same mask-plus-4 wait and return 2; clear: return 1). A
script that never supplies the state leaves the order waiting while the model looks
busy.

**RWE. MATCHES the register's reading** (`TOTALA-EXE-ECONOMY.md` section 110, which
decodes the same gates and their eleven callers, and the aircraft case where the
answer is discarded). The build-stance bit is `UnitState::inBuildStance`
(`UnitState.h:682`, set by script through `PieceController::setBuildStance`) and the
busy bit is `UnitState::cobBusy` (`:701`). Handlers poll them every tick instead of
waiting on a mask; the extra increment on the creation tick that the original's
re-run produces is the register's own correction (section 110) and is pinned by
`tools/tad-buildtime.py`. Nothing new.

## 5. The reclaim damage pulse

**Reference (section 5).** The live-unit reclaim pulse is derived from the actor's
worker time, an **integer** experience multiplier `(kills + 5) / 5`, the target's
maximum health and the caller's pulse factor; the denominator is the **larger of the
target's metal cost and 10, times 300**; the result is truncated to an integer with a
**minimum of 1**. The numerator is a 32-bit multiplication chain placed in the low
half of a 64-bit value with a zero high half, so very large inputs do **not** behave
like exact arithmetic. No cap relates the pulse to the target's remaining health; the
damage path handles that. It is damage progress, not the metal refund.

**RWE. MATCHES the register and the chapter, with one DIFFERS at the extremes.**
`computeUnitReclaimStep` (`GameSimulation.cpp:430-442`) is the formula to the term
(integer veterancy, cost floored at 10, final truncation, minimum 1; the factor 15 is
the bite counter the register calls `0x438650(reclaimer, target, 15)`,
`TOTALA-EXE-ECONOMY.md` line 1546), and the bite lands every sixteen ticks
(`UnitBehaviorService.cpp:6291-6317`) as the register says. Three points:

- **Overflow: DIFFERS.** RWE multiplies in `double`, so it never wraps. The original's
  32-bit chain wraps once worker time times veterancy times maximum health times the
  pulse factor passes 2^32. With worker time 300 and factor 15 that needs health times
  veterancy over about 954,000: no shipped target (largest FBI `maxdamage`
  below 20,000) reaches it below about fifty veteran tiers, i.e. more than 250 kills
  by the reclaimer. **Not worth reproducing**; recorded so nobody reads the double
  as the original's arithmetic for a mod with enormous health.
- **Sized once or per bite: no difference.** The register says the bite is sized
  once as the work starts; RWE recomputes each bite (`:6316`) from the reclaimer's
  kills and the target's maximum health. The first can change only if the reclaimer
  scores a kill mid-reclaim; the second is constant.
- **Payout: DIVERGENCE (documented).** The metal is credited a bite at a time in both
  resources where the original pays `trunc((1 - progress) * cost)` in one lump, metal
  only (`TOTALA-EXE-ECONOMY.md` reclaim section; `TOTALA-EXE.md` section 88).

## 6. Status delivery and speech

**Reference (section 6).** `OrStatusFlags` ORs events into the order's pending word
without replacing and without invoking the handler. `AnnounceStatusIfFlagged` tests
persistent flag 0x2000, clears it and queues unit speech of kind 5 with the supplied
text; it is a one-shot gate, not an error on every callback. `GetOrderFlags` returns
**the head's flag word only**, zero for no unit or no head, and never merges queued or
secondary flags.

**RWE.** The one-shot is `UnitCannotComplyEvent` and the `cant` captions the register
lists (`TOTALA-EXE-ECONOMY.md` sections 97 and 134 give the sites; e.g.
`UnitBehaviorService.cpp` capture caption at `:5128`). **MATCHES in effect**: each is
pushed once at the point of failure, not on every poll. No pending-event word exists
(see `BT-ORDERS-ORDER-LIST-PARITY.md` rule 6). `GetOrderFlags` has no counterpart;
nothing reads a flag word.

## 7. Goal attachment and ownership

**Reference (section 7).** `SetAttachedFx` (owner has motion only) detaches the
previous goal from the motion interface, deletes it, clears pending bits 0x3e0 (bits 5
to 9, the arrival and failure events), attaches the new one and stores it on the
order; **the order owns the goal's lifetime**. `ReattachFxToUnit` re-attaches an
existing goal after reconstruction without making a second. Three geometry helpers
build a **radius around X/Z**, an **inner/outer ring** and a **cell rectangle**; for a
non-flying owner they replace the goal and clear the same bits, for a flying definition
they **remove** an existing ground goal and create nothing (VTOL orders have their own
approach logic). A missing motion object is not handled as a complete rollback in
every branch. A radius does not guarantee a path; a footprint marker grants no build
permission.

**RWE. DIVERGENCE (structural).** A goal is not an object on the order. Navigation
state lives on the unit (`UnitState::navigationState`) and is set by `navigateTo` with
a point, a unit, or a rectangle expanded by the reach (`UnitBehaviorService.cpp:6030-6036`
is the build-reach rectangle), saved with the unit and rebuilt on load, so
`ReattachFxToUnit` has no analogue. The arrival tolerance for the original's radius
goal is the register's (`TOTALA-EXE-MISSIONS.md` section 2, `IsSatisfied`, tolerance
versus the 0.5-unit default); aircraft use their own approach in `AirMovement`. The
rectangle form is the closest thing to the footprint marker, and it is used by the
builder reach, not by the order as a permission. Nothing to port.

## 8. Changing an order's type

**Reference (section 8).** `MergeFlagsFromTable` writes a new type byte and replaces
the persistent flags with the registry entry's defaults **while retaining the
instance's bits 0x600** (the two target/position-valid bits). It is not a full
reconstruction: state, position, target and goal stay. Handlers must reset their own
state fields.

**RWE. DIVERGENCE (structural).** An order has no type byte to rewrite: an order
that changes kind is removed and the new struct pushed (the air-repair landing
order is pushed to the front of the list, `UnitBehaviorService.cpp:2252`, and the
patrol break-off pushes its attack and return move in front of the route,
`:4943-4947`). The retained-bits rule keeps "has a target" and "has a position"
through the original's rewrite; in RWE the new struct carries its own target and
position. No visible difference is known.

## 9. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or
capture run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Settle whether `StartBuilding` is called with a heading only or with heading and pitch (rule 3) | `src/rwe/sim/UnitBehaviorService.cpp:5247,6177,6332,6460` | **Yes.** Watch the nano arm's pitch for a commander building on a slope in the original; or a spec-room read of the script-start routine's argument count. |
| 2 | Refuse an order for a carried unit in the shared refusal (rule 2) | `src/rwe/game/PlayerCommandApplication.cpp:245-294`, `PlayerCommand_patrol.test.cpp` or a new case | No. The selection never contains a carried unit, so it cannot change an honest game; it removes a way to queue orders that run on unload. |
| 3 | Record the reclaim pulse's 32-bit wrap as not ported (rule 5) | `src/rwe/sim/GameSimulation.cpp:430` (a comment only) | No. |
| 4 | Give a carried unit an observable "being carried" status for the info panel (rule 2) | `src/rwe/game/GameScene_util.cpp:1935` (activity mapping) | No; presentation only. |

### Documented divergences this audit leaves alone

No registry, no type byte, no goal object on an order, no pending-event word, no
same-tick re-run (compensated by the register's section 110), the reclaim payout
credited a bite at a time.

### Conflicts with existing RWE findings, in one place

1. **`StartBuilding` argument list.** RWE passes heading and pitch
   (`UnitBehaviorService.cpp:5247`); the reference says the start passes a single
   unsigned heading. The register names the script and the queueing and is silent on
   arguments. Open until the argument count is read from the start routine or seen in
   play.
