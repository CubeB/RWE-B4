# Envoy loading-bay rendering: static comparison

Date: 2026-10-21. Spec-room work under ADR-0004, from a separate documentation
checkout. This document records static reference checks, not an executed
original-game observation. No implementation or tests were changed.

## Reference and evidence

The reference is the GOG v3.1 `TotalA.exe` represented by byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`. Its recorded executable SHA-256 is
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
The decompilation was inspected read-only. Only behavioural findings and
addresses are transcribed here.

Shipped-data checks used the extracted `totala1` and `rev31` trees at
`D:/RWE-extract`. Asset SHA-256 values:

| Asset | SHA-256 |
| --- | --- |
| `totala1/objects3d/cortship.3do` | `f63d6cc61bbfa7a5560c8cfa0e00bd710494580e934eea0eebf8c907d71eae1f` |
| `totala1/scripts/CORTSHIP.COB`, identical to `rev31/scripts/CORTSHIP.COB` | `707f7e1a45ca85ee3fa4a7d918089e614c80d4b4d76f1d426c62ee8878d45370` |
| `totala1/scripts/CORTSHIP.BOS` | `84d4cd09d49662c59792033674361b35b2a5150219fe017cd3764795bffbfb63` |
| `rev31/UNITS/CORTSHIP.FBI` | `06050e64e19b23b7a274484b58212f7d6f741111e5b47680f7942df92d0eb8b7` |

These checks establish the identity of the extracted files; archive extraction
was pre-existing and was not repeated during this investigation.

## ENV-01: The loading doors have no reverse faces or thickness

`CORTSHIP.FBI` identifies the Core Envoy, uses `CORTSHIP` as its model, and
declares `BMcode=1`, `ZBuffer=1`, `Scale=0.5`.

Each of `door1`, `door2` and `door3` in the model contains five vertices and
exactly one primitive: a textured quad using `CorSea5b`. None declares a
selection primitive. All four corners of each quad have local Y zero. The
fifth vertex is unused by that face. Door widths are about 46.841 model units;
their lengths along Z are respectively 16.1875, 16.375 and 16.1875. Their
index winding gives an upward-facing surface. There is no lower face and no
side wall forming a solid door.

The hierarchy matters: `door2` is a child of `door1`, and `door3` is a child
of `door2`. A child therefore receives the ancestor's transformation as well
as its own. The first door's attachment height is about 12.60277 model units;
the two child attachment heights are zero. The panels are coplanar at rest.

Evidence: direct binary parsing of the shipped 3DO object headers, vertex
arrays, primitive index arrays and child/sibling links; original renderer
comparison in ENV-03. A view from below an authored one-sided panel can see
through its reverse side. That is insufficient to diagnose this report:
the user's camera direction and whether the missing region is a door panel
or the exposed bay have not been captured.

## ENV-02: The compiled loading animation moves the panels; it does not hide them

The binary `CORTSHIP.COB` has fourteen named pieces, including the three
doors, and thirteen entry points. Its door-opening entry occupies code-word
offsets 129 through 182. It orders each door to local Z displacement -15
model units at speed 25 model units per second, waits 5000 milliseconds,
then orders each displacement back to zero at the same speed. Stored 16.16
values are -983040 and 1638400. Pickup and drop sequences both request
the door animation. Restarting the animation resets its hold interval.

No SHOW or HIDE opcode occurs in the complete compiled code-word array.
Thus a disappearance during ordinary loading must not be explained as an
intentional script HIDE of these panels. It may be their movement revealing
the bay, the backface rule, a rendering defect, or something not yet captured.

The shipped BOS is stale: it describes displacement -6 at speed 10. Its
other piece declarations also differ from the compiled file. Use the COB
as the shipped executable behaviour. The matching COB hash in the v3.1
override tree confirms that this difference is not a choice between those
two compiled versions.

Evidence: binary inspection of the COB header, name tables and instruction
operands, compared with the original interpreter at `0x4B0DA0` and piece
visibility setter `0x480D50`. SHOW/HIDE change one piece's visibility and
invalidate its cached drawing when needed; they are not a material opacity
operation. The decoded animation was not executed in the original game.

## ENV-03: The original does not make textured model faces double-sided

The current reference confirms the backface correction already recorded in
`TOTALA-EXE-RENDER.md` section 52. The textured-quad rasterizer at `0x4C7580`
builds directed edge chains; at `0x4C79DF` a nonpositive span width produces
no pixels. The cached unshaded textured-quad rasterizer at `0x4C8760` has
the same directed-edge, positive-width requirement. Reversing the projected
winding does not paint a mirrored reverse surface. An edge-on face also
paints nothing. There is no per-primitive instruction here to manufacture
a second side for a transport door.

The original's completed Envoy normally takes the mobile, unshaded model
path: the model-cache selector at `0x4586A0` gates the shaded path on the
building flag, rather than the SHADING option alone. Its `ZBuffer=1` gives
its cache a height plane. The unshaded piece renderer at `0x459830` visits
pieces in reverse flattened-list order and primitives in their stored order,
omitting the selection plate after loader relocation. Its textured span
filler at `0x4C7A20` uses an unsigned byte of interpolated model height:
the larger height wins; equal height permits the later draw. This is not
ordinary camera-space depth. See `TOTALA-EXE-SHADING.md` sections 22-23
for the established arithmetic and allocation rules.

The same filler copies texture samples directly when the height test passes.
It does not alpha-blend the texture or treat a selected source texel as a
transparent hole. A direct-screen textured path at `0x4C7310` likewise
copies texture samples. This establishes the original's polygon-fill rule;
the `CorSea5b` image and the later composite-bitmap blit were not separately
audited for an end-to-end colour-key interaction in this investigation.

Evidence: read-only comparison of these addresses in the matching reference,
and the cited existing render/shading register findings. These are static
checks. They do not establish that the user's exact visual glitch occurs
in the original or that changing RWE's depth policy is necessary.

## Remaining check

Capture an Envoy loading in the reported camera orientation, including the
door pose. Compare that with a normal fixed original-game view and the
shipped model before changing rendering. A blanket double-sided-material
change is not supported by this comparison. A deliberate extension allowing
viewing model undersides would require its own documented rationale if it
chooses to synthesize absent reverse faces.
