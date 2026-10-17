# Aircraft order state machines parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-ORDER-DISPATCH-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for what the
aircraft order handlers do once the order exists (landing, search circuits, escort,
patrol, bombing and strafing runs, air combat, hover attack, airborne building,
reclaim and repair, pickup and unload), per `AGENTS.md` and ADR-0004. The execution
contract is `BT-ORDERS-ORDER-LIST-PARITY.md`; the click-to-order ladder is
`BT-ORDERS-ORDER-DISPATCH-PARITY.md`; the common and ground handlers, including the
ground halves of escort, reclaim, repair patrol and transport, are
`BT-ORDERS-UNIT-ORDERS-PARITY.md`; destinations and arrival are
`BT-ORDERS-ORDER-TARGETS-PARITY.md`; the flight model (acceleration, turn limits,
brake) is `BT-UNITS-UNIT-MOTION-PARITY.md`. This audit restates none of them.

The register already carries most of what the chapter says about aircraft, because
`TOTALA-EXE-MISSIONS.md` was read from the same handlers. The rules below therefore
sort into four kinds: the chapter agrees with the register (one line and a pointer);
the chapter adds a detail the register did not record; the chapter and the register
disagree (**CONFLICT**); and RWE departs from both.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-vtol-orders.md`).
  No source, no decompilation and none of its source links were opened. The
  spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`)
  was read read-only so that resolved rows are not re-raised; the rows this audit
  touches are BT-18 and BT-69.
