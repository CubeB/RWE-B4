# Model state, cached rendering, construction wireframes and attachments parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md`
and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers
one chapter of the reference, `graphics-model-render` (module group `graphics/model_render`,
35 entries from `0x458160` to before `0x45b250`): the model instance and its piece records, how a
unit's picture is built and cached, projection and polygon material selection, the lighting
pass, the construction recolour and wireframe, attached products and cargo, the water cut and tint,
and shadows. This is the finding that implementation work cites for any of them, per `AGENTS.md`
and ADR-0004.

**This is the best-covered area of the register.** `TOTALA-EXE-SHADING.md` (both halves) decodes
the shaded rasterizer, `-RENDER.md` sections 52 to 58 and 100 to 101 the draw path, the shadows and
the halo, `-ECONOMY.md` ("The build display", "How the wireframe is drawn") the construction display,
and `NAVAL-SYSTEMS-PARITY.md` rules 11 and 19 the water cut and the recolour regimes. So this audit
**compares the chapter against those documents and records the places they agree to the
number**, and spends its words on the few that differ. Per the brief, the shading conflicts are
recorded precisely and nothing is re-derived. Every difference here is **presentation only**: the
model renderer reads the simulation and never feeds it.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-model-render.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its source
  links were followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only. BT-16 (enemy without
  sonar: cut versus tint), BT-10 and BT-17 touch this chapter and are **not re-raised**.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.**
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene_render.cpp` (the shadow pass about
  1737-1830, the unit pass about 1858), `GameScene_util.cpp` (`computeBuildPhase` about 1147,
  `unitCastsShadow` about 530), `shaders/unitTexture.frag`, `shaders/unitBuild.frag`,
  `src/rwe/render/WireframeScan.cpp` and `sim/UnitDefinition.h` at the working tree of 2026-10-17.
  Nothing was built, run or changed. Another agent had uncommitted edits in the tree, so every line
  number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Instance state, piece order and visibility (not applicable, with two checks)
3. The cache, one plane and two planes
4. Projection, bounds and polygon material
5. Lighting and supersampling: the shading cross-check
6. Construction recolour and wireframe
7. Attached products and cargo
8. Water: cut, tint and the digger cutoff
9. Shadows
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1, 2 state, name-order, transforms | `-SHADING.md` section 11 (piece flags and defaults); `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 9 | rule 2 |
| 3 cache, one or two planes | `-SHADING.md` section 23 (height plane, `ZBuffer=0`); `-ECONOMY.md` 205-235 | agrees; bit numbering differs (rule 3) |
| 4 projection, bounds, material | `-RENDER.md` sections 52, 56 | MATCHES |
| 5 lighting, supersampling | `-SHADING.md` sections 05-11, 22-26; `-RENDER.md` section 101 | MATCHES; no conflict found |
| 6 construction recolour, wireframe | `NAVAL-SYSTEMS-PARITY.md` rule 19; `-ECONOMY.md` "The build display" | MATCHES |
| 7 products, cargo | `NAVAL-SYSTEMS-PARITY.md` rules 1, 11 | not repeated |
| 8 water cut and tint | `NAVAL-SYSTEMS-PARITY.md` rule 11; BT-16 (open) | the tint formula is new (rule 8) |
| 9 shadows | `-RENDER.md` section 100; `-ECONOMY.md` "The nanoframe's shadow" | MATCHES |

## 2. Instance state, piece order and visibility (not applicable, with two checks)

**Reference (sections 1, 2).** The object state is a flat array of 0x36-byte piece records plus one
mutable point array per piece. The player-state creator **reorders the array to match the script's
piece-name list, case-insensitively**, swapping a match into slot *i*; an unmatched name gives no
diagnostic and leaves whatever the swap left there, and the tree links are rebuilt afterwards by
matching geometry pointers. A piece is marked visible **only if it has at least three vertices**;
smaller pieces exist as attachment or animation points. The update compares the owner's three base
angles with cached ones by signed-short difference and acts only on a change of **eight or more** on
some axis. Posing walks children first. A unit with a carrier is not drawn independently.

**Register.** `-SHADING.md` section 11 records the defaults at `0x45AEC0`: cache on, **SHOW if the
object has three or more vertices**, shade on. The name-order pass is not in the register.

