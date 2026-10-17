# Ground and common unit-order state machines parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-ORDER-DISPATCH-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for what the
ground and common order handlers do once an order exists: construction, assistance,
capture, reclaim, resurrection, repair, stationary and chasing combat, guard, and the
ground transport flow, per `AGENTS.md` and ADR-0004. The execution contract (return
codes, wake masks, the primary and secondary executors) is
`BT-ORDERS-ORDER-LIST-PARITY.md` rules 6 and 7; the order record, the carried-state
order, the reclaim damage pulse and goal attachment are
`BT-ORDERS-ORDER-QUEUE-PARITY.md`; what order a click becomes is
`BT-ORDERS-ORDER-DISPATCH-PARITY.md`; destinations and arrival are
`BT-ORDERS-ORDER-TARGETS-PARITY.md`; the aircraft handlers are
`BT-ORDERS-VTOL-ORDERS-PARITY.md`. This audit restates none of them.

The register already holds most of this module's subject, and a large part of the
chapter is corroboration. Where it is, the rule says so in one line and names the
section (`TOTALA-EXE-ECONOMY.md` sections 96, 97, 110-112, 130, 131 and 134;
`TOTALA-EXE-INTERFACE.md` section 133; `TOTALA-EXE-DATA.md` sections 105 and 114;
`TOTALA-EXE-MOVEMENT.md` sections 86, 102 and 138; `TOTALA-EXE-TRANSPORTS.md`
sections 34, 35 and 40).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-unit-orders.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of
  its source links were followed. Addresses are the ones the chapter names; rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so
  that rows already resolved there are not raised again; the rows this audit touches
  are BT-18, BT-61, BT-62, BT-63, BT-66, BT-69 and BT-72.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was
  run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/game` and
  `src/rwe/pathfinding` at the working tree of 2026-10-16. Nothing was built, run or
  changed, and tests were not run; a MATCHES below means the code reads as the rule
  says. Another agent had uncommitted edits in `UnitBehaviorService.cpp` and
  `UnitBehaviorService.h` while this was written, and the line numbers moved by a few
  lines between two reads; every line number below is therefore "about".
- **A census of the shipped FBIs** (retail install through `tools/hpi.py`, the scratch
  census the earlier audits used) supplies the "what would this change on shipped
  data" figures. It is a count of keys in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what is covered elsewhere
2. Stop, selection, waiting; activation, cloak and the standing settings
3. Self-destruction, paralysis and carriage
4. Stationary combat and unit-type targeting (CONFLICT on Guard_NoMove)
5. Movement, patrol and kamikaze
6. Chasing and suppression
7. Factory and shipyard construction
8. Mobile construction and assistance (path failure; the footprint radius)
9. GetBuilt, the rally route and parking
10. Stockpiled weapons
11. The three repair roles
12. Capture
13. Live-unit and feature reclaim
14. Resurrection (the tail)
15. Repair patrol and scavenging
16. Standby, mines and parking
17. Escort: the ground guard (FollowGround)
18. Ground transport and teleport
19. Registration, overlays and persistence
20. Gaps to fix

---

## 1. Shape and what is covered elsewhere

**Reference (sections 1, 21).** Fifty annotated functions, 23 common and 22 ground
registrations. A handler receives the unit, the order and the delivered event bits;
the order keeps a state byte, a linked target, an optional position, a start X/Z and
three integer slots that count, time, progress or radius depending on the handler.
Deadlines are simulation ticks. Weapon claiming and releasing is how an action owns
a weapon; assigning a target is not a promise of a shot.

**RWE. DIVERGENCE (structural), documented.** One `handleOrder`
(`UnitBehaviorService.cpp`, about 2418) matches on a variant and calls one handler per
tick on the front order; there is no state byte, no wake mask and no same-tick re-run
(`BT-ORDERS-ORDER-LIST-PARITY.md` rule 6). Progress lives in `behaviourState` or on
the order record. Weapon "claiming" has no counterpart: an order sets and clears the
unit's weapon targets (`setWeaponTarget`, `clearWeaponTargets`) and the weapon pass
owns the rest. Several rules below turn on this: where the original frees a weapon
slot for the unit's own targeting and RWE forces the slot, the difference is the
absence of the claim.

## 2. Stop, selection, waiting; activation, cloak and the standing settings

**Reference (sections 2, 3).**

- **StopOrder** announces, clears all three weapon targets and, for a flying unit in
  flight mode 2, prepends a landing at its current position. It does not itself move
  an airborne model to the ground.
- **MakeSelectable** clears runtime bit 0x8000 and sets 0x20. **WaitOrder** with a
  radius asks for visible enemies around the unit and ends on any; otherwise a
  non-positive remaining time ends it, and a positive one is reduced by a random
  150..179 ticks that becomes the next deadline, so the wait can overshoot.
  **WaitForAttack** ends when its target disappears and does not scan. **QMove** and
  **QPatrol** wait 60 ticks and return 6: they are rally metadata, not movement.
- **Activate/Deactivate** act only with the onoffable bit. **CloakOn/CloakOff** act
  only with flags2 bit 13, derived from a positive cloak cost. **StandingMoveOrder**
  writes runtime bits 18..19 and **StandingFireOrder** bits 20..21 (and, for values 0
  and 1, clears targets on weapon slots carrying flag 0x10). The chapter notes that
  other provisional views label the two pairs the other way round.

**RWE.**

- **Stop: MATCHES for the order list, DIFFERS for aircraft in a search circuit.**
  `PlayerUnitCommand::Stop` (`PlayerCommandApplication.cpp`, about 377) erases the
  unit's remaining mission script, interrupts the task and calls `clearOrders`,
  which clears the weapon targets (`UnitState.cpp:455-462`). That is the original's
  flush. The landing half is the idle branch of `update` (about 395-515): an
  aircraft with an empty list lands, **except** that the branch that flies the
  "attack ended" search circuit (about 447) keys on `UnitState::airLoiter`, and
  neither `interruptCurrentTask` nor `clearOrders` clears it. The only things that
  clear `airLoiter` are issuing some other order (`handleOrder`, about 2442), the
  idle landing branch itself and a few pad-queue paths. **Read, not run:** a bomber
  circling its dead target that is then told to Stop keeps circling, where the
  original's StopOrder prepends the landing. The same unit stopped by running out of
  orders by any other route lands as it should.
- **Wait and WaitForAttack: MATCHES** where ported. `MissionScripts.cpp` (about 278-306)
  draws `150 + randomBelow(30)` per step, tests the budget before each step is taken
  off and looks at once on arrival (`TOTALA-EXE-DATA.md` section 105, which decoded the
  same routine). The chapter's remark that the coarse sampling overshoots is the
  register's "a wait can overrun by one step".
- **QMove/QPatrol as rally metadata: MATCHES in effect.** RWE keeps the rally as a
  `MoveOrder` or `PatrolOrder` on the factory's list; the default-action ladder
  issues it from the MOVE button alone (`DefaultAction.cpp`, about 345-355) and
  `TOTALA-EXE-INTERFACE.md` section 133 records the copy at completion.
- **Activation and cloak gates: MISSING in the simulation, by the class rule.**
  `GameSimulation::activateUnit` and `deactivateUnit` (about 2755-2768) and
  `setCloakRequested` (about 2986) do not test `onOffable` or the cloak cost; the
  interface offers the buttons only when the key is set
  (`OrderButtons.cpp:90-135`). `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 10 is the
  class finding; this chapter adds the two named bits. The "only when the byte
  changes" half is `BT-UNITS-UNIT-POSITION-PARITY.md` rule 4 and
  `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 3.
- **Standing-order bit pairs: MATCHES the register.** Move mode is bits 18-19 and fire
  mode 20-21 (`TOTALA-EXE-WEAPONS.md`, "Where both modes come from"); the chapter
  agrees. The naming disagreement it mentions is with other provisional views, not
  with the register.

## 3. Self-destruction, paralysis and carriage

**Reference (section 4).** SelfDestruct keeps a count in the upper nibble of its slot
as an "initialised" marker, seeds it from the definition's three-bit countdown, steps
one number a second with speech from a six-entry array, then waits a random delay below
15 and applies 30000 damage of kind 3 to itself. A nonzero done slot bypasses the
countdown; event 2 cancels with speech unless runtime bit 0x4000 is set. Values 6 and 7
of the three-bit field index past the array. Paralyze caps the total at 1800, claims
the weapons, clears targets, removes the movement destination and sleeps. BeCarried
ends when the carrier is gone and otherwise waits ten ticks at a time.

**RWE. MATCHES the register; the known remainder is recorded.**
`TOTALA-EXE-INTERFACE.md` ("Self-destruct shows nothing on the unit") and
`TOTALA-EXE-ECONOMY.md` ("`selfdestructcountdown`") decoded the count, the 30-tick step,
the six speeches and the `rand(15)` tail. RWE blasts at exactly the countdown with no
tail, no spoken numbers, and removes the unit with no corpse where the original's
cause-3 damage runs the `Killed` ladder (`GameSimulation.cpp`, about 1278-1345;
`TOTALA-EXE.md` section 91). The 1800 cap, the immunity bit and the added duration are
`TOTALA-EXE-DATA.md` (the paralyzer paragraphs) and are ported (`paralyzer.test.cpp`).
The chapter's "values 6 and 7 index past the speech array" is the register's. No shipped
unit names a countdown above 2 (the twelve mines are the only users, at 1 and 2).

## 4. Stationary combat and unit-type targeting

**Reference (section 5).**

- **AttackNoMove** ends on a missing target or mask 0x10808, claims the weapons, assigns
  slot 0 only and never attaches a path goal.
- **GuardNoMove** releases the weapons and waits 30 ticks, **then adopts slot 0's target
  if runtime bit 0x10000000 is present, remembers the position, claims ownership, and
  picks a persistence limit of 3..5 passes**. Event 0x4000 resets the counter; otherwise
  it increments. Within the limit and reach it waits with mask 0x7008. Later an **80 percent
  random branch restarts the target cycle**, and on interrupt 0x10008 it recovers to **a
  random visible enemy within 640 world units of the remembered position**. The chapter
  stresses this differs from a moving escort.
- **AttackUType** requires `canattack`, waits 1..90 ticks, scans every slot for the named
  type and a non-allied owner, scores squared horizontal distance minus a random amount
  below half of it ("preferring the last equal score"), prepends an attack on the winner
  and rescans; no winner ends it. It adds no sight, completed-build or live-slot check.
- **AttackSpecial** resolves the ordinary attack mode, merges the result and selects
  weapon index 2; it is a conversion, not a projectile.

**RWE.**

- **GuardNoMove: CONFLICT.** The register says `Guard_NoMove` (`0x4021F0`) frees all
  three weapons, sleeps 30 ticks and **never searches**
  (`TOTALA-EXE-WEAPONS.md`, the `DefaultMissionType` table; `TOTALA-EXE-MOVEMENT.md`
  section 138: "never polls: it sleeps 30 and re-reads, which is what makes it the
  hold-position idle"). The chapter describes a handler that tracks a target, has a
  persistence counter, a random restart and a 640-unit recovery search. Neither side is
  chosen. RWE follows the register: `DefaultMission::GuardNoMove` exists
  (`UnitDefinition.h`, about line 52) and an idle tower simply leaves its weapons to
  find their own targets (`update`, about 526-536). The effect of the conflict is
  confined to the 26 armed towers that name it, and to whether a tower ever
  *turns toward* or *holds* a target the weapon pass would have dropped; a tower cannot
  walk, so the 640 recovery can only re-aim. Needs a spec-room read of the handler.
- **AttackNoMove: MATCHES in effect.** An immobile attacker goes through `attackTarget`
  (about 3603), which sets the weapon target when the target is in range and otherwise
  asks the navigator for a goal that an immobile unit cannot use. **DIFFERS in one
  detail:** RWE assigns **weapon slots 0 and 1** (`attackTarget`, about 3669-3678) where
  the chapter assigns slot 0. Same note as rule 6 of the VTOL audit; for ground units it
  matters only for immobile attackers with two weapons, of which no count was taken, and none is known to have a
  second weapon that disagrees on target class (not checked per unit).
- **AttackUType: MATCHES the register, ported** (`MissionScripts.cpp`,
  `missionscripts.test.cpp` line 226 for the Immunity case). The score expression is the
  register's `d^2 - rand(d^2/2)`. **Not established** whether RWE prefers the last or the
  first of equal scores; with a random subtraction a tie is rare, and it is an
  iteration-order question for a hashed draw. The chapter's "no completed-build check"
  agrees with the register's "no sight, radar or Immunity test".
- **AttackSpecial: MATCHES** (`handleDgunOrder`, about 4802, citing `0x403190`).

## 5. Movement, patrol and kamikaze

**Reference (section 6).**

- **MoveGround** rejects a carried mover, attaches an approach circle with the order's
  radius **plus four** and watches 0xe0. Arrival announces completion and removes the
  order; any other wake returns 9.
- **AttackKamikaze** refreshes the cached position while the target lives, rejects a
  carried mover, approaches with radius at least 16 and otherwise the definition's
  kamikaze radius, waits 60 ticks and watches 0xe0. Arrival prepends SelfDestruct with
  the done slot set (immediate blast); **path failure returns 8**; a missing target
  alone keeps the last position; 0x10008 ends it.
- **Patrol** requires motion, ensures a return node, releases weapons, attaches a
  zero-radius destination and samples for attacks each pass; a hit suspends the patrol
  behind an attack; a miss sets a random 30..59-tick deadline.

**RWE.**

- **Move radius plus four: MATCHES the register** (`TOTALA-EXE-MOVEMENT.md` section 102,
  "Arrival at the goal", which also gives the radius-to-squares division). **RWE
  DIVERGES in arrival:** a ground unit's move is complete when it is **within eight
  world units** of the goal point (`hasReachedGoal`, `UnitBehaviorService_util.cpp`,
  about 1418-1500), where the original requires the unit's map square to equal the
  goal's (a radius of 4 divides to zero squares). `TOTALA-EXE.md` section 88 records the
  waypoint radius (16 and 8 against 5) but, as far as it was searched, not this final
  test. See `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 2.