- **Evidence class.** A **static reading**; nothing was executed. Weaker than
  `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements are
  marked **CONFLICT** with no winner picked.
- **No executed observation was used.** The Gaps table says which items need a play
  of the original (an aircraft is the case where a play settles most).
- **The RWE half** is a read-only audit of `src/rwe/sim` at the working tree of
  2026-10-16. Nothing was built, run or changed. Another agent had uncommitted edits
  in `UnitBehaviorService.cpp`, so every line number is "about".
- **A census of the shipped FBIs** (the scratch census the earlier audits used)
  supplies counts. It is a count of keys, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape: orders choose objectives, motion supplies flight
2. Climb preparation and the capability gate
3. Boundary recovery
4. Landing on terrain, idle and standby
5. Move
6. Search circuits: SeekAttack and SeekGuard
7. Escort: VtolFollow
8. Patrol
9. Landing pad lookup and landing
10. Pickup and unload
11. Interruption masks and target recovery
12. Weapon slots
13. Bombing run
14. Ground strafing
15. Air combat and evasion
16. Hover attack
17. Airborne building
18. Air reclaim
19. Air repair and the pad wait
20. Air repair patrol and scavenging
21. Literal faults that are not to be ported
22. Gaps to fix

---

## 1. Shape: orders choose objectives, motion supplies flight

**Reference (section 1).** These handlers attach a PathOrder or an AirManeuverOrder
destination, claim or release weapons, call the COB script, schedule waits and insert
temporary work; the steering module then moves the aircraft. Durations are ticks.
Prepend means "do this first, resume afterwards"; append means recovery work.

**RWE. DIVERGENCE (structural), documented.** An aircraft's front order is handled once
a tick by `handleOrder` (`UnitBehaviorService.cpp`, about 2418), and the flight itself is
a variant, `AirMovementState` (Flying, TakingOff, Landing, AttackRun, HoverAttack,
Dogfight) in `AirMovement.cpp`, with a second variant, `UnitState::airLoiter`
(`AirLoiterState`, reasons AttackEnded, Guarding, IdleHop, Patrol, PadQueue), for the
search circuits. The original's pattern of a handler re-attaching a fresh destination
every few ticks becomes, in RWE, a mover that holds one goal and an order that reads the
mover's state. Where the chapter gives a distance, a radius or a wake mask, RWE has the
distance as a constant in `AirMovement.h`/`UnitBehaviorService_util.cpp` and no wake mask
(`BT-ORDERS-ORDER-LIST-PARITY.md` rule 6).

## 2. Climb preparation and the capability gate

**Reference (section 1).** Almost every initialisation requires a motion object and
definition `canfly` (bit 11), independent of `canhover`, `hoverattack` and whether the
craft is landed. **PrepVtolClimb** claims weapons, detaches a carrier, activates selector 1
and, for motion mode 1, switches to mode 2 and attaches a current-position destination at
half cruise altitude.

**RWE. MATCHES in effect.** `TakingOff` is the climb (`UnitBehaviorService.cpp` about
2476-2590) and the flight mode is `UnitState::flightMode` equivalent in the air state;
the release from a pad or carrier is the pad-release path
(`UnitBehaviorService_util.cpp`, air-base helpers about 332-500). The half-cruise climb
altitude is not separately modelled: RWE climbs to the cruise altitude and the mover
converges on it (`computeNewAirUnitVelocity`). Not a player-visible difference.

## 3. Boundary recovery

**Reference (section 2).** LandIfCan, Follow, SeekAttack, SeekGuard, AirToAir and
AirToGroundHover detect the overflow spatial bucket (a position outside the map
partition) and construct a destination **800 world units toward map centre, radius 128,
mask 0xe0**, retaining their state. AirToGround installs a 30-tick deadline and switches
to attack state 2 instead. AirStrike has no such branch.

**RWE. MISSING, probably unreachable.** RWE has no overflow bucket; an aircraft that
leaves the map is a clamped position, not a state the orders react to. Whether a
circuit or a run can carry an aircraft off the edge depends on the mover, not on the
order; `BT-UNITS-UNIT-POSITION-PARITY.md` is the place to look. Recorded because the
chapter makes the point that the recovery is not universal, which matters only if an
original edge behaviour is ever ported.

## 4. Landing on terrain, idle and standby

**Reference (section 3).**

- **LandIfCan** ends at once if another order follows or on path failure 0x40. It keeps a
  supplied origin or uses the current position, picks a random orbit angle and prepares
  climb. Its search tests the current position first with the footprint placement
  test; a valid spot starts EndTransport, attaches a current-position destination and
  clears activation selector 1. The altitude it asks for is `max(ground, sea)` and then
  "ground minus sea at or below sea, otherwise zero", so **water does not automatically
  force the landing plane to sea level**. If the current spot fails it tries **twelve
  random offsets, one at each radius from 64 to 240 in steps of 16**, each snapped to
  the footprint grid. If none works, an 0xe0 event steps the angle back by 0x5555 and it
  circles at 160 from the remembered origin, radius 64. Mode 1 is set only on arrival
  0x20.
- **VtolStandby** claims weapons, remembers the X/Z of the source unit, waits one tick
  with mask 0x10000; fire-at-will lookup can insert an attack. Otherwise a mode-2
  aircraft **with cargo** wanders 8..39 units around the remembered point at cruise
  altitude waiting 30..44 ticks; one **without cargo** prepends LandIfCan and ends; other
  cases poll every 30..59 ticks.

**RWE.**

- **Landing site: DIFFERS.** `findLandingLocation` (`UnitBehaviorService_util.cpp`, about
  274) searches **rings outward for dry land that a footprint fits on, up to 48 rings,
  once a second**, and the aircraft flies to the found spot and lands. The original
  samples a fixed twelve offsets between 64 and 240 and circles when none fits. Visible
  effect: RWE finds a landing spot in a cramped base the original gives up on, and
  never lands where the original's twelve samples would have put it. Water: the original's
  altitude arithmetic *permits* a placement the footprint test accepts over water to be
  landed on at the sea-bed height; RWE rejects water. **UNCLEAR** whether the original's
  footprint test ever accepts a sea cell for an aircraft; the shipped air-capable units
  that can sit on water are the seaplanes, of which the census has few.
- **Idle with cargo: MATCHES.** `AirLoiterReason::IdleHop` hops 8..39 around the idle
  point, which is the register's rule (`TOTALA-EXE-MISSIONS.md`, "idle aircraft"). The
  30..44 pause is within the hop pacing; not checked tick-for-tick.
- **Idle without cargo lands: MATCHES** (the idle branch of `update`, about 395-515).
- **Landing-site reserve: DIFFERS in detail only.** The chapter notes candidate sampling
  reserves no terrain; RWE has no reservation either, so two aircraft can pick the same
  spot and the later one moves on.

## 5. Move

**Reference (section 3).** VtolMove prepares climb, announces, releases weapons, snaps
its destination to its footprint grid and attaches a fixed position with 0xe0. The final
state completes, and gives the movement speech only if no later node follows; it does
not demand that the wake was an arrival.

**RWE. MATCHES in effect; DIFFERS in the arrival test.** An aircraft's move is complete
when it is within **`max(8, 4 x maxVelocity)`** of the goal (`hasReachedGoal`,
`UnitBehaviorService_util.cpp` about 1480), a hand-made tolerance with a comment saying
why. The original's test is the strict approach radius of
`BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 2. The footprint snap of the destination is not
modelled. Documented by the comment in the code; `TOTALA-EXE.md` section 88 does not list it
by name.

