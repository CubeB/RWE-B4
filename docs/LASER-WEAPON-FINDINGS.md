# Laser beam drawing in the original (render type 0)

Spec-room findings, 2026-10-20. Documentation only. This document is an
implementation-room handoff under
[ADR-0004](adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md).
It contains behavioural findings, arithmetic, constants, addresses and data
layouts, without decompiled source or byte-tactics identifiers. It allocates no
main-register section number. It is a supplement to `TOTALA-EXE-WEAPONS.md`
section 92 (the beam tail) and `TOTALA-EXE-RENDER.md` section 5 (the paint
order), and does not replace either.

## Result

A `rendertype` 0 shot is **not** a ribbon, a filled polygon, a textured sprite
or a tapered streak. It is **two one-pixel Bresenham lines in screen space**:
one in the first colour byte, on the exact projected head-to-tail segment, and
one in the second colour byte on an adjacent line.

- The core line is always one output pixel wide.
- The second line is another one output pixel wide. For a beam whose screen
  segment is more horizontal than vertical it is the same segment shifted one
  scanline **up**; for everything else it runs from one pixel **left of the
  top end to one pixel right of the bottom end**, so it crosses the core.
- The pair therefore reads as a **two-pixel** beam at 1:1, with a visible
  one-pixel kink or crossing at short/steep angles. There is no third pixel,
  no glow, no anti-aliasing and no alpha.
- Width is in **output pixels at the native 1:1 projection**. The original has
  no zoom: one world unit is one pixel horizontally and in map depth, height
  contributes half a pixel per unit. A larger resolution adds map, it does not
  enlarge the beam. Two pixels at 640x480, two pixels at 1024x768.
- The two colour bytes are palette indices in the **interface palette**
  (`GUIPAL.PAL`), converted at load to the nearest colour in the in-game
  palette (`PALETTE.PAL`) by summed absolute RGB distance; the resulting
  palette index is written straight into the 8-bit surface.
- Nothing is blended and there is no depth test. The beam is painted after the
  early unit pass and after the impact-sprite layer, and before explosions,
  the late (airborne) unit pass, health bars and damage smoke.
- The beam draw itself emits **no flash, no smoke and no sound**.

The owner's report (RWE beams thinner than the original) is consistent with a
renderer that draws one device-pixel line plus a world-space-offset companion:
at 1:1 that is half the original's core-plus-edge pair, and the companion is
often collinear with the core and invisible. The RWE section below states the
observed mechanism; it does not claim to have measured the owner's screen.

## Evidence and limits

- Byte Tactics was inspected read-only at
  `e89721c46c3fa06e59558419b54fe26061fafb73`. Its working tree was clean.
- The binary inspected is `orig/TotalA.exe`, 1,178,624 bytes, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`, SHA-1
  `764dc919c3bd0365751aefba8e9a667299a3ce2e`, SHA-256
  `3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
  That is the corpus's GOG retail v3.1 reference.
- RWE code was inspected read-only at
  `82a422c342239414d00854aa23de9c0f408d8f1d`. That checkout had pre-existing
  documentation modifications; no RWE file was written, built or tested.
- Data were read from `D:/RWE-extract/totala1` and `D:/RWE-extract/rev31`.
  These are extracted snapshots; they do not prove the archive precedence of a
  particular installation. Reproduction identities, SHA-256:

  | Extracted file | Hash |
  | --- | --- |
  | `rev31/weapons/LASERS.TDF` | `5d4b76493058df7748fefefd57be8a2eae2e3b50524a5766e1f222c1af776f56` |
  | `rev31/weapons/WEAPONS.TDF` | `4e9f0fa638dc9cca851bab27bd7332ca2c9736007099e4475dce56b58cce703c` |
  | `totala1/palettes/GUIPAL.PAL` | `6bc11b83cbd64379fff28ee6b09d36f285a55aff70e6a372b59a21c31950c165` |
  | `totala1/palettes/PALETTE.PAL` | `cda95c32d22297103ac0e375602a2480a9530fe7f7315c9d270aab3abd7367fa` |

