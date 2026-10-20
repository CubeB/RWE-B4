# Lurker construction shadows and shipyard overlap

Spec-room findings, 2026-10-20. Documentation only. This document is an
implementation-room handoff under [ADR-0004](adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md).
It contains behavioural findings, arithmetic and routine addresses, without
decompiled source or identifiers. It allocates no main-register section number.

## Result

The stock ARM shipyard's unfinished Lurker is **an attached part of the yard's
rendered composite, not an independently drawn unit**. Its independent shadow
pass is skipped. Both stock shipyards also specify `NoShadow=1`. On the traced
stock construction path there is consequently **no separate Lurker shadow to
lower**. RWE currently generates one, even while the product is being built.
That is a concrete static discrepancy to investigate before tuning an offset.

After release, a Lurker does have the ordinary mobile silhouette shadow. Its
vertical anchor is the seabed, not sea level or the yard's pad. RWE's existing
translation expresses that basic rule correctly on flat ground.

The completed Lurker and the yard become separate painter-ordered images in
the original. Their overlap is decided by coarse horizontal world-Z buckets,
not their relative elevation. A submarine on the bottom can therefore paint
over the yard. RWE instead depth-tests their geometry together. Correct hull
height does not establish correct overlap with the yard.

## Evidence and limits

- Byte Tactics was inspected read-only at
  `e89721c46c3fa06e59558419b54fe26061fafb73`. Its main checkout was clean.
- The local `orig/TotalA.exe` MD5 was independently measured in this pass:
  `8e74a1dffa1f5988624c52048f5b20cd`.
- RWE code was inspected read-only at
  `7af1632c831d035c8c16487d6927b5369bc1aca0`. The main checkout also had
  pre-existing documentation changes. This document is in a separate detached
  documentation worktree at that revision; those changes were not overwritten.
- Data were read from `D:/RWE-extract/totala1` and
  `D:/RWE-extract/rev31`. These are extracted snapshots, not verification of
  the active archive stack in the reported game.
- All new engine conclusions here are **static findings**. No original-game
  capture, RWE render, demo oracle, executable rebuild or test was run. The
  required initial checker invocation failed on the local Python environment;
  the user explicitly authorized continuing without setup. No MATCH claim is
  made.