## 6. Search circuits: SeekAttack and SeekGuard

**Reference (section 4).**

- **SeekAttack** ends on 0x40. At start it tries to issue an attack on an existing target,
  otherwise it remembers an orbit origin, a random angle and a climb. Its recurring state
  releases weapons and, **below `3 x (maxHealth >> 2)`**, looks for landing hosts within
  **3840**, prepends VTOL_LANDING on a random host and resets. Then it tries fire-at-will
  acquisition. Failing an attack, 0xe0 steps the angle back by `0x5555 + rand(0x2000)` and
  it orbits at **weapon range plus 160, radius 128**, deadline 30..59.
- **SeekGuard** also ends on path failure. Its recurring helper puts landing first (as
  above) and then **visits ground allies**: the visitor collects non-self, non-flying
  units whose own owner's alliance table lists the searcher, does not filter full health
  or require building work, and passes the **first** collected unit (not a random one) to
  guard-mode dispatch 7, prepending whatever order that yields. With none, it orbits at
  weapon range plus 160, radius 128, deadline 30, mask 0xf8, stepping back by
  `0x4000 + rand(0x2000)`.

**RWE.**

- **The 75 percent gate and the 3840 pad search: MATCHES the register** (it reads "seven
  sites"; `UnitBehaviorService_util.cpp` about 515: "whole-unit radius of 3840"; the
  check is in the loiter handlers and the attack runs).
- **Search circuit step: MATCHES.** `LoiterSeekStep` 0x5555, `LoiterGuardStep` 0x4000 and
  `LoiterStepJitter` 0x2000 (`AirMovement.h` about 183-190), with the radius constants
  128/160/320 beside them.
- **Visiting ground allies from the guard circuit: UNCLEAR, probably MISSING.** The
  Guarding loiter reason exists and circles; a search of the file did not find the
  collector that turns a visited ally into a dispatched guard order (mode 7). The
  register records the aircraft guard as decoded and not ported (`TOTALA-EXE-MOVEMENT.md`
  section 13). If it is absent, a fighter or bomber that has lost its target never goes
  to look after a friend, and only circles.
- **First, not random, ally: noted for the port.** If the visit is ever ported, the
  chapter says the first collected unit is used, so the order of the unit list matters
  and has to be the deterministic one.

## 7. Escort: VtolFollow

**Reference (section 5).** Follow requires a live target and not 0x48; copies the target's
position, prepares climb, picks a random orbit angle and releases weapons. A hostile
attacker on the guarded unit triggers an override attack on event 0x10, subject to the
exclusion set, then slot-level retaliation under runtime 0x300000 with the same flags as
the ground escort. CanRepair plus mode-8 dispatch can insert service on the guarded unit;
a builder escort copies the guarded unit's work (build orders become HelpBuild, reclaim,
repair and help-build become their VTOL variants). Otherwise 0xe0 shifts the angle; **runtime
0x80000000 chooses spacing of weapon range plus 160, its absence 320**; radius 128, wait 30,
mask 0xf8. When the target or events fail, an otherwise empty queue gets **SeekGuard appended
before Follow ends**, so a lost escort target becomes a search, not idleness.

