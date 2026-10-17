# Order destinations, target geometry and path goals parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-ORDER-DISPATCH-PARITY.md`, it allocates no
section number in the main register. This is the finding that implementation work cites for
what counts as arriving, how a destination measures distance, and how a goal object hands
waypoints to the unit, per `AGENTS.md` and ADR-0004. The goal's attachment and ownership on an
order is `BT-ORDERS-ORDER-QUEUE-PARITY.md` rule 7; the search that produces the waypoints is
`BT-AI-PATHFIND-PARITY.md`; the handlers that choose a radius are
`BT-ORDERS-UNIT-ORDERS-PARITY.md` and `BT-ORDERS-VTOL-ORDERS-PARITY.md`. This audit restates none of
them. The decoded arrival routine is `TOTALA-EXE-MOVEMENT.md`, "Arrival at the goal" (section 102)
and its section 87 on waypoint advance; the chapter corroborates both.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-order-targets.md`). Nothing
  else under that tree was opened; no source and no decompilation. The spec-room discrepancy register
  was read read-only; no resolved row concerns this module.
- **Evidence class.** A **static reading**; nothing was executed. Weaker than `TOTALA-EXE*.md`, which
  were read from the retail executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.**
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/pathfinding` and
  `src/rwe/game` at the working tree of 2026-10-16. Nothing was built or run; line numbers are "about".

Status words as in the earlier audits.

## Index

1. Three objects, six destination types
2. Circular approach and the arrival test
3. The ring: two conventions
4. The rectangle marker
5. PathOrder: flags and constructors
6. Position, altitude, heading and the arrival test
7. The advancing aircraft target
8. The path goal: waypoints, advance, reuse, throttle
9. Network and save representations
10. Lifetime
11. Gaps to fix

---

## 1. Three objects, six destination types

**Reference (section 1).** An action order owns the intent; an OrderFx-derived destination describes the
acceptable cells or a moving position; a PathGoal belongs to the moving unit and exposes waypoints. The
destination types: 1 base, 2 PathOrder (fixed, linked, or fixed-with-unit), 3 AirManeuverOrder, 4 ApproachRadius,
5 RingApproach, 6 PointMarker. A destination reports progress to its source order by OR-ing bits into the
status word; replacing one sends flag 0x80. Defaults: no goal cells, distance 0, nothing contained,
`KeepAfterComplete` false.

**RWE. DIVERGENCE (structural), documented.** `NavigationGoal` is a variant of a point, a rectangle, a landing
location, a unit and a feature (`UnitBehaviorService_util.cpp`, `hasReachedGoal`); the navigation state lives on the
unit (`BT-ORDERS-ORDER-QUEUE-PARITY.md` rule 7). No source-order status word; arrival is polled
(`BT-ORDERS-ORDER-LIST-PARITY.md` rule 6). Rules 2 to 4 below compare what each destination *means*.

## 2. Circular approach and the arrival test

**Reference (section 2).** World positions are 16.16. A cell is 16 world units and a world position is converted
by subtracting the unit's footprint pair, adding a rounding half and shifting. ApproachRadius stores the world
radius and **`(radius / 16)^2` by integer division**; containment is squared cell distance **at most** that square, so a
radius below 16 accepts the centre cell only. Goal-cell generation inserts the centre alone. The distance estimator is
**`18 x major + 7 x minor` minus the radius, clamped at zero**, deliberately unlike the containment test, so the heuristic
and the acceptance can disagree at the boundary.

