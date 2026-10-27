# Map boundaries and aircraft recovery

Spec-room finding for [#895](https://github.com/CubeB/RWE-B4/issues/895),
2026-10-27. This records original behavior; it does not identify the exact
route taken in the reported game. The owner subsequently identified aircraft
and a commander ordered to pathfind to another point.

## Evidence and limits

- Reference: byte-tactics revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`, inspected read-only from the
  separate `D:\RWE-boundary-spec` documentation checkout under ADR-0004.
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
5. No executed original-game check is available in this finding. Compare with
   a captured original run or suitable demo episode when available; describe
   static comparison and RWE regression results separately until then.

This finding refines the broad boundary summary in
[the aircraft parity audit](BT-ORDERS-VTOL-ORDERS-PARITY.md#3-boundary-recovery):
its description of RWE as already clamping aircraft was an unaudited historical
assumption, not an original-behavior rule. The current implementation room
must establish RWE's actual behavior from its own code and reproductions.