- All engine conclusions here are **static findings**. No original-game run,
  screenshot, pixel count, demo oracle or executable rebuild was performed.
  The predicted pixel footprints in finding 2 come from the line routine's
  step arithmetic, not from a capture. No MATCH claim is made.
- The owner's observation is by eye and has no resolution, zoom or screenshot
  attached. Where this document says "2 pixels", it means the renderer's own
  output pixels at 1:1, not pixels on the owner's monitor.

Principal evidence addresses: 0x49BE60 (the projectile pass), 0x49BF42 where
the render-type 0 arm begins, 0x49BF5C and 0x49BF7B (the two colour-table
reads), 0x49BFD2 (the second-colour zero test), 0x4BE950 (line entry),
0x4BEA20 (clipping) and 0x4CC7AB (the one-byte-per-step rasteriser), 0x468CF0
(the battle frame), 0x4AC7D0 (the colour-table build), 0x498148 (the call at
game load), 0x420A30 and 0x420B00 (impact effects), and 0x49BBA3 (the beam
tail, already recorded in `TOTALA-EXE-WEAPONS.md` section 92).

## 1. The geometry is two lines, not a shape

The render-type selector is the byte at weapon-definition `+0x10C`
(`TOTALA-EXE-DATA.md`). Value 0 selects a branch that computes four integers
from the projectile's current position and its second point, then makes two
line calls into the common one-pixel line routine. There is no width argument,
no polygon fill, no sprite blit and no model draw in this arm.

The line routine clips the segment to the playing-area rectangle and then
rasterises it one pixel per major-axis step; each step stores one raw byte.
Endpoints are inclusive, there is no anti-aliasing, and the store is a plain
byte write. So each call paints a one-pixel line of a single colour.

Two calls means the beam is **one core pixel plus one companion pixel**, never
a filled band. The companion is a different colour byte, and the core call is
made second, so where the two lines overlap the core colour wins.

## 2. Endpoints, direction test and exact pixel footprint

Screen coordinates are formed from the integer parts of the 16.16 world
position (the high word), minus the integer scroll, plus the playing-area
origin:

- `sx = whole(X) - scrollX + 0x80`
- `sy = whole(Z) - (whole(Y) >> 1) - scrollY + 0x20`

`0x80` and `0x20` are the playing-area origin: the map view begins 128 pixels
from the left of the screen and 32 from the top, and its last drawable row is
32 rows above the bottom (the view's minimum corner is set to those constants
and its maximum to `width - 1` and `height - 0x21` at game load). The height
term is an arithmetic shift, so it rounds downward for negative heights. There
is no sub-pixel offset and no camera rotation: map X maps straight to screen X.

Let the head be `(hx,hy)` and the tail `(tx,ty)`, both integers. The branch is
chosen on the **screen** deltas, after projection:

**If `|hx-tx| > |hy-ty|`** (the segment is more horizontal than vertical):
order the ends so that `x1 < x2` (swapping the y values with them), then

- second colour: `(x1, y1-1)` to `(x2, y2-1)`;
- first colour: `(x1, y1)` to `(x2, y2)`.

**Otherwise** (including exactly 45 degrees):
order the ends so that `y1 < y2`, then

- second colour: `(x1-1, y1)` to `(x2+1, y2)`;
- first colour: `(x1, y1)` to `(x2, y2)`.

Consequences, all following from those two calls:

- A horizontal beam is exactly **two rows** high: the core row and the row
  immediately above it (`y-1` is upward on screen).
- A shallow diagonal is a two-row staircase: each column has the core pixel
  and the pixel one row above it.
- A vertical beam is one column for the middle of its length. Its companion is
  a very shallow crossing line: one pixel left of the core for roughly the top
  quarter, through the core at the middle, and one pixel right for the bottom
  quarter. The footprint is therefore two columns wide at each end and one
  column wide through the middle.
