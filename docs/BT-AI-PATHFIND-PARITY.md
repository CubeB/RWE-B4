# Route search, terrain knowledge and shared search budgets parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-ORDER-DISPATCH-PARITY.md`, it allocates no
section number in the main register. This is the finding that implementation work cites for
how the original's pathfinder knows terrain, costs a step, shares a budget between players
and hands waypoints on, per `AGENTS.md` and ADR-0004. The decoded routine is
`TOTALA-EXE-MOVEMENT.md` sections 87 and 102; this chapter is mostly an independent
corroboration of those two, and the rules below say so in one line where it is. The goal object that
consumes the waypoints and the arrival tests are `BT-ORDERS-ORDER-TARGETS-PARITY.md`; the
footprint-aware terrain grades are `BT-UNITS-MOVEMENT-CLASS-PARITY.md`; the order that fails or
retries on an empty result is `BT-ORDERS-ORDER-LIST-PARITY.md` and
`BT-ORDERS-UNIT-ORDERS-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ai-pathfind.md`). Nothing else
  under that tree was opened; no source and no decompilation. The spec-room discrepancy
  register was read read-only; no resolved row concerns this module.
- **Evidence class.** A **static reading**; nothing was executed. Weaker than
  `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements are marked
  **CONFLICT** with no winner picked.
- **No executed observation was used.**
- **The RWE half** is a read-only audit of `src/rwe/pathfinding` and `PathFindingService` at the
  working tree of 2026-10-16. Nothing was built or run. Line numbers are "about".

Status words as in the earlier audits.

## Index

1. State and lifetime
2. What the search knows about a cell
3. Starting a search and the preliminary probe
4. Costs, heuristic and the goal threshold
5. The open list
6. Traceback and the three waypoint horizons
7. The scheduler, its credit and the adaptive weight
8. Cancellation and the empty result
9. What an empty result does to an order
10. Gaps to fix

---

## 1. State and lifetime

**Reference (section 1).** One game-owned Pathfinder runs one active search at a time. The grid
holds flags, incoming direction and a 16-bit node index per cell, rounded to a multiple of eight,
with a dirty bitmap of one bit per eight cells so a search clears only what it touched. The
scheduler allowance is **0x535 = 1333** steps per call, the base heuristic scale is **0x18000 = 1.5**.
Ten player cursors and budgets persist. An in-progress search is not serialised.

**RWE. DIVERGENCE (documented).** `PathFindingService` owns a FIFO of requests and runs them
against flat stamped scratch arrays (`AStarScratch.h`), with a budget of **4000 expansions per tick**
(`expansionBudgetPerTick`, `PathFindingService.h` line 47). The original's 1333 is accounting
steps, not the same unit as an expansion, and its per-call credit is shared by player; RWE's
queue is not per player. A half-finished search **is** serialised, as five integers
(`CLAUDE.md`, "Derived state that can change an outcome"; `TOTALA-EXE.md` section 87), where
the original's is not, because RWE's saved-game test needs it. Documented in section 87 and not re-raised.

## 2. What the search knows about a cell

**Reference (section 2).** A cell state is **0 blocked, 1 traversable at an extra 30, 2 traversable
with no extra, 3 preferred**. The origin is mapped to the coarser visibility grid; outside it, grade 0.
If the **player's visibility mask bit is absent the answer is grade 2 without reading the real
terrain**. So unknown ground can look cheaper than known grade-1 ground, and a ship can route toward
an unexplored obstruction and revise later.

**RWE.**

- **Unexplored ground is passable and free: DIVERGENCE (documented), deliberately not in the
  default backend.** The register records the rule (`TOTALA-EXE-MOVEMENT.md` section 87, the
  "sight rule that makes unexplored ground passable"); `OaUnitPathSearch.h` says the OA backend
  "deliberately" leaves it out, and both backends read RWE's own walkability tests that do not look
  at the fog. The consequence is that a computer or human unit never walks into an unseen wall and
  turns round, so RWE routes are better informed than the original's, and the original's
  "marching into an unexplored cliff" behaviour is absent. For naval units the register already
  notes the same.
- **Grade 1 costs 30, grade 2 and 3 cost nothing: DIFFERS.** RWE uses a rough-terrain test and the
  octile cost, not a 30-point penalty for a graded cell. The chapter agrees with the register's
  `TOTALA-EXE-MOVEMENT.md` section 87 here. Effect: the original avoids slopes mildly (a cost, not a
  wall), RWE refuses the cells its test marks rough and otherwise treats all as equal.

## 3. Starting a search and the preliminary probe

**Reference (sections 3, 4).** The start cell is the unit's current cell. Every enumerated goal cell gets
goal flag 4, and the nearest by squared cell distance becomes the probe's aim. If the start is
already a goal, the result is "arrived" (flag 0x100) with zero waypoints; out of the grid, 0x200 and
zero. The **probe walks along X then Y toward the aim**, follows an obstacle's outline in both
directions, and returns the best heuristic seen; a probe that made no improvement finishes the search
with no waypoints. The probe's count is charged to the scheduler but it does not respect the 100-step chunk.
After the probe the heap resets, the start enters with g = 0, f = scaled heuristic and a straight-step
count of 100, the first expansion permits turns of -4 through +4 and later ones -2 through +2.

**RWE. MATCHES the register's shape.** `BugWalk` is the wall-follow probe
(`src/rwe/pathfinding/BugWalk.{h,cpp}`) and is used "to relax the goal rather than truncate the
search" (`BugWalk.h` line 24); `PathFindingService::relaxGoalToWhatIsReachable` (about 699) keeps
the walk's closest cell when the goal is blocked, and `BugWalk.test.cpp` (about line 131) pins that
the same decision is made from either neighbouring start so a unit does not shuttle. This is the
register's "probe's best heuristic marks relaxed goals" (`TOTALA-EXE-MOVEMENT.md` section 87). **DIFFERS**
in two details: the probe in RWE always runs as the first pass of every search (the original skips it
when the start is already a goal and returns 0x100), and the first-expansion turn range (-4..+4 then
-2..+2) is the OA backend's alone. The default A* uses the full eight-way fan.

## 4. Costs, heuristic and the goal threshold

**Reference (section 5).**

- Turn costs `{0, 40, 60, 80, 100, 80, 60, 40}` by turn size; step costs 16 straight and 22 diagonal.
- Estimate = distance x scale (base 1.5, adaptive) shifted by 16, an inadmissible weighted search.
- A cell whose own heuristic is at most the probe's best gains goal flag 4: **the search can end at a
  best-reachable location, not only at the enumerated goal**.
- g = parent g + turn cost + step cost + terrain penalty (30 for grade 1); **a turn after fewer than five
  straight steps adds 75**. An open cell is updated only when g is lower; closed cells are never
  reopened. There is **no explicit diagonal corner-clearance test** in expansion; it relies on the class's
  footprint information.
- Heap ties are decided by insertion and child order, not a geometric tie-break.

**RWE. DIFFERS by design, with an optional faithful backend.** The default backend
(`AbstractUnitPathFinder.cpp`) is an admissible octile A* whose costs are the grid distances, and
it **does** refuse a diagonal when either flanking cell is not walkable (about line 160: "no squeezing
diagonally between two obstacles whose corners touch"); the original does not test it. The optional
`OaUnitPathSearch` backend (`GameParameters::pathfindingBackend`, recorded in the replay) reproduces
the turn and straight-run costs, the weighted heuristic and the relaxation. `TOTALA-EXE-MOVEMENT.md`
section 87 lists what it leaves out, which is the scheduler, the adaptive weight, the movement map and
the unexplored-passable rule. Nothing in the chapter changes any constant the register gives; the
chapter's costs and the register's agree (`18 x max + 7 x min` for the distance, 16/22, the turn table,
75 and 30).

## 5. The open list

**Reference (section 6).** A min-heap of pointers to 20-byte nodes, with a "top popped" marker that defers removing
the expanded top; a node pool that grows by half plus 16; no pool-size cap protecting the 16-bit index.
The float-keyed max-heap helpers earlier in the address range are a different structure.

**RWE. Not applicable.** `MinHeap` (`src/rwe/collections`) and flat stamped arrays; no 16-bit node index. A map
large enough to need more than 65535 open nodes is the original's overflow, not RWE's.

## 6. Traceback and the three waypoint horizons

**Reference (section 7).** The traceback keeps a **64-point ring** of direction changes and emits at most the
last 64 from the start forward, converted to world units as `(cell x 2 + footprint) x 8`. The goal then caps the
delivered array to **20**. Multiplayer sends at most **three**. So a long bendy path is a near-start horizon, and the
goal asks for another search when it runs short.

**RWE. DIFFERS.** `UnitPath` carries the whole route as waypoints and the unit consumes them in turn; there is no
20-point horizon and no mid-path re-request, because RWE's lockstep sends orders, not paths. The three-point
network horizon belongs to `TA-NETWORK.md`/the TA-host half and is `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 9.
The visible effect of the 20-point cap in the original is that a unit with a very long route walks the first 20
corners and then asks again, which costs a repath cadence of 60 ticks; RWE's single complete route walks without
the pause. Documented in the register as RWE's own (`TOTALA-EXE-MOVEMENT.md` section 87, "waypoint count").