**RWE. DIFFERS.** `handleGuardOrder` (about 4411) treats an aircraft like a ground unit:
build assistance on the guarded unit's work, otherwise stay within 200 of it. No spacing
rule, no retaliation, no repair of a damaged friend, no copied work, no fall-through to a
search circuit when the target dies (the order just ends and the idle branch lands the
aircraft). The register already says this is decoded and not ported
(`TOTALA-EXE-MOVEMENT.md` section 13); the new information is the 0x80000000 spacing
choice and the append-SeekGuard recovery. And the allied-target defect of
`BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 17 applies to aircraft too.

## 8. Patrol

**Reference (section 6).** Patrol ensures a return node, announces, prepares climb and
releases weapons. In the recurring state 0xe0 rotates the patrol node; otherwise it flies
to a point **320 units beyond** the supplied patrol position along the line toward it,
approach radius **336**. Below the 75 percent threshold it can prepend a landing host
within 3840; otherwise fire-at-will can suspend the patrol behind an attack; otherwise
wait 30.

**RWE. MATCHES.** The patrol constants 320, 336 and 30 are in `AirMovement.h`
(`AirLoiterReason::Patrol`); the rotation is `BT-ORDERS-ORDER-LIST-PARITY.md` rule 8.

## 9. Landing pad lookup and landing

**Reference (section 7).** FindLandingPad reuses a requested piece if it is free,
otherwise asks the script for up to four candidates and returns the first free one, or
-1; it is an availability check, not a reservation. VtolLanding fails if the host
disappears; with no pad it circles the host at primary weapon range adding 0x4000 to the
angle; with a pad it first approaches the **host centre within 160**, re-queries, then
approaches the **selected piece within 48**. A pad lost on the way resets with speech. The
final approach starts EndTransport, waits 15 and repeats; availability is checked once
more at the end, so another aircraft can take the pad. With cargo, the cargo attaches to
the piece; without, the aircraft does. An unladen damaged aircraft gets **SelfRepair only
if the host is complete and has both `isairbase` and `builder`**. The aircraft's motion
destination is cleared first.

**RWE. MATCHES the register in effect.** The pad queue (`AirLoiterReason::PadQueue`), the
pad claim at the final approach and the repair-by-the-pad rule are
`TOTALA-EXE-ECONOMY.md` section 94 area and `findAircraftToRepairOnPad`
(`UnitBehaviorService_util.cpp` about 380); `handleLandOnAirBaseOrder` (about 2260-2410)
is the order. **UNCLEAR:** the two-stage approach (160 to the centre, then 48 to the
piece) and the 15-tick repeat; RWE flies to the piece in one leg. Not player-visible
except as the path an aircraft takes into a crowded pad area. The pad-lost reset with
speech is not modelled. The `isairbase`-and-`builder` condition for the repair is
`BT-ORDERS-ORDER-DISPATCH-PARITY.md` territory and is already enforced by the pad helper.

## 10. Pickup and unload

**Reference (section 8).**

- **VtolPickup** requires a target, not 0x10048, no current cargo, and a target whose
  **model top plus world Y is strictly above sea level** (a submerged target fails even
  if allied). It compares the target's footprint X to the transport capacity. It approaches
  at cruise altitude within 48, asks the script for an attachment piece, passes the
  target's maximum height to `BeginTransport`, follows the target with a negative-Y piece
  offset, and on event 0x42 in the last state starts EndTransport and fails; otherwise the
  target attaches.
- **VtolUnload** ends at once if the cargo chain is empty, approaches the requested
  position within 320, checks the cargo definition's footprint placement (mode 1), and on
  failure announces "unable to unload" and returns 9 (retry). Valid: a second destination at
  the cargo's model-top height, recheck before detach, EndTransport, detach with flag 1,
  climb at the current position. Unlike the ground drop, the action itself detaches.

**RWE. MATCHES the register** (`TOTALA-EXE-TRANSPORTS.md` sections 34-36 and 135;
`handleLoadOrder` and `handleUnloadOrder`). The submerged-target rule: **UNCLEAR**. RWE's
`canLoad` checks (`TOTALA-EXE-TRANSPORTS.md` section 34) are on the transport side; whether
a target whose top is below the sea is refused was not found. For ships being loaded by an
air transport this is the case that matters; a hover or amphibious target is above the
surface. The unload placement retry matches in effect (the retry-with-message is the
register's).

## 11. Interruption masks and target recovery

**Reference (section 9).** The four attack handlers can **append SeekAttack** when
interrupted and otherwise idle. AirStrike and AirToGround use event mask **0x1000a**;
AirToAir and AirToGroundHover use **0x10008** (a real cancellation difference). A
target-required action whose reference is lost also ends and can append a position-based
search. The optional leash compares the whole-unit X/Z with the order's start point. The
overflow behaviour is not common to all four.

**RWE. MATCHES in effect.** When an aircraft's attack order ends it enters
`AirLoiterReason::AttackEnded` and circles for the next target
(`UnitBehaviorService.cpp` about 4994 and following). The mask difference (0x1000a includes
event 2, the unit being "made to stop"; 0x10008 does not) has no counterpart because there
are no masks; whether a Stop order ends a bomber's run the way it ends a fighter's is rule
2 of `BT-ORDERS-UNIT-ORDERS-PARITY.md` and the Gaps table here.

## 12. Weapon slots

**Reference (sections 9, 10, 11).** The attack handlers use **weapon slot 0**; assignment
is still subject to the weapon's own eligibility and aiming. Aircraft attacking ships do
not bypass water, sensor or weapon tests.

**RWE. DIFFERS.** `attackTarget` (about 3603) assigns weapon slots 0 and 1 together, and
during an attack run the aircraft's weapon slots are forced hot for the length of the run
rather than being left to the weapon pass. The consequence for shipped data is limited to
the aircraft with two weapons that differ in target class: a Brawler or Hawk with a
secondary that could not hit the target is the case to look at; on the shipped set the
bombers and fighters carry one weapon, so this is a rule for mods and for the gunship
class. Rank in the Gaps table is low for that reason. See also rule 4 of
`BT-ORDERS-UNIT-ORDERS-PARITY.md`.

## 13. Bombing run

**Reference (section 10).** AirStrike updates its cached target position from a live
target but can bomb a fixed point. After climb it releases weapons then claims slot 0. If
the aircraft starts **within 480 units** it flies **2240 forward** toward the target with
approach radius **960**. It then picks a randomised half-distance approach, spread
`+/-0x2000`, radius **480**. After an intermediate state the **release approach radius**
is `trunc(sqrt(2 x cruiseAlt / gravity) x 30 x speed) + 1 + attackrunlength`; zero
gravity returns 7. The release destination follows the unit if there is one, otherwise the
cached point, waits one tick with mask 0x100e8. Firing assigns the position target to slot
0 and sends the aircraft forward **attackrunlength + 960**, radius 960; it then clears the
weapon target and flies another **1440** along its heading, radius **128**. Below the
75 percent threshold it can prepend a landing host, and with no host it resets the run.
Otherwise it loops from attack state 3. Projectile release and impact are the weapon's.

**RWE.**

- **Release trigger: MATCHES.** `bombReleaseTrigger` (`UnitBehaviorService_util.cpp`,
  about 1153) is exactly `1 + attackrunlength + trunc(sqrt(2 x cruiseAlt / g) x speed)` with
  the 30 folded into the per-tick gravity, and says it was read at 0x4123A8-0x412402. The
  chapter agrees with the register here and adds the zero-gravity exit, which RWE has no
  analogue of (gravity is a constant).
- **The geometry around it: DIFFERS.** RWE's run is a phase machine (Approaching, Engaging,
  Departing, Breaking) with its own distances: the commit distance is
  `min(weapon range, max(90 x maxVelocity, 2 x turnRadius))`, the aim point in Engaging is
  one 30-tick lookahead beyond the target on the line from the aircraft through it,
  the run-out distance is `clamp(max(2.5 x turnRadius, 2 x cruiseAlt), 250, 900)`, and the
  commit angle is about 20 degrees (`computeAttackRunGeometry`, about 980; `defaultAttackRunOutDistance`,
  about 1086). None of 480, 2240, 960, 1440 or 128 appears; the randomised half-distance
  approach with its `+/-0x2000` spread does not exist. The comments in the code say the
  Engaging aim point reproduces the register's "fly through the target" and the run-out
  replaces the original's two fixed legs. The register records the numbers as decoded
  (`TOTALA-EXE-MISSIONS.md`, "The bomber run"); the chapter agrees with them. The effect
  on play is where a bomber is when it turns and how far it flies out before returning:
  RWE's run-out is 250..900 where the original's is `attackrunlength + 960` plus 1440, about
  2400 plus; bombers in RWE come back for another pass sooner and make a tighter pattern.
- **Cancellation by 75 percent health: MATCHES the register.**

## 14. Ground strafing

**Reference (section 11).** AirToGround uses the primary weapon range (its helper calls it
"speed" wrongly). It releases weapons, approaches a randomised half-distance point within
128, claims slot 0, assigns the unit or fixed-position target and approaches **within
weapon range**. The escape objective is **three weapon ranges beyond the target** along the
relative heading, with a random **128..255** acceptance radius and mask 0x100ea; it may
divert to landing below 75 percent. Otherwise a random left or right quarter-turn waypoint
at one weapon range, radius 128, sets up the next run.

**RWE. DIFFERS in the numbers, MATCHES the register in the shape.** The same phase machine
serves fighters: Approaching, Engaging, then Breaking to a waypoint (`AttackRun` with the
Breaking phase) rather than the three-range overshoot, and the break waypoint is chosen by
the run state. `TOTALA-EXE-MISSIONS.md` already records the 3R overshoot and the 90 degree
break; the chapter agrees with it. Gaps rank: low, since a fighter that breaks sooner is
indistinguishable at a glance from one that breaks later, and the numbers want the same
original check as the bomber's.

## 15. Air combat and evasion

**Reference (section 12).** AirToAir prepares climb, starts a pursuit counter and assigns
slot 0 on every state. IsAhead is a horizontal dot product of a 20-unit target direction
with the facing. On a destination event with the target ahead it builds an advancing
AirManeuverOrder starting **30 maxvelocity steps forward**, stepping at maxvelocity, and
waits **60..89** ticks. With no destination event and the counter below 90, ahead resets
the counter and behind adds 45. When separated by **over 160** it predicts the target's
position **45 target-velocity steps ahead** and steps by target velocity plus half the
target's maxvelocity along its heading, waiting 45. An expired pursuit prepends
**VTOL_EVADE**: a random side, one primary-weapon range perpendicular to the current
heading, then twice that range, radius 128, a third state to complete.

**RWE. DIFFERS.** The `Dogfight` air-movement state has phases (chasing a point running
away from the fighter, BreakingOut, BreakingAway) and a `targetIsAhead` test with the same
dot product (`UnitBehaviorService_util.cpp` about 1147). The 30-step and 45-step lead
points, the 60..89 and 45 waits, the pursuit counter, the 160 threshold and the Evade
geometry (R then 2R, perpendicular) are not reproduced as such. The register has the
pursuit decoded (`TOTALA-EXE-MISSIONS.md`, "AirToAir"); the chapter agrees with it and adds
the sub-action Evade geometry. A fighter-on-fighter fight is the case a play-test tells
apart. The advancing AirManeuverOrder whose position query advances itself
(`BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 7) has no counterpart.