- A 45-degree beam is two pixels wide at both ends and the two lines cross in
  the middle, so it **pinches to one pixel** there. This is a quirk of the
  offset convention, not a taper: the offset is not perpendicular to the
  segment except for horizontal and vertical beams.
- The side of the offset is fixed in screen space. It does not depend on which
  end is the muzzle or on the direction of travel: for shallow beams the
  companion is always above, for steep ones always left at the top and right
  at the bottom.

There is no length-dependent width and no distance-dependent width. A long
beam and a short one are both one core pixel plus one companion pixel.

## 3. Width against resolution, camera and zoom

The original has no camera zoom and no world-to-pixel scale factor. Positions
are converted by taking the integer world coordinate and subtracting the
scroll, so the projection is 1 world unit = 1 output pixel in X and in Z, and
1 world unit of height = -0.5 pixel of Y. The screen offset of a shot changes
only with the scroll.

Resolution changes the size of the offscreen surface and the playing-area
rectangle, not the scale. At 640x480 the play area is 512 by 416; at 1024x768
it is 896 by 704; at 1280x1024 it is 1152 by 960. The same two pixels are
painted in all of them, and the same world distance covers the same number of
pixels in all of them. The only way the beam gets thicker on a monitor is if
the whole frame is stretched by the display path.

This is the key relationship for RWE: the beam thickness is a **screen-space
constant tied to a 1:1 world projection**, not a world-space width and not a
fraction of the screen. In a renderer with a zoomable camera the faithful
equivalent is one core plus one companion pixel *per original pixel* — that
is, 2 x the current pixels-per-world-unit — not a fixed 2 device pixels.

## 4. The two colour bytes and the conversion rule

The first colour is the byte at weapon-definition `+0x10D`, the second at
`+0x10E`. Both are used as indices into a **256-byte table at game-state
`+0xDCB`**; the table entry, not the raw byte, is the palette index written to
the surface.

That table is the interface-colour translation table. It is written by the
routine at 0x4AC7D0 into `+0x8B2` of the object at game-state `+0x519` (which
is `+0xDCB`), and the call made during game load is at 0x498148. The rule is:
for each of the 256 entries of the interface palette (`GUIPAL.PAL`), find the
entry of the active game palette (`PALETTE.PAL`) with the smallest
`|dR| + |dG| + |dB|`, and store that index. Ties keep the **lowest** palette
index (the comparison accepts only a strictly smaller distance). The table is
therefore not the identity, even though its first sixteen entries are also used
as the interface colours. `TOTALA-EXE-RENDER.md` already records this table and
its build address in the note on interface colours; this document adds its use
by shots.

For the shipped palettes the conversion of every beam colour in the game data
is:

| TDF `color` | `GUIPAL.PAL` entry (R,G,B) | in-game index | `PALETTE.PAL` entry (R,G,B) |
| --- | --- | --- | --- |
| 0 (means "no second line") | 0,0,0 | 0 | 0,0,0 |
| 96 | 0,255,0 | 250 | 0,255,0 |
| 98 | 0,227,0 | 250 | 0,255,0 |
| 144 | 0,0,255 | 252 | 0,0,255 |
| 208 | 191,122,74 | 179 | 179,151,63 |
| 209 | 181,112,69 | 179 | 179,151,63 |
| 211 | 160,101,63 | 21 | 155,91,99 |
| 217 | 111,74,37 | 182 | 107,91,23 |
| 232 | 199,43,43 | 212 | 211,43,0 |
| 234 | 199,119,43 | 180 | 155,131,47 |

Two cautions follow.

- The shipped TDF annotations were evidently written against the in-game
  palette's numbering (232 is annotated green, 208 yellow, and the 144/217
  pair purple), but the engine does not use the number that way. It converts
  through the interface palette, and the converted results are the indices
  above. The annotations are not a specification and must not be used to
  "correct" the mapping. The Annihilator's 144 is the clearest check: it
  converts to pure blue (index 252), whereas the raw in-game entry 144 is
  near-white.
