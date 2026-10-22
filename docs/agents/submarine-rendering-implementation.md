# Submarine construction shadow implementation

Implementation note, 2026-10-20. Follows the handoff in
[submarine-rendering-handoff.md](submarine-rendering-handoff.md) and the
spec-room findings in
[SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md](../SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md).
The implementation room did not open `D:/bytetactics`; every change below is
written from those prose findings and the existing registers.

## What changed

### 1. An attached factory product casts no independent shadow

`attachedFactoryProducts()` (`src/rwe/game/GameScene_util.cpp`) collects the
products that a factory in `FactoryBehaviorStateBuilding` still names and that
are still `isBeingBuilt`. The shadow loop in
`src/rwe/game/GameScene_render.cpp` skips exactly those units, so:

- a half-built product attached to the build piece generates no shadow and no
  nanoframe cut-out batch;
- a standalone nanoframe is not in the set and keeps the shadow it already had;
- release happens on the final successful build step, because the product's own
  completion is tested as well as the factory's retained reference.

Cites `SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md` section 1 (attachment,
`0x48AB70`, the parent composite at `0x45AC20`/`0x459200`, release from
`0x41B8D0`). Presentation only: the helper reads the simulation and writes
nothing.

RWE still draws the attached product as its own construction mesh; this change
suppresses only its independent shadow. The finding also establishes that the
original draws the product inside the parent's composite image, so the
remaining difference in how yard and product are composed is a separate
render-order question, not a shadow one.

### 2. A submerged mobile silhouette is cut at the water line

`unitShadowIsWaterCut()` returns true only when a mobile unit's drawn origin is
strictly below sea level. `drawUnitShadow` passes that sea level as a cut
height, `UnitTextureShadowMeshRenderInfo` carries it, and
`shaders/unitShadow.frag` discards fragments whose own world height is below
it. Projected building and feature shadows pass `NoCutBelowY` and are
unaffected.

Cites `SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md` section 2. The finding gives
the original's encoded threshold as `sea level - H + 50` with the height
buffer's bias of 50; against the fragment's world height that is "keep at or
above sea level". The exact byte boundary is not stated in the prose, so the
strict deletes/keeps choice at exactly sea level remains a one-unit
uncertainty.

The old comment that a finished unit's copied shadow is never water-cut has
been corrected in `GameScene_util.h`: that was the older reading the new
finding qualifies.

## Verification performed

- `[noshadow]` Debug tests: 6 cases / 15 assertions, covering attachment,
  release, the non-building factory states, and the water-cut predicate.
- The complete permitted Debug suite passed after the final tree:
  **1850 cases / 193890 assertions** with `~*checksum*`. The five packet/lobby
  checksum cases then passed separately (33 assertions). The two `TaChecksums`
  cases were not run, under the standing instruction. An earlier attempt in
  the same session hit 24 socket-`bind` failures; a re-run passed every case,
  so those were environmental port contention, not test results.
- Both shadow changes were built into `rwe_test` and `rwe`, and a scenario
  through the real `GameScene` loaded the shaders and passed
  (`paused-place-select`), which is a session-level check that the changed
  fragment shader compiles and the world pass runs.
- `tools/section-budget.sh build 75` passes; the worst object is still
  `GameScene.cpp` at 24276 sections, unchanged.
- `build-release/rwe.exe` was rebuilt from the final tree for the owner's play
  check.
- No original-game executable check, demo oracle or visual play-test was run.
  These are static-reference-driven changes checked by RWE tests and a
  scenario, not executed parity.

## Yard overlap: investigated, not changed here

The reported "displayed under the shipyard" after completion is real and
follows from the second half of the findings, but reproducing it is a renderer
ordering change, not a shadow change.

RWE draws units and buildings into one batch and resolves their overlap with a
shared camera-depth buffer. That buffer holds only the unit batch: terrain is
drawn with depth off (`GameScene_render.cpp`), so the relevant depth is
unit-versus-unit, and the depth axis is world Y. The original instead paints
whole images in coarse world-Z rows of 16 units, later row wins
(`TOTALA-EXE-RENDER.md` section 5,
`SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md` section 3), with no depth
comparison between those images. A seabed submarine therefore paints over the yard in one row and under
it in another.

An implementation was prototyped in this session: each ground object's depth
was remapped into a 16-unit row slab, with a sub-slot for visible-list order
inside a row, air units in the last rank and projectiles between ground and
air. It passed new unit tests for the row and same-row ranking. It was
**withdrawn before delivery** for two reasons:

1. The remap changes the depth of every unit from its world-Y value to a row
   slab spanning the depth range. Other depth-tested geometry drawn after the
   unit pass -- line and sprite projectiles, sprite features and their shadows
   -- compares against that buffer. Those objects would inherit row-dependent
   occlusion (for example, a bullet hidden behind a unit in a late row) without
   themselves being painter-ordered. That is a visible change beyond the
   reported one and was not visually checked.
2. Making the frame coherent means giving those later passes the same painter
   rules too, or matching their explicit layers, which is a wider render-order
   change than this handoff. The handoff explicitly warns that reordering a
   list under the shared depth test may not reproduce image-level painter
   order.

The prototype is not in the tree. The recommended next step is a dedicated
render-order change:

- decide the frame's ground rows first, from the camera and each object's
  whole world Z;
- give every object that the original orders as an image the same row key,
  including sprite features and projectiles, or draw the passes that the
  original layers after the row pass with the depth test off;
- keep each model's own depth for self-occlusion, and keep terrain out of the
  comparison (it already is);
- verify with the owner's visual check on stock ARMSY/ARMSUB at seabed depths
  30 and 60, on both sides of a row boundary and with a displaced camera.

Until then the existing divergence stands: RWE has a depth buffer where the
original has painter rows. No new §88 entry is proposed for this session,
because behaviour was not changed; the entry belongs with the render-order
change when it is made. The two shadow changes above are conformance to the
new finding and need no divergence entry.

## Remaining uncertainty

- Attachment is inferred from RWE's factory state. The finding notes that
  attachment ends on the final successful build step while the factory's
  bookkeeping may retain the child longer; the `isBeingBuilt` test aligns the
  renderer with the former. Remote-owner timing remains unverified.
- The water cut is applied to mobile units only, as the finding describes the
  copied-silhouette path. Whether any projected building shadow is also cut
  underwater is not established; RWE leaves it uncut.
- No measured pixels, map, pose or original capture exist for the owner's
  report. The acceptance work in the handoff still applies.
