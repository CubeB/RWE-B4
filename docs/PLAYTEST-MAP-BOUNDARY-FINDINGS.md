# Map boundaries and aircraft recovery

Spec-room finding for [#895](https://github.com/CubeB/RWE-B4/issues/895),
2026-10-27. This records original behavior; it does not identify the exact
route taken in the reported game. The owner subsequently identified aircraft
and a commander ordered to pathfind to another point.

BOUNDARY-001 to BOUNDARY-003 were written in the first spec-room pass and are
reproduced here unchanged; BOUNDARY-004 is the follow-up requested on
2026-10-30 for the aircraft half of the report. The checklist item for a
captured route still stands for the report itself: none of these sections
identify what the reported aircraft were ordered to do.

## Evidence and limits

- Reference: byte-tactics revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`, inspected read-only from the
  separate `D:\RWE-boundary-spec` documentation checkout under ADR-0004, and
  re-checked at the same revision from `D:\RWE-spec-room` for BOUNDARY-004.
  The checkout's `git log -1` records that revision as `main` behind two
  commits, so the tree read is exactly the revision named in BOUNDARY-001.
- Build: GOG v3.1 `TotalA.exe`, 1,178,624 bytes; MD5
  `8e74a1dffa1f5988624c52048f5b20cd`; the local reference executable's SHA-256
  was measured as
  `3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
- Evidence: static comparison of the terrain loading, occupancy, movement,
  footprint validation, pass-map and aircraft order routines identified by
  addresses below. Existing systems prose was used as navigation, then the
  relevant decompilation was checked. This handoff contains prose, addresses
  and behavioral arithmetic only.
- **No original-game runtime check, demo oracle, RWE build or RWE test was
  executed by the spec room.** Static results must not be reported as an
  executed original-game reproduction. No RWE source or tests were changed.
- The exact map, unit variants, orders and executable played on 2026-10-26
  remain unknown. The report alone cannot prove that every observed aircraft
  excursion was erroneous.

## BOUNDARY-001: rendered limits and blocked border cells

The original has distinct full terrain dimensions, rendered map dimensions,
terrain-cell occupancy bounds, and aircraft order recovery. They must not be
collapsed into one global rectangle clamp.

At `0x4833B0`, rendered width becomes full world width minus **32** and
rendered height becomes full world height minus **128**. The same routine
also establishes terrain exclusion cells. Therefore these subtractions are
not merely camera settings.

The border cells selected are:

- The **last two terrain columns**, throughout the terrain height.
- At the top of each column, the initial consecutive terrain rows whose
  projected vertical coordinate is negative. The projection is
  **16 × row − floor(terrain height byte / 2)**. The scan stops at the first
  nonnegative projected coordinate.
- At the bottom of each column, the scan starts at the last row and proceeds
  upward while that same projected coordinate exceeds **full world height
  minus 128**. For each such sample it selects the **preceding terrain row**,
  one row above the sampled row. It stops at the first sample within the
  rendered height. That one-row displacement is in the reference; replacing
  this with a flat last-eight-rows rule would change the result on raised
  terrain.

The height byte used in this border projection is the individual terrain
sample, not the footprint's minimum or maximum support height. The selection
does not depend on sea level. In particular, the top/bottom boundary in world
X/Z depends on terrain height; it is not always the same as the rendered
rectangle in world coordinates.

Selection alone does not overwrite an arbitrary feature. Only a no-feature
cell (`0xFFFF`) or a feature-continuation cell (`0xFFFE`) becomes the exclusion
marker **`0xFFFD`**. An ordinary feature definition ID remains unchanged.
This qualification matters when reproducing the original's precise behavior
on authored map content. A separate lava-world pass also uses the marker for
low cells, but is outside the border defect.

**Load ordering and void cells.** The map load at `0x483610` places resolved
map features and mission features, including expansion of their footprint
continuations, before the border-marking pass. Border selection consequently
sees the resolved cells, not merely the raw TNT feature-origin list. An
ordinary feature origin is retained while its `0xFFFE` continuation cells can
be replaced. The feature placement at `0x423C50` explicitly preserves a raw
**`0xFFFC`** void marker as that marker; the border pass does not convert it.
Both movement consumers below already reject it. An importer that discards
raw void markers loses an independent movement restriction, at the border or
inside the map. Retaining only ordinary feature-origin exemptions is not a
complete substitute for preserving these raw blocked cells.

The same exclusion marker is rejected by both:

- the raw movement-class passability query, `0x47DE60`, which feeds the
  class-dependent path maps; and
- the mode-1 physical footprint placement query, `0x47DB70`, used when a
  locally simulated ground unit changes footprint cells.

Thus an RWE correction for a commander walking into this reserved border must
make the affected cells unavailable to **route planning and physical
movement**, rather than merely stopping the camera or clamping the finished
position. Depth, slope and ordinary feature checks remain separate inputs.

## BOUNDARY-002: full terrain bounds and movement modes

The occupancy insertion at `0x47CC30` assigns a unit to a separate overflow
bucket when either footprint-origin coordinate is negative, or the origin
plus footprint size is **greater than or equal to** the corresponding full
terrain sample count. Otherwise it inserts the ordinary footprint occupancy
and spatial membership. This is a footprint test, not a test of model vertices
or projected sprite extent.

The world-position conversion at `0x48A9F0` computes the footprint origin by
subtracting half its width/depth, adding eight world units, then dividing by
sixteen with floor-style fixed-point shifting. The position setter itself
does not reject or clamp the resulting world position. An out-of-range
position can consequently exist and be represented by the overflow bucket.

The footprint permission routine at `0x47DB70` rejects those same outer
bounds for ordinary ground movement, but accepts them for **movement mode
2**, the airborne case. Its mode-1 scan additionally rejects the exclusion
marker and checks occupants, depth and slope. The ordinary movement
integration at `0x43D6D0` calls this permission routine for locally simulated
players when changing cells; a refused step is held in the current footprint
cell window, with speed capped at half the definition's maximum when above
that cap. Existing blocked-cell findings describe that response in detail.

The pass-map builder at `0x440500` also uses zeroed scratch padding outside
the full terrain, so a footprint cannot gain a route by treating missing
cells as passable. This is additional to the authored/exclusion feature
policy inside the terrain.

A standalone position-clamping helper exists at `0x4853B0`, but it is not
called by the movement integration or aircraft boundary handlers inspected
here. Its existence does not establish a universal clamp policy.

## BOUNDARY-003: aircraft recovery is order-specific

An airborne unit may leave ordinary terrain occupancy. That alone is not a
conformance failure. The following handlers react to overflow membership by
replacing their movement objective with a point **800 world units toward the
rendered map centre**, with approach radius **128** and movement event mask
**`0xE0`**. They retain their order state. The point is measured from the
aircraft's current position; it is not a teleport or a clamp to the edge.

| Order behavior | Evidence address | Earlier termination that still applies |
| --- | --- | --- |
| Find terrain on which to land | `0x40F2A0` | A following order or path-failure event ends the landing search first. |
| Follow/guard a specific unit | `0x40FBE0` | Requires a retained target and no applicable cancellation/path-failure event. |
| Search after an attack | `0x4103E0` | A path-failure event ends the search first. |
| Search after losing a guarded unit | `0x410850` | A path-failure event ends the search first. |
| Air-to-air combat | `0x412D40` | Cancellation or a required target disappearing is handled first. |
| Hovering air-to-ground combat | `0x413470` | Cancellation or a required target disappearing is handled first. |

The centre uses the rendered dimensions established in BOUNDARY-001, each
integer-divided by two, with the original's top-left world origin. An RWE
implementation using centred world coordinates must translate that point;
the full-terrain centre is displaced by **−16 X, −64 Z** on the ordinary
even-sized maps. The centre's Y is not used for the heading. The recovery
branch sets no explicit cruise-altitude override of its own; do not infer a
new altitude rule from this finding.

Ordinary strafing at `0x412710` instead sets a **30-tick** deadline and returns
to attack state **2** when in overflow. It does not use the six handlers'
centre-seeking destination. The bomber handler at `0x411F50` has no equivalent
overflow branch. Neither do the direct move handler at `0x40FA20`, patrol at
`0x410E70`, or standby at `0x40F7D0`.

Standby can delegate an empty aircraft to terrain landing, which then has the
recovery above. A loaded standby transport's short random hops are not the
same behavior. Patrol installs an objective 320 units beyond its waypoint
with approach radius 336; patrol is not authority to clamp every such point
to the terrain. An ended attack can append a search order, which subsequently
applies that search order's boundary policy.

## BOUNDARY-004: what the edge means to an aircraft in flight

This section answers the four remaining questions for #895: whether an
aircraft may cross the edge, whether anything turns it back, whether the
answer differs by order, and whether any clamp or reserved-border rule
applies to air movement. It re-reads the aircraft movement and order paths at
the revision and build above. The evidence is a **static comparison**; no
original game was run, no demo was scored, and no RWE build or test was
executed.

### The short answer

1. **An aircraft may fly beyond both edges.** It may cross the rendered
   rectangle of BOUNDARY-001 and it may cross the full heightmap. No routine
   that moves an airborne unit tests its position against either edge, and the
   only position-clamping helper in the image (`0x4853B0`) has no caller in
   the recovered sources; a helper table in the reference checkout likewise
   records no call or data reference to it, so it cannot be shown to be
   reachable at all.
2. **Recovery is not a boundary rule; it is an order-handler rule.** Six
   handlers react to the unit being outside the **full terrain grid**, not to
   the rendered border, and they do so only while they are the order being
   executed; one further handler answers differently. There is no global step,
   timer or clamp that notices an off-map aircraft.
3. **The order matters, and most air orders do nothing.** Move, patrol, the
   bomber's strike and standby itself never turn back. Landing, guard, both
   post-loss searches and the two dogfight orders do. Strafing does a third,
   different thing.
4. **No reserved-border rule applies to air movement.** The exclusion marker
   that BOUNDARY-001 writes is consulted by ground route planning and by
   physical ground placement only. The airborne footprint query returns
   "can be placed" before any cell content is looked at.

### Where the boundary actually is

The state the recovery handlers test is the same one BOUNDARY-002 describes:
the unit is not in an ordinary cell bucket. At `0x47CC30` the footprint is
offered to the map with its origin cell and its size; the unit is put on the
single sentinel bucket (the word at `+0x142B7`) when the origin cell is
negative, or when the origin cell plus the footprint size is **at or beyond**
the full terrain tile count in either axis. Nothing in that test refers to the
rendered dimensions.

Three consequences an implementation has to keep:

- The test is on the **footprint** and uses `>=`, not `>`. A footprint enters
  the state while part of it is still over the last row or column, and a
  one-cell footprint can never occupy the last column or row at all.
- Because the state begins at the footprint's edge, an aircraft is already in
  it before its model or centre visibly leaves the terrain.
- The rendered rectangle is 32 world units narrower and 128 shorter than the
  full terrain (`0x4833B0`). An aircraft between the rendered edge and the
  full-terrain edge is in an ordinary bucket and is simulated exactly as it is
  anywhere else; it is **not** in the state the recovery handlers act on.
  Nothing recovers it, and nothing should.

### While it is outside, the movement never objects

- The per-tick position update `0x43D6D0` asks the footprint query `0x47DB70`
  only for a unit whose owner's controller type is 1 or 2, and records "could
  not be placed" only when the answer is negative. In the airborne mode (2)
  that query returns affirmative for every in-map cell and for every
  out-of-map cell: the outer-bound rejection at the top returns affirmative
  for mode 2, and an in-map mode-2 query returns before the exclusion marker,
  occupant, water-depth and slope tests. The blocked flag therefore cannot be
  set while airborne, so neither the clamp to the current cell window nor the
  half-speed cap that follows it is reachable in flight.
- The position setter `0x48A9F0` performs no bounds test and no clamp: it
  stores the supplied world position and only re-buckets the unit.
- Air occupancy is recorded in the cell's second occupancy channel, not the
  ground channel, so an airborne unit neither blocks nor is blocked by ground
  occupancy. Outside the grid it is only on the sentinel list, and the
  removal path `0x47D0E0` skips all cell work for it.
- The rendered dimensions are read in only two kinds of place: the border pass
  `0x4833B0` and the centre arithmetic of the six centre-seeking handlers
  below. No movement, occupancy or per-tick unit routine reads them, and the
  strafing handler does not read them either.
- **Altitude is no longer steered outside the grid.** The height step of the
  air movement `0x43D290` is skipped when the unit is on the sentinel bucket
  (its test is at `0x43D4C8`, already recorded in `TOTALA-EXE-MOVEMENT.md`
  section 1). The remaining vertical velocity is only subject to the drag
  term, so the aircraft coasts vertically instead of being driven to a goal
  altitude. The goal point's altitude is derived from the smoothed height byte
  of the unit's *current* cell bucket (a definition carrying the sea-unit flag
  at `+0x241` bit 22 uses sea level instead; that is a ship property, distinct
  from the can-fly bit at bit 11). The sentinel bucket is zeroed when it is
  created (`0x482C20`) apart from its flag word, so an off-map goal's altitude
  evaluates to the definition's cruise altitude above world zero. That value
  does not steer the aircraft while it is off-map, and it is replaced by the
  ordinary terrain-relative goal as soon as the unit is back in an ordinary
  bucket.