- A second colour byte of **zero disables the companion line entirely**. Index
  zero is the transparent/black entry, so zero doubles as "off". This is a
  shipped case, not a hypothetical: the ordinary laser in `LASERS.TDF` uses
  `color=208` with `color2=0` and therefore draws a **single one-pixel line**.
  Weapons that name a non-zero second colour draw the pair described above.

Both lines are flat single colours for their whole length. There is no palette
ramp along the beam, no per-pixel shading and no fading at either end.

## 5. Blending, transparency and the paint order

The surface is 8-bit paletted and the rasteriser stores the colour byte
directly. There is no alpha, no add, no XOR, no palette-light look-up and no
remap table in this path. A beam pixel is the palette colour, whatever was
under it. Both lines are opaque.

The original has no world depth buffer at all (`TOTALA-EXE-RENDER.md` section
5). The battle frame at 0x468CF0 is a painter's pass over one offscreen
surface, clipped to the playing area, in this order (only the parts that
matter here):

1. terrain;
2. particle layers 0, 1 and 2;
3. low features;
4. particle layers 3 and 4;
5. the early unit pass (the draw class painted before the effects),
   interleaved with tall features, row by row;
6. particle layer 5;
7. **particle layer 6 (weapon impacts)**;
8. **projectiles, including every render-type 0 beam**;
9. **explosions (the GAF sequences and debris)**;
10. particle layer 7 (VTOL/thrust exhaust; bubbles also live here);
11. the late unit pass (airborne units, per `TOTALA-EXE-RENDER.md` section 5);
12. particle layer 8;
13. health bars and unit group numbers;
14. particle layer 9 (damage smoke), over everything.

So a beam is painted **over** the units and muzzle flare of the early pass and
the impact-sprite layer, and **under** explosions, the late (airborne) pass,
health bars and damage smoke. It is never hidden by terrain or by a unit drawn
earlier, because nothing is depth-tested against it. A ridge between the firer
and the target does not shorten the visible beam.

## 6. The beam draw does not make a flash, smoke or sound

The render-type 0 arm makes two line calls and nothing else. It does not spawn
a particle, start a GAF, play a sound or touch the screen shake. Every flash
and puff associated with a laser comes from elsewhere:

- **Muzzle flash.** The firing path starts the unit's fire animation (a model
  piece flash drawn as part of the unit, in the early unit pass) and, only
  when the weapon asks for it, emits start smoke. That is drawn before the
  beam, so the beam covers the flare.
- **Impact.** On the hit, the detonation path (0x499EB0 and 0x420A30) appends
  an explosion from the weapon's `explosiongaf`/`explosionart` (a GAF
  sequence, drawn after the beam at step 9) and, for a water hit, the water
  splash. Weapons with `endsmoke` emit white smoke instead of the explosion.
  The impact-sprite particles that appear in layer 6 are drawn *before* the
  beam. Sound and screen shake are part of the same detonation path, keyed by
  the weapon's hit/water sound and shake fields.
- **Smoke.** Damage smoke is the unit script's own `emit-sfx` work, in layer 9,
  over the beam. It is not part of the shot.

This is the same general impact path the weapons register already records; the
point of this finding is that **none of it belongs to the beam draw**, so
changing the beam's geometry must not change the flash or smoke timing.

## 7. Head and tail, only as far as the width needs them

The two endpoints are the projectile's current position and its second point.
`TOTALA-EXE-WEAPONS.md` section 92 records that the second point starts moving
`duration + 1` ticks after the shot and that only render type 0 draws the pair;
that account is not re-derived here. What matters for width is:

- while the second point is still anchored, the visible line grows from the
  muzzle to the head;
- afterwards the pair is a fixed-length segment travelling with the head;
- both endpoints go through the same projection and the same integer
  truncation, and the companion offset is applied **after** projection, in
  screen pixels.