**RWE: not applicable, MATCHES in effect.** RWE keeps a model definition with `pieceIndicesByName`
(upper-cased, so case-insensitive) and resolves a script's piece number through the script's name
list to that model piece (`sim/util.cpp` about 20-60, `UnitBehaviorService.cpp` about 2208). It does
not cache a bitmap per unit, so the eight-unit angle threshold has no counterpart (eight in 65536
is about a twentieth of a degree; invisible). Pieces under three vertices draw nothing and still
serve as points (`emitVtolFromPiece` reads a thruster piece's two vertices).

- **A script piece name with no model piece: UNCLEAR, mod data.** The original proceeds with an
  arbitrary piece in that slot; RWE throws at the sites traced (`sim/util.cpp` "missing
  piece definition" for a parent lookup, `GameMediaDatabase.cpp` about 61-69). Not traced end to end; no shipped unit is known to have a name its model lacks.
- **Hidden while carried: DIFFERS in the gate, low risk.** The register (`-DATA.md` near 631) says the
  hidden-while-carried bit is set iff the unit is attached to piece -1, and the chapter says that
  bit drops the child from preparation, bounds and draw. RWE hides a carried unit only when
  `carriedPiece` is empty **and the transport is a `floater`** (`unitIsVisibleToLocalPlayer`,
  `GameScene_commands.cpp` about 706). A non-floating transport whose script attaches to no piece
  would draw its cargo; no shipped case is known.

## 3. The cache, one plane and two planes

**Reference (section 3).** A scratch frame of 600 by 600 is allocated once. The picture is built
**with one plane only when the force-depth argument is zero, the owner's z-buffer flag bit is clear
and the unit is complete (build fraction exactly zero)**; otherwise it gets a second plane (the
height plane). Lit rasterization is chosen **only when runtime bit `0x20000000` and the shading
option are both set**. A cache miss falls back to direct piece drawing.

**Register.** `-SHADING.md` section 23: a clean finished unit renders through the one-plane allocator
(`unit+0x114` bit 0 clear and the float at `unit+0x104` zero) and everything else through the
two-plane one; with **no height plane the shaded variants are skipped**, so a finished `ZBuffer=0`
unit (CORFAV, CORTRUCK) is drawn unshaded. Section 11: the shaded chain runs only for a unit with
`0x20000000` (bmcode 0, a building) **and** the SHADING option.

**RWE: MATCHES.** `zBuffer` is parsed (`io/fbi/io.cpp` 125), carried to the definition
(`UnitDefinition.h` about 258-266), and `finishedShadeStrength = zBuffer ? shadeStrength : 0`
(`GameScene_util.cpp` about 688-700); the Buildings default of the shading switch is the
building-only rule (`GameScene_menu.cpp` about 476, `shadeStrengthFor`).

- **Bit numbering: UNCLEAR, no behavioural difference.** The chapter says "z-buffer flag **bit 1**
  clear", the register says `[unit+0x114]` **bit 0** clear. Both describe the same flag by a different
  count (the chapter's numbering is not always the register's); the logic (single plane iff flag clear and
  complete) is identical. Recorded so nobody cross-reads the numbers.
- **The 600 by 600 scratch and the arena eviction** are the original's cache; RWE draws the model
  live with a depth buffer. Not applicable.

## 4. Projection, bounds and polygon material

**Reference (section 4).** Screen X is X and screen Y is `-Z - Y/2`, from whole signed coordinates;
bounds start at zero (the origin is always inside) with two-pixel margins; pieces are processed in
**reverse array order**; a piece takes part if its colour group matches or the owner is unfinished
(the construction exception relaxes the group, not visibility); each vertex gets a height value of
**model Y plus 50, or plus 125 for the digger flag**; with a first-face index other than -1 the loop
starts at face 1; flat polygons for solid-colour faces, textured **only for quads**; picture origin
**X + 128, Z - Y/2 + 32**, direct pieces drawn at X+128 and Y+32; local arrays hold 2000 points and 25
face vertices.

**Register.** All of it: `-RENDER.md` section 52 (the `(x>>16)+0x80`, `+0x20` projection; primitive
dispatch; skip primitive 0 whenever a selection primitive is declared, with the loader swap
at `0x4CB370` making that the declared plate; a quad of the wrong winding paints nothing);
`-SHADING.md` section 03 (four arrays of exactly 2000, reverse piece iteration) and section 22 (height
`modelY + (digger ? 125 : 50)`, wrapping in a byte).

**RWE: MATCHES, with the documented absences.** The flat-colour and quad-only rules, the winding
cull and the selection-plate skip are the register's items 3, 4, 5 and 6 (`-RENDER.md` section 58),
all closed. The painter order is replaced by a depth buffer (`-RENDER.md` section 5). The digger
bias is **DIVERGENCE (documented)**: `-MOVEMENT.md` "Not ported" says the 50 and 125 are the height
buffer's bias and RWE has no per-drawable height buffer. Two pop-up defences (ARMAMB, CORTOAST) set
the flag; the effect on their picture is rule 8. The 25-vertex face array has no counterpart
(RWE sizes by the model).

## 5. Lighting and supersampling: the shading cross-check

**Reference (section 5).** The light vector is set from three integer arguments **multiplied by
0.01 and not normalised**. For each face the lit routine builds the cross product of the first three
indexed vertices, normalises it, and for a face with **repeated indices uses an upward normal**; it
accumulates face normals per vertex, **averages by incidence count**, dots with the light vector,
**multiplies by 5.0, converts to an integer and masks with 31** (so a negative or oversized value
wraps instead of clamping); **unlit pieces use row 15**. At twice width and height (anti-aliasing on,
runtime bit `0x20000000`, colour group nonzero) a doubled scratch is downsampled through the alpha
table for colour and **every second sample** for the height plane.

**Register, point by point, no conflict found.**

| Point | Chapter | `-SHADING.md` | Verdict |
| --- | --- | --- | --- |
| Light vector | three integers x 0.01, not normalised | `(-0.8, 1.0, 0.25)` unnormalised (sections 07, 26) | agree (arguments -80, 100, 25) |
| Face normal | cross product of first three indexed vertices, normalised | section 05: `A = V[i0]-V[i1]`, `B = V[i2]-V[i1]`, normalised, no zero guard | agree; the register adds the collinear NaN case and that the exe's normals point inward |
| Repeated index | upward normal | `(0, +1, 0)`, "model-space up", reads as dark | agree |
| Averaging | by incidence count | section 06: mean of unit face normals, **per piece**, float32, no renormalisation | agree; the chapter does not say per piece or float32 |
| Level | 5.0 x dot to integer, `& 31` | `(int32)(5.0 * dot) & 0x1F` (section 07) | agree |
| Unlit | row 15 | `DONT_SHADE` pins row 15, measured x1.0009 (section 11) | agree |
| Which units | runtime `0x20000000` and the shading option | buildings and the feature stand-in only (section 11) | agree |
| Supersample | AA option, runtime `0x20000000`, colour group nonzero | `-RENDER.md` section 101: buildings only, a 2x2 block averaged if a cached building piece covers it | agree; RWE's `anti-alias-units` is its own addition, off by default |
| Height plane in the downsample | every second sample | section 22 (the height test) | not contradicted |

**RWE: MATCHES** at `shaders/unitTexture.frag` (the `(int)(5.0 * dot(n, (-0.8, 1.0, 0.25))) & 0x1F`
comment and body) and the post pass. The chapter's note that doubled dimensions depend on a fixed 600
by 600 scratch does not apply. **No conflict in this section.**

## 6. Construction recolour and wireframe

**Reference (section 6).** The recolour runs only if the picture has a height plane and the build
fraction is not exactly zero. A is the truncation of `buildLeft x 255`; two **triangular-wave
palette indices over `0xA0`-`0xAF`**, phases from the unit id XORed with 5 and 9 plus
`tick x 33/30` and `tick x 57/30`; the lower/middle/upper split at byte threshold L with the band
`[max(0, L-4), L)`; five regimes and thresholds (table in the chapter). After recolouring,
`DrawPieceEdges` projects each visible piece's faces (same first-face rule), repeats the first point to
close the contour and draws it with pulse 2, tested against the secondary plane: "occluded polygon
contours, not an unrestricted X-ray line grid".

**Register / earlier audit.** `NAVAL-SYSTEMS-PARITY.md` rule 19 reports **MATCHES** for the five regimes,
the wrap into a byte and the band width. `-ECONOMY.md` "The build display" is the same table, the same
palette range 160-175, "xor 5 at 33 steps for A, xor 9 at 57 for B", and decodes the wireframe as a
scan converter that writes two end pixels per row, height-tested, in colour B (= pulse 2).

**RWE: MATCHES, re-verified.** `computeBuildPhase` (`GameScene_util.cpp` about 1147-1205) was read against
the chapter's table row by row: `255 x (v-235)/20`, `255 x (v-200)/35`, `3 x (115-v) - 1`
(= `(115-A) x 255/85 - 1`, since 255/85 is exactly 3), `3 x (30-v) - 1`, `255 x v/30`, the low byte kept,
and the fill modes (erase, A, B, texture) in the right regions. The wireframe is
`scanWireframePolygon` over each polygon's projected corners, height-tested against the unit's own
surface, in colour B. **Nothing to add.** The one text point worth noting is the chapter's
"exactly zero skips the whole operation"; RWE makes the call only for a unit still being built.

## 7. Attached products and cargo

**Reference (section 7).** A factory product is a live attached unit with its own hierarchy,
build fraction and colour; the parent's picture merges the product's: bounds extended by child
minus parent offset, a forced two-plane picture for the child with its own recolour, and a
depth-adjusted merge. There is **no switch on a named ship or yard**: the wireframe differs between
products only through geometry, attachment transform, draft and polygon layout. A one-plane parent
draws eligible cargo piece by piece and so does not interleave child depth.

**Register.** `NAVAL-SYSTEMS-PARITY.md` rules 1 and 11 (height of an unfinished product, the
shipyard composite cut) and its Gaps rank 8 hold the open items. Nothing here adds a rule.

**RWE: not repeated.** Per the brief this audit does not re-raise the naval construction rendering.

## 8. Water: cut, tint and the digger cutoff

**Reference (section 8).** After composition, if the sea level is above the parent's whole Y the
difference is raised by the depth bias (50, or 125 for a digger) and used as a byte threshold. For
a **non-local owner without the sonar-contact bit** the pixels at or below it become
**transparent**; for the **local owner or a unit with that bit** they go through the **blue palette
table**. **Digger definitions also get a final cutoff at 125.** The final image is drawn with the
ordinary blit unless the cloak bit is set (or a debug mode), when it is drawn with the blended blit.

**Register.** The cut-versus-tint choice is `NAVAL-SYSTEMS-PARITY.md` rule 11 and **BT-16 (open,
original-game check)**: RWE tints every below-sea fragment and never cuts. The cloak blend is
`-VISION.md` section 17 ("half-blend with what is behind it", ported). The digger flag is described in
`-ECONOMY.md` (near 151, 219) as a **copied shadow** cut at 125 by `0x4BA1B0`; `-VISION.md`
(near 437) calls `0x4BA1B0` and `0x4B96E0` "the brightness passes" in the unit draw.

**RWE.**

- **The tint formula: DIFFERS, presentation only, and new.** RWE multiplies the lit texel by
  `(0.5, 0.5, 1.0)` for any fragment at or below sea (`unitTexture.frag` 46 and 126,
  `unitBuild.frag` 40 and 70). The original does not multiply: it maps the pixel's **palette index**
  through the blue table, whose rule (`graphics-palette` section 5) is red halved, green halved,
  **blue halved plus 50**, snapped back into the palette by the band-limited nearest search. Replaying
  that rule over the shipped `PALETTE.PAL` (`BT-GRAPHICS-PALETTE-PARITY.md`, rule 7) gives, for example,
  black to `(0, 0, 55)` (RWE: black), white to `(107, 143, 163)` (RWE: `(127, 127, 255)`), the bright
  red `(127, 7, 0)` to `(51, 19, 63)` (RWE: `(63, 3, 0)`), and an average channel error of 19.6
  out of 255 against RWE's multiplier. So the original's underwater part lifts black to a deep
  blue and **desaturates** saturated colours towards grey-blue; RWE keeps blue at full strength
  and goes darker. It rests on the chapter's claim that the tint pass uses the blue table; nothing
  in the register contradicts it, and no one has compared it to the screen.
- **Cut versus tint for an enemy without sonar: BT-16, not re-raised.**
- **The digger final cutoff on the unit's own picture: UNCLEAR, possible CONFLICT with the register.**
  The register has the 125 cut only on the **shadow copy** (`0x4BA1B0`). The chapter says a digger's
  final picture is also cut at 125, which for a model whose parts hang below its origin
  (ARMAMB, CORTOAST, the two pop-up defences) hides the buried half. If so, RWE's depth buffer
  hides the buried half only if the terrain pass writes depth; the register's "nothing for the
  flag to select" (`-MOVEMENT.md`, "Not ported") then understates it. Needs a look at the pop-up
  models, or a spec-room read of the water and digger tail of `0x459200`.
- **The text label.** Both `0x4BA1B0` and `0x4B96E0` are the chapter's cut-below and tint-below
  functions; `-VISION.md`'s phrase "brightness passes" is the register's own label for them, and
  should read "cut and tint".

## 9. Shadows

**Reference (section 9).** Requires the shadows option and no `noshadow`. One plane: a static (runtime
`0x20000000`) non-digger unit gets a **cached geometric shadow**; others a **silhouette** if the
vehicle-shadow option is on and the definition mask `0x81000` is clear (hover or floater excluded).
Two planes: a digger path with the cutoff, the static geometric path, and the same vehicle option. The
geometric-shadow gate tests the definition index nonzero **or** origin height at or above the sea, so
with a normal definition it applies regardless of water. The measured shadow shape uses X + Y/4 and
-Z - Y/4; the drawn shape sets height Y + 25; the built shadow is cut by the source picture at
**offset (5, 0)**, compressed, and cached; the silhouette copies a frame and replaces every
non-key pixel with 0. Shadow and image-copy helpers rely on scratch capacity.

**Register.** `-RENDER.md` section 100 gives the same two passes with the same gates (option bits 2 and 3,
`noshadow`, `0x81000`), the 5-pixel x offset at the ground's height, the single index-0 silhouette
through `PALETTE.ALP`, and no water cut for a finished unit; `-ECONOMY.md` ("The nanoframe's shadow")
the build state.

**RWE: MATCHES.** `GameScene_render.cpp` about 1737-1830: `shadowsEnabled`, `unitCastsShadow`
(`noshadow`), `vehicleShadowsEnabled` for a mobile unit, no shadow for a mobile hovercraft or floater, a
floating building keeps its shadow on the sea bed, the aircraft's shadow positioned at ground height,
a nanoframe's shadow kept out of its own outline. The (5, 0) cut-out in the chapter's `BuildShadow` is
what the register describes as the building's own shape cut out of its projection
(`-RENDER.md` section 100); RWE's cut batch for a nanoframe is the finished-versus-frame case. **No
difference found.**

---

## 10. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Underwater tint formula.** RWE multiplies by `(0.5, 0.5, 1.0)`; the original remaps the pixel's palette index through a blue table (halve R and G, halve B and add 50, snap to the palette). Black goes to dark blue, bright colours desaturate. Rests on the chapter's reading that the tint pass uses that table. | DIFFERS, presentation | an original-game look at a half-sunk ship, then a shader change (the table can be computed once from the palette; see the palette audit) |
| 2 | **Digger final cutoff at 125 on the unit's own picture** (ARMAMB, CORTOAST). The register has it only on the shadow. | UNCLEAR, possible CONFLICT | spec-room read of the digger branch at the end of `0x459200`; a look at the two models in RWE |
| 3 | **Hidden-while-carried** is gated on the transport being a floater, not on "attached to no piece". | DIFFERS, low risk | spec-room confirmation; no shipped case known |
| 4 | **Script piece name with no model piece** throws in RWE; the original continues with an arbitrary piece. | UNCLEAR, mod data | none |
| 5 | **Register text**: `-VISION.md` calls `0x4BA1B0` and `0x4B96E0` "brightness passes"; per the chapter they are the water cut and the water tint. | documentation | fix the label |
| 6 | **Z-buffer flag numbering** differs between the chapter ("bit 1") and the register ("bit 0"). | UNCLEAR, no behaviour | note only |

Everything else in the chapter, the shading arithmetic, the recolour regimes and their phases, the
projection, the material rules and the two shadow passes, **agrees with the register and is
implemented**. No source file was changed.