- A goal that follows a **unit** rather than a point refuses to produce a
  position while that target unit is on the sentinel bucket (`0x44E3C0`
  returns "no position"), and the goal then keeps its last valid point. So an
  aircraft guarding or attacking something that is itself outside the grid
  holds the last on-map point; it is not dragged off the map by it. This is a
  corner case, not a boundary rule, but it is the behaviour to copy.

### The recovery: identical in six handlers, different in a seventh

While the unit is on the sentinel bucket and the handler's own earlier
termination tests have not fired, these handlers replace their movement
objective with a point **800 world units from the aircraft, along the bearing
to the centre of the rendered rectangle**, with approach radius **128** and
movement event mask **`0xE0`**. The centre is the rendered width and height
each integer-divided by two, exactly as BOUNDARY-003 states; the heading
computation reads only X and Z (`0x48A980`). The branch does not advance the
order's state (it answers "no change"), so it is re-issued on every order-list
pass — once per unit tick — and the aircraft is walked toward the centre in
800-unit steps until its footprint fits inside the full grid again. It is not a
teleport and not a clamp.

| Handler | Address | Earlier termination that pre-empts recovery |
| --- | --- | --- |
| Landing search (`VTOL_LandIfCan`) | `0x40F2A0` | A following order, or the path-failure event. |
| Follow/guard (`VTOL_Follow`) | `0x40FBE0` | No retained target, or a cancellation/path-failure event. |
| Post-attack search (`VTOL_SeekAttack`) | `0x4103E0` | The path-failure event. |
| Post-guard search (`VTOL_SeekGuard`) | `0x410850` | The path-failure event. |
| Air-to-air attack (`AirToAir`) | `0x412D40` | Cancellation, or a target that has gone; it first queues a post-attack search. |
| Hover air-to-ground attack (`AirToGroundHover`) | `0x413470` | The same two. |

