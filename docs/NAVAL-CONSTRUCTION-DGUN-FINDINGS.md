# Naval construction height and D-gun water behaviour

Spec-room findings, 2026-10-16. Stable anchors in this document do not allocate
new section numbers in the main register.

## Evidence and scope

This is a static comparison against byte-tactics revision
`97d7ac59b460417bcdfa629cf90da99f31818c65`, inspected read-only. The local
original executable has independently checked MD5
`8e74a1dffa1f5988624c52048f5b20cd` and SHA-1
`764dc919c3bd0365751aefba8e9a667299a3ce2e` (the corpus's GOG v3.1 reference).
The source working tree reported no tracked or untracked changes on inspection.
The matching project's complete-build claim was read; no compiler matching
check or executable rebuild was run in this investigation.

Real data were read from `D:/RWE-extract/totala1` and the overriding FBI files
in `D:/RWE-extract/rev31`. Model geometry was independently decoded from the
3DO files, without running the game. These are local extracted-data snapshots,
not proof of the active archive precedence in a particular game installation.
For reproducibility, SHA-256 of `totala1/objects3d/armsub.3do` is
`6c5cc65c81df757e84353ae9fdf9bb88a6bec29397ba0cc9f69117921ecb9055`,
`corsub.3do` is
`4d8028137ed6c5d5222145ace0ad4c092e6831322e53128675b2b1313c512e4a`,
and `totala1/weapons/WEAPONS.TDF` is
`2a9f88fd64da13fd71ee30bb758a5310cce784511f3ae17640b18239a4841a17`.

**No original-game runtime experiment was executed.** No demo measurement,
HP capture or original save was collected. All expected results below are
static findings or explicitly marked predictions. RWE source and tests were
not inspected or changed. This document establishes reference behaviour,
not the cause of the reported RWE regression or an implemented fix.

## Construction height

### Build-piece position is an input, not the final height

At `0x402640`, a factory waits for its script's build stance, queries
`QueryBuildInfo`, obtains that piece's world position, and checks the product's
footprint at its horizontal coordinates. It then creates an unfinished product
and attaches it to that piece. A blocked footprint causes a retry after 15
ticks; inability to allocate a product causes a retry after 300 ticks.

Creation at `0x485F50` immediately runs normal height placement (`0x48A870`)
before map insertion and before the subsequent attachment. Consequently a
product's first registered height need not equal the build piece's height.
There is no submarine-specific subtraction of 20 in the factory handler.

World coordinates and model offsets are 16.16, with 65536 integer increments
per world unit. Piece positioning (`0x43DEF0`, `0x43E060`) includes the piece's
model offset, script translation, parent transforms, factory orientation and
factory world origin. It is not simply the factory's origin with an added
vertical constant.

The accompanying extracted ARM shipyard BOS text chooses its slip piece. Its unanimated
3DO offset from the factory root is (0, -8.75, 0) world units. The CORE shipyard
BOS text chooses its base piece, whose unanimated root offset is zero. Script
animation can alter these positions; compiled COB instructions were not independently
compared with the accompanying BOS text in this pass. The rest offsets alone do not establish
the live position when build stance is reached. The actual model and scripts
must therefore accompany any reproduction. Both are stationary factories in
these data; hypothetical moving factories are not evidence for their behaviour.

### Ordinary placement also applies while a submarine is attached

Attachment (`0x48AAC0`, `0x48AB70`) selects movement mode 1 for factory products.
At `0x43D6D0`, an attached product follows the selected piece in all three
coordinates and adopts its orientation. A product with the raw FBI `Floater`
flag has that piece height limited from below by sea level minus `WaterLine`.
When a carrier has movement, its velocity is copied; otherwise the attached
product's velocity and speed become zero.

That is not the end of the unit update. At `0x48AD30`, orders run before motion,
and ordinary height placement follows motion. Position-setting marks the
height dirty even if the piece has stayed in the same cell. `0x48A870` performs
placement for a mobile product in mode 1. An `Upright` unit without `CanHover`
takes the terrain height under its centre. This remains true for an attached
unfinished submarine: no unfinished-product or attachment exemption appears
in this height-placement rule.

In the checked ARM and CORE submarine FBI data, `Upright=1`, `WaterLine=20`,
and raw `Floater` and `CanHover` are absent. Thus their registered origin height
is **the seabed**, including during construction after the placement pass.
The intermediate piece-following height and the final registered height must
not be conflated. Lowering every submarine origin by its `WaterLine` from sea
level would not reproduce this static trace.

Other branches explain why one generic water-unit rule is unsafe: a raw
`Floater` which is not `Upright` is placed at sea level minus `WaterLine`;
a `CanHover`/`Upright` combination is limited by the greater of terrain height
and that waterline height; other ground units use terrain alignment. These
conditions describe raw parsed flags, not a port's broader water-building class.

### Shipyard waterline matters through building placement

The FBI parser at `0x42BF40` reads raw `Floater`, `CanHover`, `Upright` and
`WaterLine` separately. The inspected loading pass does not derive `Floater`
from an underwater category, a movement class or a water yard map.

For a stationary building, placement uses its yard footprint. At `0x47D820`,
when there are no terrain-supporting cells in the footprint mask, the origin
height is sea level minus `WaterLine` (the result is an unsigned byte).
The checked shipyards use water/yard cells of this kind: ARMSY has
`WaterLine=1`, CORSY has `WaterLine=22`; both have `MinWaterDepth=30`.
Their own initial roots are therefore below sea level by different amounts.
The factory root and its build piece must not be assigned one shared sea-level
height merely because both factories float visually.

This qualifies the old weapons-register section 12. Its statement that
`WaterLine` has only two readers and matters only for water-state reporting
is incomplete. `0x48A870` and `0x47D820` are additional placement uses.
Its warning that raw `Floater` differs from a broader water-building class
remains sound. Neither warning proves that submarine origin height is
sea level minus 20.

### Depth gates and completion

The referenced movement class replaces FBI footprint, water-depth and slope
limits during loading (`0x42BF40`). Both ARMSUB and CORSUB name `BOATD3`.
The checked `MOVEINFO.TDF` supplies a 3 by 3 footprint and minimum depth 15,
so the raw FBI `MinWaterDepth=20` and CORSUB's raw 4 by 4 footprint are not the
effective movement limits. Absent maximum depth defaults to 10000 in the
movement-class reader. Archive/mod precedence can change these results.

For ordinary mobile placement in mode 1 (`0x47DB70`), every footprint cell
must satisfy the effective depth limits, occupancy, blocking and slope tests.
Terrain low must be at least sea level minus maximum depth; terrain high must
be at most sea level minus minimum depth. Equality is permitted for these
depth bounds. The factory queries this placement at its build-piece horizontal
position. It does not establish legality from the piece's vertical coordinate.

Successful build work is charged before progress completes (`0x41BA60`). The
completion notification (`0x41B8D0`) detaches the finished mobile product for
an active locally simulated human/computer owner, at the final successful work
step. The factory's later completion stage repeats notification and retires
its product reference. No separate submarine diving animation, deferred dive
timer or mandatory wait for a rally move was found in these transitions.

An individual unit runs orders, motion, then height placement. Whether a newly
created or newly detached product gets its own pass later in that same global
tick depends on its position in the owner/unit iteration; otherwise its next
pass is the following tick. This is an ordering dependency, not a promised
extra tick of surface residence. The ordinary upright seabed rule applies
both before and after completion.

## D-gun eligibility, water collision and damage

### Shipped weapon attributes

The checked base data give both commander disintegrators range 240,
reload time 1.2, energy per shot 400, velocity 200, area of effect 48 and
default damage 5500. They enable line-of-sight flight, turret, beam,
`NoExplode` and `CommandFire`. They omit `WaterWeapon`, `UnitsOnly`,
`GroundBounce` and `EdgeEffectiveness`, whose parser defaults are false,
false, false and zero. These are the shipped commander weapons, not the
mod weapon MINDGUN. A mod's `UnitsOnly` setting changes the following collision
result and is not a substitute for checking these attributes.

### A fully submerged target normally fails acquisition

At `0x49ABB0`, a non-water weapon requires both shooter and target to satisfy:
whole-unit origin height plus whole-unit model height is strictly greater
than sea level. Equality rejects. It then applies flat range and any other
weapon restrictions. There is no ship-category exemption or D-gun immunity
flag. A surface ship with its model top above the water passes the water gate.
A submarine wholly below it fails, as can a submerged commander firing it.

A ground-position shot uses `0x49AA80`: it checks flat range and the shooter's
dry gate, without a target-unit height gate. Thus failure to acquire a submarine
is not proof that a manually aimed nearby detonation cannot hurt it.

Model height is obtained at loading (`0x4CB5F0`, `0x42D2E0`) from the highest
vertex including hierarchy offsets. The eligibility gate reads its whole-unit
part. Independently measured unanimated heights are ARMSUB 14.0652923584,
CORSUB 15.6478118896, ARMPT 14.9999847412, CORPT 12.9999847412, ARMCOM
39.9999847412 world units. The gate consequently uses 14, 15, 14, 12 and 39.
At legal flat-water depth 15, both checked submarines fail dry targeting,
including CORSUB whose fractional geometry top extends slightly above the
surface. A submarine at an integer origin depth below its whole model height can pass;
such a position is outside the checked BOATD3 flat-water placement limit and
needs a slope, altered data or another explicit setup, not an assumed normal
shallow-water case.

### Water detonations do not consume this projectile

Collision at `0x49B090` checks enemy unit occupancy and vertical model bounds
before terrain/water. A first occupancy layer checks the upper bound only;
the other also checks the lower bound. The firing player's units do not
trigger direct collision. `UnitsOnly` would skip the subsequent feature,
terrain and sea-level collisions; the shipped disintegrators do not set it.

A non-water projectile below sea level ordinarily detonates; exactly at sea
level it does not trigger that particular test. Terrain collision and direct
unit collision can still detonate it. At `0x499EB0`, `NoExplode` prevents an
ordinary detonation from marking the projectile dead. Therefore the D-gun can
continue underwater and detonate on later ticks. There is no general rule
that its first water crossing removes its blast or zeros its damage.
The separate crossing effect in `0x49B720` is presentation, not protection.

A map option that suppresses sea-level triggering is an exception: water-only
collision is skipped, and a no-unit detonation above underwater terrain is
marked dead and returns before damage in that special map mode. A direct unit
hit does not take that early return. Ordinary water is the baseline; record
map settings when comparing results. Out-of-map removal also bypasses damage.

### Splash can hurt ships and submarines

Area damage at `0x49A120` has **no underwater filter**, no water-depth scaling
and no submarine category immunity. The radius is half area of effect, hence
24 world units here. Distance is the three-dimensional distance to the unit's
unanimated axis-aligned collision box, reduced to whole units. Damage is
applied only when that whole distance is strictly below 24. The zero-distance
scale is one; otherwise the zero-edge scale is `(1 - distance/24)^2`.
The floating damage product is truncated to integer before subsequent damage
modifiers. This can produce a lethal hit on a near-surface submarine even when
it could not be selected by this weapon.

The firing unit alone is excluded from the area candidates; other friendlies
can be hurt. `0x499CD0` applies normal weapon damage, veterancy and global
cheat multipliers. `0x489BB0` applies armoured-state damage modification for
amounts below 30000 and target veterancy. No water-dependent halving, clamp or
zero appears in either path. The shipped 5500 is below that armour bypass
threshold; a D-gun hit is not an unconditional scripted kill.

**Expectation verdict:** blanket immunity for surface ships is refuted by
this static trace. Failure to directly acquire a normally submerged submarine
is supported. Blanket immunity for submerged submarines from manually aimed
shots, nearby blasts or a projectile continuing underwater is refuted.
Whether the reported RWE event is a mismatch remains unestablished without
its exact command, geometry, map settings and unit/data identities.

## Original-runtime checks still required

Use the reference executable hash above, stock archive precedence and a flat
water fixture with documented sea and seabed heights. Record tick, owner,
unit type, HP before/after, origin height, command kind, muzzle/impact position
and completion fraction. Visual appearance or a splash sprite alone is not
an HP result. Keep firing units at zero kills and disable damage cheats.

| Case | Concrete setup and static prediction to check |
| --- | --- |
| ARM construction | ARMSY builds ARMSUB over flat depths 30 and 60. Record creation, first unit update, half-built, final work tick and first detached update. Final origin follows seabed, rather than assuming sea minus 20 or the slip height. |
| CORE construction | CORSY builds CORSUB over the same depths. Distinguish the factory's sea-minus-22 root from the product's seabed origin. |
| Build blockage | Block the selected product footprint, then clear it. Record the 15-tick retry; repeat across a depth boundary using effective BOATD3 limits. |
| Surface target | ARMCOM versus enemy ARMPT (560 HP) and CORPT (570 HP), fully built and stationary. Direct special attack is water-eligible. Record whether a blast occurs and the actual HP result. |
| Deep target acquisition | ARMCOM versus enemy ARMSUB (610 HP) or CORSUB (590 HP) at flat depth 30 or 60. Direct unit attack should fail the target dry gate; ground-position special attack is a distinct experiment. |
| Near-surface splash | Same submarines at legal flat depth 15 and greater depths. Ground-fire nearby, recording the detonation's distance to the box and HP. Eligibility remains rejected at 15; splash protection is not predicted. |
| Depth boundary | Sweep depths around 14, 15, 16 and 24. Mark positions below effective placement limits as specially constructed cases. Collision geometry and whole-height eligibility have different boundaries. |
| Shooter submerged | Move ARMCOM through water until its whole origin plus 39 is at/below sea level, using an explicitly attainable fixture. Check both ground and unit shot gates. |
| Flags and maps | Separate stock commander weapons, a controlled UnitsOnly variant, ordinary water, and the map option suppressing sea-level triggering. Do not combine their outcomes. |

No row in this table has been executed in this investigation. Acceptance of
an implementation needs the applicable original-runtime evidence or an
explicitly recorded remaining static-only limitation.

## Prioritized remaining research

The older register and `PORTING-125-138.md` already resolve much of the basic
repair, patrol, pad and classic-AI behaviour. This pass did not reverify their
unresolved details; no new conclusion on them is implied by the naval work.

1. Run the naval matrix above and obtain the reported RWE scenario before
   selecting a cause. Confirm active archive precedence and live shipyard
   animation offsets as part of that evidence.
2. Repair/patrol/airbase: exact within-cell candidate ordering, metal-versus-
   energy reclaim draw ordering, mission wake-up timing versus phase polling,
   return-leash comparisons, waypoint retirement, cached 30-tick pad snapshots,
   occupied-pad retry/selection and unarmed circling radius. Preserve the
   explicitly deferred sections 132 and 138.
3. Network: status 0x20 and player-blob unresolved meanings; 0x19 speed scale,
   pause and 0x0f quit burst. Require real original-peer captures to distinguish
   wire layout from accepted/rejected runtime semantics.
4. Classic AI: sequential score multiplier rounding, candidate enumeration,
   low-wind threshold, net-income definition, placement distances and absent
   water-key handling, scout positions, group position/centroid and movement
   leashes. Existing implementation tests do not settle these source questions.
5. Demos: final six 0x28 floats/cumulative triples and common unknown records.
   Identify writers and cumulative/reset conditions, then compare successive
   original records across spending, reclaim, transfer, death and game end.
   Do not label an accumulating number income merely from monotonic samples.


## RWE implementation status, 2026-10-16

Written by the implementation room from the findings above; it did not open
byte-tactics. Every change below is checked against these findings only, which
are a **static** comparison. **No original-game runtime check was run for any of
it**, and the runtime matrix above is still outstanding. Tests are RWE tests
and establish that the code does what this document says, not that the
original does.

### Already matching, left alone

- Submarine products settle on the sea bed, both at creation and every tick
  while attached (`trySpawnUnit`, the factory build handler): `Upright` without
  `CanHover`, `WaterLine` ignored. Shipped data: no floater sets `WaterLine` or
  `Upright`, so the raw-flag branches for floaters and hovercraft cannot differ
  from RWE's `max(terrain, sea)` on shipped units.
- Yard retry of 15 ticks on a blocked pad; depth bounds inclusive at both ends;
  shipyard root at sea level minus `WaterLine`.
- `NoExplode` keeps the disintegrator alive through water and it detonates on
  later ticks; the area routine has no underwater filter and exempts only the
  firer; the firer's own units are not hit directly.

### Changed

| Finding | RWE change | Test |
| --- | --- | --- |
| Water gate reads whole-unit origin plus whole-unit model height, strictly above sea | `GameSimulation::modelTopIsAboveSea`; CORSUB at depth 15 is no longer a legal target | `dgunorder.test.cpp`, "obeys the water rule" |
| Point shot checks the shooter's gate only | `weaponCanFireFrom`, used by ordinary attack orders on a point | same |
| D-gun has no exemption from the gate | `handleDgunOrder` now applies it: refused order ends and fires nothing | same |
| Below sea level detonates, exactly at sea level does not | sea test `y < seaLevel` | `seacollision.test.cpp` |
| `unitsonly` is tested before features | `unitsonly` rounds pass through wrecks | `unitsonly.test.cpp` |
| Splash distance is whole units and must be strictly under the radius | `blastDamageScaleForUnit`; radius 8 or under keeps the old single-target treatment | `dgun.test.cpp` |

### Assumptions to confirm

- A refused D-gun or point order is **dropped** (the order ends). The findings
  say acquisition fails; they do not say what the order does afterwards.
- "Reduced to whole units" is implemented as truncation (floor). Rounding would
  differ on every fractional distance.
- The gate and cut-off are applied to the blast with the same radius for odd
  `AreaOfEffect` (a half-unit radius) as for even; no shipped commander weapon
  has one.

### Not implemented, and why

- The `nosealeveltrigger` early return before damage: the findings do not say
  which terrain test selects "above underwater terrain".
- The upper-bound-only test on the first unit slot: which of RWE's occupancy
  layers is the original's first slot is not established, and the effect is
  confined to rounds below a unit's origin.
- The 300-tick retry when a product cannot be allocated: RWE has no such
  failure.
- The effective BOATD3 footprint on a unit whose FBI names a larger one is
  already taken from the movement class; no change.