**RWE. DIFFERS.** `TOTALA-EXE-MOVEMENT.md` section 102 decoded the same object (`PointGoal`, the squared-square test, and
the `Move` mission's "radius + 4" which "divides down to 0 map squares: the unit must end up standing on the goal square").
RWE's ground arrival is **within eight world units of the goal point**, tested on flat squared distance
(`hasReachedGoal`, `UnitBehaviorService_util.cpp` about 1470-1490), for a point goal, a rectangle's closest point and a
landing location alike. A radius goal (the register's "radius is weapon range" etc.) is not an object RWE has; the
approach-to-range orders (attack, build reach) stop by their own range test, not by a cell-squared test. Consequences:

- **A move order ends sooner in RWE.** The original's unit stops on the goal's map square (a point within 16 units of
  it in any direction, depending on where in the square the click was); RWE stops within eight of the click point. In
  a crowd, where the original's later arrivals cannot reach the square and stall until the stuck-unit rule gives them
  up, RWE's eight-unit disc is also smaller, so the two do not fill a formation the same way. The register's section
  88 records the waypoint-advance radius departure (16 and 8 against 5); I did not find the final-goal test
  recorded as a departure. The chapter and the register agree on the original's side.
- **The 18/7 heuristic** is the register's (`TOTALA-EXE-MOVEMENT.md` section 87); RWE's octile grid A* uses its own
  distance.

## 3. The ring: two conventions

**Reference (section 3).** RingApproach stores two radii and two squares. **Containment: distance squared at most the
first radius squared and at least the second**, inclusive. **The heuristic distance instead calls the first radius the
inner and the second the outer**: above the second returns the excess, below the first the deficit, else zero. The two
are not the same annulus. If the first is smaller than the second, the containment interval is **empty** while the
heuristic still returns a distance. Goal-cell generation inserts one point offset in the second coordinate by
`(first + second) / 32`; `FillWorldPos` converts the centre, takes the heading from the source unit to that point and
steps back by the mean radius.

**RWE. DIFFERS.** There is no ring goal. The attack-chase stand position is a single point on the line from target to
attacker (`attackApproachGoal`, `UnitBehaviorService.cpp` about 3301), at the larger of "weapon range minus 8" and
"footprint half-extent plus 8". `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 6 lists what the original's staged ring searches
would be for. **The chapter's remark that the ring's two tests disagree is a finding about the original's own code, and
the reason a unit using a ring (the chase handler, which passes radii in range pairs) can fail to be "satisfied" in
the cell test while the search thinks it is close.** RWE should not copy the inconsistency; if the staged search is
ever ported, the containment test has to be chosen from the caller's argument order, which the chapter says is not
established from its source. Open until a spec-room read of the chase handler's calls.

## 4. The rectangle marker

**Reference (section 3).** PointMarker's first X/Y endpoints are relative to the source unit's footprint pair and
the other two are input plus size **without** that subtraction (an asymmetry the chapter preserves). **Containment is
the inclusive border, not the interior.** Goal-cell generation emits top and bottom for every inclusive X, then left and
right for interior Y, with duplicates for degenerate rectangles. `FillWorldPos` is the midpoint of the X endpoints at the
second Y endpoint: **the middle of one edge, not the centre**. Distance: 16 x the distance to the nearest border when inside
(so positive in the interior), 16 x the perpendicular distance outside an edge, and **`16 x major + 6 x minor`** outside a
corner.

**RWE. DIFFERS in form, MATCHES in intent.** The footprint of a build or repair target is a `DiscreteRect` goal. The unit
walks to the nearest point of the rectangle it can stand at (`findClosestPointToFootprintXZForUnit`,
`UnitBehaviorService_util.cpp` about 1400-1444), and the build reach is tested against the nearest point of the target's
rectangle (`withinBuildReachOfRect`). The original accepts any border cell and aims at the midpoint of one edge; RWE
accepts any point whose closest approach is in reach. For a one-tile target the two are within a cell; for a large
target (a factory of four-by-four) the original's unit may stop on a different side. The `16 x major + 6 x minor`
corner metric (against `18/7` for circles) is the register's only if it was read; it was not, so treat the 6 as new. Not
a player-visible rank.

## 5. PathOrder: flags and constructors

**Reference (section 4).** Flags: 0x01 destination has a unit reference; 0x02 following offset, heading may face the
referenced unit; 0x04 exact target heading required; 0x08 explicit altitude; 0x10 strict radius completion; 0x20 position
in the bitstream; 0x40 explicit heading; 0x80 do not refresh the position from the linked unit. The order-plus-target
constructor selects **flags 7 and a following offset equal to the mover's primary weapon range (100 if zero) when the
target's definition is `canfly`**, otherwise flag 1 (it examines the target's definition, not the mover's). Unit-and-piece
is flags 5; fixed position is 0x20; unit-plus-position is 0xa3 (reference kept, position fixed). `KeepAfterComplete` is true
only with flag 1 and a live reference.

**RWE. Not applicable, with one consequence recorded.** Following a flying target with an offset equal to the follower's
weapon range is what the original's escort and chase of an aircraft does; RWE's chase navigates to a stand-off point from
the target's present position (`attackApproachGoal`), which is the same offset for a ground-to-air chase of a unit that
holds still and differs for a moving one: the original's destination keeps a heading-relative offset that rotates with the
target. `KeepAfterComplete` corresponds to RWE's guard and follow orders staying active after arrival; RWE implements it as
"the order does not end".

## 6. Position, altitude, heading and the arrival test

**Reference (section 5).**

- `FillWorldPos` with a unit reference queries the target piece position (refusing a null reference or a target in the overflow
  bucket) and applies the heading-relative offset and altitude. Without a reference and without explicit altitude it rebuilds Y
  as the mover definition's **cruise altitude plus sea level or the ground height** (selected by definition flag bit 22). **Y is
  capped at 511 world units**; there is no lower clamp.
- `SetAltitude` sets flag 8; for a position-bearing target it sets Y to `max(ground, sea) + altitude`, capped at 511.
- `GetDesiredHeading` considers a live reference only with 0x04 or 0x80: flag 2 faces mover to target; otherwise explicit heading
  wins; otherwise the target's own heading. With neither source it fails.
- **Arrival**: with flag 0x10, **`radius > distance`, strictly**, and nothing else is checked. Otherwise the horizontal Euclidean
  distance must be **at most 0.5 world units**, flag 1 requires a live reference, flag 4 requires exact heading equality, and flag
  8 allows **one unit** of vertical error. There is no angle tolerance.

**RWE.**

- **The 511 cap and cruise altitude: UNCLEAR.** RWE's aircraft converge on `cruiseAltitude` above ground (or sea, for water)
  by the mover (`computeNewAirUnitVelocity`) and have no 511 ceiling; a map with a plateau above 511 world units of height plus a
  tall cruise altitude is not shipped. Do not port the cap without a map that exercises it.
- **Strict radius versus RWE's tolerance: DIFFERS.** The orders that pass an approach radius (the aircraft ones: 48, 128, 160,
  320, 336, 960) are tested by the mover or the loiter against their own constants. The chapter's rule that a radius goal is
  **strict** (`radius > distance`) matters at equality only. The 0.5-unit exact arrival used for flags other than 0x10 is the
  case of a unit following a moving target to a precise spot (pad landing): RWE's landing requires "actually over the spot"
  (`hasReachedGoal`, the `NavigationGoalLandingLocation` exemption) with an eight-unit disc.
- **Exact heading equality at arrival** (flag 4) is the aircraft's pad approach; the pad's landing orientation is
  `BT-UNITS-UNIT-POSITION-PARITY.md` territory.

## 7. The advancing aircraft target

**Reference (section 6).** AirManeuverOrder's position query **returns the current point and then advances it** by the step
vector in X/Z; calling twice advances twice. With heading mode on, the step vector is rotated toward the stored heading by at most
**one eighth of the unit's turn rate per query**, and a rotation zeros the step's Y. `SetAltitude` does nothing. The desired
heading points at the current internal target. **Completion: horizontal distance strictly below 48, or, with heading mode, the step
heading exactly equal to the stored heading**; `KeepAfterComplete` is false.

**RWE. MISSING as an object, covered by the fighter's phases.** The air-combat pursuit is `AirMovementStateDogfight` with a point
that runs away from the fighter (`computeNewDogfightVelocity`); nothing advances a goal in a query. The 48-unit completion and the
turn-rate-eighth rotation are not reproduced. `BT-ORDERS-VTOL-ORDERS-PARITY.md` rule 15 carries the geometry; this rule is the
mechanism, and it is a pure query with a side effect (a determinism hazard to avoid in a port: the position must advance once a
tick, in the mover, not on a read).

## 8. The path goal: waypoints, advance, reuse, throttle

**Reference (sections 7, 8).**

- The AI goal holds **20** short coordinates; `SetWaypoints` copies at most 20; an empty result reports 0x40 unless the owner already
  satisfies the destination; the old count is not reset (active state decides if stale points are used).
- `TickTowardGoal`: if the destination says arrived, **flag 0x20** goes to the source order and the destination is cleared unless
  `KeepAfterComplete`; with two or more points, **within squared distance 25 (five world units) of point 1** the array shifts forward;
  fewer than two points makes the path inactive; a repath is requested when the owner's occupancy bit 2 is set or fewer than two points
  remain.
- `SetPathOrder` cancels a scheduled search, reports 0x80 to the old source, and installs the new destination. **Reuse:** with at least
  three points, if the final point divided by 16 passes the new destination's cell test; or if the final point is **less than half the
  mover's distance** from the new destination; else, for a mover whose motion object lacks flag 0x800000, a **two-point direct seed**
  (owner to destination). A search is requested regardless.
- `TryClaimRepath` returns the goal only when requested and the tick is at least the last claim plus **60**.

**RWE.**

- **Waypoint advance: DIVERGENCE (documented, measured).** The register keeps the advance radius at 16 and 8 where the original
  has 5 (`TOTALA-EXE-MOVEMENT.md` section 87, about line 1371: "One departure, and it is measured": at a hundred units in `path_bench`,
  five arrived). Not re-raised.
- **Direct seed: MATCHES in effect.** A new goal gives the unit a straight-line stand-in until the search lands
  (`BT-AI-PATHFIND-PARITY.md` rule 7 describes the queue), which is the chapter's two-point seed.
- **60-tick claim throttle: MATCHES** (the per-unit cooldown in `PathFindingService`).
- **Path reuse on a new destination: UNCLEAR, probably MISSING.** A search for the new goal is queued on every new order; the
  three-point and half-distance reuse tests were not found. The cost is extra searches for a unit given a series of nearby move
  orders, not a visible difference.
- **Occupancy-bit repath trigger: UNCLEAR.** The original re-requests when its occupancy bit 2 differs; RWE repaths on blockage by its
  own rule (`BT-UNITS-UNIT-MOTION-PARITY.md`).
- **Empty result and flag 0x40**: see `BT-AI-PATHFIND-PARITY.md` rule 9.

## 9. Network and save representations

**Reference (sections 9, 10).** The AI goal serialises one occupancy-mode bit and a two-bit count, **at most the first three of its 20**
points, 32 bits each; the receiver's patrol goal restores 0..3 points and needs two for readiness. PathOrder's bitstream is 8 flag bits plus
optional 16-bit piece and target id, optional radius, altitude and heading (16 each) and optional XYZ (32 each), with no following offset.
AirManeuverOrder sends one flag bit, six 32-bit components and an optional heading. Save records are fixed (16, 24, 20, 54, 42 bytes) with
uninitialised leading bytes and unchecked write counts.

**RWE. Not applicable to the lockstep; relevant to the TA host half.** RWE sends orders, not paths; the three-point horizon is what a joining
`TotalA.exe` expects of a unit record (`docs/TA-NETWORK.md`, `src/rwe/net/ta/`); the decoder side was read there and the chapter's 3-bit header
agrees with the unit-record layout recorded. Not checked bit by bit here. The save records are the original's file format, not RWE's
(`save_util.cpp` writes JSON). The chapter's "invalid received ids are not checked against array bounds" is the class of hazard that
`docs/SECURITY-AUDIT.md` exists for; RWE's TA-side decoders must range-check unit and piece ids from the peer, and
`malformed_input.test.cpp` is where a case would go.

## 10. Lifetime

**Reference (section 11).** Create and destroy the shared pathfinder do not check for an existing object. Point-vector helper
specialisations are plain storage.

**RWE. Not applicable.** `PathFindingService` is a member of the simulation.

---

## 11. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture run on retail Total
Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Final-goal arrival is within eight units of the point in RWE; the original requires the unit's map square to equal the goal's (radius 4 divides to zero squares). Rule 2 | `UnitBehaviorService_util.cpp` (`hasReachedGoal`), every test that pins the eight-unit disc | **Yes**, then a replay-gate decision: it is hashed behaviour. A group move order to a point in the open: where do the units stop, and do they stop sooner in RWE? The register's section 88 should record it either way. |
| 2 | The ring's two inconsistent conventions and the staged chase searches. Rule 3 | `UnitBehaviorService.cpp` (`attackApproachGoal`) | Spec-room read of the chase handler's calls first. |
| 3 | Rectangle marker: border cells and edge-midpoint aim against RWE's closest-point rule; the corner metric. Rule 4 | `UnitBehaviorService_util.cpp` (rectangle goals) | Low; a factory build from a distance. |
| 4 | Path reuse on a new destination; occupancy-bit repath. Rule 8 | `PathFindingService.cpp` | No; an efficiency question, not a fidelity one. |
| 5 | Heading-relative following offset for a flying target. Rule 5 | `UnitBehaviorService.cpp` (guard and chase of aircraft) | With the escort work of `BT-ORDERS-VTOL-ORDERS-PARITY.md` rank 1. |
| 6 | Range-check the unit and piece ids a TA peer sends in the position/target records. Rule 9 | `src/rwe/net/ta/`, `malformed_input.test.cpp` | No: a hardening item under `docs/SECURITY-AUDIT.md`. Not checked whether it is already done. |

### Documented divergences this audit leaves alone

The variant goal instead of destination objects; polled arrival instead of a status word; waypoint advance 16 and 8 against 5; the stand-in
straight line; the whole route instead of a 20-point horizon; JSON saves.

### Conflicts with existing RWE findings, in one place

None. The chapter agrees with `TOTALA-EXE-MOVEMENT.md` sections 87 and 102 on the radius-over-16-squared test, the cell-based arrival, the 25
(five-unit) waypoint advance and the 60-tick claim. New in it: the ring's disagreeing conventions, the `16 x major + 6 x minor` rectangle corner
metric, the self-advancing aircraft target and its 48-unit completion, the 511-unit altitude cap and the path-reuse tests.
