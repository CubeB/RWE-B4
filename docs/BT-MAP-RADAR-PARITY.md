# Minimap imagery, mapped terrain and contact display parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `map-radar` (module group `map/radar`, eight functions,
`0x4665d0` to `0x466dc0`): how the minimap picture is sized and built, how the mapped and
currently-seen shading is applied, which units and projectiles get a symbol, what a selected
unit's range circles are, and what the picking list holds. This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The chapter states plainly that it **consumes** the contact flags and does not compute them;
the sensor pass that produces them is audited in `BT-INGAME-INFO-PANEL-PARITY.md`. Sight and
exploration are `BT-MAP-LINE-OF-SIGHT-PARITY.md`. All differences below are **presentation only**
(nothing here is hashed or reaches the simulation), except where a row says it is the source of
information the player acts on.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\map-radar.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only. BT-6 and BT-68 touch
  this chapter and are **not re-raised**.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** A census of the shipped weapon files in
  `totala1`'s `weapons` folder (below) was run with throw-away scratch commands, outside the tree.
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene_render.cpp` (`renderMinimap`
  about 691, the rings about 837-975), `GameScene_commands.cpp` (`getUnitUnderCursor` about 45,
  `unitIsDetectableByLocalPlayer` about 755, the minimap fog about 1335-1385), `GameScene.cpp`
  (`worldToMinimapMatrix` about 1156), `TextureService.cpp` (`getMinimap`) and `LoadingScene.cpp`
  (about 259) at the working tree of 2026-10-17. Nothing was built, run or changed. Another agent
  had uncommitted edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The picture: size, source and the unplayable border
3. Mapped terrain and current-sight shading
4. Dots: eligibility, position, damage blink, picking
5. Selected-unit range circles
6. Projectile symbols
7. Lifetime, cadence and cleanup (not applicable)
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1, 2 picture size, init, viewport rectangle | `TOTALA-EXE-INTERFACE.md` places the sidebar (PREFS at y 126); no sizing rule | rule 2 |
| 3 mapped and current-sight shading | `TOTALA-EXE-VISION.md` "Drawing" (the 3D fog frames only) | rule 3 |
| 4 unit contact eligibility, picking | `-VISION.md` section 18 (`0x466DC0`, `radlogo`, blink) | agrees; rule 4 |
| 5 range circles | `-VISION.md` section 25 (full decode, colours in `-RENDER.md` section 50) | agrees; rule 5 |
| 6 projectile symbols | none | rule 6 (MISSING) |
| 7 lifetime | none | rule 7 |

## 2. The picture: size, source and the unplayable border

**Reference (sections 1, 2).** `BuildRadarPicture` fits the playable map dimensions into a
**126-pixel** maximum, preserving aspect in integer arithmetic, records the centred gadget origin
and the width and height, and allocates the picture. When map loading supplied an embedded radar
image it downsamples that; otherwise it samples the terrain artwork (a double-size temporary
surface, each pixel taken from the tile index at the scaled world position, an out-of-range tile
index falling back to tile zero) and downsamples. The picture shows terrain artwork, not occupancy.
A degenerate aspect ratio can truncate a short side to zero with no clamp. The rescale routine
fits supplied dimensions **minus 32 horizontally and 128 vertically**, "matching the playable border
exclusion". `InitRadar` sets the display rectangle inclusive, starts the **blink timer at 7**,
and `CalcRadarViewportRect` scales the scroll into minimap coordinates and derives the inclusive
right and bottom from the viewport's tile dimensions times 16.

**RWE: DIFFERS in the fit, MISSING in the fallback.**

- **The 126 limit: UNCLEAR.** RWE scales the TNT's embedded minimap into a **128 by 128** box
  (`minimapViewport = (0, 0, GuiSizeLeft, GuiSizeLeft)`, `GameScene.cpp` about 190 and 237,
  `scaleToFit`), so a square map is two pixels larger than the original's 126. There is no
  minimap rectangle in `SIDEDATA.TDF` to decide it (searched in the shipped file). The register
  places the in-game options sidebar at y 126 (`-INTERFACE.md` section 65), which agrees with 126
  being the picture's height. Every dot, ring and click position scales with it, so a square map's
  minimap is 1.6 percent larger than the original's. Presentation only.
- **The fallback picture: MISSING.** RWE reads the TNT's embedded minimap
  (`TextureService::getMinimap`) and throws if the TNT has none; the original builds one from the
  terrain artwork. Every shipped map embeds one, so nothing is lost on retail data. A mod map
  without the block would not load in RWE.
- **The border exclusion: UNCLEAR.** The chapter's 32 and 128 pixel margins in the rescale
  routine and the "playable map dimensions" in the fit both say the minimap shows the playable
  area, not the whole TNT. RWE maps the terrain's own world bounds
  (`leftInWorldUnits` to `rightCutoffInWorldUnits`) onto the image, which is the same idea. Whether the
  TNT image already excludes the border was not checked here.
- **Viewport rectangle: MATCHES in colour and in effect.** RWE projects the camera corners
  through the inverse view matrix (`renderMinimap` about 732-745) and outlines them in
  `(247, 227, 103)`, which `-RENDER.md` section 50 resolves as interface slot 14 (palette 194).
  The chapter's derivation (scroll scaled, plus tile dimensions times 16) gives the same
  footprint up to a pixel of rounding; the rounding rule is not audited.
- **The blink timer start (7)**: see rule 4.

## 3. Mapped terrain and current-sight shading

**Reference (section 3).** `UpdateRadarMapped` rebuilds only when a "map changed" bit is set,
clearing it first. Each minimap pixel maps to **one half-resolution information cell** (the same
32-unit cell as the fog grid). If the displayed player's **permanent mapping bit** is absent the
pixel is the **fog colour**; if mapped **and the current-sight byte is nonzero** it is the base
terrain pixel; if mapped but not currently seen it is the base pixel passed **through the display
colour map** (the "mapped but unobserved" shade). The byte grid named "explored" in the source is
in fact live sight coverage; the unsigned-short buffer keeps the exploration history. A separate
"pending" bit controls presentation of the finished surface; the camera rectangle is drawn after it.

**Register.** `-VISION.md` "Drawing" is about the 3D view's marching-squares fog frames. The
minimap's own shading, the colour map and the fog colour are **not decoded** in the register.

**RWE: DIFFERS in the colours, MATCHES in the three states.** The minimap fog is a separate
overlay texture with **one texel per vision cell** (`GameScene_commands.cpp` about 1335-1385,
`VisionCellSizeInTiles = 2` tile cells, i.e. 32 world units, the same cell as the original's):
**visible** is clear, **explored and not visible** is black at alpha 120 (about 47 percent), and
**unexplored** is opaque black. So:

- **The three-state structure: MATCHES.**
- **The unexplored colour: UNCLEAR.** The chapter calls it "fog colour" and does not give the
  value. Opaque black is a guess; the register has no decode of the fog colour byte.
- **The mapped-unobserved shade: UNCLEAR.** The original remaps the base pixel through a
  palette-to-palette colour map (a dimming of some kind, possibly the gray table of
  `BT-GRAPHICS-PALETTE-PARITY.md`); RWE blends 47 percent black over it. The colour map's
  construction and which table it is were not found in either source.
- **Mapped-game start: MATCHES in effect.** `addPlayer` explores the whole map for a Mapped game
  (`GameSimulation_visibility.cpp` about 70), so the minimap comes up in the dim state, like the
  original's mapping bit set with no sight.
- **The pixel resolution**: the original produces one minimap pixel per source pixel through
  its 126-pixel picture; RWE stretches a one-texel-per-cell texture over the rectangle with the
  GPU's filter. Edge crispness differs; presentation only.

## 4. Dots: eligibility, position, damage blink, picking

**Reference (section 4).** `DrawRadarUnits` clears the hot-radar count and copies the mapped
terrain into the final surface, then scans the unit array. A unit with a nonzero definition draws
if information restrictions are off, **or** the runtime contact mask `0x300` (the radar or sonar
bit) is nonzero, **or** its owner is the displayed player. Restrictions are off when neither
mapping nor dynamic sight is in play, or a UI option overrides. The loop tests **no alliance, no
cloak, no radar distance, no sonar depth and no jammer**: those were applied upstream. Position:
`x` scales whole X; `y` scales whole Z **minus half the whole height**, so an aircraft's dot shifts
up the map by half its altitude. The base logo is in the owner's colour; a **nonzero recently-damaged
timer suppresses the logo on one blink phase**; the hovered id gets the highlight logo separately,
even during the suppression. Every eligible unit also gets a **ten-byte picking record** (short id and
two screen coordinates), and blinking does not remove it. The list has no capacity check.

**Register.** `-VISION.md` section 18 has the same gate (`test ah,0x3`, `0x466E6A`), the same
position formula, the `radlogo` four-by-four sprite (frame = owner colour), `radlogohigh`
six-by-six for the hovered unit, and the blink: timer `+0xFA` set to `0xF0` (240) by the damage
routine, phase bit flipped every eight ticks by `0x466580`. **BT-68 (resolved)** records that the
timer is read only by a display loop.

**RWE.**

- **Eligibility: MATCHES, with one documented departure.** `renderMinimap` draws every unit
  `unitIsDetectableByLocalPlayer` accepts (own, seen, or a radar or sonar contact;
  `GameScene_commands.cpp` about 755-770), which is the register's `0x300` test over the same
  contact sets. The original's minimap does not test cloak, so a cloaked enemy inside your radar still
  shows; RWE does not copy that (`-VISION.md` section 18, "RWE does not copy it": **DIVERGENCE
  (documented)**).
- **Position and sprites: MATCHES.** `worldToMinimapMatrix` applies the cabinet projection with
  0.5, so an aircraft's dot rides half its altitude up the map (`GameScene.cpp` about 1156-1179);
  the sprites are `radlogo` (ten frames) and `radlogohigh` (`LoadingScene.cpp` about 259-264).
- **Units riding in a transport: UNCLEAR.** RWE skips a unit with `carriedBy` set (about 706);
  the chapter's loop has no such test, and the register does not say where a carried unit's
  position is kept. A carried own unit probably draws in the original, on or near its carrier.
  Low value.
- **The damage blink: MISSING, presentation only** (recorded as not ported in `-VISION.md`
  section 18). No timer exists on `UnitState`; the dots never flash. The rule, from the register
  and the chapter together: a unit hit sets the byte to 240; while it is nonzero the logo is
  skipped on every other blink phase (phase flips every eight ticks); the highlight logo for a
  hovered unit ignores the suppression; the timer counts down once a tick. The chapter warns
  not to infer a seven-tick cadence from the initial blink counter value 7 (it is the reload value
  of a counter whose decrement is elsewhere); the register's decode (8-tick phase) is the better
  evidence. Needs a decaying per-unit field set from damage events, and is the one thing here that
  tells a player "that unit is being hit" at a glance when the camera is elsewhere.
- **Picking: MATCHES.** `getUnitUnderCursor` over the minimap tests the same predicate against
  the sprite rectangle of each dot (about 45-85), so a blinking dot stays pickable. The first match
  in map order wins; the original's tie rule (which of two overlapping dots) is not stated and was
  not compared.
- **Hover highlight: MATCHES** (`renderMinimap` about 720).

## 5. Selected-unit range circles

**Reference (section 5).** Only **selected** units draw circles. Radar, sonar, radar-jammer and
sonar-jammer ranges are shown when the unit is active **or its definition is not on/off
capable**; detectors share one interface colour, jammers another. The radius scales by minimap width
over playable map width for **both** axes (no ellipse). An anti-weapons definition additionally
scans all three weapon slots for the interceptor bit and draws a ring of **coverage minus 512**
world units; a nonzero stockpile draws it **dashed** (dash parameter 32 with the blink phase), empty
stockpile solid. A negative displayed radius is not clamped.

**Register.** `-VISION.md` section 25 and `-INTERFACE.md` section 99 decode all of it: four fields
(sight is deliberately not drawn), the on/off gate (anti-missile outside it), the 32-gon circle with
clipping to the minimap, horizontal scale for both axes, selected-only for all selected units,
colours 83,223,79 / 255,71,0 / white, the dashed ring with 16 of 32 segments and the eight-tick
parity, and the -512.

**RWE: MATCHES.** `renderMinimapDetectionRings` and `renderMinimapCoverageRings`
(`GameScene_render.cpp` about 837-975) implement every one of those points, including
`buildMinimapRing`'s 32 segments, Liang-Barsky clipping and the dash parity from `sceneTime / 8`. Two
edges:

- **The antiweapons gate: DIVERGENCE (documented).** The original gates the block on
  `antiweapons` (flags bit 29); RWE does not parse it and gates on the interceptor weapon itself.
  The comment in `renderMinimapCoverageRings` records that the two are the same set in shipped data
  (ARMAMD and CORFMD against `AMD_ROCKET` and `FMD_ROCKET`).
- **A coverage under 512: DIFFERS, mod data only; UNCLEAR.** RWE skips a ring whose
  `coverage - 512` is not positive (and any radius under one pixel). The chapter says a negative
  radius is not clamped, which in a routine that multiplies a radius by sine and cosine most likely
  draws the circle of the absolute value. Inference, not read; no shipped weapon has a coverage
  under 512 that an interceptor carries.

## 6. Projectile symbols

**Reference (section 6).** The same composition pass walks live projectile records at their
projected position. A projectile whose weapon is **neither targetable nor an interceptor** draws
**one pixel** unless the weapon has the **noradar** bit, provided information permits that cell
(the live per-player sight byte when dynamic sight is on, else the displayed player's permanent
mapping bit, with a bounds check) **or the projectile belongs to the displayed player**. A weapon
with the **targetable** (bit 29) or **interceptor** (bit 30) bit draws instead the **nuclogo**
sprite coloured by the projectile's player, if information permits it or the owning unit's player
is the displayed player; this branch does not repeat the noradar test and dereferences the owner
without a guard, so an ownerless projectile of those kinds is not fully protected. Projectile
symbols are not in the picking list, and the pass does not select or fire an interceptor.

**Register.** No row: `nuclogo` is not named anywhere in the register, and `BT-WEAPONS-WEAPON-TYPES-PARITY.md`
records `noradar` as "parsed and never read" with its effect **UNCLEAR**. **This chapter supplies
the effect.**

**RWE: MISSING, and a player would notice the nuclear case.** `renderMinimap` draws units, the hover
highlight, the rings and the camera box; there is no projectile pass (`renderMinimap` about 691-745,
and no `nuclogo` anywhere under `src/rwe`). So:

- **No flying projectile is ever a pixel on the minimap.** Every shot, rocket and bomb that the
  original plots in the owner's cell of vision (and your own, anywhere) is absent.
- **No nuke warning.** The original plots a launched nuclear missile (`NUCLEAR_MISSILE` and
  `CRBLMSSL`, the two `targetable=1` weapons in `totala1`'s `rockets.tdf`) as a coloured icon wherever
  the cell is seen, and your own anywhere; also an anti-missile projectile (`interceptor=1`). The
  `nuclogo` frames exist in `anims/FX.GAF`.
- **`noradar`: its effect is now stated.** It removes an ordinary projectile's single pixel from
  the minimap. **No weapon in `totala1` sets it** (census, all eight weapon files), so on retail
  data the rule has no visible case.

This is the only chapter finding here that changes what information a player has; RWE gives
**less** than the original (a missed warning), the opposite sign from the footer leak in
`BT-INGAME-INFO-PANEL-PARITY.md` rule 7. Presentation only.

## 7. Lifetime, cadence and cleanup (not applicable)

**Reference (section 7).** The timer initialisation and phase consumption are in this module and
the routine that advances blink time is elsewhere; the caller decides when composition runs, and
`DrawRadar`'s pending gate is not a fixed frequency. There is no serializer or packet; restore
rebuilds the surfaces. `FreeRadar` frees three surfaces and nulls three pointers and does not
reset the rest.

**RWE: not applicable.** The minimap is a sprite and a fog texture rebuilt when the fog grids
change (`fogVisibleSnapshot`/`fogExploredSnapshot` comparison, about 1339), redrawn every frame, not
saved. The cadence is a presentation choice; the 8-tick blink is the only timing rule that matters
and is rule 4.

---

## 8. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **No projectile symbols on the minimap**: a launched nuke (or an anti-missile round) is not shown anywhere; ordinary shots never plot as a pixel. | MISSING | code: a projectile pass using the same predicate as the dots, `nuclogo` from `anims/FX.GAF` for targetable and interceptor weapons |
| 2 | **No damage blink** on the dots (a hit unit's dot should flash for 240 ticks, phase flipping every 8). | MISSING (recorded as not ported) | code: a presentation-side per-unit hit timer, never hashed |
| 3 | **Fog colours**: unexplored is opaque black, mapped-unseen is 47 percent black; the original's two colours (fog colour, colour map) are undecoded. | UNCLEAR | spec-room read of the update routine's fog byte and the colour map (and a screenshot comparison) |
| 4 | **Picture size 128 versus 126** and a missing fallback builder from terrain artwork. | UNCLEAR / MISSING (mod maps) | spec-room read of the gadget rectangle; then a one-line change |
| 5 | **Units in a transport** are hidden from the minimap; the chapter's loop has no such test. | UNCLEAR | spec-room read of the position kept for a carried unit |
| 6 | **A coverage ring under 512** is skipped; the original probably draws the mirrored circle. | UNCLEAR, mod data only | none |

No source file was changed.
