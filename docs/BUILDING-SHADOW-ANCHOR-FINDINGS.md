# Building-shadow origin and terrain sampling

Spec-room static findings, 2026-10-22, under ADR-0004. This supplements
[TOTALA-EXE-RENDER section 100](TOTALA-EXE-RENDER.md#100-two-shadow-passes-a-units-shadow-is-a-copy-of-its-own-sprite-a-buildings-is-a-projection)
and the later submarine construction/shadow findings. No RWE implementation
or tests were inspected or changed in this pass.

## Finding BUILDING-SHADOW-01: the anchor follows the unit's ground sample

The original places both mobile silhouette shadows and projected building
shadows relative to the unit's horizontal world origin, at the sampled terrain
height beneath that origin. The shadow image has its own internal origin,
accounting for the model's bounds; image top-left coordinates are therefore
not the unit's anchor coordinates.

With whole world coordinates X and Z, unit-origin height H, sampled ground
height G, and camera offsets Cx and Cz, the original anchor arithmetic is:

| Image | Screen X | Screen Y |
| --- | --- | --- |
| Unit picture | X - Cx + 128 | Z - Cz - floor(H/2) + 32 |
| Shadow picture | X - Cx + 133 | Z - Cz - floor(G/2) + 32 |

The five-pixel rightward bias is a **screen-space image-anchor bias** shared
by the two shadow kinds. It is additional to the building's projected model
shape, rather than a complete description of where every shadow pixel lies.
The vertical anchor difference is floor(H/2) minus floor(G/2).

Building-shadow geometry is projected from posed vertices relative to the
model origin. For a model-local vertex at horizontal x,z and local height h,
the original shadow image receives x plus floor(h/4) horizontally, and
negative z minus floor(h/4) vertically, subject to the original integer
quantization. This local height does not include the map's absolute elevation.
The shadow picture is subsequently anchored at the terrain height beneath
the unit. Applying the map's absolute ground height as though it were local
model height would introduce an elevation-dependent horizontal displacement
that is absent from this reference calculation.

The projected shape is rasterized from faces into a bounded image, with an
internal origin derived from those projected bounds. It is not a ground-plane
ray intersection at every world-space terrain point. The ground sample moves
the whole shadow image's vertical anchor; it does not deform the image to fit
the terrain underneath each projected vertex.

## Finding BUILDING-SHADOW-02: ground sampling is a coarse integer interpolation

The ground sample uses the unit origin's whole horizontal coordinates, dropping
their fractional components. It identifies a cell on the 16-world-unit terrain
grid and interpolates the four adjacent stored height values in two stages:
first along X on each of the two Z rows, then between those results along Z.
Each stage performs integer arithmetic, with division truncation, rather than
retaining a floating-point bilinear result until the end. There is no clamp
to sea level. Outside the valid region with four available samples the return
value is -1.

Consequently a triangle-mesh intersection or smooth floating-point bilinear
sample can differ from the original on uneven terrain. The evidence here does
not quantify that difference for the reported CORE buildings or map. An exact
original comparison needs their world position and shipped height data.

## Interpretation of the reported detached shadow

The owner supplied `Screenshot 2026-10-22 170301.png`, reporting a detached
shadow roughly 100-150 screen pixels from CORE buildings. This pass has not
measured that screenshot or identified the dark regions in it. A five-pixel
anchor bias alone does not explain such a displacement. Tall local model
geometry legitimately extends projected pixels away from the anchor, and a
unit-origin/ground-height difference moves the vertical anchor. Neither is
evidence for a large universal offset. Check map elevation, model-local height,
screen/world conversion and image bounds separately before choosing a fix.

An original-game comparison on flat ground, repeated at different absolute
terrain elevations with the same model pose, isolates unwanted use of absolute
elevation in the local projection. On slopes, compare the one origin sample
separately from any vertex-based ground sampling. These are proposed acceptance
checks, not checks already executed.

## Evidence and limits

- Read-only byte-tactics revision:
  `e89721c46c3fa06e59558419b54fe26061fafb73`, clean checkout.
- Reference build: GOG/v3.1 `TotalA.exe`, independently measured installed
  executable MD5 `8e74a1dffa1f5988624c52048f5b20cd`.
- Static routines: independent picture/shadow placement at `0x459200`;
  projected building bounds at `0x45A510`; projected building face rendering
  at `0x45A610`; terrain-origin sampling at `0x485070`.
- The existing register already establishes the two shadow kinds and the
  five-pixel screen-X bias. This pass directly checked those conclusions
  against the stated byte-tactics revision and adds the local-height/absolute
  elevation distinction and terrain sampling detail.
- No original game was launched, no original rendering was captured, no
  screenshot was measured, and no demo oracle or matching checker was run.
  This finding is a **static reference check**, not an executed original-game
  validation or a diagnosis of the specific screenshot.
- No active archive stack, CORE model, building pose or screenshot coordinates
  were established in this pass. Those remain necessary for a measured
  screenshot-specific result.