## 16. Hover attack

**Reference (section 13).** AirToGroundHover starts with the randomised half-distance
point, claims slot 0 and approaches within primary range. It tracks failed reach samples;
**two misses allocate a random range-distance destination with radius 128 and add 0x110e8
but never attach it** (an allocation with no assignment). Otherwise it alternates angles
**+/-0x2000** around the target direction, builds a point **two thirds of range** away, with a
PathOrder that keeps the linked unit but suppresses live position refresh, radius 16, cruise
altitude, mask 0x100e8. Low health can prepend landing. Because the fixed point is
recomputed on later callbacks, this is not a perfect live orbit.

**RWE. MATCHES the shape.** `hoverAttackRingRadius` is two thirds of the weapon's reach
(`UnitBehaviorService_util.cpp` about 1098) and the station alternates around the bearing
(`hoverAttackStation`, `hoverAttackBearing`); the register's gunship pendulum (2R/3, +/-0x2000)
is the same. The two-misses allocation fault is not to be ported. Whether the station is
refreshed per tick or per reach sample is **UNCLEAR**; the effect is how smoothly a
gunship swings.

## 17. Airborne building

**Reference (section 14).**

- **VtolMobileBuild** handles event 2 as completion and 8 as termination; claims weapons,
  detaches, activates and conditionally climbs; snaps the product footprint; approaches
  within the builder's build range; path failure ends it. Blocked placement: announce once,
  retry every 30, fail after more than ten retries. **Allocation failure ends with 8** (the
  ground builder waits 300). Success assigns GetBuilt and starts the build animation. State
  3 calls the stance wait **but ignores its return** and falls into work. **Every global
  tick divisible by 150**, when this state runs, it replaces the movement with an offset point
  at build range and a heading shifted by **0xdb6e**. Work is integer buildRate over 30, six
  forward particles only on a successful progress call, wait 1 with mask 0x0a.