- **Path failure and Move: DIFFERS.** In the original a failed search (zero waypoints,
  unit not at the goal) wakes the order with 0x40, and Move turns that into return 9:
  with another order queued it is deleted, otherwise it restarts after 30..59 ticks and
  retries for ever. In RWE a goal the search could not reach is "reached" once the unit
  has walked to the nearest point it could (`hasReachedGoal` using
  `reachableDestination`), so the order completes. For a queued order the outcomes agree;
  for a lone order RWE ends where the original keeps the unit in "Moving". See
  `BT-AI-PATHFIND-PARITY.md` rule 9.
- **Kamikaze: MATCHES** in trigger distance (`kamikazeRun`, about 3758: the larger of the
  definition's distance and 16, then `selfDestructUnit`, so the blast is the
  `SelfDestructAs` one). **DIFFERS** in three small ways: RWE tests Euclidean distance
  where the original's approach circle is a cell-squared test; RWE has no 60-tick wake
  and no path-failure exit (the unit walks the straight-line stand-in at an
  unreachable target for as long as the order stands); and RWE tests the kamikaze flag
  before the weapon test where the chapter places it later (`BT-ORDERS-ORDER-DISPATCH-PARITY.md`
  rule 3 already covers the ordering; the 14 shipped kamikaze FBIs name no weapon).