All six then retain their state, their order data and their target; only the
attached movement objective is replaced.

**Strafing is the exception.** `0x412710` (`AirToGround`, the non-hover
air-to-ground mission) does not aim at the centre at all. In the same state it
sets a **30-tick** deadline and forces state **2** — the attack state — which
then re-issues the attack goal at the target's current position with the weapon
range as approach radius. Its own ordering means the overflow test runs after
the two "target gone" paths and before the weapon-range leash. An off-map
strafing aircraft therefore keeps attacking from outside rather than heading
home.

**Orders with no such branch at all:**

| Order | Address | What it does instead |
| --- | --- | --- |
| Direct move (`VTOL_Move`) | `0x40FA20` | Snaps the order's position to the footprint grid (pure cell quantisation: floor to 16-unit cell centres, with no clamp to the map) and flies to it. No boundary test at any state. |
| Patrol (`VTOL_Patrol`) | `0x410E70` | Aims 320 world units beyond the patrol point (approach radius 336); re-installs the same point every 30 ticks. A patrol point near the edge therefore sends the aircraft past it, repeatedly, and nothing turns it back. |
| Bomber strike (`AirStrike`) | `0x411F50` | Attack-run geometry relative to the target; no boundary test in any state. |
| Standby (`VTOL_Standby`) | `0x40F7D0` | No overflow test of its own: an empty aircraft queues a landing search, which recovers; a loaded one hops 8 to 39 units around the anchor it recorded when it went idle, so it stays where it is. |
| Landing on a pad (`VTOL_Landing`) | `0x4118E0` | Flies to the pad, which is a map cell, so it is already a return. No boundary test. |
| Stop | `0x401C20` | For an airborne unit, queues a landing search at its own position, which recovers through `0x40F2A0`. |

