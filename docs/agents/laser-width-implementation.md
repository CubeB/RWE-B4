# Laser beam width implementation

Implementation note, 2026-10-20, for issue #723. Follows the spec-room finding
in [LASER-WEAPON-FINDINGS.md](../LASER-WEAPON-FINDINGS.md) (exact copy of
`D:/RWE-spec-room/docs/LASER-WEAPON-FINDINGS.md`, SHA-256
`E79C423A2EC343EBBADE97CE5766F6C6715B616BCECB7C1F6A483BB58868D329`).

The owner re-tested the issue and reported that the flash and smoke now look
acceptable; the remaining fault is that RWE's beams are not as wide as the
original's. This change addresses only the beam geometry. The impact flash and
smoke paths are untouched, and the finding confirms they do not belong to the
beam draw.

## What the finding establishes

A render-type 0 shot is two one-pixel lines in screen space:

- the core, on the projected head-to-tail segment, in the first colour;
- a second line in the second colour, one scanline **up** when the segment is
  more horizontal than vertical, otherwise from one pixel **left of the top
  end to one pixel right of the bottom end**, crossing the core;
- at 1:1 the pair reads as a two-pixel beam;
- a second colour of zero suppresses the companion entirely;
- both lines are opaque, unblended, and the beam has no depth test in the
  original.

The original's projection is fixed at one world unit per output pixel, so the
width is a screen constant at that scale. In a zoomable renderer the faithful
equivalent is a fixed **world** width of one unit per line, which is one
original pixel at 1:1 and grows with the camera's pixels-per-world-unit.

## Changes

- `ProjectileRenderTypeLaser` gains `hasCompanion`, set from the raw
  `color2 != 0` in `LoadingScene_util.cpp`. Before this, a zero second colour
  was converted to palette black and still drawn, so ordinary lasers such as
  `ARM_LASER` had a black companion the original does not draw.
- `appendLaserBeam` in `GameScene_util.cpp` replaces the two `GL_LINES`
  segments. Each line is a one-world-unit-wide ribbon in the projection's
  screen plane; the core is added first so a shared pixel keeps the core
  colour, and the companion follows the horizontal/vertical rule above.
- `drawProjectiles` sends the beam to `ColoredMeshBatch::triangles` instead of
  `lines`. The palette conversion is unchanged; the finding confirms RWE
  already matches it.

Why world-space one-unit ribbons: RWE's projection maps world X directly to
screen X and `z - y/2` to screen Y. One world unit of either is one output
pixel at the fixed 1:1 scale, and the same ribbon becomes two pixels at a
zoomed-in camera. A fixed device-pixel width, which the old `GL_LINES` default
gave, would keep a hairline however far the camera zoomed in.

## Verification

- `[fogeffects]` Debug tests pass, including new cases for the one-line
  `color2=0` weapon and for the horizontal and steep companion geometry.
- The complete permitted Debug suite and the five packet checksum cases were
  run; results are in the session report.
- No original-game executable check, demo oracle or owner visual confirmation
  was performed for this change. The spec-room finding is static, and the
  black-box captures it lists remain the acceptance test.

## Remaining differences, not changed here

- **Depth.** RWE still draws beams in the world pass with the depth test and
  depth writes enabled, so a ridge or a unit can hide part of a beam. The
  original has no world depth buffer and paints the beam over the early unit
  pass. The finding lists this separately from the width and the owner's
  report is about width, so the depth/order change is left for its own pass.
- **Paint order.** The original paints beams under explosions, the airborne
  pass, health bars and damage smoke. RWE draws them after all unit meshes and
  before the feature/particle/flash passes. Also a separate ordering question.
- **45-degree pinch.** The finding's two Bresenham lines cross and pinch to one
  pixel at exactly 45 degrees. A one-unit ribbon approximates the two lines;
  horizontal and vertical beams are exact, but the 45-degree pinch is not
  reproduced pixel for pixel. The finding itself records that the predicted
  footprint comes from step arithmetic and needs a capture.
- **Length.** RWE still doubles the TDF duration when building the beam and
  uses its own tail timing. The finding says the width must not depend on it;
  the length gap remains as recorded in
  `BT-WEAPONS-WEAPON-TYPES-PARITY.md` section 2.