- The owner previously observed a submarine near the surface while being
  built, then on the seabed below the pad on completion. That by-eye observation
  is recorded in [the earlier naval findings](NAVAL-CONSTRUCTION-DGUN-FINDINGS.md#original-game-observation-submarine-under-construction).
  The current report says RWE's Lurker hull height looks right but its shadow
  looks too high. Neither report includes measured pixels, depth or a capture.

Reproduction identities, SHA-256:

| Extracted file | Hash |
| --- | --- |
| `rev31/units/armsub.fbi` | `c4dd2566cae2d3ef57e6cde5a64a06e61ec308dff78b370359bc31ca234b0e93` |
| `rev31/units/armsy.fbi` | `cd731a21ee9ca83d9d68080796a826e38a4deee712ad26c791656c1df4639fb8` |
| `totala1/objects3d/armsy.3do` | `bfe9f45330176a76b93bc213ed0711107086db5783d2a26d9b1ebf47573f7c00` |
| `totala1/scripts/armsy.cob` | `a5935882e3a72029892f4dc83eab0ddb274cb92e09278a554927b9b8128f802f` |

The FBI snapshots identify ARMSUB as Lurker, with `BMcode=1`, `ZBuffer=1`,
`Upright=1`, `WaterLine=20`, and no raw `Floater` or `CanHover`. ARMSY has
`BMcode=0`, `ZBuffer=1`, `WaterLine=1`, `NoShadow=1`. The checked base CORSY
also has `NoShadow=1`, with `WaterLine=22`. These flags matter separately:
being a submarine does not make the Lurker a raw floater.

## 1. Construction and release are also rendering transitions

At `0x402640`, the factory queries its script's `QueryBuildInfo`, obtains that
piece's world position, creates the unfinished product, and attaches it to the
piece. At `0x48AB70`, attachment links the product under the factory and
removes its previous root-list membership. A real piece number leaves the
product eligible to be visible in its parent's image; the special hidden
attachment case is different.

At `0x45AC20`, an attached unit has no independent top-level draw. The yard
poses its visible attached products and draws the parent. At `0x459200`, the
parent's height-buffered path builds and recolours each visible attached
product's image, then combines it into the parent's image. That child-image
operation does not invoke the child's independent shadow calculation.

This matters even though the product's own FBI allows shadows. The parent's
shadow is handled separately, before the attached images are combined. It is
not the silhouette of the completed parent-plus-product image. For stock
ARMSY, `NoShadow=1` suppresses the parent's shadow too. There is no traced
fallback that draws an attached Lurker's separate silhouette on the seabed.

Construction progress reaching completion invokes `0x41B8D0` from
`0x41BA60`. For an active locally simulated human/computer player's mobile
product, that notification releases the attachment. This happens in the final
successful build step, rather than requiring the factory's later bookkeeping
stage or a rally move. At `0x48AB70`, release restores root-list membership.
The product then becomes independently drawable, with its own shadow and
world-order entry. Remote-owner update timing needs separate verification.

Thus an independent shadow first appearing on release is consistent with the
traced factory path. Do not generalize this to all unfinished units: a
standalone nanoframe can take its own normal draw/shadow path.

### Height evidence remains distinct

Piece following at `0x43D6D0` uses the live piece position and orientation.
Only a raw floater receives a lower limit of sea level minus `WaterLine` there.
The ordinary upright ground-placement rule at `0x48A870` puts a released
non-hovering submarine on the seabed. The earlier observation supports the
visible construction height near the build piece and the drop on completion.

The earlier naval document's static account of placement throughout attachment
was already superseded by that observation. This pass does not resolve the
remaining update-order conflict: `0x48AD30` still visibly calls ordinary
height placement after motion. The composite finding establishes ownership
of drawing and shadow suppression; it does not prove a second hidden vertical
offset that reconciles that simulation trace. Do not use it to reintroduce
seabed-height construction or subtract `WaterLine=20` from the pad height.

## 2. The released Lurker's shadow

The independent renderer at `0x459200` distinguishes static building
projections from mobile-unit silhouette copies. Unit initialization at
`0x485A40` sets the selector from whether `BMcode` is zero. This strengthens
the older register's provisional interpretation of that selector.

For the ordinary mobile path, `0x45A470` copies the cached picture and turns
its nontransparent pixels black. It preserves the picture's shape and origin;
it does not project every vertex onto a plane using a light ray. At
`0x459200`, that silhouette is placed five original-scale pixels right of the
unit, with its base vertically anchored to the sampled ground height.

For explanatory arithmetic, let X and Z be whole world coordinates, H the
unit's whole origin height, G the sampled ground height, and Cx/Cz the camera
offsets. With screen Y increasing downward, the original image anchors are:

| Image | Screen X | Screen Y |
| --- | --- | --- |
| Unit | X - Cx + 128 | Z - Cz - floor(H / 2) + 32 |
| Mobile silhouette | X - Cx + 133 | Z - Cz - floor(G / 2) + 32 |

The shadow displacement is therefore 5 pixels right and
`floor(H/2) - floor(G/2)` pixels down. This preserves the silhouette's internal
height and orientation. It is not a flattened footprint. The whole-coordinate
truncation and halving are observable integer quantization; a floating-point
port can differ by about a pixel even with the same broad rule.

For example, a hypothetical independent unit at H=54 above a flat seabed G=20
has a silhouette 17 pixels lower than its body, plus 5 right. Once settled at
H=G=20, its silhouette has no vertical separation from its body. The example
explains the independent path; an attached factory product skips that path.

The ground sampler at `0x485070` samples terrain, without clamping to sea
level. It uses whole horizontal coordinates and integer interpolation over a
16-unit grid. RWE's terrain height comes from its terrain mesh intersection;
uneven ground can therefore expose a separate sampling difference. Flat-water
fixtures remove that variable.

The copied path rejects raw hovercraft/floaters, but the checked Lurker has
neither flag. In the height-buffered path, the copied silhouette is cut below
the waterline when the unit origin is below sea level. The ordinary encoded
height threshold is `sea level - H + 50`, with the byte threshold semantics
of `0x4BA1B0`; surviving pixels are above that threshold. Consequently a
normally fully submerged released Lurker need not show a mobile silhouette
shadow at all. Do not infer that every submerged mobile unit always casts a
full black shadow. This also qualifies the older render register's broad
claim that finished unit shadows are never water-cut.

## 3. What appears over or under the yard

### While attached: local height comparison inside one composite

The parent composite is expanded to cover the attached product at
`0x4589C0`. At `0x459200` and `0x4B90A0`, the product image is positioned
relative to the yard using its horizontal difference and half its vertical
difference. Covered pixels are compared using the two images' stored height
values, with the product-to-yard origin height difference added. The product
wins a tie. For ordinary models the stored height has a bias of 50.

This is a per-pixel **vertical-height comparison local to the composite**,
not an unrestricted scene-wide camera-depth comparison. Raised parts of the
yard can hide lower product pixels; taller product pixels can cover lower yard
pixels. The recoloured construction image, including its transparent regions,
is combined before the resulting yard composite is painted onto the world.
There is no rule that the whole submarine must be above or below the whole yard
throughout construction.

### After release: whole images paint in coarse Z order

At `0x468CF0`, visible units enter rows indexed by their whole horizontal Z
relative to the camera, in groups of 16 world units. Rows are drawn in
increasing order. The normal ground-mode units, including the ordinary yard
and released ground-mode Lurker, share the first unit pass. Other movement
modes are drawn in a later pass, so the bucket rule is not a universal ordering
across every movement mode.

For those normal ground-mode units:

- A submarine in a later Z row paints after the yard and covers overlapping
  opaque yard pixels, despite being lower in the water.
- A submarine in an earlier Z row paints first and the yard can cover it.
- Within the same row, existing visible-list order is retained. There is no
  secondary per-pixel inter-unit height comparison or elevation sort there.
  This pass did not establish which entry wins that tie for a particular save.
- Moving sideways without changing the row need not change ordering. Moving
  across a row boundary can change overlap abruptly. Camera alignment affects
  the row boundary, so record camera position too.

The final world blit does not propagate the local model height buffer into a
shared world depth buffer. Height still changes screen position and underwater
tint/cut, but does not determine which of these separate images paints last.
An owned submerged Lurker can remain blue-tinted and paint over a visible part
of the yard as it leaves. This is not evidence that its origin stayed at pad
height. Nor does the static rule guarantee it will always paint over the yard
in every exit direction.

The exact exit route and which yard pieces overlap depend on the live script
pose, factory orientation, camera, list order, map heights and active assets.
No fixed 'always above until clear' or 'always underneath after completion'
rule was found.

## 4. Read-only comparison with current RWE

| Area | RWE inspection | Comparison |
| --- | --- | --- |
| Attached height | `src/rwe/sim/GameSimulation.cpp:5337` initializes a product at the build-piece height; `src/rwe/sim/UnitBehaviorService.cpp:5978` follows that piece until completion. | Consistent with the owner's accepted visible-height observation. Keep height separate from the shadow investigation. |
| Completion height | `UnitBehaviorService.cpp:5933` settles the product and its previous position together. | Explicit seabed snap for this submarine; avoids an interpolated descent. Exact original tick timing remains unmeasured. |
| Construction shadow eligibility | `src/rwe/game/GameScene_render.cpp:1808-1874` visits visible units independently and creates a separate shadow/cutout batch for a unit being built. No factory-product exclusion is present there. | DIFFERS from the attached original path. Stock shipyard `NoShadow` does not suppress this separate product batch. |
| Shadow displacement | `src/rwe/game/GameScene_util.cpp:775-808` uses interpolated origin height and terrain height; `shaders/unitShadow.vert` translates the mobile silhouette 5 right and down by half the origin-to-ground difference under the standard camera. | Broad arithmetic agrees for an independently drawn mobile unit on flat terrain. No demonstrated missing pad-height subtraction. |
| Underwater shadow cut | `shaders/unitShadow.frag` discards vertices' interpolated world-height fragments below ground height, rather than applying the copied silhouette's sea-level cut. | DIFFERS from the height-buffered mobile path. Most relevant once the product is released underwater. |
| World shadow composition | RWE combines stencil shadows before drawing unit meshes. | Different from the original's shadow-then-body operations at each independently drawn unit's place in painter order. |
| Yard/product overlap | `GameScene_render.cpp:1908` enables depth testing for the unit batch; `src/rwe/RenderService.cpp:206` draws finished models and then construction meshes. | Shared camera-depth geometry differs from attached local-height composition and released image-level Z ordering. |

These are source comparisons, not a claim that the reported dark pixels have
been identified in a captured frame. In particular, a yard's dark texture,
underwater hull tint, or a masked part of another shadow can look like the
submarine's shadow. The current report alone cannot distinguish them.

## 5. Focused acceptance checks for subsequent work

Use stock ARMSY building ARMSUB on flat seabeds at depths 30 and 60, recording
sea/seabed height, archive identities, yard heading, camera and shadows settings.
Compare the same construction phases in the original and RWE:

1. First attached frame, early nanoframe, half-built and nearly complete:
   record hull outline and every dark region separately. The static prediction
   is no independent product shadow while the attachment persists. Toggle
   vehicle shadows to help identify a separate silhouette.
2. Final successful build step and the next two updates: record attachment,
   completion and hull height independently. Check when the independent draw
   and its water-cut shadow begin, without assuming an extra surface tick.
3. Leave the released submarine below the pad, then move it out in both Z
   directions and sideways. Compare screenshots immediately before and after
   a 16-unit ordering boundary. Record where the sub covers yard pixels and
   where yard pixels cover the sub. Test same-row ties separately.
4. Repeat with the camera displaced vertically in world Z. A coarse painter
   boundary can move even with the same unit positions; a simple depth-test
   solution will not reproduce that behaviour.
5. Use a deliberately independent above-seabed mobile unit to isolate the
   5-right/half-height shadow translation. It is a separate fixture from the
   attached Lurker. Then compare underwater independent units to isolate the
   sea-level silhouette cut.

The implementation handoff is to address attachment-aware shadow eligibility,
underwater silhouette coverage and yard overlap as separate behaviours. This
finding does not authorize changing the observed-correct construction height,
adding a global shadow-height correction, or assuming physical 3D depth gives
the original's released-unit overlap.