The remaining air missions — pickup, unload, mobile build, help build, repair
patrol, repair unit, reclaim and evade — have no such branch either.

So the answer to "does it turn back on an order or idle transition" is:
**only through the landing handler, or through one of the other handlers that
has the branch.** A new move, patrol or strike order sent toward the interior
naturally flies the aircraft back, because its goal is inside; a move order
sent outside the rendered rectangle is not clamped by the handler, and the
snap preserves the outside point. Whether the user interface can produce such
a click was not audited.

Landing is the one path that cannot complete outside: the physical placement
test used by the landing search (`0x47E2D0`) rejects any footprint outside the
full grid and treats the reserved-border marker `0xFFFD` as occupied, so an
aircraft must be back inside the grid (and off the reserved border) before it
can put down. The landing handler's own recovery branch is therefore what
makes an idle or stopped aircraft return rather than hover outside for ever.

### What RWE should do

Do not add a clamp, a reserved-border rule, or an "aircraft must stay over
terrain" test of any kind. Reproduce the original's shape instead:

1. Define the aircraft's "outside the grid" state from its footprint against
   the **full** terrain tile counts — origin cell below zero, or origin cell
   plus footprint size at or beyond the count — evaluated whenever the unit's
   cell changes, not from the rendered rectangle and not from the world
   position alone. RWE has no sentinel bucket, so this predicate is the thing
   to implement; the rest follows from it.