- **VtolHelpBuild** requires a live target, not event 8, `canfly` and a non-null builder list.
  Path failure ends it, a completed target finishes immediately. It starts the build with the
  **absolute** heading to the target, with the same 150-tick offset and no dedicated stance
  wait.

**RWE.**

- **The work orbit: MATCHES the register.** `UnitState::AirWorkOrbitState` (about
  `UnitBehaviorService.cpp:2855`) keeps a work position and a slot that rotates; the seventh
  of a turn (0xdb6e is -0x2492, one seventh) every 150 global ticks is the register's
  `TOTALA-EXE-MISSIONS.md`/`ECONOMY.md` record. `aircraft.test.cpp` (about line 352) tests
  the orbit around the work position.
- **Allocation failure ends the order: UNCLEAR.** RWE's air builder uses the ground
  `createNewUnit` path, so a failed allocation waits as the ground builder does (the
  open row BT-18). The chapter says the air builder gives up instead. Reachable only at
  the unit cap.
- **Stance fallthrough and absolute heading: not to be ported** unless a play shows them;
  an aircraft's build stance has no pose.
- **Blocked retries (30, more than ten): MATCHES** (`retryBlockedSite`).

## 18. Air reclaim

**Reference (section 15).**

- **VtolReclaim** (features) resolves the feature every call; requires `canfly` and
  `canreclamate`. Duration is integer **`30 + 0.5 x (energy + metal)`**, against the ground
  handler's base 15; decremented by two every two ticks; while more than **30** remains it
  requests two sets of six reverse particles; work deadline tick plus 300; expiry calls
  ReclaimFeature once. No stance wait and no worker-rate scaling.
