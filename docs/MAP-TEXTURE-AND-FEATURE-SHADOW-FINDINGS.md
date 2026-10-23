# Map-selector artwork and feature-shadow controls

Spec-room findings, 2026-10-23. Documentation only under ADR-0004. No RWE
implementation or tests were inspected or changed. Existing map-layout,
antialiasing and render/interface findings were consulted first.

## Finding FEATURE-SHADOW-01: one original UI button sets three renderer flags

The original visual-options Shadows button sets all three shadow flags to the
chosen button state: master Shadows, VehicleShadows and FeatureShadows. Its
options handler at `0x45E100` performs that grouped update. Therefore **turning
Shadows Off in that original UI suppresses tree/sprite shadows too**. A menu
button that disables building shadows while leaving ordinary tree shadows
visible does not reproduce this original UI behavior.

The renderer's flags are nevertheless separate; confusing the UI action with
the low-level gates obscures the result:

| Content | Original renderer gate |
| --- | --- |
| Billboard/GAF feature shadow, including a sprite tree | FeatureShadows, bit 4 (`0x10`), and an available/enabled shadow image |
| 3D model feature's ordinary projected shadow | Master Shadows, bit 2 (`0x4`), through its synthetic building-class renderer and the applicable model-shadow eligibility rules |
| Building projected shadow | Master Shadows and applicable unit shadow eligibility |
| Ordinary mobile silhouette shadow | Master Shadows plus VehicleShadows, bit 3 (`0x8`), plus mobile eligibility |

At `0x46A610`, sprite-feature rendering checks FeatureShadows for the normal,
animated and separately stored per-instance shadow cases. It draws the visible
feature image independently; disabling the shadow does not hide the tree.
That function does not additionally require the master flag for its sprite
shadow branch. The same dispatcher sends 3D model features through the
ordinary unit renderer. Initialization at `0x421F20` establishes that stand-in
as building-class; `0x459200` gates its projected shadow with the master flag.
The vehicle-only flag is not a substitute for the sprite-feature gate.

Independent registry settings and console commands can create combinations
that differ from the grouped menu selection. This is why the independent
renderer gates and the original unified UI behavior can both be true.
Whether RWE stores one master preference with subsidiary preferences or
synchronizes several values is an implementation choice; the requested Off/On
visible behavior has an original UI counterpart and needs no divergence claim.

## Finding MAP-ART-01: SELMAP has explicit native bitmap artwork

The original skirmish map-selector opener at `0x47AAF0` loads `SELMAP.GUI`
and then explicitly selects the background named `DSELECTMAP2`. The multiplayer
map-selector path also selects that background. It is therefore insufficient
to infer this screen's final art solely from the GUI's generic panel value.

The base archive contains `bitmaps/DSelectmap2.pcx`, a **640 by 480** PCX.
The v3.1 archive has no replacement of that named asset in the checked data.
The earlier map-layout finding records SELMAP's dialog as 494 by 420, at
(84,12), with `panel=NULL`. The full-screen bitmap and the dialog rectangle
are distinct dimensions, not evidence that a small panel texture should be
stretched to fill the dialog.

The background loader at `0x4288D0` loads/caches that bitmap and installs it
as the current GUI layer's background surface via `0x4AB290`. The GUI render
path at `0x4A81E0` copies a supplied layer background surface; its ordinary
surface-copy helper at `0x4C6B70` performs an offset blit, not a rescale.
Thus this checked background path preserves native bitmap pixel scale rather
than stretching the texture to new dialog dimensions.

There is also a different generic GUI frame/background path at `0x4B0230`.
Where its inputs provide background cells, they repeat at their stored step;
where they provide a multi-frame border set, it lays out corner/edge/interior
frames at their native dimensions. A one-frame background is simply drawn.
This is separate from SELMAP's explicitly installed PCX. A claim that every
original GUI background tiles, or that SELMAP's complete PCX is a repeatable
tile, would be unsupported.

## Enlarged description layout is an adaptation, not an art-rescaling rule

The original map-description rectangle is fixed; the owner's requested
expansion is already documented as a deliberate presentation divergence in
`MAP-DESCRIPTION-LAYOUT-FINDINGS.md`. This pass finds no original dynamically
enlarged SELMAP background behavior to reproduce.

Expanding the text area consequently requires an explicit RWE layout/art
decision. It does not justify stretching authored texture details or replacing
the established artwork without regard to its scale. Preserving original art
at native scale and extending/repeating only genuinely repeatable material is
a reasonable adaptation, but that recommendation is not a decoded original
enlargement mechanism. Exact choices depend on which artwork RWE actually
uses, which was not inspected in this spec-room pass.

The reported changed/wrong textures have not been measured or tied to a
particular render in this investigation. This finding establishes the original
asset and rendering distinction; it does not diagnose the current screenshot.

## Evidence and limits

- Byte-tactics inspected read-only at
  `e89721c46c3fa06e59558419b54fe26061fafb73`, clean checkout.
- Reference build: GOG/v3.1 `TotalA.exe`, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`, independently measured earlier in this
  same spec-room session from the installed executable.
- Static evidence addresses are given beside the findings. The separate
  shadow preference names are corroborated by the original registry-loading
  and saving code, and their independent console toggles. No decompiled source,
  internal identifiers or rendered control flow is transferred here.
- Real shipped art was read from `D:/RWE-Data/totala1.hpi` using the existing
  archive reader. `bitmaps/DSelectmap2.pcx` SHA-256:
  `c566409ef086961426d6c987a501002a84ccbc87079223d90b86e9b08c20aa07`.
  The PCX header records inclusive bounds (0,0)-(639,479).
- The earlier map-layout finding records the archive identities and base/v3.1
  SELMAP rectangles. Current base/v3.1 archive searches established the PCX's
  presence in the base archive and absence under that name from v3.1.
- This is a **static original-reference and shipped-data check**. No original
  executable was launched, no original or RWE screenshot was measured, no demo
  oracle or matching checker was run, and no active RWE archive overrides were
  established. Exact final pixel positions, clipping and palette appearance
  remain unmeasured. No executed original-game parity is claimed.
- Proposed acceptance: toggle the menu Shadows setting with a sprite tree,
  an actual 3D model feature, a building and a mobile unit visible; their
  silhouettes must remain while eligible shadows disappear. For the enlarged
  selector, inspect authored texture details and borders at fixed viewport
  scale before and after long descriptions; text-area growth should not
  silently alter the scale of the surrounding material. These checks were
  proposed, not executed here.