## 7. The scheduler, its credit and the adaptive weight

**Reference (section 8).**

- Credit per active player per call = 1333 / number of players, integer division, in ten budget slots.
- **Every 150 calls** each player's scan load divided by units per player selects the heuristic scale for that player:
  **ratio below 1: base x 6 (9.0); below 2: base x 3 (4.5); otherwise base (1.5)**. The global named a "budget cap" is
  actually the scale.
- With no active search, rotate the player until one has positive credit, advance that player's unit cursor, charge one step for
  the scan; a unit qualifies if its definition, motion and movement class exist, and its goal's repath claim (60-tick throttle)
  succeeds. An accepted request charges **100 more** steps and runs StartSearch (the probe included).
- With a search active, expand until a goal, exhaustion or 100 counted steps; found paths are traced. The outer loop may continue
  the same search after that player's balance goes negative: **shared accounting with persistent active-search ownership**.
- Remainders are discarded. A pending request does not mean one path every 60 ticks.

**RWE. DIVERGENCE (documented), with the original's pieces in the OA backend's neighbours.** A single FIFO queue of requests, a
4000-expansion budget per tick, a **60-tick per-unit request cooldown**, and no per-player credit and no adaptive weight
(`TOTALA-EXE-MOVEMENT.md` section 87; `PathFindingService.cpp` about 235). The register already records the 9.0/4.5/1.5 weights
and the 150-call re-evaluation as "decoded, not ported"; the chapter agrees to the number, and also confirms the register's reading
that the name `playerBudgetCap` is misleading (the value is the scale). The consequence that the original's heavy players get
cheaper (coarser) routes and its light players get tighter ones has no analogue; in a crowded fight RWE queues and delays instead.
`RWE_PATH_PROFILE=1` is the place to see it.