2. Let air movement ignore terrain entirely: no placement refusal, no cell
   clamp, no half-speed cap, no exclusion-marker rejection, and no ground
   occupancy conflict while airborne. Only landing uses the ground placement
   rules.
3. Implement the 800-toward-centre objective, radius 128 and mask `E0` in the
   six handlers, with the same earlier-exit conditions, and have it re-apply
   every tick while outside. Keep the order's state and target.
4. Implement the strafing handler's different answer — 30-tick deadline and a
   return to its attack state — rather than the centre objective.
5. Leave move, patrol, bomber and standby as they are; a patrol point near the
   edge is supposed to take the aircraft outside, and a loaded standby is
   supposed to stay where it is. Funnel Stop and an empty order list into the
   landing search so they recover.
6. Stop steering altitude outside the grid, and let the ordinary
   terrain-relative goal altitude resume on re-entry. Do not add an altitude
   floor, ceiling or "return to cruise" rule that the original does not have.

A universal air clamp, or a centre-seeking objective on every air order, would
be a deliberate divergence and would need the divergence register's usual
treatment. So would refusing an off-map move order, which the original also
does not do.

### Evidence

- **Boundary and centre.** `0x4833B0` (rendered width minus 32, height minus
  128, plus the border marking); `0x482C20` (the sentinel bucket: a
  zero-initialised record whose flag word is set to `0x1F`); `0x48A980`
  (heading from X/Z only).
- **The outside-grid state.** `0x47CC30` (the footprint test, the `>=`
  comparison, the sentinel bucket at `+0x142B7`); `0x47D0E0` (removal skips
  all cell work for a unit on the sentinel bucket).
- **Movement and placement.** `0x47DB70` (mode-2 short-circuits: outer bounds
  affirmative, in-map affirmative before content tests); `0x43D6D0` (the
  placement test only for controller types 1 and 2, the blocked flag's
  persistence, the clamp and half-speed cap); `0x48A9F0` (position setter, no
  clamp); `0x43DD20` (the per-tick order of goal, steering, position, rate,
  occupancy); `0x43D290` and its `0x43D4C8` height-step test (already in
  `TOTALA-EXE-MOVEMENT.md` section 1); `0x43D210` (flight-mode switch).