- **VtolReclaimUnit** needs `canfly`, `canreclamate` and CanReclaim, ends on a missing target
  or 0x10008, and computes a 15-tick pulse. **The reach is the build range only**, without the
  ground handler's added target radius. Out of range it waits 30 and resets; path failure
  returns 9.

**RWE. DIVERGENCE (documented) for features, MATCHES for units.** RWE has one feature
reclaim (`computeFeatureReclaimWork`, `GameSimulation.cpp` about 444), `metal + energy +
hitPoints/4` at the builder's worker time, the documented departure of
`TOTALA-EXE-ECONOMY.md` section 97; the air base of 30 against the ground's 15 is therefore
not represented, and the chapter's "no worker-rate scaling" for either is the original's
quirk that RWE replaced. The live-unit reach is `BuildDistance`
(`reclaimTarget`, about 5897-5950), which is the chapter's air reach and, by the register's
reading, the ground one too; the "plus target radius" question is in
`BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 13. Path failure returning 9 versus ending: see
`BT-AI-PATHFIND-PARITY.md` rule 9.

## 19. Air repair and the pad wait

**Reference (section 16).** **VtolRepairUnit** fails on a missing target, completes on
leash overrun, requires target mode 1, refreshes the cached position, checks CanRepair and
prepares climb. A moving target (runtime 0x0c) causes a 15-tick wait and restart. Each pass
calls AddRepairProgress with integer worker time over 30 **but ignores the return**, and
requests six nano particles regardless: the particles do not prove funded repair. It waits one
tick watching event 8. **VtolGetRepaired** only waits: it checks health, waits 30 and sets a
started slot until full, then announces; it debits nothing. The pad's own SelfRepair does
the work.

**RWE. MATCHES the register** for the pad (`TOTALA-EXE-ECONOMY.md` section 94 area). The
"particles regardless" fault: RWE emits nano particles only while funded work is applied;
this is a presentation difference and not to be ported. The 0x0c wait: see
`BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 11.

## 20. Air repair patrol and scavenging

**Reference (section 17).** VtolRepairPatrol resets after 30 on 0x48. Initialisation
requires `canfly` and flags2 0x200, which **the loader derives from `canreclamate` while
setting `canresurrect`** (not a repair capability). It rotates on 0xe0, otherwise flies to the
patrol position at cruise altitude, waits 45 and first considers low-health landing. With at
least 20 percent energy it scans within the definition's range for repair targets (the
visitor excludes self, non-allies, non-mode-1, complete full-health units and this owner's
last reclaim victims), picks one at random, requires CanRepair, and issues repair for a
complete target and **VTOL_HELPBUILD for an unfinished one**. **It also searches reclaimable
features within 240 units regardless of whether storage is below 20 percent**, priority metal
if metal-low, energy if energy-low, then metal whose full yield fits, then energy.

**RWE. MATCHES the register and the port record** (`TOTALA-EXE-ECONOMY.md` section 131;
`PORTING-125-138.md`; the air side of `runRepairPatrolDecision`, side length 240 at about
`UnitBehaviorService.cpp:5090`; `repairpatrol.test.cpp` pins `pickRepairPatrolReclaimTarget`
with 240). The `flags2 0x200` derivation from `canreclamate` is `TOTALA-EXE-DATA.md` and
`BT-UNITS-UNIT-TYPES-PARITY.md`; in RWE `canRepairPatrol` is read from the definition. The
census has the ARM and CORE Construction Aircraft (and the Hawk-class patrol aircraft are
not repairers); nothing new.

## 21. Literal faults that are not to be ported

The chapter lists them honestly (section 18); for the record, none is in RWE and none
should be added without an original check that shows a player can see it: an unowned
pickup climb destination (the pickup success branch never assigns it); the pickup's leading
cargo-present guard returning 8 after a successful attach; the unload speech skipped when
detachment empties the chain; the unattached hover reach-miss destination; the air builder's
stance fallthrough and absolute heading; unconditional repair particles; the LandingPadList
uninitialised allocator byte. These are the original's bugs and RWE's `Result` and
variant-based state have no room for them.