The width rule is therefore independent of the length rule. A change to the
duration or the tail handling must not change the two-pixel geometry.

## 8. What this reading does not correct

No existing register section states the beam's raster geometry, so nothing is
superseded. Two adjacent statements stay as they are: the render-type byte's
offset at `+0x10C` (`TOTALA-EXE-DATA.md`) and the tail's `duration + 1` rule
(`TOTALA-EXE-WEAPONS.md` section 92). The colour-table note in
`TOTALA-EXE-RENDER.md` is confirmed by this reading and extended: the table is
also what maps shot colours.

## RWE comparison

Read-only inspection of RWE at the revision above. The mechanism is visible in
`src/rwe/game/GameScene_util.cpp:1727-1737`,
`src/rwe/LoadingScene_util.cpp:107-120` and `:694-700`,
`src/rwe/game/GameScene_render.cpp:2068-2077` and
`src/rwe/render/GraphicsContext.cpp:745`.

| Aspect | Original | RWE today | Consequence |
| --- | --- | --- | --- |
| Primitive | Two screen-space one-pixel lines | Two `GL_LINES` segments built in world space | Geometry is right in kind, wrong in space and width |
| Core width | 1 output pixel at 1:1, from a 1-px Bresenham line | 1 device pixel; no line-width call, so the GL default | Half the original's core+edge pair before any zoom is considered |
| Companion width | A second 1-px line, so 2 px total | A second 1 device-pixel line | Same one-pixel shortfall, and see the next row |
| Companion placement | Screen space: one scanline up for shallow beams, one pixel left at the top and right at the bottom for steep ones | One **world unit** in -Z added to both endpoints (`pixelOffset(0,0,-1)`) | The offset is not perpendicular in screen space; when the beam runs along Z it is collinear and the companion is invisible. For other directions its screen size depends on the camera |
| `color2 = 0` | The companion call is skipped: a single-pixel beam | The zero colour is converted to palette 0 (black) and the companion is still drawn | A black line is drawn beside weapons that should have none (`LASERS.TDF`'s ordinary laser is the shipped example) |
| Colour selection | Interface palette entry, nearest in-game palette colour, first minimum wins, written as the palette index | Same conversion at load (`LoadingScene_util.cpp:107-120`), then drawn as RGB | Matches; do not change it. The TDF comments are not a guide (see finding 4) |
| Blending | Opaque byte, no alpha, no lighting | Flat RGB colour, no alpha | Matches visually |
| Depth/occlusion | No depth test; beam is painted over terrain and the early unit pass | Drawn in the world pass with the depth test and depth writes still enabled, before depth writes are turned off | RWE hides the part of a beam behind a ridge or a unit; the original does not |
| Order against effects | Over the early unit pass and its muzzle flare, under explosions, the late (airborne) pass, health bars and damage smoke | Projectiles drawn after unit meshes and before features and the particle/flash passes | Roughly the right side of units; the flash and smoke passes need checking against the original's fixed order |
| Resolution | 2 px at every supported resolution; 1 world unit = 1 pixel | Fixed 1 device pixel regardless of resolution, zoom or display density | Beam thickness relative to units changes with zoom; the original's does not |
| Length | Head plus the `duration + 1` tail | Back position with the defined duration, doubled at load (`LoadingScene_util.cpp:699`) | Out of scope here; noted because the width must not depend on it |

The actionable half of the table is the first five rows. In particular, the
companion offset must be computed in screen space, after projection, and the
zero case must suppress the second line rather than draw it black. Neither of
those needs a new simulation field.

## Acceptance checks

Static or test-level, once an implementation exists:

1. **Horizontal beam at 1:1.** A shot with `|dx| > |dy|` paints exactly two
   rows: the core colour on the segment's row and the second colour on the row
   immediately above, for the full length. No third row anywhere.
2. **Vertical beam at 1:1.** A shot with `|dx| <= |dy|` is one column wide
   through the middle of its length, with the companion one pixel left for
   roughly the top quarter and one pixel right for roughly the bottom quarter.
   The end quarters are two columns wide, the middle is one.
3. **45-degree beam.** The two lines start two pixels apart, cross in the
   middle, and end two pixels apart; the middle shows only the core colour.
   This is the sharpest test that the offset is screen-space and not
   perpendicular.
4. **Short beam.** A one- or two-pixel segment still shows the core and the
   companion where the rule says they land; no minimum-length clamp and no
   special case.
5. **Second colour zero.** A weapon with `color2=0` paints one line only. No
   black companion may appear. Use the ordinary laser in `LASERS.TDF`.
6. **Palette conversion.** With the shipped `GUIPAL.PAL` and `PALETTE.PAL`,
   the index table above is produced, first minimum wins on ties. The
   Annihilator's 144 must resolve to pure blue (index 252), not to the raw
   in-game entry 144.
7. **No blending.** Painting a beam over a known background leaves exactly the
   palette colour, with no intermediate values at the edges.
8. **No occluder.** A beam fired across a ridge and across a ground unit is
   fully visible in both cases, as the original has no depth buffer. A second
   check with an aircraft over the beam should show the aircraft over it.
9. **Order.** An explosion at the impact point is painted over the beam; the
   muzzle flare under it; damage smoke and health bars over both.
10. **Resolution.** At 640x480, 800x600, 1024x768 and 1280x1024 the beam is
    the same two output pixels at the same world scale. It must not be two
    device pixels at 1080p while the units shrink.
11. **Zoom.** If RWE keeps its zoom, the beam's core+companion must scale with
    the projection's pixels-per-world-unit, not stay a fixed device-pixel
    count. A zoomed-out view must not show a hairline where the original shows
    a two-pixel beam.

## Black-box checks that would settle the rest

A single capture at a known resolution and camera position can settle the
static predictions:

1. Park a known laser unit (the commander is easiest, range 200) so its target
   is due east/west on flat ground. Capture at 640x480 and at 1024x768 and
   count the beam rows and their colours at a column in the middle.
2. Repeat with the target due north/south and with a 45-degree bearing, and
   record the end kinks and the 45-degree pinch.
3. Put a ridge between firer and target, and a unit in front of the beam, and
   record whether the beam is continuous. This is the depth test.
4. Fire the ordinary laser (`color2=0`) and confirm one line, not two.
5. Fire the Annihilator and confirm a blue core (index 252) with the darker
   companion, which arbitrates the palette conversion against the stale TDF
   annotations.
6. Capture an impact frame: confirm the explosion is over the beam, the
   impact sprite under it, and damage smoke over everything.

The remaining uncertainties are:

- **Runtime table contents.** The table is rebuilt by nearest match at load
  and at each palette change. On an installation where `PALETTE.PAL` is
  regenerated from `PALETTE.PCX` (the loader does this when the `.PAL` is
  absent), a few entries could shift. The indices in the table above are for
  the shipped extracted files. A capture of the Annihilator settles the
  direction of the mapping; a capture of the J7 settles 232.
- **Display-path scaling.** The battle frame is drawn into an offscreen surface
  allocated at the display size, so the normal path is 1:1. A fullscreen or
  stretched mode would scale the whole frame, and this static reading cannot
  say which mode the owner used.
- **The airborne pass.** The late unit pass is inferred to be the airborne one
  from `TOTALA-EXE-RENDER.md` section 5; the exact predicate was not re-read.
  A capture with an aircraft over a beam settles the layer.
- **Aliasing details at very shallow angles.** The predicted footprint comes
  from the line routine's step arithmetic. A capture is the acceptance test
  for the one-pixel kinks.
- **Whether RWE's flash and smoke passes sit where the owner sees them.** That
  is outside this document; the paint-order list above is the reference to
  compare against.