- **Patrol: MATCHES** (`BT-ORDERS-ORDER-LIST-PARITY.md` rule 8; `TOTALA-EXE-MOVEMENT.md`
  sections 86 and 138, where the poll cadence is "decoded, not ported, deferred on
  purpose").

## 6. Chasing and suppression

**Reference (section 7).**

- **AttackChase** ends on the commandfire event 0x800, on mask 0x10008, on a missing
  target or on exceeding the optional start-position leash. It initialises only with
  motion, a non-flying definition and runtime bit 0x80000000 (the definition names a
  weapon). It clears movement before testing reach. A reachable target is assigned after
  weapon claims and held with mask 0x13808. Otherwise it **searches for a stand position
  in stages, using the weapon's range R**: a full-range approach; randomised positions at
  range with a quarter-range acceptance; a half-range approach; the exact centre; ring
  radii R and R/2; ring radii 2R and R; then repeat. A vertical difference above eight
  world units skips the side approaches toward the half-range stage. After movement the
  0x40e0 wake returns to the reach test. The ring acceptance/heuristic disagreement of
  `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 3 applies to these radius pairs.
- **Suppress** attacks a fixed position, rejects flying definitions, stores the selected
  weapon's range, claims slot 2 (D-gun) or slots 0 and 1, waits with 0x1c00 and releases.
  A mobile unit with positive remaining radius approaches, decreases that radius by a
  random amount below one third of the weapon's range and resumes firing.

**RWE.**

- **Stand position: DIFFERS.** `attackApproachGoal` (about 3301) hands the navigator one
  stand-off point on the line from the target to the attacker, at the larger of "weapon
  range minus 8" and "footprint half-extent plus 8", cached until the target moves 8
  units (`UnitBehaviorService.cpp:61-80`). The register decoded only the first stage
  ("closes to the weapon's full range, halved and then reduced only after repeated
  failures, sub-states 5-8", `TOTALA-EXE-WEAPONS.md` section 89 area). The chapter gives
  the whole ladder. RWE has **no staged fallback at all**: an attacker that cannot get
  a shot from the first point stands there. What triggers the original's step from one
  stage to the next (failed reach samples, the vertical rule) is not stated precisely in
  the chapter. **UNCLEAR** as to behaviour; the staging exists in the original and not
  here.
- **Leash and commandfire end: MATCHES** (`handleAttackOrder`, about 3223, and the D-gun
  path of `TOTALA-EXE-WEAPONS.md` section 89).
- **Suppress: DIFFERS in the approach, MATCHES in the test.** A ground unit ordered at a
  bare point walks straight at the point until the flat range test passes
  (`attackTarget`, about 3680). The shrinking-radius approach is not ported. Aircraft are
  excluded in both. The D-gun's slot 2 is `handleDgunOrder`.

## 7. Factory and shipyard construction

**Reference (section 8).** BuildingBuild waits for runtime 0x20000000, checks the queue
count, activates, waits for the build stance, asks the script (`QueryBuildInfo`) for the
build piece and tests the product footprint at the piece's world position using the
yard's runtime mode. **Blocked placement retries after 15 ticks; allocation failure
after 300.** A new unfinished target attaches to the piece (flag 1), takes both standing
fields and receives `GetBuilt`. Work is integer worker time over 30, as a float; a
successful request emits six nano particles. Cancellation (event 2) credits the integer
part of `(1 - buildLeft) x metal`, scaled 0.5 or 0.7 for the active AI type 2 on
difficulty 0 or 1, and damages the frame by 30000 of kind 9; it is **not** an energy
refund. Event 8 announces stopped construction and decrements the count.

**RWE. MATCHES the register on every point it covers.** The factory state machine is
`handleBuild` (about 5289-5480) and is the subject of `TOTALA-EXE-ECONOMY.md` sections
110-112 and 134 (blocked pad: silent 15-tick wait; the refund; the handicap on the
credit; the death of the product flushing the list). The 300-tick wait on allocation
failure is the open row **BT-18** (unreachable below the unit cap). Nothing new here.

## 8. Mobile construction and assistance

**Reference (section 9).**

- **MobileBuild** snaps the site to the footprint grid and attaches a **border**
  destination. **A path failure is tolerated if the distance minus both footprint
  half-diagonals is within the build range; otherwise the site is unreachable.** Blocked
  placement announces once, retries every 30 ticks and fails after more than ten prior
  retries. Clear placement claims weapons and creates the frame; allocation failure waits
  300. Work is integer worker time over 30, successful six-particle requests, a work
  deadline of tick plus 300 and one-tick waits. Event 2 completes and event 8 fails;
  neither uses the factory refund.
- **HelpBuild** requires motion and the builder bit. The target-footprint radius is
  computed as the integer of `sqrt(X*X + Y + Y)` times 16, halved: **the second term is
  not squared**, which the chapter says is not a two-squared-axis formula. Ring radii
  are build range plus that radius, and that radius. Path failure reports unreachable;
  an already complete target ends the assist.

**RWE.**

- **Blocked-site ladder: MATCHES** (`retryBlockedSite`, `GameSimulation.cpp:4887`,
  citing `TOTALA-EXE-ECONOMY.md` section 112).
- **Path failure at the site: DIFFERS, and RWE says so deliberately.** `createNewUnit`
  (about 5829) asks the navigator to take the builder to the site's footprint; if the
  navigator reports arrival *because the search could only get as near as it did*
  (`reachableDestination` set) the order **fails with "Target area was blocked"**,
  with the comment "give the order up rather than lathing across a wall". The chapter's
  rule is the opposite: when the unreachable site is within the build range measured
  from the footprint edges, the failure is **tolerated** and the frame is placed. This
  is the Total Annihilation habit of building across a river, a cliff or a wall that
  fits in the arm's reach. RWE's rule refuses all of those regardless of distance. The
  same note applies to HelpBuild and repair, whose navigation to the target
  (`buildExistingUnit`, about 6006) uses the build range already and has no
  "unreachable" refusal. **Needs an original check** because the chapter's
  tolerance is a static reading; see Gaps rank 2.
- **Approach rings: DIFFERS in form.** RWE measures reach to the nearest point of the
  target's footprint rectangle against `BuildDistance` (`withinBuildReachOfRect`,
  about 2500) and navigates to the rectangle expanded by the reach in cells. The
  original uses a ring pair with the formula above. For a one-tile target the figures
  are close; for a large target (a factory) the original's radius is not the footprint
  half-width but the odd mixed expression. **UNCLEAR** whether the quirk is the chapter's
  mis-transcription or the original's; it changes the stand-off by a few world units,
  not a rank-one gap.
- **Assist ends at completion: MATCHES.** Event bits and speech are the register's
  (`TOTALA-EXE-ECONOMY.md` section 110).

## 9. GetBuilt, the rally route and parking

**Reference (section 10).** GetBuilt runs on the product and links back to its builder.
While unfinished it claims weapons, waits 300 then 30 ticks, then repeatedly; event 0x8000
schedules 30 ticks and event 1 schedules 11 and applies unfinished-build decay for 11
ticks. At zero build left it marks selection dirty; a motion-enabled product walks the
builder's primary list and dispatches each QMove/QPatrol position into its own concrete
movement or patrol action (a ship, an aircraft and a vehicle get different orders from
one marker). If both units are Ready it copies the standing fields; for the local
player's type 1 it copies the group value directly. If no movement was copied it assigns
**Park**. The chapter (section 18) describes Park: for a motion unit, a flying definition
becomes a current-position VTOL move; others attach **a rectangle around their position**,
sized from the footprint X plus three when the signed height field is non-negative, with
extent "size times eight by size times six cells, starting size times four over three
behind the current cell"; arrival or another queued node ends it, otherwise it waits 30
and resets; the marker accepts a border.

**RWE. MATCHES the register's section 133 and PORTING-125-138.md**; the nanoframe decay is
`nanoframedecay.test.cpp`. Park is the one open item: `TOTALA-EXE-INTERFACE.md` section 133
says "its handler was not read here" and RWE uses `BuggerOffOrder` as the stand-in
(`UnitBehaviorService.cpp`, about 5409-5413, a rectangle grown by `3 x footprint - 4`).
The chapter now supplies a rectangle, but its units ("cells") and the "size times eight
by size times six" extent are not reconcilable with a 16-unit cell and a 3-by-3
footprint (that would be a rectangle 24 by 18 cells), so the numbers cannot be used as
written. **UNCLEAR**; a spec-room read of the routine settles the extent. Not a rank-one
gap: the effect is only how far a product walks to clear the door.

## 10. Stockpiled weapons

**Reference (section 11).** BuildWeapon uses the selected slot, a count and a progress
tick count. Stockpile at least 200 waits 300 ticks. A round advances by five and is capped
at the weapon's build time; each step charges the difference of truncated cumulative
energy and metal costs; both must be granted before progress changes; failure retries in
ten ticks, a partial success waits five; completion increments the stockpile and decrements
the queue. Build time is a divisor with no zero guard.

**RWE.** Covered: `TA-SYSTEMS-PARITY.md` rule 27 for the cost and rate, and
`BT-ORDERS-ORDER-LIST-PARITY.md` rule 9 for the queue percentage. RWE advances the
stockpile from the weapon pass (`updateWeaponStockpile`, about 948), not from a secondary
order, so there is no secondary-list node to compare (`BT-ORDERS-ORDER-LIST-PARITY.md`
rule 7). Nothing new.

## 11. The three repair roles

**Reference (section 12).**

| Handler | Relationship | Requirements |
| --- | --- | --- |
| SelfRepair | linked builder repairs the executing unit | completed linked builder, active executing unit, no approach |
| RepairUnit | executing builder repairs the target | motion, builder bit, completed mode-1 target, optional leash |
| RepairUnitNoMove | executing builder repairs the target | builder bit, completed target, active executing unit, no approach |

All three use integer worker time over 30 from the actual builder. A successful repair
step requests six nano particles; a failed one retries without them. The work deadline is
tick plus 150 on the repaired unit for SelfRepair and on the builder for the others. Mobile
repair subtracts both footprint half-diagonals from the distance; out of range it attaches
a footprint destination, watches 0xe8 and waits 30..59; in range it claims weapons and
waits for the stance; a moving target (runtime 0x0c) stops the animation, waits 15 and
restarts the approach. No-move repair advances toward completion on that mask.

**RWE.**

- **SelfRepair and the pad: MATCHES the register** (`TOTALA-EXE-ECONOMY.md` section 94
  area, "SELFREPAIR, and the repair tick every repairer in the game shares"):
  the pad is the worker, at the pad's `WorkerTime / 30`, and the aircraft's own order
  only watches its hit points (`handleLandOnAirBaseOrder`, about 2260-2410;
  `findAircraftToRepairOnPad`, `UnitBehaviorService_util.cpp`, about 380).
- **RepairUnitNoMove: MISSING as a separate handler, harmless.** The two shipped
  immobile builders are `ARMASP` and `CORASP`, the air repair pads, and the pad path
  covers them. A mod's immobile repairing tower would meet the `repairExistingUnit`
  navigation (`about 5253`), which would try to walk an immobile unit; not checked.
- **Moving target restarts the approach: MISSING.** The `0x0c` wait is the same one the
  register lists for capture ("State 4 also refuses to work while `target+0x110 & 0xC` is
  set", `TOTALA-EXE-ECONOMY.md` section 96, which also notes those two bits are unexplained).
  RWE keeps repairing a target that moves while it is in reach and navigates when it drifts
  out. Low impact.
- **Repair target gate: see `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 4** (the `canreclamate`
  copy against RWE's `builder`).

## 12. Capture

**Reference (section 13).** Capture requires motion and `cancapture`, and rejects a target
that itself has `cancapture` ("cannot be captured"). Unfinished targets, missing targets and
0x10008 fail. The base duration is `energy x 30 / 2000 + metal x 30 / 140 + 150`, integer,
capped at 1800, then scaled by `(health + maxHealth) / (2 x maxHealth)` and by
`(kills / 5 + 10) x 10 / 100`. The loop emits six reverse particles every two ticks,
increases elapsed by two and sets a work deadline of tick plus 900; a moving target with
runtime 0x0c stops the animation, waits 30 and resets. At the full duration it gives the
unit to the player without checking a status; the progress loop charges nothing.

**RWE. MATCHES.** `TOTALA-EXE-ECONOMY.md` section 96 decoded the same routine to the
instruction, including the float chain (which the chapter's integer wording reproduces),
the nanoframe refusal, the kill ceiling and "workertime is not in it", and RWE's
`computeCaptureTime`/`CaptureOrder` carry it. The `0x0c` wait is the one deliberately
unported piece. The armed-click question (an ally can be captured) is
`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 6 and the spec-room row **BT-72**.

## 13. Live-unit and feature reclaim

**Reference (sections 14, 15).**

- **ReclaimUnit** requires motion, `canreclamate` and the reclaim test. It computes a damage
  pulse for 15 ticks, starts the animation and waits for the stance. Work requires the
  squared flat distance within **`(build range + target radius)^2`** and a renewed reclaim
  test; at accumulated duration at least 15 it applies the pulse as kind-5 damage; two-tick
  intervals can cross the threshold at 16. Each valid interval requests six reverse
  particles, sets the work deadline to tick plus 900 and charges no resource itself.
- **Reclaim** (features) resolves the feature every call; a missing feature or one without
  the reclaimable bit fails. Duration is integer `15 + 0.5 x (metal + energy)`, decremented
  by two every two ticks; while more than 15 remain it requests two sets of six reverse
  particles; work deadline tick plus 300; expiry calls the feature reclaim once. Worker time
  does not scale it.

**RWE.**

- **Live-unit pulse and cadence: MATCHES** (`BT-ORDERS-ORDER-QUEUE-PARITY.md` rule 5;
  `TOTALA-EXE-ECONOMY.md` section 97; the sixteen-tick cadence and the factor 300).
- **Feature duration: DIVERGENCE (documented).** RWE's feature reclaim work is
  `metal + energy + hitPoints/4` at the builder's worker time per tick
  (`computeFeatureReclaimWork`, `GameSimulation.cpp`, about 444), which
  `TOTALA-EXE-ECONOMY.md` section 97 records as a deliberate difference from the flat
  `15 + (metal + energy)/2` that does not scale with worker time. The chapter repeats the
  original's figure and adds nothing for the ground handler. `BT-MAP-FEATURES-PARITY.md`
  rule 7 covers the payout timing.
- **The "+ target radius" term: CONFLICT with the register.** The register reads the
  live-unit reach as the builder's `BuildDistance` and notes that the original also adds a
  term off the target's definition ("read at that one instruction and written nowhere else
  in the binary, so it contributes nothing", comment in `reclaimTarget`, about 5897-5950).
  The chapter calls that term the target radius and contrasts it with the aircraft handler,
  whose range is "build range only". If the field is never written, both statements
  describe the same reach; if it is the chapter's radius, a ground reclaimer reaches
  further than an air one. Needs a spec-room read of which definition field is read.
- **Reclaim reach for features** is RWE's own approximation (`reclaimTarget`, comment at
  about 5944-5950), recorded in the code.

## 14. Resurrection (the tail)

**Reference (section 16).** Resurrect resolves a reclaimable feature through states 0..5,
needs motion and `canresurrect`, approaches and waits for the stance. It copies a 64-byte
feature name, truncates at the first underscore and resolves that prefix as a unit
definition; a missing definition fails. Preparation time is
`target buildTime x 0.3 / (workerTime / 30)` with the inner division integer and unguarded
against zero; it requests six forward particles a tick and sets the work deadline to
tick plus 300. It creates the unit at the order position for the resurrector's owner
(allocation failure waits 300 and retries), validates the origin feature cell, copies the
feature's rotation, removes the feature and, in game type 3, broadcasts a six-byte removal
packet. The new unit is complete **with health 1**. **A later state announces and
dispatches mode 8 against the new unit, prepending repair or service when available.**
The creation precedes the cell validation, and a failure there does not delete the unit.

**RWE.**

- **Everything but the last two items: MATCHES the register** (`TOTALA-EXE-ECONOMY.md`
  section 98, "Resurrect"): the name rule, the time formula (worker time *does* scale this
  one, `handleResurrectOrder`, about 4526-4640), the complete unit at one hit point
  (`GameSimulation::spawnNewUnits`, about 5060-5105).
- **The auto-repair after raising: MISSING, and the register is silent on it.** The register
  stops at "Resurrection complete" (the announcement) and says "it has to be repaired
  afterwards". The chapter says the order itself then asks the mode-8 dispatcher for a
  repair of the new unit and prepends it. Shipped data: the only unit that sets
  `canresurrect` is `CORNECRO` (and the Core Contingency resurrection sub, if present);
  in RWE a raised unit sits at 1 hit point until someone repairs it, and the resurrector,
  on completion, goes idle.
- **The 300-tick retry: DIFFERS, documented in the code.** RWE deletes the corpse first and
  ends the job if the spawn then fails (`spawnNewUnits`, about 5080-5090); the original
  keeps the corpse and retries. Reachable only at the unit cap (the open row **BT-18** is
  the same condition).
- **The cell-validation leak: do not port** (`docs/SECURITY-AUDIT.md` spirit; nothing in RWE
  creates a unit it then abandons).
- **No worker time: RWE returns finished** rather than dividing by zero
  (`handleResurrectOrder`, "a unit that cannot work cannot raise anything"). A safe
  departure.

## 15. Repair patrol and scavenging

**Reference (section 17).** RepairPatrol remembers its linked target's position, ensures its
return node, approaches with radius 16, waits 60 and watches 0xe0. At least 20 percent
stored energy permits a scan within the definition's range for damaged or unfinished
allies, picking one at random; normal mode-8 dispatch still gates the inserted work. The
collector excludes self, non-allies, modes other than 1, completed full-health units and
units last damaged by this owner with kind-5 reclaim damage. Below 20 percent energy or
metal the patrol looks for reclaimable features: metal if metal-low, energy if energy-low,
then metal whose full yield fits, then energy whose yield fits; it clears movement and the
mask and prepends Reclaim.

**RWE. MATCHES the register and the port record.** `TOTALA-EXE-ECONOMY.md` section 131
decoded both routines (ground 60 ticks, radius 16; the ground reclaim scan only below
20 percent, side length equal to sight; the best-of-three sampling on a 6-by-6 grid) and
`PORTING-125-138.md` records the port (`runRepairPatrolDecision`, about 5015;
`RepairPatrol.cpp`). The chapter's "within definition range" is the register's "sight
distance". The chapter agrees with the register that the collector excludes the owner's
own reclaim victims. The air variant's differences (scavenging independent of the 20
percent test, a 240-unit side) are `BT-ORDERS-VTOL-ORDERS-PARITY.md` rule 16.

## 16. Standby, mines and parking

**Reference (section 18).** Standby requires motion, releases weapons and waits a tick with
0x10000; later fire-at-will lookup can insert an attack and end standby, otherwise it
polls every 30..59 ticks. StandbyMine instead requires runtime 0x20000000 (the building
bit) and, "with a mode-1 candidate and a **nonzero 0x300000 standing mask**", prepends an
immediate SelfDestruct.

**RWE. MATCHES the register** (`pollMine`, about 4711: ground-standing target, poll at
`30 + rand(30)`, the definition's own countdown). One wording difference: the chapter
words the fire-mode condition as "nonzero", the register's search routine does nothing
unless the fire mode is exactly Fire At Will (value 2). If the mine's body tests "nonzero"
*in addition* to the search, the two readings agree; if the body's test is the only one,
a Return-Fire mine would go off. Shipped mines: `ARMMINE6` and `CORMINE6` name
`StandingFireOrder=0` and have the button; nothing else is Return Fire. **UNCLEAR**,
mod-visible only. Parking is rule 9.

## 17. Escort: the ground guard (FollowGround)

**Reference (section 19).** FollowGround ends on a missing target, rejects carried
followers and flying targets, releases weapons, and picks a random offset with spacing
`(follower footprint X + target footprint X + 2) x 16`. Each refresh approaches the guarded
unit's current position plus that offset with half-spacing radius, deadline 30 and mask
0x18. It **reacts to the guarded unit's attacker** on event 0x10 when the attacker's
alliance table does not list the follower's owner and the follower's exclusion bitset
lacks that definition: an attack order with override first; failing that, with nonzero
runtime 0x300000 it retargets individual weapon slots (flags 2 and 0x10, without the
weapon-definition bit 26) if empty, unreachable or excluded. **A builder follower can
interrupt to repair a damaged guarded unit** through mode 8; with both units builders and
guarded action flag 0x100000 it **copies the guarded unit's actionable work** (build
orders become HelpBuild; other work is copied target and position); it avoids assisting an
action aimed back at itself; temporary work is prepended while the guard stays resumable.

**RWE.** `handleGuardOrder` (about 4411-4478).

- **Allied target: MISSING, and visible.** The simulation ends the order at once if the
  guarded unit is not owned by the follower's player (`target->get().isOwnedBy(...)` with
  the comment "TODO: real allied check here"), while the default-action ladder offers
  Guard on any allied unit (`DefaultAction.cpp`, about 145: `allied && canGuard`, and
  the register's `TOTALA-EXE-INTERFACE.md` lines 1322 and 1355 give the original an
  *ALLIED* test). So right-clicking a teammate's unit with a guard-capable unit shows the
  guard cursor, issues the order, and the order evaporates on the next tick. A builder
  told to guard a teammate's factory therefore also never assists it. `canguard` is set
  on 150 of the FBIs in the census, so the case is common. (`isOwnedBy` is a plain owner
  comparison, `UnitState.cpp:423`.)
- **Retaliation on the guarded unit's attacker: MISSING.** RWE's guard only builds, or stays
  within 200 of the target. The register's `VTOL_Follow` decode (`TOTALA-EXE-MOVEMENT.md`
  section 13) already lists the aircraft half as decoded and not ported; the chapter shows
  the ground half has the same shape.
- **Repair of a damaged guarded unit: MISSING.** RWE's builder-guard assists construction
  (frames and a factory's product) and does nothing for a damaged finished unit.
- **Copying the guarded unit's reclaim, repair or capture: MISSING**, as the register
  already says for the aircraft (`TOTALA-EXE-MOVEMENT.md` section 13, "Decoded here and
  deliberately not ported").
- **Spacing and the random offset: DIFFERS.** RWE walks to within 200 of the target and
  stops; the original keeps a random point at a footprint-derived distance and re-aims it
  every 30 ticks. The 200 is RWE's own (`handleGuardOrder`, about 4471).
- **Flying guarded unit: the chapter rejects it for ground followers.** RWE navigates a
  ground guard toward a flying target (`navigateTo(unitId)`); not checked what the unit
  then does.

## 18. Ground transport and teleport

**Reference (section 20).** GroundPickup requires motion and `canload` and compares the
target's footprint X with the transport's capacity ("not an area comparison"); it starts
the script, waits 15 ticks, and counts any non-null carrier on the target as success
("not necessarily this transport"); at most three attempts approach the target exactly
between retries. GroundUnload takes the cargo head, starts the drop with the packed X/Z,
waits 15 and succeeds when the cargo's carrier differs from this unit; three attempts then
return 9; a hover definition's approach radius is `height x 1.5`. It drops one cargo-head
per action. Teleport scans the whole unit array for units inside the teleporter's bounds,
preserves each offset at the destination, requests 30 particles and sets the position with
flag 1; it has no alliance, completion or passability test.

**RWE.** Pickup and unload are `TOTALA-EXE-TRANSPORTS.md` sections 34, 35 and 40, which
decoded the same two handlers (three attempts, the 1.5 hover tolerance, "any carrier is
success"); RWE implements them in `handleLoadOrder` (about 2901) and `handleUnloadOrder`
(about 3072), with the two documented departures (the crane reach and the self-attach
fallback, `TOTALA-EXE-INTERFACE.md` line 1680). **Teleport: MISSING by design**, row
**BT-69** is resolved: the key has no reader and no arming site issues the mission.

## 19. Registration, overlays and persistence

**Reference (section 21).** 23 common and 22 ground registrations, shared callbacks
explaining the count; overlays are command UI separate from nano particles; handlers have
no serializer of their own and the order persistence records type, state, reused fields,
target and destination; four unit-pointer vector helpers do not delete units.

**RWE. Not applicable.** No registry; the order variant is saved by `save_util_orders.cpp`
and hashed by `GameHash_util.cpp`. Overlays are drawn by the scene. The chapter's literal
quirks list (naming, capture refusing a capturer, the assistance footprint formula, the
integer work rates, the limited speech array, resurrection's validation after creation) is
covered above or in the register.

---

## 20. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | A ground guard on an **allied** unit ends at once; the UI offers it. Make the guard follow any friendly unit and keep the builder assist working on a teammate's factory. Rule 17 | `src/rwe/sim/UnitBehaviorService.cpp` (`handleGuardOrder`, the `isOwnedBy` test), `src/rwe/sim/guardassist.test.cpp` | No for the allied half: the register already gives the original an *ALLIED* test (`TOTALA-EXE-INTERFACE.md:1322,1355`). Use the lobby team relation (`arePlayersAllied`) as the existing documented divergence. |
| 2 | A mobile builder whose site the search cannot reach but which is within build range of must still build; today it fails with "Target area was blocked". Rule 8 | `src/rwe/sim/UnitBehaviorService.cpp` (`createNewUnit`, the `reachableDestination` refusal), `GameSimulation.cpp` (`retryBlockedSite` unaffected), `unitnotifications.test.cpp` | **Yes.** A builder on one bank of a stream, a site on the other within the arm's reach; and the same on a cliff. The chapter's tolerance is a static reading. |
| 3 | Resurrection should end with the resurrector repairing the unit it raised (the unit arrives at one hit point). Rule 14 | `src/rwe/sim/GameSimulation.cpp` (`spawnNewUnits`, the resurrecting branch), `UnitBehaviorService.cpp` (`handleResurrectOrder`), `resurrect.test.cpp` | Light: watch a Necro after a raise (`CORNECRO` is the one shipped resurrector). The register is silent, so this needs a spec-room read of the state that follows the announcement. |
| 4 | Stop does not end an aircraft's search circuit; the original's StopOrder prepends a landing. Rule 2 | `src/rwe/game/PlayerCommandApplication.cpp` (the `Stop` arm), `UnitBehaviorService.cpp` (`interruptCurrentTask`, `UnitState::airLoiter`), `aircraftidle.test.cpp` | No: a read-only observation of the code path (clear `airLoiter` on stop), confirm with a one-aircraft scenario. |
| 5 | Guard_NoMove: the register and the chapter disagree on whether the handler searches. Rule 4 | none until answered | Spec-room read of `0x4021F0`. Affects the 26 armed towers that name it. |
| 6 | Guard retaliation, damaged-unit repair and copied work for a ground guard. Rule 17 | `UnitBehaviorService.cpp` (`handleGuardOrder`), `DefaultAction.cpp` unaffected | No for the shape; the aircraft half is already a register entry. A play-test with a constructor guarding a damaged unit confirms. |
| 7 | AttackChase's staged stand positions and Suppress's shrinking approach. Rule 6 | `UnitBehaviorService.cpp` (`attackApproachGoal`, `attackTarget`) | Spec-room read of what advances the stage, then a unit that cannot get a shot (a target behind a ridge). |
| 8 | Final-goal arrival: square equality in the original, eight world units in RWE. Rule 5 | `UnitBehaviorService_util.cpp` (`hasReachedGoal`) | Not before `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 2; hashed, replay gate applies. |
| 9 | Kamikaze path-failure exit and 60-tick wake; unreachable Move order keeping the unit in "Moving". Rules 5 | `UnitBehaviorService.cpp` (`kamikazeRun`, `handleMoveOrder`) | No (static). Needs `BT-AI-PATHFIND-PARITY.md` rule 9 first. |
| 10 | Park's rectangle, repair of a moving target, the 0x0c waits. Rules 9, 11 | `UnitBehaviorService.cpp` | Spec-room read for the Park extent. |

### Documented divergences this audit leaves alone

The one-handler-per-tick executor; the lobby-team alliance; feature reclaim's `metal +
energy + hitPoints/4`; the self-destruct tail and corpse (section 91); the crane reach and
the self-attach fallback; the stockpile advanced from the weapon pass; the nanoframe
refusal and the unported capture `0x0c` wait.

### Conflicts with existing RWE findings, in one place

1. **`Guard_NoMove`.** Register: frees the weapons, sleeps 30 ticks, never searches.
   Reference: tracks a target with a 3..5-pass persistence limit and a 640-unit recovery
   search. Open until a spec-room read.
2. **Reclaim reach "plus target radius".** Register (comment in RWE, from the
   disassembly): the extra term is never written and contributes nothing. Reference: the
   target radius, contrasted with the aircraft handler's "build range only". Open.
3. **Resurrection's last state.** Register: announces and ends. Reference: also asks the
   mode-8 dispatcher to repair the new unit. Open.
4. **`StandbyMine`'s fire-mode word.** Register: the search requires exactly Fire At Will.
   Reference: the body tests a nonzero mask. Probably compatible; recorded as wording.
5. **A stale line in the register** (not a reference conflict): `TOTALA-EXE-ECONOMY.md`
   section 98 says no FBI sets `CanResurrect`; the census has `CORNECRO` setting it.