---

## 22. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | An escorting aircraft (and an escorting builder) does not retaliate against the guarded unit's attacker, repair a damaged friend, copy the friend's work, keep the 0x80000000 spacing or fall back to a search circuit when its target dies. Rules 7 and 6 | `src/rwe/sim/UnitBehaviorService.cpp` (`handleGuardOrder`, the Guarding loiter), `AirMovement.h` | Register already has the shape (`TOTALA-EXE-MOVEMENT.md` section 13). Play: an escort fighter with an enemy fighter attacking the bomber it guards. |
| 2 | Allied target on an air guard ends at once (shared defect with ground). Rule 7 | as `BT-ORDERS-UNIT-ORDERS-PARITY.md` gap 1 | No; the register gives the *ALLIED* test. |
| 3 | Bomber geometry: 480 / 2240 / 960 / 480 / 1440 / 128, the randomised half-distance approach and the two legs after release, against RWE's 250..900 run-out. Rule 13 | `UnitBehaviorService_util.cpp` (`computeAttackRunGeometry`, `defaultAttackRunOutDistance`, the Engaging aim point), `AirMovement.cpp` | **Yes.** Watch a Thunder pass over an isolated target, time to return, and compare. The register has the numbers, the chapter agrees; the question is whether RWE's emergent pattern is close enough. |
| 4 | Stop does not end the "attack ended" search circuit (`airLoiter` is not cleared by Stop). Rule 11 | `PlayerCommandApplication.cpp` (`Stop`), `UnitBehaviorService.cpp` | No: clear the loiter on stop, check with an aircraft scenario. Read, not run. |
| 5 | Ground-ally visit in SeekGuard (the first collected unit, guard dispatch mode 7). Rule 6 | `UnitBehaviorService.cpp` (loiter Guarding), `AirMovement.h` | Spec-room read of the visitor, then a play. Probably absent; **UNCLEAR**. |
| 6 | Weapon slots 0 and 1 forced hot in attack runs; the chapter assigns slot 0. Rule 12 | `UnitBehaviorService.cpp` (`attackTarget`), `AirMovement.cpp` | No for the shape; low for shipped data (single-weapon bombers). |
| 7 | LandIfCan's twelve-sample site search against RWE's ring search; the water altitude rule. Rule 4 | `UnitBehaviorService_util.cpp` (`findLandingLocation`) | Yes for the water case only. |
| 8 | AirToAir lead points, waits and Evade geometry; AirToGround overshoot numbers. Rules 14, 15 | `UnitBehaviorService_util.cpp`, `AirMovement.cpp` | Yes: fighter engagements. |
| 9 | Pad approach in two legs (160 to the centre, 48 to the piece), pad-lost reset with speech. Rule 9 | `UnitBehaviorService.cpp` (`handleLandOnAirBaseOrder`) | Low; a crowded pad area. |
| 10 | Pickup of a submerged target; air builder allocation failure ending. Rules 10, 17 | `handleLoadOrder`, `createNewUnit` | Cap-bound; low. |

### Documented divergences this audit leaves alone

The one-handler-per-tick executor and the mover variant; the 8-or-4-maxVelocity arrival
tolerance for aircraft (commented in `hasReachedGoal`); the lobby-team alliance; feature
reclaim's `metal + energy + hitPoints/4`; dry-land-only landing; nano particles only on
funded work.

### Conflicts with existing RWE findings, in one place

1. **None between the chapter and `TOTALA-EXE-MISSIONS.md` on aircraft numbers.** The
   chapter's bomber distances (480, 2240, 960, 1440, 128), the 3840 pad radius, the
   75 percent gate, the 2R/3 hover ring, the 320/336 patrol and the `0x5555`/`0x4000`/`0x2000`
   steps all agree with the register. This is corroboration of the register by an independent
   reading, and it is worth recording as such.
2. **`VtolReclaimUnit` reach.** Chapter: build range only, contrasted with the ground handler's
   added target radius. Register's reading of the ground handler: the extra term is never written
   and contributes nothing. Compatible if the term is a no-op; recorded in
   `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 13.
3. **`Guard_NoMove`** is a ground-and-tower question and is in
   `BT-ORDERS-UNIT-ORDERS-PARITY.md`.
