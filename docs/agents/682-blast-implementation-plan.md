# #682: blast reach on multi-cell features

Status: implemented; Debug game and focused validation passed (65 cases,
4503 assertions, including eight blast cases). Full batch validation pending. The #681
prerequisite passed 57 focused cases / 4348 assertions before these source edits.
Authority: owner-approved scheduling in the continuation brief and explicit
2026-10-19 approval of the deterministic active-sprite policy. That approval is
recorded in spec-room section 88, documentation commit `9d39950e`.

Reference: BT-47, BT-256 and BT-257, byte-tactics
`97d7ac59b460417bcdfa629cf90da99f31818c65`, retail GOG v3.1. Static comparison
only; no original-game check or demo oracle has run.

BT-260 additionally establishes original ground-unit, airborne-unit, feature
ordering within each scanned cell and deferred unit death processing. RWE's
existing feature-before-unit order and immediate unit-death pipeline remain an
unported adjacent gap. This geometry change preserves those existing behaviours;
it does not claim full area-effect ordering parity or change unit coverage.

Shipped fixture values, independently read with tools/hpi.py:

| Definition | Archive and path | Footprint | Damage/HP | Kind |
| --- | --- | --- | ---: | --- |
| DragonsTeeth | totala1.hpi, features/all worlds/dragonsteeth.tdf | 2x2 | 4000 | armdrag model, height20 |
| RockTeeth | same | 2x2 | 2000 | rockshurt2 / rock4b sprite, height20 |
| SMALL_BUILDINGEX | totala1.hpi, weapons/UNITS.TDF | AoE205 | 40 | ordinary feature-damaging blast |
| DryRock07 | totala1.hpi, features/desert/rocks.tdf | 3x3 | 2000 | dryrocks / rock07 sprite, height5 |

The integer interpolation fixture uses actual Coast To Coast.tnt heights at
(12,0), corners 84,84,84,83, independently extracted from RWE-Data/totala2.hpi.
The patch is translated to a controlled test position. At half-cell offsets,
staged signed division gives84 while float bilinear interpolation gives83.75.

Feature reach uses whole radius102 for that weapon, not the stored half-AoE
float102.5. A covered cell may qualify after an earlier cell failed. Once hit,
record the origin identity subject to the existing 64-entry cap. Ordinary sprites
use the scanned cell's shifted centre and terrain height. A model's scanned
origin alone uses its stored position; its continuations use shifted centres.
Active sprites skip reach and hit-list accounting under the approved policy.

Implementation constraints:

- Convert between RWE's centred world coordinates and the original's map-origin
  coordinates explicitly. Build the feature window from floor whole-world
  coordinates followed by truncating division by16, with an exclusive upper edge.
- Use the shifted point's four terrain corners and staged signed integer
  interpolation; do not use the normal floating terrain sampler or clamp to sea.
  Missing corners return -1. Keep arithmetic bounded for external footprints.
- Preserve the existing unit sweep's bounds, radius and eligibility while
  replacing feature reach. A union of windows must not accidentally let units
  from extra cells into the old unit pass. BT-260 documents the separate
  per-cell ordering gap; do not change that in this geometry patch.
- Keep new geometry in a separate translation unit. Source must stay unchanged
  while any make is running in either build tree.
- Save/hash need no additional mutable state for geometric reach. hasModel from
  #681 supplies static model identity. Future #683 temporary sprite states must
  join the active-sprite predicate when those states exist.

Planned regressions: low-side near-edge rejection; high-side continuation reach;
failed first cell followed by a qualifying continuation; exactly-once damage;
model origin versus continuation Y; fractional radius/strict boundary; staged
terrain interpolation and missing-corner sentinel; unitsonly suppression; 64-entry
overflow with an active sprite proving the approved deterministic accounting.
Map coordinates and boundary health are test conditions; feature and weapon
parameters come from the shipped values above.

Implementation uses a separate FeatureBlast translation unit and the union of
the feature and existing unit windows. The old unit window and rectangle test
remain explicit gates. Strict squared distance against the whole radius avoids
a square root; under ordinary map distances this is the same reach predicate
as floor(distance) < radius. Extreme fixed-point overflow is not reproduced or
claimed. RWE still represents positions as SimScalar floats; this is not a claim
of bit-for-bit original fixed-point arithmetic for arbitrary external positions.

New regressions cover both history cases: an active sprite plus63 ordinary
features allows the model to be remembered as the64th identity (one hit), while
64 ordinary features fill the list and the following model takes four hits via
its four covered cells. This preserves the cap rather than removing overflow.