- **The recovery.** `0x40F2A0`, `0x40FBE0`, `0x4103E0`, `0x410850`,
  `0x412D40`, `0x413470` (the six), `0x412710` (strafing, 30-tick deadline and
  state 2), `0x44E3C0` (goal position from a followed unit, refusing when that
  unit is on the sentinel bucket), `0x44E6C0` (altitude from the bucket's
  smoothed height byte) and the goal tick `0x490690` (which re-derives the
  goal altitude from the owner's bucket).
- **The handlers without a branch.** `0x40FA20`, `0x410E70`, `0x411F50`,
  `0x40F7D0`, `0x4118E0`, `0x401C20`; and `0x47E2D0` for the landing
  restriction.
- **The dead clamp.** `0x4853B0`; the reference's function table lists no
  call or data reference to it.
- **Order-state advance.** `0x43B7C0`: the answer "progressed" increments the
  one-byte state at `+0x05`, "no change" leaves it, which is why the recovery
  branch re-issues its objective every pass. `TOTALA-EXE-MISSIONS.md` section
  11 called this whole special case inferred; the sites above confirm it. That
  section names three order handlers and the goal resolver; there are six
  handlers, and the strafing handler answers differently.

### Confidence

High that aircraft are never clamped and never refused by air movement, and
that the reserved-border marker is not consulted for air: both follow from
single queries whose comparisons are explicit (`0x47DB70`'s mode return before
any content test, and `0x43D6D0`'s guarded placement call). High for the six
handlers' constants and earlier exits, which are identical to one another and
match BOUNDARY-003. High that the trigger is the full-terrain footprint test
rather than the rendered rectangle: the rendered dimensions appear only in the
centre arithmetic and the border pass.

### Remaining uncertainty

- **The reported route is still unknown.** The owner's identification — some
  aircraft and a commander pathfinding elsewhere — does not say what the
  aircraft were ordered to do. Given the tables above, an aircraft seen past
  the rendered edge, or past the full edge while patrolling, strafing or
  bombing, is behaving as the original does. The report cannot be called a
  defect, or cleared, until the order state and the crossing are captured.
- **No executed original check exists.** This is a static read. The visual
  claim "aircraft cross the terrain" would be settled by watching one patrol
  leg at a map edge in the original; the static reading already predicts it,
  so that check is confirmatory rather than a prerequisite.
- **RWE's own state is not established here.** The implementation room must
  reproduce the crossing first: RWE has no sentinel bucket, so whether its
  aircraft can even reach the predicate in item 1 — and what its air movement
  currently does at the edge — has to come from its own code and a
  reproduction, not from this finding.
- **Off-map combat and presentation were not audited.** Whether weapons,
  targeting, line of sight and rendering of an off-map aircraft have their own
  special cases was not traced; only movement and order handling were.
- **The user interface's order positions were not audited.** The move handler
  does not clamp, but whether the click-to-world conversion can deliver a point
  outside the rendered rectangle was not read.
- **One corner of the altitude rule is inferred.** The sentinel bucket's
  smoothed height byte being zero rests on the record's zeroing at `0x482C20`
  plus the flag store; the branch that would read it off-map does not steer,
  so nothing observable was found to depend on it.

## Implementation contract and remaining checks

1. For the commander report, first reproduce a route across a border cell
   selected by BOUNDARY-001 on a real map. Record the map's terrain dimensions,
   height samples and effective commander/movement-class footprints. Establish
   whether RWE omits the exclusion marking, route refusal, physical refusal,
   or more than one. A boundary in camera space alone is insufficient.
2. Preserve marker semantics when map content already contains ordinary
   feature definitions. Do not silently replace authored features with a new
   universal blocker while claiming literal conformance.
3. Test each edge and corner, raised terrain, both pathfinding backends and a
   direct physical step. Confirm valid interior routes still succeed. Border
   policy is shared by land and naval mode-1 movement; water depth remains an
   additional constraint.
4. For aircraft, exercise idle landing, guarded-unit circling and attack-ended
   search from overflow. Preserve the original ability to fly beyond the
   terrain. An all-aircraft clamp or universal centre recovery would be a
   deliberate divergence requiring the register's usual treatment.
5. BOUNDARY-004 narrows item 4 to a predicate and a set of handlers. Implement
   the outside-grid test from the footprint against the full terrain counts,
   then the six 800-toward-centre objectives and the strafing handler's
   30-tick answer; leave move, patrol, bomber and standby without one. Check
   an idle and a stopped aircraft from outside, a guard of an on-map unit from
   outside, a strafing run whose orbit leaves the map, and a patrol point one
   cell inside the rendered edge. Confirm a move order back to the interior
   returns the aircraft and that an empty order list recovers it through
   landing. Note what RWE's air movement does at the edge *before* the change;
   this finding does not establish that.
6. No executed original-game check is available in this finding. Compare with
   a captured original run or suitable demo episode when available; describe
   static comparison and RWE regression results separately until then. For
   the aircraft report, the capture wanted is the order state of the aircraft
   at the moment it crossed: BOUNDARY-004 predicts no recovery for move,
   patrol, bomber or a loaded standby, and recovery for landing, guard,
   post-attack search and both dogfight orders.

This finding refines the broad boundary summary in
[the aircraft parity audit](BT-ORDERS-VTOL-ORDERS-PARITY.md#3-boundary-recovery):
its description of RWE as already clamping aircraft was an unaudited historical
assumption, not an original-behavior rule. The current implementation room
must establish RWE's actual behavior from its own code and reproductions.
