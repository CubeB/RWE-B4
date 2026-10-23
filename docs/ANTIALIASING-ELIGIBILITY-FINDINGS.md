# Antialiasing eligibility and sample-grid origin

Spec-room findings, 2026-10-23. Documentation-only handoff under ADR-0004.
The existing `TOTALA-EXE-RENDER.md` section 101 was read first. No RWE
implementation or tests were inspected or changed in this pass.

## Finding AA-ELIGIBILITY-01: filtering belongs to individual cached images

The original's antialiasing option enables a doubled-size temporary image for
the building-class cached-model pass. That image is reduced to the ordinary
cached image before world composition. The corresponding shaded and unshaded
model rasterizers use the same eligibility gate. Ordinary mobile units do not
become eligible merely because they also use an image cache.

| Rendered content | Original option's effect |
| --- | --- |
| Cached pieces of a building-class unit | Eligible for the doubled-size image and reduction |
| Ordinary mobile unit | Does not receive this reduction |
| Live, `DONT_CACHE` pieces drawn directly into the scene | Bypass the cached-image reduction |
| GAF sprite/billboard map feature, including a tree represented this way | Direct sprite drawing; bypasses this reduction |
| Map feature represented by a 3D model | Uses the synthetic building-class renderer; its cached-model pass is eligible |
| Terrain, map tiles and final composed world | No whole-scene antialias reduction on this path |

The eligibility distinction is the original renderer's building classification,
not whether an object appears stationary. For normal units, initialization
derives it from the FBI's `BMcode=0`. The synthetic unit used for model
features is explicitly assigned that same classification.

### Correction to section 101's blanket exclusion of features

Section 101 says the original filters a building's cached bitmap and nothing
else, and groups features with unfiltered units. The traced 3D feature path
qualifies that account: the feature renderer lends each placed model's state,
position and orientation to a synthetic building-class unit, then invokes the
ordinary unit renderer. Cache generation therefore reaches the eligible
building-class model pass when antialiasing is enabled. This is a static
refutation of an unconditional exclusion of **all** modelled map features.

Conversely, ordinary sprite features draw their existing GAF frames directly.
Whether a particular tree is a billboard or a 3D model must be established from
its active assets before claiming that its smoothing is either required or
incorrect. A tree's appearance or gameplay category alone does not answer it.

Cached-only rendering and direct live-piece rendering remain distinct. The
scene's live-piece update has filtering disabled; the independent direct-piece
rasterizer has no doubled-size image/reduction step. A rotating piece that the
script keeps outside the cache therefore remains sharp even on a filtered
building. This is a property of the cache pass, rather than a universal
instruction to smooth everything belonging to a building.

## Finding AA-PHASE-01: reduction starts at each image's own origin

The temporary colour image has exactly twice the destination image's width
and height. Its stored drawing origin is also doubled. Each destination pixel
reduces the corresponding adjacent 2 by 2 source pixels, starting with the
source image's upper-left block. The reduction uses the palette blending table:
combine the two top samples, combine the two bottom samples, then combine
those two intermediate palette indices. This directly confirms the table-based
three-combination mechanism that section 101 previously described as inferred.

The sample phase is therefore **local to the cached image**, with its doubled
origin keeping the model aligned inside it. The filter receives no camera,
map-tile or screen coordinate from which to establish a global phase.
Subsequently moving the already filtered image on screen does not choose a
different pair of source pixels merely because the image crosses an even/odd
screen coordinate or terrain-tile boundary.

There is an ordinary 2 by 2 reduction lattice *inside each eligible source
image*. This does not establish a visible screen-wide checkerboard, tile grid,
or alternating regions where the whole scene gains and loses filtering.
The traced world composition paints already filtered images and unfiltered
content together. Terrain does not opt nearby trees into antialiasing by
sharing a global downsample block with them.

## RWE-specific machinery and the reported grid

Section 101 documents RWE's supersampled world framebuffer, category/occlusion
mask and selective postprocessing as the means used to approximate the original
image treatment. Those are **RWE-specific implementation mechanisms with no
counterpart in the original path checked here**. The original neither assigns
scene-mask category channels to terrain and sprites nor alpha-composites such
labels to decide what a completed screen-space block should filter.

The RWE option to additionally smooth mobile units and sprite features is
likewise an extension; original eligibility is the table above, independently
of the current value of that extension. The present report of blurry objects
and an apparent grid is not itself evidence of which extension was enabled or
which mask operation was responsible. This spec-room pass did not inspect an
RWE shader, settings file, screenshot or executable, so it does not diagnose
the reported release or verify the implementation-room mask-blending hypothesis.

For acceptance, distinguish sprite trees, actual 3D model features, mobile
units, cached building parts and live parts. Compare an unchanged object while
panning across native pixel parities and terrain-tile boundaries. A persistent
blur on eligible cached content can follow the original rule; sudden smoothing
changes tied to a screen-wide or terrain-tile grid do not follow from this
reference mechanism. This is a proposed check, not an executed result.

## Evidence, reference identity and limits

- Byte-tactics was inspected read-only at revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`; its checkout was clean.
- Reference build: GOG/v3.1 `TotalA.exe`, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`. The installed executable at
  `D:/SteamLibrary/steamapps/common/Total Annihilation/TotalA.exe` was
  independently hashed again in this pass and matches that identity.
- Static addresses: normal unit classification `0x485A40`; synthetic feature
  classification `0x421F20`; feature sprite/model dispatch `0x46A610`;
  cache construction `0x4586A0`; cache drawing/rebuild `0x458810`;
  shaded/unshaded cached-model rasterization `0x459C70` / `0x459830`;
  cached/world composition and live update `0x459200`; direct live-piece
  rasterization `0x4584D0`; per-image reduction `0x4B95A0`.
- The inspected reference progress table records the cited feature dispatch,
  synthetic initialization, two cache rasterizers, direct-piece rasterizer,
  cache rebuild and reduction routines as matched. This pass did not rebuild
  byte-tactics or independently run a matching checker.
- This is a **static reference check**. No original game was launched, no demo
  oracle was run, and no actual tree/model asset or rendered sample was measured.
  Exact visible edge colours, transparency artefacts, cache invalidation after
  every possible script change, and exceptional construction/composite cases
  remain outside the measured result. No claim of executed original-game parity
  or screenshot-specific diagnosis is made.
- Only this new documentation file was added. Existing findings in the
  separate `D:/RWE-spec-map-layout` documentation checkout were preserved.