## 8. Cancellation and the empty result

**Reference (section 9).** The release path asks the movement class to refresh the requester if stale and clears the owner. The
abort path clears object, path, target and owner only if the supplied goal matches. Goal replacement or destruction aborts, so the
scheduler cannot later write into a stale goal. An empty waypoint set is interpreted by the goal: if the owner does not already
satisfy the destination, it reports **action flag 0x40** and clears the path.

**RWE. MATCHES in effect.** A new goal for a unit cancels or replaces its queued request
(`PathFindingService::update`/`beginSearch`), and an exhausted search returns a route that begins and ends at the unit
(`OaUnitPathSearch::takeResult` says so). The action flag does not exist; see rule 9.

## 9. What an empty result does to an order

**Reference (sections 9, 10).** No route (zero waypoints, owner not at the destination) wakes the action with **0x40**. What each
action then does is in the order chapters: Move turns it into return 9 (retry in 30..59 ticks, delete if another order follows),
mobile build tolerates it within the build range, kamikaze returns 8, air orders mostly end on it, repair and reclaim end or retry.

**RWE. DIFFERS.** There is no wake. A destination the search could not reach is "reached" when the unit has walked to the nearest
point it could (`hasReachedGoal` uses `NavigationStateMoving::reachableDestination`, `UnitBehaviorService_util.cpp`
about 1418-1440), so the order completes. Per order:

| Order | Original (static reading) | RWE |
| --- | --- | --- |
| Move | retry for ever (30..59 ticks) unless a later order exists | completes at the nearest reachable point |
| Mobile build | tolerated within build range, otherwise unreachable | fails with "Target area was blocked" for any unreachable site |
| Attack kamikaze | returns 8 | walks the straight-line stand-in at the target |
| Help build, repair, reclaim | ends or retries per handler | navigates by build range; no unreachable result |
| Aircraft orders | ends (VTOL handlers) | not applicable: aircraft do not path |

Ground Move and mobile build are the two player-visible rows; they are ranks 1 and 2 below.

## 10. Gaps to fix

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | A mobile builder's unreachable site within build range is refused; the original tolerates it. | `UnitBehaviorService.cpp` (`createNewUnit`) | Yes: a builder across a stream or cliff. Shared with `BT-ORDERS-UNIT-ORDERS-PARITY.md` rank 2. |
| 2 | An unreachable Move completes; the original keeps retrying (and a queued order deletes it). | `UnitBehaviorService.cpp` (`handleMoveOrder`), `UnitBehaviorService_util.cpp` (`hasReachedGoal`), `PathFindingService.cpp` | Yes: order a unit onto an island or behind a wall, watch the order panel and the unit. The existing tests pin RWE's current behaviour; a change is a hashed-state change and needs the replay gate. |
| 3 | Unexplored ground is passable for the original's player-side search; RWE sees through fog. | `AbstractUnitPathFinder.cpp`, `OaUnitPathSearch.cpp` | Yes; and it is a deliberate kindness. Decide whether the OA backend should offer it. |
| 4 | Diagonal squeeze: RWE refuses it, the original does not test it. | `AbstractUnitPathFinder.cpp` (about 160) | Yes: a gap one cell wide diagonally between two corners. |
| 5 | Grade-1 cost 30 and the 75-point bend cost in the default backend (present only in the OA one). | `AbstractUnitPathFinder.cpp` | Low. |
| 6 | Per-player credit and the 9.0/4.5/1.5 adaptive weight. | `PathFindingService.cpp` | Not a fidelity gap a player sees; skip unless route quality under load is measured. |

### Documented divergences this audit leaves alone

The FIFO queue, the 4000 budget and the 60-tick cooldown; the whole route instead of a 20-point horizon; the saved suspended search;
the wall-follow relaxation; the octile A* default and the optional OA backend (`TOTALA-EXE-MOVEMENT.md` section 87).

### Conflicts with existing RWE findings, in one place

None. The chapter agrees with the register on every number it gives (1333, 1.5 and its three-step ladder, 16/22, the turn table,
75, 30, 64, 20, 3, the 60-tick claim throttle). It adds two points the register had not recorded as such: the `playerBudgetCap`
misnomer, and that the probe's work is charged to the scheduler outside the 100-step chunk.
