# What the original executable does

Notes from disassembling Total Annihilation's `TotalA.exe` to work out how the
original behaves, so RWE can match it instead of guessing. Everything here was
read out of the binary and, in most cases, checked against RWE by replaying the
decoded arithmetic side by side with ours.

This is a findings document. It records addresses, constants and algorithms —
the facts you need to reimplement a behaviour — not disassembly listings. Short
instruction excerpts appear only where the exact operand order is the finding.

Aircraft *attack* behaviour — which point an aircraft is sent to, rather than
how it flies there — lives in a companion document,
[TOTALA-EXE-MISSIONS.md](TOTALA-EXE-MISSIONS.md): the mission table, the bomber
attack run, the gunship pendulum, and what `hoverattack` and
`maneuverleashlength` really do.

## The binary

| | |
|---|---|
| File | `TotalA.exe`, GOG release |
| Size | 1,178,624 bytes |
| MD5 | `8e74a1dffa1f5988624c52048f5b20cd` |
| SHA‑1 | `764dc919c3bd0365751aefba8e9a667299a3ce2e` |
| Image base | `0x400000` |

Section table, which is all you need to convert between virtual addresses and
file offsets:

| Section | VA | File offset | Size |
|---|---|---|---|
| `.text` | `0x401000` | `0x400` | `0xFA92A` |
| `.rdata` | `0x4FC000` | `0xFAE00` | `0x468C` |
| `.data` | `0x501000` | `0xFF600` | `0x10A00` |
| `.tls` | `0x52C000` | `0x110000` | `0x14` |
| `.rsrc` | `0x52D000` | `0x110200` | `0xA58` |

Method that worked, in case this needs redoing: disassemble `.text` in full to a
flat listing, extract the string table with file offsets, then pivot on strings.
The FBI/TDF key names are all present as literals, so finding where a key is
compared gives you the field's offset in the unit definition struct, and from
there `mov` sites against that offset give you every routine that reads it.
Scanning `.data`/`.rdata` for the little-endian encoding of a code address finds
call sites and jump tables. The helper scripts used for this (VA↔offset
conversion, string reads at an address, pointer-table dumps) are in `tools/exe/`.

---

## 1. Aircraft flight model

The most thoroughly verified area, because RWE's aircraft were visibly wrong in
three separate ways and each turned out to have a different cause.

### The per-tick air movement, `0x43D290`

Seven steps, in this order. The order matters: bank is fed the *total* change in
velocity for the tick, after everything else has had its say.

| Step | Address range | What it does |
|---|---|---|
| Drag | `0x43D2FB`–`0x43D38B` | `vel *= 1 − Acceleration/MaxVelocity` |
| Brake | `0x43D38E`–`0x43D47D` | if horizontal speed > `BrakeRate`, scale it back to `BrakeRate` and re-inject the excess along the nose |
| Height | `0x43D4D0`–`0x43D502` | `vel.y = clamp(goalY − y, −r, r)`, set outright, where `r` is a quarter of the speed stored at the end of the last tick, or 1 below a speed of 4 |
| Turn | `0x43D520`–`0x43D585` | heading steps toward the desired heading, limited by `TurnRate` |
| Profile | `0x43D58B`–`0x43D5EA` | `desiredVel = toTarget × sqrt(2 × Acceleration / max(distance, 8))`, in x and z only |
| Clamp | `0x43D60A` | the horizontal change in velocity is limited to one `Acceleration` |
| Bank | `0x43D68B`–`0x43D6B3` | roll from the tick's total velocity change |

**Height is not steered.** Found 2026-09-11, and missed by the six-step
reading above until then. The profile and the clamp are flat: the distance
is the hypot of x and z (`0x43D51B`), the clamp tests the hypot of the two
horizontal deltas (`0x43D605`), and the result is added to `vel.x` and
`vel.z` alone (`0x43D653`, `0x43D65A`). Height has its own step, which
does not accelerate at all. It takes the goal altitude less the unit's
height and writes it straight into `vel.y`, clamped to ±`r`. Here `r` is
`[mover+0x20] >> 2`, a quarter of the `|vel|` that `0x43D688` stores at
the end of each tick, and a flat 1.0 when that speed is under 4
(`0x43D4D8`). The step is skipped only for a unit in the off-map bucket
(`unit+0x82 == [gs+0x142B7]`, `0x43D4C8`). An aircraft therefore closes
on its altitude as fast as that limit allows and stops on it, and it
cannot overshoot. RWE had height inside the profile, sharing the one
`Acceleration` with the turn. An Atlas (0.04) that took its cargo while
still coming down at a unit a tick, then turned for home, sank fifty
units under the ground and the sea before it climbed back.

**There is no throttle/brake mode switch.** This is the single most important
finding in this section. RWE had a two-branch law — full acceleration toward
`MaxVelocity` while outside the braking distance, full deceleration inside it —
which meant the step was pinned at exactly one `Acceleration` on every tick of
every trip, and the two branches chattered against each other whenever an
aircraft sat near its destination. The original just steers continuously toward
a speed it can still shed, and the step goes slack by itself on the profile.

**The `8` is a floor on the range, not a cap.** `fcom` against `8.0f` (stored at
`0x4FD2E0`) at `0x43D58B`, then `test ah,1; je`, keeps the real distance when it
is ≥ 8 and substitutes 8 below it. So speed rises as `sqrt(2·a·d)` with distance,
and the last eight units become a straight linear run-down to a stop rather than
a curve demanding ever harder braking. Reading it as a cap instead gives every
aircraft a top speed of about one world unit per tick, which is obviously wrong
the moment you try it.

**`MaxVelocity` is not enforced anywhere.** The drag term *is* the speed limit:
equilibrium sits where the acceleration added each tick balances the speed drag
removes, which works out to exactly `MaxVelocity`. No clamp is needed and none
exists.

### Bank, `0x43D0D0`

Honest coordinated-turn physics, not an animation:

```
43d0d9   accum.{x,y,z} = accum * 0xF333 >> 16     ; one-pole lag, 0.9499817
43d13e   accum += deltaVelocity                    ; the tick's total change
43d16a   mov cx, WORD PTR [esi+0x66]               ; the unit's HEADING
43d173   call 0x4B7173                             ; Rotate2D(heading, {accum.x, accum.z})
43d1a0   mov ecx, [esp+0x8]                        ; tmp[0], the lateral component
43d1ad   neg ecx
43d1af   mov eax, [edx+0x1A2]                      ; BankScale
43d1c2   call 0x4B715A                             ; atan2(lateral × BankScale, G)
43d1ca   mov WORD PTR [esi+0x64], ax               ; store roll
```

`0x4B7173` computes `v0' = v0·cos − v1·sin`, an ordinary 2D rotation.

Two things fall out of the constants. `G` at `0x43D186` is **gravity × 20**, and
the one-pole lag's steady-state gain is `1/(1 − 0.95) = 20` — so the two twenties
cancel and the whole thing reduces to `roll = atan(lateral_acceleration ×
BankScale / gravity)`. The lag is therefore purely a smoother: it gives the ramp
on entering a turn and the wash-out on leaving it, with no rate limit or clamp
anywhere. Gravity is 112 world units per second squared on nearly every shipped
map, i.e. `112/900` per tick squared.

**The lateral component is taken against the nose, not the direction of travel.**
`[esi+0x66]` is unambiguous. This matters because it looks wrong for a
construction aircraft, which points at its job while dashing sideways to the next
station — all of its forward acceleration lands in the lateral term. That is
genuinely what the original does; the reason it does not shake is that its
accelerations are small, not that it uses a different frame. Resolving against
the velocity direction instead was tried and measured *worse* (25.4° peak, 18
sign reversals per hop), because the velocity change flips relative to the flight
path too and the heading lags behind it.

### The construction aircraft build pattern

The aircraft holds station one `BuildDistance` from the job, on a ring, stepping
a seventh of a turn around it and holding each station, with the nose locked on
the job rather than on the flight path.

### Deliberately not ported

- ~~**The `BrakeRate` nose re-aim** (`0x43D38E`–`0x43D47D`)~~: ported 2026-09-11
  (`applyBrakeRateNoseReaim`, in the flying state's velocity update, after drag
  and before the profile as in the original). It is why the original's aircraft
  barely crab: speed above `BrakeRate` is stripped and re-injected along the
  nose, so they fly roughly where they point. It had been left out because it
  moved the construction-aircraft bank peak by 0.00° (a construction aircraft
  never reaches its `BrakeRate` of 1.5) and nothing faster had been measured.
  Measured now with the shipped fighters' numbers: the worst crab angle through
  a ninety-degree turn drops to a few degrees (`brakerate.test.cpp`). Applied
  in every air state since B4 #41: `Mover::Update` (`0x43DD20`, §87) hands any
  `canfly` unit to `0x43D290` whatever its mission, so the gunship ring and the
  dogfight, which already flew through the flying state's function, had it
  from the first port, and the attack run, which steers its own heading, now
  runs the brake step before that swing.
- **Pitch.** The original also pitches aircraft from the longitudinal component
  of the same accumulator, via `PitchScale` (`def+0x1A6`) into `unit+0x68`. RWE
  has no pitch for units at all and the renderer applies only yaw and roll.

---

## 2. Fog of war and line of sight

### Storage

The grid is **32 world units per cell**, two heightmap samples across. Three
visible states come out of two arrays:

- a **global explored array** of `uint16` used as a bitmask, one bit per LOS
  group, set-only and never cleared;
- a **per-player visible array**, one byte per cell, used as a **reference
  count** of how many of that player's units currently see it.

`UpdateUnitLOS` at `0x4827B0` runs per unit per tick but is **incremental**: it
early-outs unless the unit changed cell — or, in terrain mode, moved its eye by
more than 5 — then subtracts its old stamp and adds the new one.

### The three sight modes

Sight range comes from FBI `SightDistance` at `unitdef+0x202`. The skirmish GUI
exposes three modes, **Permanent / True / Circular**, defaulting to **True**.

**Circular** does not ray-trace at all. It blits a hand-drawn mask sprite from
`anims/vismasks.gaf`, ten frames of radius 5 to 14 cells, so effective sight is
`clamp(SightDistance/32, 5, 14)` cells and saturates at 448 world units.

**True** walks a fixed fan of authored rays read from `gamedata/los.tdf`
(`TABLE1`..`TABLE9`, each a list of stepped `dx,dy` runs) with a running
maximum-slope horizon — shadow casting, not a per-cell retrace. Its radius is
capped at **8 cells / 256 world units**, because the table index is
`min(sight/32, numtables−1)` with `numtables = 9`.

**The quadrant replication is 90° rotations, not sign flips.** Measured on the
real `los.tdf` at radius 8: rotations cover the disc exactly, while sign flips
leave ten holes along the east–west axis. The tables are authored to tile that
way — each fan's first ray runs up the +y axis and its last stops one cell short
of +x.

### Terrain occlusion

A separate grid holds **two heights per cell**:

- `max(seaLevel, (2·max + min) / 3)` decides whether a cell is **revealed**;
- `max(seaLevel, (max + 2·min) / 3)` decides whether it **occludes**.

So it deliberately errs toward revealing. The eye is `clamp(unit Y, ≥ seaLevel+1)`
plus the model's own height — the maximum vertex Y of its 3DO, not an FBI field.
The horizon is only raised on cells the ray actually revealed, so once a ray is
blocked the horizon stops rising. Water is flat ground at its surface.

### Radar

**Radar has no grid at all.** It is a per-tick unit-versus-unit range query.
Terrain never blocks it, and it flags units rather than revealing ground. Its
reach has two halves and only one of them is the obvious one:

```
R = min(RadarDistance + 2 × floor(detector's own y), max(RadarDistance, SonarDistance))
```

The lift is the *detector's* altitude, not the contact's, and the cap is what
the sweep will even offer the test (`0x4675A3` feeding `0x47E9E4`). **No shipped
unit has more sonar than radar while having any radar at all**, so on the real
data the cap always binds and radar reaches exactly `RadarDistance` — which is
what the minimap ring draws. The two agree by arithmetic rather than by design;
a mod shipping a sonar-heavy dish would find the ring under-drawn.

Sonar is flat `SonarDistance`, and a jammer's radius is flat too: the jam
context is a single dword, so a jammer has no per-contact test at all beyond
the sweep's own.

Getting this wrong is visible from the minimap. RWE had the lift without the
cap, which put a Peeper's detection at about 1060 against a 700 ring.

### Drawing

**Marching squares over cell corners**, not per-cell blocks. An overlay grid
records which of the four cells surrounding a corner are dark, and each 32×32
screen tile picks one of 14 authored frames from `anims/fog.gaf` (`Black1..4`
and `Gray1..4`, 14 frames each). The variant is chosen as
`(cellX + cellY + scroll) & 3`, so the ragged edge is fixed in world space and
does not shimmer when the map scrolls. Fully-unexplored tiles are a solid
palette fill.

**The ragged boundary is in the artwork, not in a shader.** RWE previously
approximated it with procedural noise; it now uses the original's frames.

---

## 3. Nanolathe spray and the construction display

### The spray

- Colours are **palette entries 161–167**. Each particle **advances one entry per
  tick and wraps**, so the stream shimmers from pale to dark along its length —
  it is not one colour per particle for life.
- Speed is **4 world units per tick**. The original divides the distance by four
  to get a step count and walks the particle one step per tick; a target closer
  than one step gets no particle at all.
- Emission is a **burst of five per tick, with two bursts in flight**, so ten
  particles a tick. **Reclaiming a feature runs two emitters**, which is why a
  reclaim stream reads twice as thick as a build stream.
- The landing point is a **uniform sample from the middle three elevenths** of
  the target's bounding box on each axis — it fans across the middle of the work,
  it does not converge on a point.
- Each particle fills a **two-pixel square**, which at one world unit per pixel
  is a half-size of one.

### The build display

The original keeps an **8-bit height per pixel** — the model-space Y of the
topmost surface at that pixel, plus 50 — and remaps every pixel by comparing it
against a moving threshold. Three regions are drawn independently: **above** the
line, in a **four-unit band** on it, and **below** it. Each is drawn one of four
ways: erased, build colour A, build colour B, or the finished texture.

Which three ways depends on how far the build has got, at five thresholds on the
fraction still to build (scaled to a byte, so 255 = nothing built):

| Remaining | Above | Band | Below | Reads as |
|---|---|---|---|---|
| 236–255 | erase | A | erase | a bare line sweeping down |
| 201–235 | erase | A | erase | a second, slower sweep down |
| 116–200 | erase | B | A | a solid silhouette growing up from the base |
| 31–115 | A | B | texture | the texture following it up |
| 0–30 | texture | A | texture | one last line sweeping back down |

Only the low byte of the computed threshold reaches the remapper, which is why
two of the phases can compute a negative number and still sweep upwards.

The two colours are **triangle waves over palette entries 160–175**, running at
different rates — A comes round in about 0.97 s, B in about 0.56 s — each offset
per unit so two frames side by side are not in step.

**The wireframe is colour B**, per unit. RWE had invented a green → white →
black cycle for it.

### How the wireframe is drawn

**It is not drawn with lines.** Decoded 2026-09-11. `0x458DD0`, the
construction display, runs on the unit's cached bitmap whenever that bitmap
has a height plane and `[unit+0x104]` is non-zero. It computes the two
colours (`xor 5` at 33 steps a second for A, `xor 9` at 57 for B, both folded
over palette 160-175), picks the phase from the fraction still to build, runs
the per-pixel remap `0x458D30`, and then, in **every** phase, calls
`0x458FA0` with colour B:

```
458fe1  test byte [piece+0x28],1     ; SHOW, else skip the piece
459032  sar  16 on x, y, -z          ; each corner, integer
45904b  sy = -z - (y >> 1)           ; the cabinet projection
45904f  h  = y + 0x32 (or 0x7d)      ; the height-plane value
45908a  cmp [piece+0xc],-1           ; a selection plate: start at primitive 1
459128  call 0x4C0820                ; each primitive, loop closed, colour B
```

`0x4C0820` is a polygon scan converter. It finds the first corner with the
least y and the first with the greatest, walks the chain of corners
**backwards** from the top one to the bottom one to get each row's left end
and **forwards** to get its right end, both in 16.16 with `+0xFFFF` added to
the starting x so each row's end is rounded up, over rows from the top corner
down to and not including the bottom one. It does not fill the row. It calls
`0x4C0A90` for each row where the right end lies strictly to the right of the
left, and that routine writes **two pixels**, the left end and the right end,
each only if the height plane there is not higher than the edge
(`cmp [plane],h / ja skip`), updating the plane where it writes. With no
height plane it writes both unconditionally.

So the look follows from the scan conversion rather than from any line
drawing:

- **A steep edge is a solid line one pixel wide**, one pixel a row.
- **A shallow edge is a dotted line**: still one pixel a row, so the dots
  are as far apart as the edge runs across in a row.
- **A horizontal edge is not drawn at all.** The top and bottom of a wall
  standing on the ground, which is why the footprint never shows.
- **A polygon facing away draws nothing.** Its winding on screen is the other
  way round, so its "right" chain lies to the left of its "left" one and
  every row fails the test. There is no normal and no culling step; the
  winding does it.
- **A row with no pixel between its ends draws nothing either**, so a sliver
  narrower than a pixel vanishes.
- **The edge between two polygons is drawn by both**, the first polygon's
  right end landing in the same column as the second's left end. That
  includes two polygons lying in one plane, if the edge between them is
  steep. (RWE outlined a flat face only once for a few hours on 2026-09-11,
  going by how the original looked in play; the dotted and missing shallow
  edges were the difference being seen.)
- **Only the model's own surfaces hide it.** The height test is against the
  unit's own bitmap, and keeps a pixel only where no higher surface of the
  model covers it.

RWE runs the same scan conversion (`scanWireframePolygon`,
`src/rwe/render/WireframeScan.cpp`) on each polygon's corners projected to
output pixels, with pixel centres standing in for the original's integer
corners, and draws each pixel it keeps as a one-pixel quad at the depth of
the edge it came from. The depth buffer, with the quad lifted slightly
towards the camera, stands in for the height test. The selection plate is
left out rather than primitive 0, as it is from the model (§58, the 3DO
selection-plate row).

### The nanoframe's shadow

**What the original decides, decoded 2026-09-11.** The shadow is drawn by the
unit draw routine `0x459200`, every frame, before the unit's own image, and
the construction display is applied afterwards to a scratch copy of the
unit's cached bitmap (`0x4589C0` → `0x458DD0`), never to the bitmap the
shadow is taken from. The shadow pass tests, in order:

- the master shadow option, bit 2 of the options word `[globals+0x37F06]`;
- `noshadow`, bit 25 of `def+0x241`;
- `digger`, bit 30: a copied shadow cut at height 125 (`0x4BA1B0`), for
  ARMAMB and CORTOAST only;
- **`unit+0x110` bit 29, which is `bmcode == 0`** — set at unit creation,
  `0x485A8B`-`0x485A9E`, from `def+0x22F`, and on the "Feature Unit" that
  draws map features (`0x421FD3`) — so it is "is a building or a feature".
  That takes the **projected** shadow: built once by `0x45A790` from the
  pieces that are both shown and cached, each corner at `x + y/4`,
  `-z - y/4`, and cached on the unit until its bitmap is re-rendered. A
  building below the water line casts none unless `[unit+0xA6]` is set;
- anything else — every mobile unit — takes the **copied** shadow if the
  second option bit (bit 3) is set and it neither hovers nor floats
  (`def+0x241 & 0x81000`, canhover and floater: so hovercraft and ships have
  no shadow). The cached bitmap is copied (`0x45A470`), every opaque pixel
  becomes index 0 (`0x4B96A0`), anything below the water line is cut away
  (`0x4BA1B0`), and it goes down five pixels right at ground level (§100).

**There is no build-progress test anywhere in it.** A nanoframe's cached
bitmap is the whole model — the display's erasing happens on the copy — and
a building's projected shadow is built from every shown, cached piece, which
is all of them from the first frame. So by the code a frame's own shadow is
there from the start, and the erased parts of the frame, which are written
transparent (`0x458DA8`), would let it show through.

**In play the shadow is there, but not inside the frame.** Watched on
2026-09-11 and corrected the same day: a nanoframe casts its whole shadow
from its first frame, as the code says, but none of it shows inside the
frame's own outline. The frame is treated as solid for its own shadow the
whole time, while other units' shadows do show through it. RWE matches that
(`RenderService::drawUnitShadowMeshBatch`): each nanoframe's shadow is drawn
after every other shadow, and only outside the whole model's outline. An
earlier version cut every nanoframe's outline out of the whole stencil,
which hid other units' shadows behind it too. The version after it held
the frame's shadow back until the green layer had finished, on a first
reading of the same observation.

**The three leads, followed (B4 #40, 2026-09-11).** None of them is it.

1. *Is the shadow built from a drawable that is mostly transparent key?*
   No. The projected shadow `0x45A790` never reads the bitmap. Its extent
   `0x45A510` walks the 54-byte piece records from `[obj+0x22]`, keeps a
   piece on `[piece+0x28]` bit 0 (SHOW) and a positive vertex count at
   `[[piece]+0x4]`, and takes the cached transformed corners from
   `[piece+0x22]`. It tests no CACHE bit and nothing about construction.
2. *Is the cache at `[obj+0x14]` left empty during construction?* No. It is
   filled on demand at `0x4592FE` and `0x45955B` whenever it is null, and
   cleared only when the bitmap is rebuilt (`0x458905`).
3. *Does anything clear a nanoframe piece's SHOW bit?* No. The only writers
   of the piece flag byte are `0x45AF21` (SHOW on, when the piece has three
   or more vertices) and `0x45AF27`/`0x45AF31`, in the piece set-up.

**What the cached bitmap does carry is a height plane.** The cache renderer
`0x4586A0` renders a clean, finished unit (`[unit+0x114]` bit 0 clear and
`float unit+0x104 == 0`) through `0x437B50`, which allocates `w*h + 0x18`
bytes, and everything else through `0x437BE0`, which allocates `2*w*h +
0x18`: pixels and a height plane. The construction display needs that plane
(`0x458DDA` returns at once without one). And the draw routine tests it
first: at `0x459271`-`0x459282`, a bitmap with a plane goes to the shadow
block at `0x45949D`, one without to the block at `0x459288`. **Both blocks
draw the shadow.** They are the same sequence of gates -- option bit 2,
`noshadow`, the building bit with its water test, the projected shadow, else
the copied shadow under option bit 3 and the hover/floater exclusion -- and
differ only in `digger`: with a plane a digger's copy is cut at height
`0x32 + 0x4B` (`0x4594D8`-`0x459503`), without one it takes the plain copy.
So the plane is not a gate either; it only changes how a digger is cut.

**So the code and the look agree that the shadow is drawn. What is still
open is what keeps it out of the frame's outline**, and the listing has
not settled it. The likeliest place is the blit. Both shadows go through
`0x4B8500` at `x + 0x85`, before the unit image, and the image is the
display's scratch copy with its erased pixels written as the transparent
key (`0x458DA8`). If the frame's own shadow is masked by the whole cached
bitmap, which is the finished model, or the blitter treats the shadow's
single index against the frame's key, the shadow would be hidden exactly
within the model's outline and nowhere else. Other units' shadows would be
untouched, which is what play shows.

RWE does not wait on that: it cuts the frame's own shadow by the model's
outline, which is the look. The mobile case the issue asked about goes the
same way. A unit under construction on a factory pad has a bitmap with a
plane, takes the `0x45949D` block, and gets the copied shadow like any
other mobile unit. The copy is of the cached bitmap, not of the display's
scratch copy, so its shadow is the whole silhouette from the first frame,
and RWE cuts it by the outline in the same way.

---

## 4. Effects a script asks for: `emit-sfx`

`TAObjScr::EmitSfx` transforms the emitter piece and then dispatches on the
type: 0–5 through the jump table at `0x481128`, and 257–259 by three compares at
`0x480FDE`–`0x4810D2`. Every handler allocates one emitter object, calls its
`Init`, and files it in one of the ten particle layers of §5 — the layer is a
literal at the call site, not a property of the effect.

| Type | Name | Handler | Layer | Sequence |
|---|---|---|---|---|
| 0 | `SFXTYPE_VTOL` | `0x472330` | 7 | `flamestream` |
| 1 | thrust | `0x472330` | 7 | `flamestream` |
| 2–5 | the four wakes | `0x472430` | 2 | `smoke 1`, palette 97–103 |
| 257 | `SFXTYPE_WHITESMOKE` | `0x472810` | 9 | `smoke 1` |
| 258 | `SFXTYPE_BLACKSMOKE` | `0x4728F0` | 9 | `smoke 2` |
| 259 | `SFXTYPE_SUBBUBBLES` | `0x472530` | 7 | — |

The sequences all come out of `anims/FX.GAF`, loaded once at `0x429870` into a
run of slots on the globals block: `+0x147CF` `smoke 1`, `+0x147D3` `smoke 2`,
`+0x147D7` `fire1`, and on through `+0x147F3` `flamestream`.

**That slot map is direct, and an earlier reading of this document that said the
loader stores each handle one slot late was wrong.** The store *looks* misplaced
because the compiler schedules it between the *next* call's argument pushes —
`0x4298AC` pushes `"smoke 2"` and only then does `0x4298B2` write `eax`, which
still holds the result of the `"smoke 1"` call at `0x4298A1`. Reading the two
uses settles it independently: the wake emitter at `0x474A7F` loads `+0x147CF`
and immediately loads `0x61` and `0x67` — palette 97–103, pale blue to deep
blue, which is water foam and so has to be `smoke 1`; the Atlas exhaust at
`0x474526` loads `+0x147F3`, which is `flamestream`. Both identifications below
stand; only the explanation of why did not.

### `SFXTYPE_VTOL` — the Atlas exhaust

`TAObjScr::EmitSfx`'s jump table sends type 0 to a handler that passes the
emitter piece's two vertices to a particle playing the **`flamestream`** sequence
out of `anims/FX.GAF`: one frame per tick, drift `(secondVertex − firstVertex)/6`
per tick, six-tick life.

`flamestream`'s frames run from 2×2 up to about 19×13 — solid yellow blobs with
ragged pixel-noise edges and no white core — so the plume is a graded line whose
oldest and largest puffs are furthest from the nozzle. That is the "blobs of
yellow that get larger as they descend" the effect is recognised by. **It is a
sprite, not coloured dots.** Neither Spring nor TA3D found this: Spring draws a
generic heat cloud, TA3D a fire texture with no growth at all.

In the game data, the Atlas and Valkyrie are the only OTA units that emit
`SFXTYPE_VTOL`, from three (four) thruster pieces at `sleep 67`.

### Smoke from a damaged unit

**Nothing in the engine decides when a unit smokes.** There is no health
threshold in the per-tick unit update, and there is no code path from a unit's
hit points to a smoke emitter at all: every call site of every spawner in the
particle manager is accounted for below, and none of them is in a unit update.
The whole trigger is the unit's own `SmokeUnit` script, which `Create` launches
as a thread and which then loops for the life of the unit.

Straight out of `ARMSOLAR.COB`, and identical word for word in 186 of the 200
scripts shipped in `rev31`:

```
SmokeUnit(healthpercent, sleeptime, smoketype)
{
    while (get BUILD_PERCENT_LEFT) sleep 400;
    while (TRUE)
    {
        healthpercent = get HEALTH;
        if (healthpercent < 66)
        {
            smoketype = 256 | 2;                              // black
            if (Rand(1, 66) < healthpercent) smoketype = 256 | 1;   // white
            emit-sfx smoketype from base;
        }
        sleeptime = healthpercent * 50;
        if (sleeptime < 200) sleeptime = 200;
        sleep sleeptime;
    }
}
```

So, answering the questions in the shape they are usually asked:

- **The threshold is 66% and there is only one of it.** Above it a unit does not
  smoke at all.
- **The rate scales with the damage**: one puff every `max(health% × 50, 200)`
  milliseconds, so 3.25 s just under the threshold and a floor of 200 ms —
  every sixth tick — from 4% health down.
- **Light and heavy are mixed, not staged.** Every puff rolls `Rand(1, 66)` and
  comes out white when the roll is below the health percentage, so a unit just
  under the threshold is nearly all white and one about to die is nearly all
  black, with a mix in between. `Rand` never returns below its low bound, so at
  1% health the smoke is black without exception.
- **It comes off one piece**, the one named in the `emit-sfx` — `base` on a
  building, `torso` on a Peewee. The engine adds no offset and no spread: the
  puff starts at the piece's origin exactly.

The engine's half is the emitter class at vtable `0x4FD618`, shared by every
`smoke 1`/`smoke 2` effect in the game:

| | |
|---|---|
| constructor | `0x474CD0` |
| `Init` | `0x474D50` |
| emit one puff | `0x474DF0` |
| per-tick update | `0x475340` |
| is it due to emit | `0x475440` |
| is it finished | `0x474F80` |

`Init(position, frameCap, interval, framePeriod, lifetime, useSmoke2)`. A
whitesmoke gets `(piece, 0, 1, 0, 0, 0)` and a blacksmoke the same with the last
argument 1; a zero `framePeriod` defaults to 7 at `0x474DD5`. **A zero lifetime
means one puff and no more**: `0x471D70` sets the emitter's end time to `now`,
`Init` emits once directly at `0x474DCA`, and that emit pushes the next emission
to `now + 1`, which `0x475440` then refuses for being past the end. So one
`emit-sfx` is one puff, and the script's sleep is the entire emission rate.

A puff is a 32-byte record — sequence, position, stopping frame, current frame,
frame period, countdown — and `0x475340` steps every one of them each tick:

- `x += windX × 8`, `z += windZ × 8`, `y += gravity × 4`. The wind is the same
  two words at `globals+0x37ECC` the ballistic projectiles use (§7); gravity is
  the one at `globals+0x14263`, which at the 112 nearly every map ships with
  works out at 0.498 world units of lift per tick.
- The countdown starts at the frame period, 7. When it runs out the frame
  advances and the countdown is reloaded with `half + rand·half/0x8000` where
  `half` is the period halved — **3 to 5 ticks per frame after the first, which
  gets the full 7**.
- **A puff stops on a frame drawn when it is born**, `2 + rand·(frameCount −
  3)/0x8000`, and is deleted when the current frame reaches it (`0x4753D3`).
  `smoke 1` has twelve frames and `smoke 2` sixteen, so a puff shows between two
  and ten, or two and fourteen, of them. Most therefore die while they are still
  small blobs and the last frame of a sequence is never reached at all. That
  spread, not the drift, is what stops a smoking unit reading as a column of
  identical clouds.

The same class does the rest of the game's smoke, so the numbers are worth
having in one place:

| Effect | Spawner | Called from | `interval` | `lifetime` | Layer |
|---|---|---|---|---|---|
| damage smoke | `0x472810` / `0x4728F0` | `EmitSfx` 257 / 258 | 1 | 0 (one puff) | 9 |
| explosion smoke | `0x472630` | `0x420AE1` | 7 | 15 | 9 |
| burning wreck | `0x472630` | `0x48644B` | 15 | 900 (30 s) | 9 |
| weapon smoke trail | `0x4729D0` | `0x49CC07`, `0x49CDC3`, `0x49CFE6` | 1 | 0 | 9 |

Two details from that table. The explosion and wreck emitters check the world
`y` of the point against the sea level byte at `globals+0x1427F` first
(`0x420AD4`, `0x4863E7`) and **emit nothing underwater**. And the weapon smoke
trail passes a `frameCap` of 3 and a `framePeriod` of 30, which pins its
stopping frame at exactly 2 — a trail puff is two frames of `smoke 1` held for
30 then 15–29 ticks, far slower and far smaller than a damage puff.

There is a fifth spawner, `0x472720`, identical to `0x472630` but asking for
`smoke 2`. Nothing references it; it is dead in this build.

---

## 5. Render order

**There is no depth buffer at the world level at all.** TA is a pure 8-bit
software painter: terrain, units, features, shadows and particles all go through
the same three blit routines in call order, and the last one wins.

The world render function is `0x468CF0`. Objects are bucketed by **world Z
alone, quantised to 16 units** (`0x4697ED`), ignoring height entirely even though
the projection is `screenY = z − y/2`. Within a bucket there is no secondary
sort. `sortbias` is parsed from the FBI into `unitdef+0x21A` but read nowhere —
dead in this build.

**Particles are not in that list.** They live in a separate manager with **ten
hard-coded layer buckets**, and each SFX type is hand-placed in one: wakes on 2,
weapon impacts on 6, **VTOL and thrust exhaust on 7**, damage smoke on 9 (over
everything, including health bars). So particles are not uniformly in front of or
behind the world; each effect is placed by hand.

**Why the Atlas exhaust sits under the aircraft**: explicit layering, not depth
and not the spawn position. Layer 7 is drawn at `0x469B38`, and the very next
pass at `0x469B3D` draws every object whose `occupy != GROUND` — airborne units
get their own late pass, so the aircraft is blitted over its own exhaust a few
instructions later. Had it been on the ground, the exhaust would have landed on
top of it.

RWE reaches the same result by a different route, depth-testing against world Y,
which is what its camera uses for depth. The two agree for an airborne aircraft
and differ only in that RWE will also let a tall ground unit in front occlude the
plume, which TA would not. Judged the better behaviour for a renderer that has a
real depth buffer, so kept.

---

## 6. The damage pipeline

Every point of damage in the game passes through the same four stages. Nothing
between the weapon's `[DAMAGE]` table and the victim's hit points is a straight
multiply.

### Direct hit or blast, `0x499FA0`

A weapon with `areaofeffect <= 16` (`0x49A049`) hits one unit only and does so at
full strength — `0x49A05B` pushes a literal `1.0f` scale. Anything wider goes to
the area routine at `0x49A120`. Ordinary contact damage takes the same one-target
path through `0x499C70`, again at `1.0f`.

### Blast falloff, `0x49A120`

The blast radius is `areaofeffect / 2` (`0x49A149`–`0x49A152`), not
`areaofeffect`. Candidates come from a scan of map squares within that radius,
and for each one the distance is measured from the impact point to the unit's
**axis-aligned bounding box** — zero if the point is inside it
(`0x49A2AA`–`0x49A35A`, three per-axis clamps). Positions are 16.16 fixed point,
so the integer world distance is the high half, which is what `0x49A397` reads.

Beyond that:

```
49a39e  cmp eax,ecx                     ; distance >= radius -> no damage at all
49a3aa  mov edx,[ecx+0xd8]              ; edgeeffectiveness
49a3a8  test eax,eax ; 49a3e6 -> 1.0f   ; distance 0 -> full damage
49a3b6..49a3e0                          ; (d/R - 1)^2 * (1 - E) + E
```

The curve is **quadratic**, and `edgeeffectiveness` is the multiplier reached
*at* the rim rather than a slope. The weapon-TDF parse at `0x42E5A9` supplies a
default of `0.0` into float `wdef+0xD8`, so an ordinary weapon falls to nothing —
but it falls off far faster than a straight line on the way there, reaching a
quarter of its damage at the half-way point rather than a half. Seventeen
weapons override it, and they are the big-area ones: flak and Sentinel at 0.90,
the nuke and the Krogoth's tremor at 0.25, Commander self-destruct at 0.75.

### Attacker's veterancy, then the global speed flags, `0x499CD0`

The `[DAMAGE]` block is a sorted name→value table looked up by the victim's
armour class (`0x499D0C`–`0x499D6F`), multiplied by the blast scale and
truncated (`0x499D73`–`0x499D7B`). Then, if the shot has a known firer:

```
499db5  mov cx,[ebx+0xb8]               ; the ATTACKER's kill count
499dc7  cmp edx,5                       ; tier = min(5, kills/5)
499dd9  lea ecx,[0x64+ecx*2]            ; 100 + 6*tier
499de0..499dea                          ; * damage / 100
```

so a veteran gains up to **+30% damage dealt** at 25 kills.

### Armour and the victim's veterancy, `0x489BB0`

The single choke point every damage source funnels into. It takes
`(attacker, victim, damage, type, direction)`; `type == 10` is a repair and skips
straight past both adjustments to add hit points instead (`0x489BBD`,
`0x489D5F`). Otherwise:

```
489bc3  mov al,[esi+0x10e] ; test al,2  ; victim ARMORED?
489bd1  cmp edi,0x7530                  ; damage >= 30000 -> armour ignored
489be4  mov eax,[eax+0x1aa]             ; DamageModifier, 16.16
489bea  imul edi ; 489bec call 0x4e43d0 ; damage * modifier >> 16
489bfa  mov cx,[esi+0xb8]               ; the VICTIM's kill count
489c0c  cmp edx,5                       ; tier = min(5, kills/5)
489c20  sub ecx,edx ; 489c25 shl ecx,2  ; (25 - tier) * damage * 4
489c28..489c2d                          ; / 100  ==  100 - 4*tier percent
```

so a veteran also takes up to **−20% damage** at 25 kills, and the two effects
partly cancel when veterans fight each other.

`DamageModifier` is parsed at `0x42C1D5` into 16.16 at `def+0x1AA` with a default
of `0x10000`; nine units ship one, between 0.15 and 0.5. The ARMORED bit is
`unit+0x10E` bit 1 and is set only by the script: COB `SET ARMORED` is value id
20, whose handler `0x480BE3` calls the flag setter with mask `0x2`. So armour is
a state a unit enters, which is why a closed solar collector is tough and an open
one is not. The `0x7530` cut-out is why the D-gun ignores it.

`unit+0xB8` really is a kill count: it is zeroed at unit creation (`0x485C76`)
and incremented on the killer at `0x4869CA`, reached through the victim's
last-damager pointer at `unit+0xF0` which `0x489DBA` records. Note the original
**declines to credit a kill when the killer and the victim share a player**
(`0x4869BA`–`0x4869C8`); RWE currently credits friendly fire.

Besides damage, more than five kills also earns a unit target leading
(`0x48A324`), which RWE does not implement.

### `GET VETERAN_LEVEL` does not work in the original

The COB `GET` opcode (`0x4B160F`) hands the value id straight to the dispatcher
at `0x480770`, which handles ids 1 to 20 and returns zero for everything else.
Value 32 is not in that range, so a script asking for `VETERAN_LEVEL` in this
build always reads back 0 — as do all the ids from 21 up.

## 7. Missile and projectile flight

Every projectile in TA is one of five kinds, chosen by a flag on the weapon
definition. The per-tick update is `0x49B720` and it dispatches on them in this
order, from `0x49B9AE`:
`selfprop` (bit 20), `lineofsight` (0), `ballistic` (1), `dropped` (8),
`meteor` (5). The order matters because almost every missile also carries
`lineofsight=1`; the motor wins. The launch side dispatches separately at
`0x49D42E`, and there `vlaunch` (bit 4) is tested before `selfprop`, which is
how the same missile gets a different launch and the same flight.

### The motor, `0x49BA16`

Three speeds, all off the weapon definition, all converted as the TDF is parsed:

| Key | Offset | Conversion | Meaning |
|---|---|---|---|
| `weaponvelocity` | `wdef+0x68` | `×65536/30` | a **ceiling** the motor works up to |
| `startvelocity` | `wdef+0x6C` | `×65536/30` | the speed it leaves the tube at |
| `weaponacceleration` | `wdef+0x70` | `×65536/900` | added to the speed every tick |

```
49ba16  mov eax,[ebp+0x3a]      ; the projectile's speed
49ba19  mov ecx,[esi+0x68]      ; weaponvelocity
49ba1e  cmp eax,ecx ; jae ...   ; already at the cap, leave it
49ba22  add eax,[esi+0x70]      ; speed += weaponacceleration
49ba2d  cmp eax,ecx ; jbe ...
49ba31  mov [ebp+0x3a],ecx      ; clamp
```

The launch speed is picked at `0x49C980`: `startvelocity` if it is set,
otherwise `weaponvelocity` if there is no acceleration to build up with,
otherwise **zero**. So `startvelocity` is the launch speed and
`weaponvelocity` is the cap — never the other way round, and a missile with an
acceleration and no `startvelocity` starts from a standstill.

Velocity is rebuilt from the missile's own attitude every tick rather than
being steered as a vector (`0x49BA74`): `vy = sin(pitch)·speed`, and the
horizontal `cos(pitch)·speed` is split by heading. A missile therefore flies
exactly where its nose points.

### How long the motor burns, `0x49C920`

```
if (weaponvelocity != 0 && !noautorange)
    life = (range << 16) / weaponvelocity     ; ticks to fly `range` at the cap
else
    life = weapontimer                        ; seconds × 30
```

`noautorange` is bit 27 of the flag word. **Running out is not death.** Unless
the weapon has `burnblow`, the missile keeps its velocity, takes gravity from
then on and coasts (`0x49BAC3`) — which is why a rocket kbot firing at the very
edge of its range arrives unguided, and why missiles that miss arc into the
ground instead of vanishing. `burnblow` detonates instead, which is what makes
a torpedo go off at the end of its run.

Note the consequence for an accelerating missile: `ARMKBOT_MISSILE` gets
`604 / (650/30)` = 27 ticks of motor, and covers only about 447 units of its
604 range under power before it starts coasting.

### Guidance, `0x49B520`

Steering is gated on **`guidance` (bit 12), not `tracks`** (bit 13). The
routine takes the vector from the projectile to the aim point, works out the
heading it wants with `atan2(dx, dz)` and the pitch it wants with
`atan2(-dy, hypot(dx, dz))` — both in whole world units — and steps the
projectile's stored heading and pitch toward those, each by at most
`turnrate`, independently.

`turnrate` is a weapon TDF key, `wdef+0xE8`, parsed at `0x42E609` with a
`×1/30` — so the TDF number is a 16-bit angle **per second** and the missile
turns a thirtieth of it per tick. `ARMKBOT_MISSILE`'s 33000 is 1100 a tick, or
about 6°. Because heading and pitch are limited separately, a missile can turn
up to `turnrate` in each on the same tick.

If the wanted heading or pitch is more than **27000** of a turn away (a little
under 150°) and the weapon has `burnblow`, the routine returns failure and the
caller detonates the missile where it is.

`tracks` decides only whether the missile keeps chasing a *unit*: a two-phase
missile without it forgets its target on turnover and finishes at the place it
was aimed (`0x49BB34`).

### The aim point, `0x49B3E0`

In order: a **cruise** missile more than 1024 world units from where it was
aimed steers at that point held at **Y = 700** (`mov WORD PTR [esi+0x16],0x2bc`
overwrites the high half of the aim point's Y), which is what makes a nuke fly
in flat and then come down; otherwise a target projectile if it has one (that
is the anti-nuke intercepting), then a target unit, then the fixed point it was
fired at.

### Vertical launch, `0x49CC20`

`vlaunch` gets its own spawn: heading 0, **pitch `0x4000`** — straight up — and
a **zero velocity vector**, so the entire climb comes out of the motor. Nothing
else about the flight is special; what turns it over is `twophase`.

### Two-phase, `0x49BAE1`

A `twophase` missile flies its first phase **blind**, whatever `guidance` says:
the guidance test at `0x49BA44` is replaced by a test of the phase counter in
the projectile's flag word. When the first phase's motor time runs out, the
missile takes one tick of gravity *without* rebuilding its velocity from its
attitude, then:

- the phase counter goes to 1, so guidance comes on;
- the motor time is reset to `now + flighttime`;
- if the weapon does not have `tracks`, the unit target is dropped.

**`flighttime` is `WORD wdef+0xFC`**, parsed at `0x42E6DD` with a `×30`, i.e.
seconds. (`wdef+0xFA` is `smokedelay`; the pairing is off by one slot if you
read the parser's stores in the order they appear.) So the whole profile is:
climb for `weapontimer` seconds, turn over, then burn for `flighttime` seconds.

Replaying this against the real data — a standalone transcription fed the
shipped TDF numbers, tick by tick — gives a Diplomat's `ARMTRUCK_ROCKET`
(`weaponacceleration=40`, `weapontimer=5`, `flighttime=10`, `range=800`) a
503-unit climb over 150 ticks, a peak of 596, and an arrival 11 units from a
target 800 away; and `NUCLEAR_MISSILE` a 637-unit climb, a peak of 716 — just
above its own hardcoded cruise altitude of 700, which is the cross-check that
pins the `/900` on `weaponacceleration`. At `/30` the nuke would climb 1750 and
have to dive to reach its cruise height.

### Wind

Ballistic and dropped projectiles have the map's wind vector added to their
position every tick (`0x49BD10`, the three words at `globals+0x37ECC`), on top
of gravity. Not ported; recorded because it is easy to miss.

The vector is built once at `0x490CA4`–`0x490D35`: the speed is
`minwindspeed + rand(maxwindspeed − minwindspeed)` straight out of the OTA, the
direction is `rand(0x10000)`, and the two components stored are
`−2·cos(dir)·speed` and `−2·sin(dir)·speed` in the same 16.16 units as a
position. Smoke uses the same two words, scaled by 8 per tick (§4), which is the
one place the missing wind is actually conspicuous — every puff in the original
leans downwind together. Porting it needs a map-wide wind vector RWE does not
have yet, so the smoke rises straight up for now.

---

### Decoded but not ported

- **Wind on ballistic projectiles** (`0x49BD10`) is decoded but not ported.
- The `meteor` projectile kind (`0x49BD46`, flag bit 5) adds a per-tick spin to
  the projectile's own heading and pitch from two words at `proj+0x1E` and
  `proj+0x26`, each shifted left by 8. Only `METEORS.TDF` uses it and RWE has no
  meteor showers, so it is recorded and not implemented.
- The **interceptor** target slot on a projectile (`proj+0x56`, an anti-nuke
  tracking an incoming missile) is decoded but there is no anti-nuke in RWE to
  fill it, so `getSelfPropelledAimPoint` skips that step.
- A `waterweapon` above sea level takes gravity and has its pitch forced to zero
  (`0x49B9EB`); RWE keeps torpedoes below the surface from launch instead, so the
  case does not arise.

## 8. Which way a finished building faces

Every unit is spawned through `0x485A40`, and the last thing it does before
handing back is set the heading:

```
485bd6  mov ax, WORD PTR [edx+0x210]    ; buildangle, zero-extended (eax was cleared at 485bbf)
485bde  call 0x4b6c30                   ; rand
485be9  mov dx, WORD PTR [ecx+0x210]    ; buildangle again
485bf0  mov ecx,0x8000
485bf5  shr dx,1 ; 485bf8 sub ecx,edx   ; 0x8000 - buildangle/2
485c00  add eax,ecx
485c06  mov WORD PTR [esi+0x66],ax      ; heading
```

so the heading is `0x8000 − buildangle/2 + rand(buildangle)`, taken modulo
65536 by the `WORD` store. Roll and pitch are zeroed alongside it, at `485c02`
and `485b92`.

`0x4B6C30` really is `rand(n)`, and it matters that it is: it is Park–Miller
(`a = 16807`, `m = 2^31 − 1`) evaluated by Schrage's trick, with the state at
`ds:0x51FC88`, seeded at `0x4B6CA0` by XORing with `0x66E29572` and forcing the
low bit. `485bde` returns `state % n` — uniform over `[0, n)`, exclusive of the
top. **For `n < 2` it returns 0 without advancing the state**, which is how the
fortification walls (the only two units that write `buildangle=0` out loud) come
out in a dead straight line. That early exit leaves `edx` untouched rather than
zeroed, so `485be9`'s `mov dx` merges into a stale pointer; the garbage lands
entirely in the high half and the `WORD` store discards it, leaving exactly
`0x8000`. Sloppy, but not a bug.

`buildangle` is parsed at `0x42C54E` into `WORD def+0x210`, defaulted to 0 by
the `esi` zeroed at `0x42C0F0` and shared by that whole run of key reads. It is
copied field-for-field at `0x42B6F1`, between `+0x20E` and `+0x212`, which
confirms the offset independently of the parser's store-after-the-next-push
pipeline.

**The other four readers are all the same override.** Each of the spawn
routines — `0x485E50`, `0x485E90`, the one containing `0x4860E6`, and the one
containing `0x4862FA` — calls `0x485A40`, and then:

```
485f0f  cmp BYTE PTR [edi+0x22f],0x1    ; bmcode == 1, i.e. mobile
485f16  jne  ...
485f18  push 0x2f ; call 0x4b4f10       ; 47 bytes
485f29  call 0x43dc00                   ; construct it over the unit
485f38  mov DWORD PTR [esi],eax         ; hang it off unit+0
485f3a  mov ax, WORD PTR [edx+0x210]
485f41  mov WORD PTR [esi+0x66],ax      ; heading = buildangle, verbatim
```

So a **mobile** unit throws the random heading away and takes `buildangle`
literally. It is not a nanoframe path and not a factory path: it is the mobile
branch of the ordinary spawn, and `bmcode` is what selects it (`0x485A81` uses
the same byte the other way, flagging bit 29 of `unit+0x110` when it is zero).

In the rev31 data 80 of 189 units name a `buildangle`. Seventy are buildings;
the ten mobile ones are exactly the capital ships — Millenium, Colossus,
Conqueror, Crusader, Hulk, Warlord, Hive, Executioner, Enforcer, Envoy — every
one at 16384. The building values span a factor of thirty-two: 1024 on the
vehicle plants and air repair pads, 2048, 4096 on the kbot labs and fusion
plants, 8192 on most of the towers and extractors, 16384, and 32768 on the two
light laser towers and the CORE solar collector. **The shipyards and aircraft
plants name none at all** — that, not any special case for factories, is what
keeps their roll-off square.

RWE's heading is offset half a turn from TA's: `createUnit` already gives a
mobile unit with no facing of its own `HalfTurn`, which is TA heading 0, and its
buildings sit at rotation 0, which is TA's `0x8000` centre. So the arc lands in
RWE as `rand(buildangle) − buildangle/2` about rotation zero, with no shift.

## 9. Target selection, and what the firing modes really do

### Categories are sets of unit types, not ids on a unit

There is no "category id". `0x488C50(name)` is a memoised map from a category
*name* to a freshly allocated **64-byte block — a 512-bit set of unit type
indices**, held in the sorted table at `ds:0x51E6B4 .. 0x51E6B8`. When a unit
definition's `Category` list is parsed, `0x488E70` walks the list with
`" %s %n"` and, for each name, sets **that definition's own index** in the named
category's set, plus the same bit in the set called `ALL` (`0x507C14`).

The definition's index lives in `WORD def+0x21E`, written at `0x42B2BB` as the
loop counter that walks the definition table backwards in strides of `0x249`
(585 bytes, the record size). The same number is copied onto every instance as
`WORD unit+0xA6` by `0x485E90`, which derives the definition pointer from it.
So `unit+0xA6` is the **unit type index**, not a category id — the priorities
document's entry 1 has that wrong — and the filter `mask[id>>5] & (1<<(id&31))`
is asking "is this unit's *type* in that category's set". Note the block is 512
bits: the original cannot carry more than 512 unit types.

`wpri_/wsec_/wspe_badTargetCategory` and `noChaseCategory` each hold a pointer
to one such set at `def+0x231 / +0x235 / +0x239 / +0x23D`, defaulting to the set
called `none` (`0x42C00E`–`0x42C0AB`). The per-slot indexing is confirmed by
`0x40B9FD`, which reads `[def + 4*slot + 0x231]`. Every value in the shipped
data is a single name: `VTOL`, `NOTAIR` or `NOTSUB`.

### The scan, `0x4089A0`

Called per player per tick, but it only advances its cursor `unitCount/30 + 1`
units, so **every unit is looked at about once a second**, not every tick. A
unit is considered only if it is fully built (`float unit+0x104 == 0`), has
`unit+0x110` bit 31 set, and its **firing mode is Fire At Will** —
`(unit+0x110 & 0x300000) == 0x200000`, see below. Then, per weapon slot:

- the slot's flag byte `unit+0x1F+28*slot` must have bit 1 (armed) and bit 4
  ("weapon free": no mission owns it, set and cleared by `0x489800` and
  `0x4898B0`);
- `dropped` weapons never auto-acquire, so bombs are only ever aimed by the
  bomber's own mission;
- `commandfire` weapons do not either, unless the player is of type 2;
- `interceptor` weapons get a position from `0x49D120` instead of a unit;
- an existing target is kept unless it has become allied, has left range, or is
  in that slot's bad-target set — a `paralyzer` also drops an already paralysed
  target (`weapondef+0x111` bit 7 against `unit+0x10E` bit 4);
- otherwise `0x40B7B0(unit, slot, 1)` picks a new one.

### The choice itself, `0x40B7B0`

Candidates come from `0x40AD80(playerIdx, &centre, radius, 0, &out)`, the
enemies of that player within `radius` — the **weapon's range** when the third
argument is 1 (the auto-acquire above), the unit's **`SightDistance`** when it
is 0 (`0x43B700`, the search that decides whether to go and find a fight).

Then, up to **fifty** times, it draws a candidate **at random from the pool and
removes it**, and rejects it if:

- it is dead (`unit+0x110` bit 28 clear or bit 14 set);
- it does not set `ShootMe` (`def+0x241` bit 15) — **unless** the attacking
  player's type byte `player+0x73` is 2, or the global bit `0x37F30 & 4` is set.
  The fifty-four units in the shipped data without `ShootMe` are exactly the
  passive buildings, which is why nothing ever wanders over to an enemy solar
  collector and opens fire on it, and why a computer player's units do;
- `0x49ABB0(unit, candidate, slot)` says no. That routine owns range *and*
  eligibility: a `waterweapon` needs its target at or below sea level (floaters
  exempt, hovercraft excluded by their model height), a non-water weapon needs
  both parties out of the water, a **`toairweapon` needs the target airborne**
  (`unit+0x110 & 3 == 2`), a `ballistic` weapon needs an arc that reaches, and
  finally `dx² + dz² ≤ range²` in whole world units. Kamikaze units skip it
  entirely;
- `noChaseCategory` names its type — **only when the third argument is 0**, so
  it governs going to look for a fight, not shooting what is already in range;
- a `paralyzer` weapon is offered an already paralysed unit.

Each survivor is scored as **`rand(dx² + dz²)`** (`0x40B9D8`, on distances in
whole world units) and the **lowest score wins**. So the nearest target usually
wins but not always, which is what spreads a group's fire instead of putting
every gun on the same unlucky Peewee. Two winners are tracked, one among
candidates that are *not* in the slot's bad-target set and one among those that
are, and the bad one is returned **only if there is no other** (`0x40BA4D`). A
bad target category is therefore a preference, not a veto: a Samson with no
aircraft about still shoots at tanks.

### Firing modes, and Return Fire

The firing mode is **bits 20–21 of `unit+0x110`**, written by the
`Standing_FireOrder` mission handler at `0x403100` from `mission+0x36 & 3`:
0 Hold Fire, 1 Return Fire, 2 Fire At Will. Setting 0 or 1 **clears all three
weapons' targets there and then** (`0x403135`–`0x403150`).

Return Fire is implemented entirely in the damage path. `0x489BB0` computes the
damage — armour and veterancy, §5 of the priorities note — and emits an event;
`0x489CE0` applies it, and on the way records the attacker on the victim
(`unit+0xF0`, its player at `+0xF4`, the cause at `+0xF5`) and calls
**`0x406F80(attacker, victim, damage)`**, its only caller. That routine is the
whole of Return Fire:

- the cause byte must not be `0xB` (a heal is cause `0xA` and returns earlier,
  cause 1 is a weapon hit, cause 2 a paralyser);
- the victim's player type must be 1 or 2, the attacker must not be allied
  (`player[attackerIdx + 0x108] == 0`), and the victim's definition must have
  `def+0x241` bit 16 — set at `0x42CF19` when any of `Weapon1/2/3` is present —
  or be kamikaze;
- if no mission is holding the unit (`mission+0x42 & 0x20000`) and the attacker
  passes `noChaseCategory` *and* `wpri_badTargetCategory` and is inside weapon
  0's range, `0x43B1F0` issues an attack;
- then, for each armed and free weapon slot, if the attacker is inside that
  slot's range it becomes the target — replacing an existing target only when
  that target is gone, out of range, or in the slot's bad-target set.

All of it is gated on `unit+0x110 & 0x300000` being **non-zero** (`0x4070F2`),
i.e. Return Fire *or* Fire At Will. That single test is the difference between
the two modes: both shoot back, only Fire At Will goes looking.

### Where both modes come from, and the movement mode beside them

The firing mode is not the only two-bit field in `unit+0x110`. **Bits 18–19 are
the movement mode**, written by the `Standing_MoveOrder` handler at `0x4030D0`
exactly as `0x403100` writes the firing mode, and the pair are seeded together
out of the definition when a unit is built:

```
485c8a  mov edx, [ecx+0x241]      ; ecx = the definition
485c90  and eax, 0xfff3ffff       ; eax = unit+0x110
485c95  and edx, 0x3              ; StandingMoveOrder
485c98  shl edx, 0x12             ; -> bits 18-19
485c9d  mov [esi+0x110], eax
485ca3  mov edx, [ecx+0x241]
485ca9  and edx, 0xc              ; StandingFireOrder
485cac  and eax, 0xffcfffff
485cb1  shl edx, 0x12             ; -> bits 20-21
485cb6  mov [esi+0x110], eax
```

That is the only copy: from there on the two are the unit's own state and only
the buttons and the COB `SET` move them. So the FBI decides what a unit is
built doing, not what it can be told to do.

The parser puts `standingmoveorder` in **bits 0–1 of `def+0x241`** (`0x42C419`,
stored `0x42C445` with the usual xor-and-xor bitfield insert) and
`standingfireorder` in **bits 2–3 of the same dword** (`0x42C437`, stored
`0x42C465`). **Both default to 2** — the `push 0x2` at `0x42C417` and
`0x42C433`. The pipeline alignment is checked by the store immediately before
them, `mov BYTE PTR [ebp+0x22f], al` at `0x42C422`, which is `bmcode` at the
offset §82 already records.

Values run 0, 1, 2 for both, and the buttons cycle them 0 → 1 → 2 → 0
(`0x41A4F6`, a four-way jump over a global order state where 3 means "the
selection disagrees"). The movement values are **0 hold position, 1 maneuver,
2 roam**, settled by `0x43B1F0`, the routine that turns a retaliation or an
idle sighting into an attack: it returns without doing anything when bits 18–19
are zero (`0x43B211`), and when they are exactly 1 (`0x43B25D`) it plants a
second order at the unit's own position, `unit+0x6a`, before the attack — the
leash that makes Maneuver come home and Roam not.

In the shipped data **every mobile unit names a `StandingMoveOrder`**; the 67
that stay silent are all buildings, so the default of 2 is never what a moving
unit ends up with. 117 say 1, one vehicle plant says 2, and four say 0 —
`armcom`, `corcom`, `armyork` and `corsent`. `StandingFireOrder` is 2 on 104
and **0 on thirteen**: `armbrtha`, `armemp`, `armlance`, `armsilo`, `armsnipe`,
`armthund`, `armvader`, `corint`, `corroach`, `corshad`, `corsilo`, `cortitan`,
`cortron` — the long-range artillery, the nuke silos, the mines and the
bombers, i.e. exactly the things that must not open fire on their own.

### `MobileStandOrders` and `FireStandOrders` gate the buttons

They are **bits 0 and 1 of `def+0x245`** (`0x42C8C4` stored `0x42C8EB`,
`0x42C8DD` stored `0x42C910`), and they default to **0** — `esi`, zeroed at
`0x42BE76` in the parser's prologue. They sit at the bottom of the same dword
as the capability flags: bit 2 `onoffable`, 3 `canstop`, **4 `canattack`**,
5 `canguard`, 6 `canpatrol`, 7 `canmove`, 8 `canload`, 10 `canreclamate`,
11 `canresurrect`. `canattack` at bit 4 is independently fixed by the missions
document §3, where `0x43F154` refuses to build an attack mission without it, so
the pairing here is checked against something outside the parser.

The reader settles what they do. `0x41B3F6`–`0x41B44E` walks the selection and
accumulates, per capability, what the order panel should show:

```
41b3f6  mov edi, [esi+0x245]      ; esi = the definition
41b3fe  shr ecx, 1
41b400  test cl, 0x1              ; FireStandOrders
41b40c  shr ebx, 0x14 / and 3     ;   -> the firing mode
41b425  test BYTE [esi+0x245], 0x1 ; MobileStandOrders
41b435  shr edx, 0x12 / and 3     ;   -> the movement mode
```

Each starts at a sentinel 4, takes the first unit's value, and drops to 3 when
a later unit disagrees. A definition without the flag is skipped entirely, so
**the priorities note's guess is right: these decide whether the button is
offered at all**, and a unit that does not offer one is not dragged round the
cycle with the rest of a mixed selection. The data agrees — `armsolar` names
neither, `armllt` names only `firestandorders`, and the only four units that
write an explicit `0` are the transports `armatlas`, `armtship`, `cortship`
and `corvalk`.

### `DefaultMissionType` is what a unit does when it runs out of orders

A string key, read at `0x42BFE2` and turned into a mission id by
`MissionId::FromName` (`0x438760`, the binary search the missions document
describes) and stored as a **`BYTE` at `def+0x230`** at `0x42C018`. That it is
a byte is confirmed away from the parser by the definition-copy routine, which
moves it with `mov dl, [ecx+0x230]` / `mov [eax+0x230], dl` at `0x42B7D9`.

It is applied at the tail of the per-tick mission service loop `0x43B7C0`, at
`0x43B9AD`–`0x43BA30`, on the branch taken when the unit's mission list has
just become **empty**. So it is not a build-time thing and not an idle-time
thing but both through one path: a new unit has no missions, so its first
service tick installs it, and so does every later tick on which the unit runs
out. It is gated on the owning player's type byte `player+0x73` being 1 or 2,
and a zero id — the value `FromName` returns for a name it does not know, and
for the 63 units that name nothing — installs no mission and leaves the unit
idle.

The three names in the data resolve to:

| Name | Handler | What it is |
|---|---|---|
| `Standby` | `0x405FE0` | Call `0x43B700` (the sight-range search) and hand any sighting to `0x43B1F0`; on a miss, clear the weapons' targets and sleep `rand(30)+30` ticks |
| `Guard_NoMove` | `0x4021F0` | Free all three weapons (`0x489800(unit, 3)`), sleep 30 ticks, and never search |
| `VTOL_Standby` | `0x40F7D0` | The aircraft equivalent, record 0 of the VTOL table |

`Standby` is therefore the "go and look for a fight about once a second" loop,
and it is the one place the standing move order earns its keep: `0x43B1F0`'s
two gates are what stop a unit on Hold Position or Hold Fire from acting on
what it sees.

### What RWE does with this

Implemented: the two-bucket bad-target preference, the `rand(d²)` scoring,
`ShootMe` with the computer-player exemption, `toAirWeapon`, `NoChaseCategory`
on the search that breaks a patrol off, the firing-mode target clear, and
Return Fire from `GameSimulation::applyDamage`.

Deliberately not ported:

- **The once-a-second cadence.** RWE acquires on the tick a weapon falls idle.
  The original's cursor exists to spread the cost over a frame budget, and the
  delay would make units slow on the draw, so the cadence stays unported.
  The reason once given for it here -- that RWE's check costs nothing by
  comparison -- was true at small unit counts and measurably false at large
  ones: at eight hundred units the acquisition scan was ten milliseconds a
  tick, the single most expensive thing in the simulation. It is cheap again
  because the candidate gather now goes through the same kind of cell grid
  the original uses (section 34), not because the check was ever free.
- **The fifty-candidate cap and the sampling without replacement.** Below fifty
  candidates the two are identical; above it the original is simply sampling,
  where RWE scores them all.
- **The retaliation attack order.** RWE points the weapons and issues no order,
  because the original only issues one when the attacker is already inside
  weapon range, where the two come to the same thing.
- **`unitsOnly`, `turret`, `lineOfSight`, `minbarrelangle`.** Decoded far enough
  to say they are no part of this decision: `0x49ABB0` never looks at them.

## 10. Weapon target eligibility, `0x49ABB0` in full

§9 gave this routine a sentence. It is the whole of "may this weapon shoot at
that unit", it is asked of every candidate the auto-acquire considers, and two
of its four rejections were missing from RWE. Here it is branch by branch.

It is `stdcall(attacker, target, slot)` — `ret 0xc` at `0x49AC18`, with the
three arguments loaded at `0x49ABB0`, `0x49ABC1` and `0x49ABE3`. The slot picks
the weapon out of the attacker's three 28-byte weapon records, which begin at
`unit+0x10`:

```
49abb0  mov  eax,[esp+0xc]              ; slot
49abb7  and  eax,0xff
49abc1  mov  esi,[esp+0x1c]             ; the attacker
49abc6  shl  ecx,0x3 / sub ecx,eax      ; slot*7
49abcb  mov  edi,[esi+ecx*4+0x10]       ; -> the weapon definition
49abcf  mov  eax,[edi+0x111]            ; the weapon flags
49abda  test dl,0x1                     ; bit 16, waterweapon
49abdd  je   0x49acaf                   ; -> the dry branch
```

`0x49ADF0` immediately below it is the same address arithmetic on its own,
returning `[weapon+0xdc]`, the range — which is how `0x40B7B0` gets the radius
it hands to the candidate gather at `0x40B81F`.

### The wet branch, `0x49ABE3` — a `waterweapon`

```
49abf3  mov  edx,[targetdef+0x241]
49abf9  test edx,0x80000                ; bit 19, floater
49abff  jne  0x49ac1b                   ; a floater is exempt
49ac01  movzx bp,BYTE [world+0x1427f]   ; sea level
49ac09  cmp  WORD [target+0x70],bp      ; the integer part of the target's y
49ac0d  jle  0x49ac1b
49ac0f  xor  eax,eax / ret              ; above water: reject
```

then, still on the wet branch:

```
49ac1b  test dh,0x10                    ; bit 12 of def+0x241, canhover
49ac1e  je   0x49ac47
49ac20  movsx ecx,WORD [targetdef+0x170]
49ac2b  sar  ecx,1                      ; half the model height
49ac2d  add  edx,ecx                    ;   + the target's y
49ac37  cmp  edx,ecx / jle 0x49ac47     ; must still be under the surface
49ac3b  xor  eax,eax / ret              ; reject
```

So a torpedo may reach anything at or below sea level, plus anything that
`floater`s whatever its height; and a hovercraft, which sits *on* the surface
with `y == sealevel`, is thrown out by the second test because half its model
height is above the water. That is the well known immunity, and it is also what
identifies `def+0x170`: only a height makes both tests read sensibly, and a
zero there would leave hovercraft torpedoable.

The two flag bits are settled in the parser, where the boolean helper's result
is masked and shifted immediately after the call that read it, so the pipeline
trap of §82 does not apply:

| Key | String | Shifted at | Bit of `def+0x241` |
|---|---|---|---|
| `canfly` | `0x503B64` | `0x42C6F4` | 11 |
| `canhover` | `0x503B58` | `0x42C727` | 12 |
| `upright` | `0x503B50` | `0x42C746` | 20 |
| `floater` | `0x503B48` | `0x42C779` | 19 |
| `amphibious` | `0x503B3C` | `0x42C798` | 21 |

`def+0x170` is the **integer part of the definition's height**: `0x489B5B`
reads the whole 16.16 dword at `def+0x16E` against a sea level shifted left
sixteen, and `0x486737` hands the word at `+0x170` to `0x482910` alongside
`SightDistance`, which is the LOS update. It is written nowhere with an
absolute displacement, so it is computed from the model at load rather than
parsed from the FBI — *inference*, but the only reading that fits both uses.

### The dry branch, `0x49ACAF` — everything else

```
49acbd  mov  cl,BYTE [world+0x1427f]    ; sea level
49acc3  movsx edx,WORD [attacker+0x70]
49acc7  movsx ebx,WORD [attackerdef+0x170]
49acce  add  edx,ebx
49acd0  cmp  edx,ecx / jg 0x49ace0
49acd4  xor  eax,eax / ret              ; the SHOOTER is underwater: reject
49acea  movsx edx,WORD [target+0x70]
49acee  movsx ebp,WORD [targetdef+0x170]
49acf5  add  edx,ebp
49acf7  cmp  edx,ecx / jg 0x49ad07
49acfb  xor  eax,eax / ret              ; the TARGET is underwater: reject
```

**This is the submarine rule.** A weapon that is not a `waterweapon` needs both
ends of the shot out of the water, and "out of the water" is `y + height >
sealevel` — the top of the model, not its origin. A submerged submarine fails
it, so a tank, a Peewee or a laser tower is never offered one as a target. Note
the asymmetry with the wet branch, which compares the raw `y` and not `y + h`:
the original is deliberately generous about what counts as *in* the water and
strict about what counts as *out* of it, so a thing sitting at exactly sea level
is reachable by both kinds of weapon.

Only then does the air rule run, and only on this branch:

```
49ad07  test eax,0x20000                ; bit 17, toairweapon
49ad0c  je   0x49ad28
49ad0e  mov  ecx,[target+0x110] / and ecx,0x3
49ad17  cmp  cl,0x2 / je 0x49ad28       ; airborne
49ad1c  xor  eax,eax / ret              ; reject
```

and then the ballistic arc, and then the range:

```
49ad28  shr  eax,1 / test al,0x1        ; bit 1, ballistic
49ad59  push [weapon+0xc8] / push [weapon+0x68]
49ad67  call 0x49a890                   ; solve the arc
49ad6c  cmp  ax,0x8000 / jne ok         ; 0x8000 means "no solution"
49ad7e  ...                             ; dx^2 + dz^2 <= range^2
```

The range test (`0x49AC47` on the wet branch, `0x49AD7E` on the dry one) is the
same both times: the two position deltas are 16.16, each square is taken as a
64-bit product and shifted right 32 (`__allmul` at `0x4E4400`, `__allshr` at
`0x4E43D0` with `cl = 0x20`), which is the square of the distance in whole
world units, and it is compared against `range * range`. **It is flat** — `dx`
and `dz` only, `dy` never enters. A target directly overhead is at distance
zero as far as this test is concerned.

### The air rule only points one way

This is worth being blunt about, because assuming the symmetry is easy and
wrong. `toairweapon` is tested in exactly four places in the whole binary —
`0x49AD07` here, and `0x43E5CF`, `0x43F184` and `0x43F1E1` in the attack-order
builder — and every one of them asks the same question: *this weapon is
anti-air, is the target in the air?* **Nothing anywhere refuses an ordinary
weapon an airborne target.** There is no reciprocal flag, no unit category test
in `0x40B7B0`, and no filter in the candidate gather `0x40AD80`, which walks the
player's enemy list applying only a flat radius, the alive bit and bit 14.

Those three tests are all `0x40AD80` applies, but do not read that as "the
candidate set is unfiltered". **The list it walks is can-see filtered at
construction**: `0x40AA40` rebuilds it and puts every enemy through `0x465AC0`
before appending (the call at `0x40AB11`). Cloak, the waterline and line of
sight have all been applied one call upstream, which is why the scan itself
does not repeat them. See §17a for the chain.

What keeps ground units off aircraft is the **preference**, and only the
preference: sixty of the shipped units name `wpri_badTargetCategory=VTOL` and
forty-four name `NoChaseCategory=VTOL`. §9 has the mechanism right — the
bad-target bucket at `0x40BA13` is returned only when the good bucket is empty
(`0x40BA4D`), so a unit with nothing else in range *does* shoot at the
aeroplane. `armllt`, the Light Laser Tower, names `wpri_badTargetCategory=VTOL`
and no `NoChaseCategory` at all, which is exactly why an LLT plinks away at
passing aircraft in the original. Turning this into a veto would be a larger
divergence than leaving it alone, so RWE leaves it alone.

`NoChaseCategory` is a veto, but only over *going to look*: `0x40B7B0` tests it
at `0x40B927`, under `cmp [esp+0x54],ebp` — only when the third argument is 0,
the sight-range search. That is why a Peewee ignores an aircraft it would have
to walk towards and still shoots one that comes to it.

### The same preference read the other way: the fighter

The interesting case is the reverse of the Peewee's. Every fighter carries
`wpri_badTargetCategory=NOTAIR`, and **143 of the 157 shipped units name
`NOTAIR` in their own `Category`** — every building, every tank, every kbot.
So a Freedom Fighter's bad-target set is not a handful of awkward targets, it
is nearly the whole map. Nothing else holds it back: `ARMVTOL_MISSILE` has no
`toairweapon`, `ARMFIG.FBI` has no `NoChaseCategory`, and an attack order onto
a ground unit produces a perfectly ordinary `AIRTOGROUND` mission
(`0x43F2FA`). A fighter *can* strafe a tank, and with an empty sky it will.

What it cannot do is prefer one. That is the whole of the behaviour, and it
only works if the preference is applied to the decision to **break off** as
well as to the decision to fire — which it is, because there is no separate
routine for breaking off. `0x43B700` is four instructions: test that the unit
is on Fire At Will, and tail-call `0x40B7B0` with its third argument zero. Six
mission handlers call it (`0x4033DE`, `0x406002`, `0x4060B6`, and, for
aircraft, `0x40F98D` in `VTOL_Standby`, `0x4105FB` in `VTOL_SeekAttack` and
`0x41106D` in `VTOL_Patrol`), and all six therefore get the bad-target buckets,
the `ShootMe` rule and the `rand(distance²)` scoring, not just a nearest-enemy
sweep.

RWE had a second search here — nearest living enemy inside weapon range, with
`NoChaseCategory` and `0x49ABB0` applied and nothing else — so fighters took
whatever was closest. The two are now one function with a mode argument; the
mode changes the radius (weapon range against `SightDistance`) and whether
`NoChaseCategory` is consulted, and nothing else.

Only four shipped weapons set `toairweapon`: `armyork`, `armflak`, `corsent`
and `corflak`. The Samson's `armtruck_missile` does not, so §9's "a Samson with
no aircraft about still shoots at tanks" is right, and is a statement about the
preference rather than about the flag.

### What was wrong in RWE

`GameSimulation::weaponCanHitUnit` implemented one of the four rejections:

- a **non-`waterweapon` had no rule at all**, so a tank, a tower or a Peewee
  would happily acquire a submerged submarine — the first reported bug, and
  `0x49ACEA` is the line that forbids it;
- a `waterweapon` tested only `target.y <= sealevel`, missing both the
  `floater` exemption and the `canhover` half-height rejection, so a torpedo
  would refuse a ship and accept a hovercraft;
- nothing checked that the **shooter** was out of the water (`0x49ACC3`);
- `toAirWeapon` was tested *instead of* the water rules rather than after them,
  because it returned early. On the original's control flow the air test is
  reached only on the dry branch and only once both height tests have passed.

Separately, `UnitBehaviorService::findEnemyInWeaponRange` — the search behind
the idle-aircraft engagement and the patrol break-off — ran **no eligibility
test whatever**. The original has one acquisition routine, `0x40B7B0`, whose
third argument changes only the radius and whether `NoChaseCategory` applies;
`0x49ABB0` is called on both paths, at `0x40B914`. So in RWE an idle gunship
carrying an anti-air weapon would break off at a ground unit and a patrolling
unit would engage a submarine, neither of which the original can do.

### Deliberately not ported

- **The ballistic arc test** (`0x49A890`). RWE has no arc solver and aims its
  ballistic projectiles from their own code; a reachability gate here would
  need that solver first.
- **The flat range test.** RWE measures the full 3D distance. The original's
  `dx² + dz²` makes a target directly overhead free, which — given that nothing
  else stops a ground weapon engaging an aircraft — would make ground fire at
  aircraft *more* common than RWE's, not less.

### One correction to §9

§9 says the per-tick scan keeps an existing target "unless it has become
allied, has left range, or is in that slot's bad-target set". The range half of
that is not there. `0x4089A0` fetches the current target with `0x48A190` and
then tests exactly three things before deciding to re-acquire:

```
408ac4  cmp BYTE [players + idx + 0x108],0 / jne drop   ; the target became allied
408af2  test [def + 4*slot + 0x231], bit                ; it is in the bad-target set
408aff  test BYTE [weapon+0x111],0x80 / test [target+0x10e],0x10  ; paralyzer, already paralysed
```

and `0x48A190` itself is a pure accessor — it checks `WORD [slot+0x6] ==
0x8000`, the marker that says the slot holds a unit rather than a position,
looks the unit id up in the table at `[world+0x14357]`, and returns. No
distance is measured anywhere on that path. Dropping a target that has walked
out of range must happen in the weapon's own service, if it happens at all;
it is not part of the re-acquire decision.

## 11. `turret`, and what a hull-mounted gun waits for

`turret` is **bit 19 of `wdef+0x111`**, parsed at `0x42E8F9` — the key string is
pushed there, the read follows at `0x42E906`, and `and eax,1 / shl eax,0x13` at
`0x42E911`–`0x42E91A` puts it in place. The bits either side are `smoketrail`
(18, key at `0x504150`) and `selfprop` (20, `0x50413C`), which agrees with the
table in §82.

### It chooses the weapon's fire handler

`0x49E010(wdef)` writes a function pointer into `wdef+0x60`, and the tests run
in this order:

| Flag | Handler |
|---|---|
| `turret` (19) | `0x49D580` |
| else `vlaunch` (4) | `0x49DB70` |
| else `lineofsight` (0) or `selfprop` (20) | `0x49D9C0` |
| else `dropped` (8) | `0x49DD60` |
| else | left null |

Because `turret` is tested first, a turreted weapon uses `0x49D580` whatever
else it sets, and a `turret=0` **ballistic** weapon gets no handler at all — the
per-tick update bails at `0x49E1F7` when `wdef+0x60` is null, so such a weapon
can never fire. Nothing in the shipped data is one.

### Only a turret runs an aim script

The per-tick weapon update is `0x49E1A0(unit)`, walking the three slots at
`unit+0x1F+28*slot`. At `0x49E1FD` it loads the weapon flags and at `0x49E205`
shifts out bit 19: a turret goes on to compute a heading and pitch and call the
slot's aim script — `0x509688[slot]`, started on the unit's COB at `0x49E31C`
and again through `0x456200` at `0x49E3A6` — then sets bit 0 of the slot flag
byte to say it is waiting. A non-turret falls to `0x49E33D`, which tests
`vlaunch` and, finding it clear, jumps straight to the fire check at `0x49E3AE`.

**So the original never calls `AimPrimary`/`AimSecondary`/`AimTertiary` for a
weapon that is neither `turret` nor `vlaunch`.** There is nothing on a mount to
swing, so there is nothing to wait for.

The two calls at `0x49E31C` and `0x49E3A6` look at first like the script being
run twice, which would be a tidy explanation for upstream issue #42. It is not
one: `0x4B0A70` starts the thread on the unit's own COB, whereas `0x456200`
resolves the script through `0x4B07C0`, checks the global bit
`[ds:0x511DE8+0x2A44] & 1`, and if it is set packs a 0x16-byte record — type
`0x10`, the unit id from `unit+0xA8`, the script and its two arguments — and
hands it to `0x451DF0`, the same emitter `0x49DB4D` uses for a projectile spawn.
It is the network and replay echo of the call, not a second execution, and it
does nothing at all in a local game. **Issue #42 is not explained by this.**

### The hull is what has to come round

`0x49D9C0`, the handler a plain `turret=0` weapon gets, takes the muzzle from
`0x43E240`, works out the bearing to the target with `atan2` (`0x4B715A`) into
`slot+0x16` and the elevation into `slot+0x18`, and then at `0x49DA59`–`0x49DA65`
calls the tolerance check with **the unit's own heading `unit+0x66` and pitch
`unit+0x68`**. If the check says no it returns 0 and the shot does not happen.

The check itself is `0x49D880(unit, slot, heading, pitch)`:

- `tolerance` is `wdef+0x106`. If it is zero, the figure is `0x7D0` (2000, about
  11°) when `unit+0x110 & 0xC` is set and `0x96` (150, about 0.8°) otherwise
  (`0x49D895`–`0x49D8B2`).
- `pitchtolerance` is `wdef+0x108`, and **when it is zero the heading tolerance
  is used for pitch as well** (`0x49D8B4`–`0x49D8CF`). Sixty of the shipped
  weapons name a tolerance and stay silent about pitch; exactly one (`vtol_emg`)
  sets both.
- Both differences are taken as signed 16-bit quantities and compared by
  absolute value against `>` (`0x49D8D1`–`0x49D8F9`), so a tolerance of 32767 —
  what the six torpedoes ask for — admits everything except dead astern, whose
  difference of 32768 negates to itself.

**The original refuses the shot; it does not steer.** Nothing in the weapon code
turns the unit: `turnrate` (`def+0x1BA`) is read only in the movement routines
(`0x43CBCA`, `0x43CF96`, `0x43D014`, `0x43D542`, `0x44EADE`) and in the unit-info
string builder at `0x48943C`. When the eligibility test rather than the tolerance
fails, `0x49E53A` and `0x49D664` raise bit 12 of the unit's event word
`unit+0xBA`, which missions wait on (the dispatcher is at `0x43B7FE`) — but a
tolerance failure raises nothing at all and simply does nothing that tick.

`0x49DB70` (vertical launch) computes the same two angles and stores them but
**never calls `0x49D880`**, and `0x49DD60` (bombs) does not either. A silo does
not have to face what it is firing at.

### Which weapons this is

Merging `totala1\weapons`, `rev31\weapons` and `rev31\gamedata\WEAPONS.TDF`
gives 132 weapon definitions, 94 with `turret=1`. The 38 without are **not**
tank hull guns — there is no such thing in the shipped data. They are aircraft
weapons (12, tolerance 6000–11000), torpedoes and depth charges (9, almost all
32767), vertical-launch missiles and nukes (12, tolerance 4000), bombs (4), and
`mindgun`/`noweapon`/`earthquake`. The behaviour this recovers is therefore that
**a gunship or fighter has to be pointing at its target before it shoots**, not
that tanks turn their bodies.

### `minbarrelangle` is inert

It is a float in radians at `wdef+0xC8`, read at `0x42E724` and scaled by
`ds:0x4FD260` = π/180, with a default of `-11.25` degrees supplied as the double
`0xC026800000000000` pushed at `0x42E70F`. §82 has this right and the priorities
note had it wrong twice over: `wdef+0xFE` is `holdtime`, and the field is not a
clamp.

Its only reader is the ballistic launch-elevation solver `0x49A890`, called from
the eligibility test `0x49AA80` (`0x49AB75`) and from the turret handler
(`0x49E28E`). That routine solves the standard ballistic quadratic — discriminant
`v⁴ − 2ghv² − g²X²`, assembled at `0x49A908`–`0x49A964` with the constant
`ds:0x4FDA60 = -2.0` — and returns `acos(√(vx²)/v)` (`0x4E67F0`, which computes
`√((1+x)(1−x))` and then `fpatan`) scaled to 16-bit angle units by
`32768/π` (`ds:0x4FDA88`, `ds:0x4FDA90`). `0x8000` means no solution, and both
`0x49AA80` and `0x49D580` treat that as "cannot shoot this".

`minbarrelangle` is the lower bound the two roots are tested against at
`0x49AA11` and `0x49AA39`, with a hard upper bound of π/4 (`ds:0x4FDA80`) that
selects the flat root of the pair. But an `acos` of a non-negative quantity is
never negative, so **the returned elevation always lies in [0°, 45°] and a
negative bound can never bind**. Transcribing the routine and sweeping 72,900
geometries — ranges 20–2000, height differences ±800, five launch speeds —
`minbarrelangle = -35°` gave an answer identical to no bound at all in every
single case, and not one of the 36,734 solved cases produced a negative
elevation. Every shipped value is negative (17 weapons, −15° to −40°, plus
`arm_paralyzer` at 0 which is `lineofsight` and so never reaches this routine),
as is the −11.25° default. **The field does nothing in the original.** What
actually refuses a shot is the discriminant going negative — the target is out
of reach — or both roots exceeding 45°.

### `aimrate` is not a key

The string `aimrate` **does not exist anywhere in `TotalA.exe`**, so the two
weapons that set it (`arm_berthacannon` and `core_intimidator`, both 2500) are
handing the parser a key it does not recognise and it is discarded. There is
nothing to implement.

### `holdtime`

`WORD wdef+0xFE`, parsed at `0x42E6F1` and multiplied by `ds:0x4FD250 = 30.0`,
so it is a count of seconds stored as ticks. The same two weapons set it, both to
1. No reader has been found: the only word-sized read of `+0xFE` in the image is
`0x41DD7C`, which is in unrelated code. Left unimplemented for want of evidence.

---

## 12. Where a shell actually lands, screen shake, and waterline

Three keys that were parsed and thrown away — `accuracy`, `shakemagnitude` /
`shakeduration`, and `waterline` — and, because the first of them could not be
answered without it, the whole of the original's ballistic firing solution.

The question that started this was a player's: *"Vulcan was very inaccurate over
long distance, that might be intentional."* It is intentional, and it is
`accuracy`. The rest of this section is the working.

### The ballistic firing solution, `0x49A890`

A ballistic weapon aims in two halves. The heading is a plain
`atan2` (`0x49D5EE`), taken from the muzzle to the target and then relieved of
the unit's own heading (`sub ax,[edi+0x66]` at `0x49D5F3`). The pitch is
`0x49A890`, called at `0x49D614` with five arguments pushed at `0x49D60F`:

```
0x49A890(dx, dy, dz, weaponvelocity, minbarrelangle)
```

where `dx`/`dy`/`dz` are **muzzle minus target** (`0x49D5DA`–`0x49D5E6`, note
the direction — it is not target minus muzzle) and `minbarrelangle` is the float
at `wdef+0xC8`. It returns a 16-bit pitch, or **`0x8000` meaning "no solution",
in which case the weapon does not fire at all** (`cmp ax,0x8000` at `0x49D61B`).

The routine is IEEE double throughout. Writing `d` for the horizontal distance
`hypot(dx, dz)` (`0x4FB440`), `s` for the speed and `G` for the per-tick
gravity, it forms

```
D = (s² + G·dy)² − G²·(d² + dy²)          ; = s⁴ + 2·G·s²·dy − G²·d²
```

at `0x49A908`–`0x49A964`, gives up if `D < 0` (`0x49A966`), and then solves not
for `tan(pitch)` but for the **square of the vertical launch speed**:

```
vy² = d²·(s² + G·dy ± √D) / (2·(d² + dy²))
```

taking `√` of that and dividing by the speed to get a sine, then `asin`
(`0x4E67F0`, at `0x49A9C8` and `0x49A9FE`). Written out with `dy` flipped to the
usual "target minus muzzle" sense, `D` is `s⁴ − 2gs²y − g²x²` — **exactly the
discriminant RWE's `computeFiringAngles` already computes**, arrived at by a
different factoring.

Which root it uses is decided at `0x49AA11`–`0x49AA53` against two constants:
`minbarrelangle` as a floor, and `0x4FDA80` = **π/4** as a ceiling. It tries the
`+√D` root first and the `−√D` root second. Since the high root exceeds 45° for
every target inside the gun's maximum range and only equals it exactly at that
range, **the 45° ceiling means the high shot is always rejected and the original
always fires the flat one** — the same choice RWE makes with `pitches->second`.
The result is scaled to a 16-bit angle by `× 32768 × (1/π)` (`0x4FDA88`,
`0x4FDA90`) and truncated (`0x4E43A0`).

So there is no lofted artillery arc in Total Annihilation. Every ballistic gun
in the game is a flat-trajectory direct-fire weapon whose barrel never goes above
45°, and a target it cannot reach under that cap simply does not get shot at.

### The original's trigonometry is coarse

`0x4B70EF` (sine × length) and `0x4B7123` (cosine × length, the same table read
a quarter turn along) index the table at `0x509F00` with

```
byteOffset = ((angle + 0x20) >> 6) & 0x3FE
```

The mask drops the low bit of a value that ranges over 0–1023, so the table is
**512 entries per full turn** — confirmed by reading it: entry 0 is 0, entry 1 is
101, entry 128 is `0x2000`, entry 384 is `−0x2000`, i.e. `round(8192·sin(2πk/512))`.
The mantissa is 13-bit: `(table[i] × len + 0x1000) >> 13`.

One table step is **128 of 65536, 0.703°**. Whatever the double-precision solver
decides, the shell leaves the barrel on one of 512 headings and one of 512
pitches. Replaying it, half a step of pitch moves the fall of shot by up to 98
units for a Big Bertha and 180 for an Intimidator. RWE's `sin`/`cos` in
`SimAngle.cpp` are `std::sin`/`std::cos` on a float, so RWE is *more* precise
here than the original, not less.

### `accuracy`, `WORD wdef+0x104`

Parsed at `0x42EBFE` and stored at `0x42EC19` — §82's table is right, and the
neighbouring `tolerance` `+0x106` and `pitchtolerance` `+0x108` are right too.
The data file's own comment defines it: *"amount of accuracy in 64K deg that
weapon is good for, 0 = 100%"*.

There is exactly one reader, `0x49D6D7`, and it sits in the fire path **after**
the aim has been solved and the unit's heading added to it (`0x49D6B4`), and
**before** the spawn dispatch (`0x49D742`) hands off to the ballistic
(`0x49CDE0`) or line-of-sight (`0x49C9C0`) spawn. Both of those read the aim
angles straight back out of the weapon slot (`0x49CE4F`–`0x49CE5B`), so the
jitter reaches every kind of projectile, not just ballistic ones.

The whole of it, `0x49D6BC`–`0x49D73E`:

```
acc  = WORD[wdef+0x104]                        ; accuracy
acc -= (health << 11) / maxdamage              ; 0x49D6C2-0x49D6CE, 0x49D6E7
acc += 0x800                                   ; 0x49D6F5
vet  = killcount / 3                           ; 0x49D6E0-0x49D700
if (vet > 1) acc /= vet                        ; 0x49D702-0x49D711
if (acc != 0) {
    heading += rand(acc) - acc/2               ; 0x49D723-0x49D72F
    pitch   += rand(acc) - acc/2               ; 0x49D733-0x49D73E
}
```

Everything is 16-bit: `acc` lives in `cx`, the halving is an unsigned `shr` and
the arithmetic is masked to 16 bits at each use, so the intermediate carry out of
bit 15 that `add ecx,0x800` can produce is discarded.

Three things fall out of that:

- **The health term cancels at full health.** `(health << 11) / maxdamage` is
  exactly `0x800` when `health == maxdamage`, so an undamaged unit gets its
  weapon's `accuracy` unmodified. As it takes damage the term shrinks and the
  error cone grows, by up to a full `0x800` — **11.25°** — at the point of death.
  A half-dead Big Bertha's cone goes from 500 to 1524, three times worse.

  None of this is conditional on the weapon having an `accuracy` at all. There
  is no branch around the arithmetic and none around the health term, so
  **every weapon in the game spreads as its owner is damaged**, including the
  hundred and sixty that leave the key at zero — a weapon with `accuracy=0` is
  perfect only while its owner is untouched. Given how carefully the term is
  arranged to cancel at full health this looks deliberate rather than
  accidental, but it is worth flagging as the one part of this section with a
  large blast radius.
- **Kills make a unit more accurate.** This is the veterancy the original
  actually has: three kills do nothing, six halve the cone, nine divide it by
  three. It is an integer divide, so it never reaches zero.
- **Heading and pitch are drawn independently**, each uniform on
  `[−acc/2, acc/2)` — two separate calls to `0x4B6C30`, which returns
  `[0, n)`. The error is not a cone around the aim line; it is a rectangle in
  (heading, pitch).

### What that does to a long shot

`ARMVULC_WEAPON` is not in the retail data — the Vulcan is a Core Contingency
unit and its files live in `ccdata.ccx`. Read out of the archive, it is
`range=3080`, `weaponvelocity=800`, `areaofeffect=100`, **`accuracy=800`**, on a
unit with `MaxDamage=1400`. Its Core opposite number `CORBUZZ_WEAPON` (Buzzsaw)
is `range=3800`, `weaponvelocity=900`, `areaofeffect=120`, `accuracy=800`. The
two retail long guns are `ARM_BERTHACANNON` (4096, 800, AoE 80, `accuracy=500`)
and `CORE_INTIMIDATOR` (5120, 1000, AoE 100, `accuracy=1000`).

Transcribing `0x49A890`, the 512-entry sine table, the original's integration
order and the `accuracy` draw into a standalone program and firing 50 000 rounds
at each gun's full range over flat ground gives, in world units:

| Weapon | range | AoE | `accuracy` | range error sd | range error min/max | lateral sd | lateral min/max |
|---|---|---|---|---|---|---|---|
| Vulcan | 3080 | 100 | 800 | 219 | −361 / +350 | 68 | ±132 |
| Buzzsaw | 3800 | 120 | 800 | 277 | −519 / +393 | 84 | ±161 |
| Big Bertha | 4096 | 80 | 500 | 114 | −234 / +163 | 57 | ±102 |
| Intimidator | 5120 | 100 | 1000 | 407 | −687 / +743 | 141 | ±276 |

The player's reading was right. A Vulcan firing at its own maximum range in the
original puts its shells anywhere in a patch roughly 700 units deep and 260
across, and its blast is 100 across. It is *supposed* to walk its fire over the
target and connect only some of the time — which is what the quarter-second
reload is for.

### And what RWE does instead

The same replay, run through RWE's own arithmetic — `computeFiringAngles` in
float, `SimAngle`'s `std::sin`, and `updateProjectiles`' integration:

| Weapon | TA solved pitch | RWE solved pitch | TA lands at | RWE lands at |
|---|---|---|---|---|
| Vulcan | 2968 | 2969 | 3083 (+3) | 3055 (−25) |
| Buzzsaw | 2885 | 2885 | 3747 (−53) | 3771 (−29) |
| Big Bertha | 4167 | 4168 | 4065 (−31) | 4071 (−25) |
| Intimidator | 3184 | 3185 | 5173 (+53) | 5088 (−32) |

**The two solvers agree to one part in 65536** — a twentieth of a degree, and in
one case exactly. RWE's solver is not losing precision at long range and there is
nothing to fix in it. `SimScalar` is a `float`, not a fixed-point type, and at
these magnitudes single precision is comfortably enough: the largest intermediate
is `s⁴ ≈ 5×10⁵`.

Both engines land a little off the aim point, by 25 to 53 units on shots of three
to five thousand, and for the same reason: a parabola integrated in whole ticks
is not a parabola. They differ in the direction of the error because they take
gravity at opposite ends of the tick — the original does `position += velocity`
and *then* `velocity.y -= G` (`0x49BCE3`, `0x49BD3F`), while RWE does
`velocity.y -= G` and then `position += velocity`
(`GameSimulation.cpp:2957`, `:2996`). Neither is worth changing; both are an
order of magnitude smaller than the scatter the original deliberately adds.

So the honest answer to the question is that **RWE's long guns are not
inaccurate, they are too accurate**, and the missing piece is a key that was
being parsed and dropped on the floor.

### `sprayangle`, `WORD wdef+0xEE`, and where RWE differs

Parsed at `0x42E670`. It has exactly two readers, `0x49B8F1` and `0x49B911`, and
both are inside the **burst-continuation** branch of the per-tick projectile
update `0x49B720` — the branch entered only when the projectile's burst counter
`proj+0x60` is non-zero. Two consequences that RWE does not currently match:

- **`sprayangle` is the whole width of the spread, not the deviation either
  side of the aim.** The original draws `rand(sprayangle) − sprayangle/2`,
  uniform on `[−sprayangle/2, +sprayangle/2)`. RWE drew `[0, sprayangle]` and
  flipped a coin for the sign, so every burst weapon in the game scattered
  twice as wide as it should. All five weapons that set the key set it to
  1024, so the spread was ±5.6° where it should have been ±2.8°. **Fixed.**
- The original does not spray **the first shot of a burst**, only the
  continuations — the readers are inside the branch guarded by a non-zero burst
  counter. RWE sprays every shot. Recorded, not changed: unpicking it would
  mean threading a shot index through the fire path for very little.

The axis is the same in both: the original rebuilds `velocity.x` and
`velocity.z` from the sprayed heading (`0x49B932`–`0x49B94B`) and leaves
`velocity.y` alone, and RWE's `rotateDirectionXZ` likewise turns about the
vertical only.

### Screen shake, `DWORD wdef+0xCC` and `DWORD wdef+0xD0`

`shakemagnitude` is read as an integer and stored at `0x42EC5A`;
`shakeduration` is read as a **float**, multiplied by the 30.0 at `0x4FD250` and
truncated (`0x42EC60`–`0x42EC70`), so it is seconds on the way in and ticks in
the struct. §82's offsets are right.

The reader is not obvious, because the weapon definition's own fields are only
touched at one site and it is easy to miss. The way in is the string `NoShake`
at `0x502444`, which has no absolute reference anywhere in `.text` — it is a
console command, sitting in a table of twelve-byte `{name, handler, arity}`
records at `0x501D38` alongside `Contour`, `ScrollSpeed`, `IFace` and `Give`.
Its handler is `0x416E60`, and all it does is toggle **bit 4 of
`WORD [globals+0x37F2F]`**.

That bit is tested in exactly two places, `0x41C5E6` and `0x41C646`, which are
the two halves of the shake:

- `0x41C5E0(magX, magY, duration)` **sets** a shake: duration into
  `[globals+0x1432F]` and `[globals+0x14333]`, the two magnitudes into
  `[globals+0x14337]` and `[globals+0x1433B]`, and bit 0 of
  `[globals+0x1434E]` to say a shake is running. Nothing calls it.
- `0x41C640(magX, magY, duration)` **accumulates** into a shake, and is called
  from exactly one place. If no shake is running it clears both magnitudes
  first. Then it **averages the durations** — `(new + current) / 2`, an
  arithmetic mean, at `0x41C67B`–`0x41C690` — and **adds** the magnitudes.

The single caller is `0x499FBA`, in the projectile detonation routine, and it
passes the weapon definition's own fields straight through:

```
0x499FAB  mov edx,[edi+0xd0]     ; shakeduration, already in ticks
0x499FB1  mov eax,[edi+0xcc]     ; shakemagnitude
0x499FB7  push edx               ; duration
0x499FB8  push eax               ; magY
0x499FB9  push eax               ; magX
0x499FBA  call 0x41C640
```

`shakemagnitude` is pushed twice, so the horizontal and vertical amplitudes are
always equal, and — this is the part worth knowing — **there is no falloff with
distance.** The routine never looks at where the explosion was or where the
camera is. A Big Bertha shell landing in the far corner of the map shakes the
screen exactly as hard as one landing under the cursor.

The consumer is `0x41C6F0`, which steps the shake on by one each time it is
called. The original runs its simulation and its display at the same thirty a
second so it makes no difference there which of the two you call it, but it
does in RWE, where the display can be faster: `shakeduration` arrives as
seconds multiplied by thirty, so RWE steps it on the simulation tick and a
shake lasts the same time whatever the frame rate.

```
remaining = [globals+0x14333]
if (remaining <= 0) { clear the running bit; return }
ampX = magX * remaining / duration            ; 0x41C721-0x41C725
ampY = magY * remaining / duration
cameraX += rand() * ampX / 0x8000 - ampX/2    ; 0x41C737-0x41C755
cameraY += rand() * ampY / 0x8000 - ampY/2    ; 0x41C757-0x41C775
remaining--                                   ; 0x41C7A2
```

The amplitude ramps down **linearly** to nothing over the shake's life, and each
frame's offset is uniform on `[−amp/2, amp/2)` — `0x4E4870` is a `rand()` with a
`0x7FFF` ceiling and the divide by `0x8000` normalises it — applied to the two
components of the camera's scroll position at `[globals+0x1431F]` and
`[globals+0x14323]`, which is what `0x41C574` writes when the camera is moved
normally. So it is a screen-space jitter of the scroll, not a change of view
angle.

Two details of the accumulate are worth having written down because they look
like bugs and are not. The running duration at `[globals+0x1432F]` is read
before it is written and is **never cleared**, even for a shake starting from
nothing — so a weapon asking for two seconds of shake against a standing start
gets one, and only a second explosion arriving while the first is still running
gets anything near what it asked for. And because the durations are averaged
rather than maxed, a small explosion landing during a big one *shortens* the
big one.

**Which weapons actually shake.** Every weapon in the shipped data that sets
`shakemagnitude` is an explosion rather than a gun: `LARGE_BUILDING`,
`LARGE_BUILDINGEX`, `ESTOR_BUILDING`, `BIG_UNIT`, `COMMANDER_BLAST`,
`CRAWL_BLAST`, `ATOMIC_BLAST`, `EARTHQUAKE` and their variants — the things
units name in `explodeas` and `selfdestructas`. Magnitudes are 8, 24 or 32 and
durations 0.3, 0.5, 1.5 or 2 seconds. So the shake is something you feel when a
big building or a commander dies, or when a nuke goes off, and never when
artillery lands. RWE hooks both the projectile detonation and the unit death
because the original's one call site covers both, but only the second will ever
fire on stock data.

RWE differs in one deliberate way: the original adds its offset to the scroll
position every frame and never takes it off, so a long shake leaves the camera
a little way from where the player parked it. RWE remembers the offset it
applied and removes it before applying the next one, which gives the same
jitter without walking the view away.

### `waterline`, `BYTE def+0x22C`

Stored at `0x42C259`, and it has the two readers §B named.

`0x43D72E` is the one the key is named for. For a unit with the **`floater`**
flag (flags word A, `def+0x241` bit 19, tested at `0x43D71E`) the height the
movement code wants is clamped:

```
y = max(wantedY, seaLevel − waterline)
```

`seaLevel` is `BYTE [globals+0x1427F]`, in whole world units. The comparison is
done in 16.16 and the clamp value is built at `0x43D734`–`0x43D745` by an
idiom worth writing down, because taken literally it looks like nonsense:

```
mov eax,edx ; shl eax,0x10 ; sub eax,edx ; add eax,<sea> ; shl eax,0x10
```

That is `((w << 16) − w + s) << 16`. The `w << 32` term falls off the top of the
register, so the low 32 bits are exactly `(s − w) << 16` — the compiler's way of
negating `w` inside a value it is about to shift left by 16 anyway. Sea level
minus the waterline, in 16.16. A floater is therefore never allowed to sit lower
than `waterline` below the surface, and since everything else is pushing it down
it settles exactly there. That is the depth the hull sits at.

`0x43DBA9` is the second reader, and it turned out to be the more interesting
one: it is the routine that calls the COB entry point **`setSFXoccupy`** (the
string is at `0x505248`, pushed at `0x43DBE3` into `0x4B0A70`). It works out a
state number 0–4 describing how the unit sits in the water and, if it has
changed since last time (`[unit+0x10A]`, `0x43DBCC`), tells the script:

```
if (movementMode != 1 && movementMode != 2)        state = 0    ; 0x43DB84
else if (unitY > seaLevel)                         state = 4    ; 0x43DB8C
else {                                             state = previous
    if (unitY - seaLevel > -5)                     state = 1    ; 0x43DB9C
    if (unitY + waterline == seaLevel)             state = 2    ; 0x43DBB5
    if (unitY + WORD[def+0x170] < seaLevel)        state = 3    ; 0x43DBC7
}
```

`unitY` here is `WORD [unit+0x70]`, in whole world units, not 16.16. Note that
the three tests in the last branch are not exclusive and do not start from
zero — if none of them fires the unit keeps whatever state it had, which is a
real quirk of the original and not a mistranscription. `WORD [def+0x170]` is not
identified; it behaves like a hull height.

State 2 — floating at exactly its waterline — is the settled-ship case, and is
what a ship's script is waiting for before it starts emitting a wake.

**Which of the two readers actually matters.** Twenty-one shipped units set
`WaterLine` (submarines at 20, tidal generators at 8, floating shipyards at 1,
up to 25) and eighteen set `Floater`, and **the two sets do not overlap at
all** — checked over the whole of `rev31\UNITS`. So the floating clamp at
`0x43D72E`, which is gated on `Floater`, only ever sees a `waterline` of zero
and reduces to `max(wantedY, seaLevel)`, which is exactly what RWE already
did. Everything the key visibly does, it does through `setSFXoccupy`.

That is also why RWE does not apply the clamp. RWE's `floater` is not the FBI
key: `LoadingScene_util.cpp:481` also sets it for anything whose yard map
contains water, which catches every floating building — precisely the units
that *do* set `WaterLine`. Feeding the clamp RWE's broader notion of a floater
would sink floating shipyards and tidal generators by up to twenty-two units,
which the original never does. So `waterline` is parsed and used for the water
state, and how things float is left alone.

### Recorded, not implemented

- The ballistic spawn at `0x49CE62`–`0x49CE8A` sets the launch `velocity.y` to
  `sin(pitch)·weaponvelocity − ([slot+0x10] / weaponvelocity)·G` rather than
  plainly `sin(pitch)·weaponvelocity`. `[slot+0x10]` divided by a speed to give
  something multiplied by a per-tick gravity has to be a distance, but taken at
  face value with the flight time the correction is far too large to be the
  half-tick term the integration order calls for, and no write to `[slot+0x10]`
  was found to settle it. The term is left out of RWE; every number in the
  tables above was computed without it, and the systematic errors it would have
  to explain are only tens of units in any case.
- The **console command table** at `0x501D38` is decoded far enough to name
  `NoShake`, `Contour`, `ScrollSpeed`, `IFace` and `Give` and to find their
  handlers. Nothing else was pulled out of it.

## 13. What an aircraft does with nothing to do

Two things the player reported, which turn out to be the same question asked
twice: a bomber "immediately stops in place and lands" once its target is
destroyed, where the original "flies around after and sometimes goes circling
back over the now destroyed target"; and construction aircraft guarding an idle
factory, which in the original "fly around in the nearby vicinity".

The original has three different answers depending on *why* the aircraft has
nothing to do, and the difference is the whole finding. An attack that runs out
of target is replaced by a search mission that circles the spot for ever. A
mission list that merely empties gets `DefaultMissionType`, which for an
aircraft means going and landing. A guard order flies a circuit of its own for
as long as the order lasts. RWE had one answer — go and land — for all three.

Companion to the missions document, whose conventions this section uses
throughout: `Dir(θ)` is the direction an object with heading θ faces, and
`0x48A980(a, b)` is the bearing such that `Dir` of it points from `a` to `b`.
Arrival tolerances handed to `0x44E730` are whole world units; distances
written as `0x1400000` and the like are 16.16.

### 1. An attack that ends because the target died: `VTOL_SeekAttack`

`AirStrike` does not simply finish. Its prologue, at `0x411FEA`, reads the
mission's target unit; when that is null and `mission+0x42` has bit 9 set — the
flag that says the mission was created against a unit rather than a patch of
ground — it allocates a fresh mission, resolves the name `"VTOL_SEEKATTACK"`
(`0x501C9C`) through `MissionId::FromName`, constructs it with **no target unit
and the attacker's own position** (`lea edx,[edi+0x6a]` at `0x412020`, where
`edi` is the unit), appends it with `0x43AD10`, and returns 5 so that the strike
mission itself is deleted. `AirToGround`, `AirToGroundHover` and `AirToAir` do
the same on their `flags & 0x1000A` / `0x10008` paths, with the mission's last
known target position instead.

So the aircraft's next mission is anchored on **where it was standing the moment
its target went away** — which, at the end of a bombing run, is over or just
past the wreckage.

**`VTOL_SeekAttack` is `0x4103E0`**, arguments `(unit, mission, flags)`.

Prologue:

* `if (flags & 0x40) return 5` — `0x4103EA`. One of the move-goal outcome bits
  ends the mission outright.
* Off-map (`unit+0x82 == [gs+0x142B7]`, the sentinel bucket of §11 of the
  missions document): waypoint = `unitPos + Dir(0x48A980(unitPos, mapCentre)) ·
  800`, arrival tolerance `0x80`, `mission+0x06 |= 0xE0`, return 2.
* Dispatch on `mission+0x05`: 0 → `0x410701`, 1 → `0x41050F`, anything else →
  return 7 (flush the list).

**State 0 (`0x410701`) — take off, and take a bearing.**

* `unit->mover == 0` or `canfly` clear → return 7.
* If `mission+0x16` is a live target after all, hand it to `0x43B1F0` and
  return 0 (restart) if that issues an attack.
* Otherwise: if `mission+0x22`, `+0x26` and `+0x2A` are all zero, fill them
  with the unit's own position (`0x41074A`–`0x410771`); then
  **`mission+0x36 = rand(0x10000)`** (`0x410774`) — a uniform full-circle
  bearing, the state the circuit runs on — and `mission+0x3A = mission+0x36 & 1`.
* `0x4898B0(unit, 3)` (the mission owns all three weapons), detach from a pad
  if `unit+0x86`, `0x48B090(unit, 1, 1)`, and if the mover is in landed mode
  switch to flying and climb to `cruisealt / 2` on the spot. Return 1.

**State 1 (`0x41050F`) — the circuit. This state never advances.**

```
41050f  0x489800(unit, 3)                       ; weapons free again
410518  if (word[unit+0x108] < (dword[def+0x1FA] >> 2) * 3)   ; below 75% health
            pads = 0x40B530(player, &unitPos, 0xF00, &list)   ; within 3840
            if (pads not empty):
                0x4388D0(mission, 0)            ; drop the move goal
                push VTOL_LANDING (0x501B94) on rand(n) of them, 0x43ACB0
                mission+0x06 = 0 ; return 0
4105fa  t = 0x43B700(unit)                      ; the sight-range search
        if (t && 0x43B1F0(unit, t, 0)) return 5 ; found a fight: this mission is done
410620  if (flags & 0xE0)                       ; the last hop's goal completed
            mission+0x36 -= 0x5555 + rand(0x2000)
41063e  r = ([[unit+0x10] + 0xDC] + 0xA0) << 16 ; weapon 0's range, plus 160
410654  waypoint = mission+0x22 + Dir(word[mission+0x36]) * r
4106c4  0x44E730(goal, 0x80)                    ; arrival tolerance 128
4106d0  0x4388D0(mission, goal)
4106d8  0x439E80(mission, rand(30) + 30)
4106ea  mission+0x06 |= 0xE0
4106f2  return 2
```

Three things fall out of that.

* **The bearing steps back by `0x5555 + rand(0x2000)`, i.e. 120° to 165°, and
  only when the previous goal actually completed.** The timer wake re-installs
  the *same* goal at the *same* bearing, so it changes no geometry; its job is
  to re-run the health check and the sight search a couple of times a second.
* **The radius is weapon range plus 160**, which for all four bombers — whose
  bombs all reach 1280 — is **1440**.
* **A step of more than a quarter turn and less than half of one means every
  leg is a chord, not an arc.** A 120° chord of a 1440 circle passes 720 from
  the centre, a 165° chord passes 188 from it. That is exactly the reported
  "sometimes goes circling back over the now destroyed target", and it is why
  the step is worth transcribing rather than rounding to a neat orbit.

Nothing else ends the mission. There is no maneuver leash on it — `0x43A0C0`'s
sixth argument is pushed as 0 at `0x41201C`, so `mission+0x3E` is zero and the
handler never tests it anyway. An undamaged bomber whose target dies **circles
that spot indefinitely**.

### 2. A mission list that merely empties: `VTOL_Standby`

The other case, and the one RWE already had right. §9 records that
`DefaultMissionType` is installed at `0x43B9AD` on the branch of the service
loop taken when a unit's mission list becomes empty. All twenty-one aircraft in
the shipped data name `VTOL_Standby`, whose handler is **`0x40F7D0`**.

**State 0 (`0x40F9B8`)** — `0x4898B0(unit, 3)`, `mission+0x06 |= 0x10000`, wake
next tick, and then `mission+0x2E = word[unit+0x6C]`, `mission+0x30 =
word[unit+0x74]`: the anchor is **where the aircraft was standing when it went
idle**. Return 1.

**State 1 (`0x40F988`)** — `0x43B700` and `0x43B1F0`; if that issues an attack,
clear the mask, reset to state 0 and return 3. Otherwise return 1.

**State 2 (`0x40F7F8`)** — the decision:

```
40f808  if (!(def+0x241 & 0x800))        goto sleep   ; canfly
40f811  if ((unit+0x110 & 3) != 2)       goto sleep   ; not airborne
40f823  if (unit+0x8A == 0)              goto land
40f82f  θ = rand(0x10000) ; r = (rand(0x20) + 8) << 16
        waypoint = anchor + Dir(θ) * r                ; anchor = mission+0x2E, +0x30
40f8bd  0x44E6C0(goal, word[def+0x21C])               ; full cruisealt, no tolerance
40f8d4  0x439E80(mission, rand(15) + 30)
40f8e6  mission+0x05 = 1 ; return 2

sleep (0x40F957):  mask |= 0x10000 ; wake rand(30)+30 ; state = 1 ; return 2
land  (0x40F8F9):  push VTOL_LANDIFCAN (0x5012BC) at mission+0x22 ; return 5
```

`unit+0x8A` is the **head of the list of units attached to this one**: it is
written at `0x48AC82` when something is attached, chained through `unit+0x8E`,
and `unit+0x86` is the reverse pointer to the carrier
(`0x48AC15`–`0x48AC82`).

So an idle aircraft **hops about a ring of 8 to 39 units around the spot it
went idle on, at full cruise altitude, once every 30 to 44 ticks — but only
while it is carrying something.** Empty, it pushes `VTOL_LandIfCan` and goes
and lands. RWE's "no orders, go and find somewhere to set down" is the right
answer for this case; the reported bug was that it was also the answer for
case 1.

### 3. Guarding: `VTOL_Follow`

A guard order on an aircraft becomes `VTOL_Follow` directly, not `VTOL_SeekGuard`.
`0x43F4C7` requires `def+0x245` bit 5 (`canguard`) and a target, then tests
`def+0x241` bit 11 (`canfly`) at `0x43F4E2` and picks the name at `0x505368` =
`"VTOL_FOLLOW"`. **The handler is `0x40FBE0`**, arguments `(unit, mission, flags)`.

Prologue: if `mission+0x16` is null, or `flags & 0x48`, push a `VTOL_SeekGuard`
(`0x501B1C`) carrying the same target and position and return 5 (`0x410341`);
handle the off-map case as above; otherwise **refresh `mission+0x22` from the
guarded unit's live position every tick** (`0x40FCE9`).

* **State 0 (`0x41025D`)** — announce `"Guarding"` (`0x5014E8`),
  `0x4898B0(unit, 3)`, detach, `0x48B090(1, 1)`, and if landed switch to flying
  and climb to `cruisealt / 2` in place. Then `mission+0x36 = rand(0x10000)`
  and `mission+0x3A = mission+0x36 & 1`. Return 1.
* **State 1 (`0x410245`)** — `0x489800(unit, 3)`, free all three weapons.
  Return 1.
* **State 2 (`0x40FD26`)** — everything else, in order: retaliate on whoever
  last hit the guarded unit (`guardee+0xF0`, gated on the alliance byte, on
  `flags & 0x10` and on the guard's own `noChaseCategory` at `def+0x23D`);
  point each armed, free weapon slot at it; then two attempts to copy the
  guarded unit's work — `0x4899B0(unit, guardee)` followed by `0x43F0E0` at
  `0x40FE86`, and cloning the guarded unit's own head mission at `0x40FEDC`
  through a chain of `FromName` comparisons. If none of those bite, control
  reaches `0x41013B`, and that is the circuit:

```
41013b  if (flags & 0xE0)
            mission+0x36 -= 0x4000 + rand(0x2000)
41015b  if (unit+0x110 & 0x80000000)
41016c      r = ([[unit+0x10] + 0xDC] + 0xA0) << 16   ; weapon 0's range, plus 160
        else
4101b0      r = 0x1400000                             ; 320
410193  waypoint = guardeePos + Dir(word[mission+0x36]) * r
410211  0x44E730(goal, 0x80)                          ; tolerance 128
410225  0x439E80(mission, 0x1E)                       ; wake in 30 ticks
41022e  mission+0x06 |= 0xF8
410236  return 2
```

Same shape as the search circuit, with a **quarter-turn** base step instead of a
third, no altitude call (it keeps whatever the take-off left it at), and a
radius that depends on whether the guard is armed.

**`unit+0x110` bit 31 is "this unit's definition names a weapon".** It is
copied out of the definition at `0x485AAD`–`0x485AC5`, where
`(def+0x241 & 0xFFFF0000) << 15` keeps bit 16 alone and everything above it
shifts out of the register; and `def+0x241` bit 16 is set at `0x42CF19` unless
all three of `def+0x1EE`, `+0x1F2` and `+0x1F6` — the weapon-1, -2 and -3
definitions — are null. The test has to be there, because the armed branch
dereferences weapon slot 0's definition and an unarmed unit has none.

So **a construction aircraft guarding a factory works a ring of 320 world units
around it, twenty tiles across, moving 90° to 135° round every time it arrives**,
with a 128-unit arrival tolerance so it really does fly to each point. That is
the reported milling about, and it is a good deal larger than it looks in the
disassembly: `0x1400000` is 320, not 20 — `0x140000` is the 20 that `AirToAir`
uses, and that 20 turns out not to be a hop at all but the length of the probe
vectors in a facing test. See §90.

An armed guard — a Brawler told to guard something — works a much wider ring,
`370 + 160 = 530`.

### 4. Constants

| Value | Meaning | Address |
|---|---|---|
| `0x5555 + rand(0x2000)` | **120°–165°**, the search circuit's bearing step, subtracted | `0x410625`–`0x41063B` |
| `0x4000 + rand(0x2000)` | **90°–135°**, the guard circuit's bearing step, subtracted | `0x410142`–`0x410158` |
| `rand(0x10000)` | the opening bearing of either circuit | `0x410774`, `0x410310` |
| `+0xA0` | **160** added to weapon 0's range for the circuit radius | `0x41064B`, `0x410175` |
| `0x1400000` | **320**, the circuit radius for a unit with no weapon | `0x4101B0` |
| `0x80` | **128**, both circuits' arrival tolerance | `0x4106C4`, `0x410211` |
| `rand(30) + 30` | the search circuit's wake timer | `0x4106D8` |
| `0x1E` | **30 ticks**, the guard circuit's wake timer | `0x410225` |
| `rand(0x20) + 8` | **8–39**, `VTOL_Standby`'s hop radius | `0x40F857`–`0x40F861` |
| `rand(15) + 30` | `VTOL_Standby`'s hop timer | `0x40F8D4` |
| `cruisealt` | `VTOL_Standby`'s hop altitude, full rather than half | `0x40F8BD` |
| `(maxdamage >> 2) * 3` | 75 % health, the search circuit's go-home threshold | `0x410518` |
| `0xF00` | **3840**, its repair-pad search radius | `0x410558` |

### 5. What RWE does with this

Implemented, in `UnitBehaviorService`:

- `UnitState::AirLoiterState` — an anchor, a bearing, and which of the two
  circuits it is. `beginAirLoiter` takes the opening bearing from `sim->rng`;
  `flyAirLoiterCircuit` puts the goal on the ring, steps the bearing when the
  aircraft is inside 128 of it, and hands the point to `AirMovementStateFlying`
  directly, the same way the construction aircraft's work pattern does.
- `attackTargetAir` arms the `AttackEnded` circuit on the tick the target goes
  away, anchored on the aircraft's own position, and the idle path flies it
  from the next tick instead of looking for somewhere to land. The circuit
  takes over from an attack run or from a gunship's ring as well as from level
  flight; the aircraft really will be in one of those when its target dies, and
  a handover that only works from level flight leaves it stuck.
- `handleGuardOrder` flies the `Guarding` circuit around a guarded unit that has
  no work to hand, at weapon range plus 160 or 320 for something unarmed, and
  asks a grounded aircraft to take off first. It used to do nothing at all once
  the guard was within 200 units, which is why the planes hung still.
- Any order other than `Guard` clears the circuit, so an aircraft that has been
  given something else to do goes home afterwards as `VTOL_Standby` would; and a
  cancelled guard clears it on the first idle tick for the same reason.

Decoded here and deliberately **not** ported:

- **The sight-range search `0x43B700`**, which is what both `VTOL_SeekAttack`
  state 1 and `VTOL_Standby` state 1 do before they fly anywhere. RWE has no
  equivalent — see §90 — and its own idle weapon acquisition stands in.
- **The go-home-when-hurt branch** is now ported — see §94. This bullet used to
  say RWE had no air repair pads and nothing to fly to; it has both.
- **`VTOL_Standby`'s carrying-something hop** (8–39 units around the idle spot,
  every 30–44 ticks). It only applies to a transport with units aboard, and RWE
  parks a loaded transport rather than fidgeting.
- **`VTOL_Follow`'s work copying** — the `0x43F0E0` order build at `0x40FE86`
  and the mission clone at `0x40FEDC`. RWE's guard already assists a builder and
  a factory through `handleGuardOrder`'s own branches; what the original does
  that RWE does not is copy a *reclaim*, *repair* or *capture* the guarded unit
  is engaged on.
- **The wake timers.** Both circuits re-enter on a timer as well as on arrival,
  but a timer wake re-installs the same goal at the same bearing, so it changes
  nothing geometrically. RWE checks arrival every tick instead.
- **The off-map recovery** (`0x41041D`, 800 units back toward the map centre),
  which shares the unconfirmed reading recorded in the missions document §11.

One correction to `docs/REVERSE-ENGINEERING-PRIORITIES.md`, entry 6: it says
`DefaultMissionType` is "left decoded but unported" because "`Standby` versus
`Guard_NoMove` is only the question of whether an idle unit walks off to find a
fight". That is true of the two ground missions but not of the third value in
the data. `VTOL_Standby` also decides whether an idle aircraft hops about or
goes and lands, on `unit+0x8A`, and that half of it does not need `0x43B700` at
all.

## 14. Recoil

Only some units rock when they fire, and the amount they rock by is a constant.

### The engine asks every unit; the script decides

There are exactly four places in the image that push the string `"RockUnit"`
(`0x5096F8`), and three of them are live: `0x49CBE6`, `0x49CDA1` and
`0x49CFC5`, one in each of the three projectile spawn routines `0x49C9C0`
(line-of-sight and self-propelled), `0x49CC20` (vertical launch) and `0x49CDE0`
(ballistic). The dispatcher at `0x49D0C0` picks between them off the weapon
flags at `wdef+0x111`, and a weapon that is none of those three spawns nothing
at all.

The fourth push, at `0x499C57`, is inside `0x499C10`, which is byte-for-byte
the same routine as the block in the other three and is **dead in this build**:
no `call` resolves to it and `xref.py` finds no absolute reference either.

**Bombs do not rock the unit.** `0x49DD60`, the fire handler a `dropped` weapon
gets, builds its projectile by calling `0x49C740` directly instead of going
through one of the three spawn routines, and never pushes `"RockUnit"`.

All three live sites do the same thing, immediately after starting the weapon's
own `FirePrimary`/`FireSecondary`/`FireTertiary` (the name table at
`0x509678`, started through `0x4B0940`):

```
si  = slot[0x16] - unit[0x66]        ; the shot's bearing in the hull's frame
edi = -(0x320 * sin(si))             ; 0x4B70EF
eax = -(0x320 * cos(si))             ; 0x4B7123
StartScriptByName("RockUnit", 0, 0, 2, eax, edi, 0, 0)   ; 0x4B0A70
```

So `RockUnit(anglex, anglez)` is called with `anglex = -800·cos(bearing)` and
`anglez = -800·sin(bearing)`. `slot+0x16` is the heading the fire handler
worked out towards the target (§11), so the bearing is measured from the hull's
nose and the hull heels away from wherever the round went: firing forwards
lifts the nose, firing off the beam lifts that flank.

`0x4B0A70` looks the name up in the script's own function-name table and, when
it is not there, hands `-1` to `0x4B0B00`, which returns without starting
anything (`0x4B08C0` rejects a negative index at `0x4B08C8`). **A unit with no
`RockUnit` in its COB therefore does not rock, and that is the entire filter.**
The same routine also caps a unit at eight concurrent COB threads (`0x4B08E3`).

### The angle is 800, for every weapon and every unit

`0x320` is a literal at all three sites — `0x49CB9C`, `0x49CD57`, `0x49CF7B` —
and nothing scales it. Not the weapon, not its damage, not the unit's mass. In
COB's 16-bit turn units that is about 4.4°.

### Which units have a RockUnit

Nine of the 157 scripts in `totala1` and seventeen of the 200 in `rev31`:
ARMBULL, ARMCROC, ARMFHLT, ARMFRT, ARMLATNK, ARMMANNI, ARMMART, ARMSTUMP,
CORFHLT, CORFRT, CORGOL, CORLEVLR, CORMART, CORRAID, CORREAP, CORSEAL,
CORSENT. The nine in the original release are that list without the Core
Contingency additions.

The scripts do not call it themselves; they only `#include "rockunit.h"`, which
supplies the body:

```
RockUnit(anglex, anglez)
{
    turn base to x-axis anglex speed <50>;
    turn base to z-axis anglez speed <50>;
    wait-for-turn base around z-axis;
    wait-for-turn base around x-axis;
    turn base to z-axis <0> speed <20>;
    turn base to x-axis <0> speed <20>;
}
```

There is a second header, `rockwater.h` (also `water2.h`, `WATER3.H`), with the
same signature and a five-stage overshoot-and-settle for floating structures.
Either way the engine's side is identical: it does not know or care which one
the script got.

### Barrel recoil is a separate thing, and it is entirely script-driven

The gun sliding back in its mount is not this. It is in the unit's own fire
script — `move barrel to z-axis [-2.4] speed [500]` and a slow `move` back in
ARMSTUMP's `FirePrimary` — so an engine that runs the fire script gets it for
free with no engine support at all. RWE runs the fire script already; there was
nothing to add.

### What RWE does

`UnitBehaviorService::tryFireWeapon` starts `RockUnit` after every non-bomb
shot with the pair of angles from `computeRockUnitAngles`, which matches the
original's convention, and `CobEnvironment::createThread(name, ...)` returns
`std::nullopt` and starts nothing when the script has no such function, which
matches `0x4B0A70`.

**RWE used to invent the angle from the weapon's damage**, `clamp(120 + 3·dmg,
120, 900)`, on the reasoning that a machine gun and a heavy cannon should not
heave a hull equally. The original disagrees: it is 800 for both. That is now
`rockUnitAngle` in `UnitBehaviorService_util.h` and the damage lookup is gone.

Not ported, and small: the original starts the *fire* script inside the
projectile spawn routine, so a burst weapon runs `FirePrimary` once per
projectile, where RWE runs it once per burst.

---

## 15. Thermal vents

### What a vent is

A plain map feature with `geothermal=1` in its TDF, which the feature parser
turns into **bit 5 of the word at `featdef+0xFE`** — the key string `0x502D48`
is pushed at `0x422B2F`, read at `0x422B3E`, and `and eax,1 / shl eax,5` at
`0x422B4A`–`0x422B56` puts it in place. The neighbouring bits, worth having
because the same flag word gates the rest of this section:

| Bit | Key | | Bit | Key |
|---|---|---|---|---|
| 0 | *(no `object=` key: a 2-D sprite feature)* | | 6 | `blocking` |
| 1 | `animating` | | 7 | `reclaimable` |
| 2 | `animtrans` | | 8 | `autoreclaimable` |
| 3 | `shadtrans` | | 9 | `indestructible` |
| 4 | `flamable` | | 10 | `nodisplayinfo` |
| 5 | **`geothermal`** | | | |

Bit 0 is not a key: `0x4225FB` tests whether the section named an `object`, and
sets the bit when it did not.

Every vent in the shipped data is written the same way — a 1×1 (2×2 for the
Urban manholes) `animating=1` sprite with `geothermal=1`, `indestructible=1`,
`hitdensity=0` and no `blocking` — and in the original release `animating=1` is
set on **vents and nothing else**; `rev31` adds one non-vent, the acid world's
Gasbag.

### The steam is engine particles, not the feature's animation

It is tempting to read `animating=1` as the steam, and it is not: every entry
in every vent GAF (`VENTS.GAF`, `greenvents.GAF`, `WETVENTS.GAF`, …) has
**exactly one frame**. The sprite is a static hole in the ground.

The steam is a dedicated emitter the engine builds when the feature is placed.
`0x423C50`, the feature placement routine, tests the geothermal bit at
`0x423F73` (`mov cl,[esi+0xfe] / shr cl,5 / test cl,1` — which is why a grep
for `test byte ptr [x+0xfe],0x20` finds only the placement check of the last
part of this section) and calls `0x472C50` at `0x423FE3` with **particle layer
4**.

`0x472C50` allocates a 52-byte emitter, constructs it at `0x4750B0` (vtable
`0x4FD638`) and files it in the layer. Each of the ten layers of §5 holds at
most 400 entries and drops its oldest to make room (`0x472CD9`).

The class is laid out exactly like the smoke emitter of §4, and the two share
the puff record and most of the behaviour:

| | vent (`0x4FD638`) | damage smoke (`0x4FD618`) |
|---|---|---|
| constructor | `0x4750B0` | `0x474CD0` |
| `Init` | `0x475150` | `0x474D50` |
| emit one puff | `0x4751C0` | `0x474DF0` |
| per-tick update | `0x475600` | `0x475340` |
| is it due to emit | `0x4750F0` | `0x475440` |
| is it finished | `0x475330` | `0x474F80` |

`Init(position, 5, 0, 150)`:

- **The sequence is `smoke 1`** out of `anims/FX.GAF` — `globals+0x147CF`,
  loaded by the §4 slot map, read at `0x475181`.
- **The interval is 5 ticks** (`this+0x1C`, `0x475175`), and one puff goes out
  immediately from `Init` itself (`0x4751B2`).
- The frame period argument is 0, so it defaults to 7 (`0x4751AB`), the same as
  a damage puff.
- The 150 is not a lifetime. **`isFinished` at `0x475330` is `xor eax,eax /
  ret`** — a vent's emitter is immortal and puffs every five ticks for the rest
  of the game. All 150 does is size the puff vector's first reservation
  (`0x4751DA`).

Position: when the caller hands `0x423C50` no explicit one, `0x423F8D` computes
`(2·cell + footprint) << 19` for x and z and takes the ground height between
them, which is the centre of the feature's footprint on the ground. **The puffs
carry no spread** — `0x4752A5` copies the emitter's position into the puff
unchanged — so the plume leaves from one spot. (The burning-feature smoke
below does scatter; the vent does not.)

Per puff, identical to §4: the stopping frame is drawn at birth as
`2 + rand·(frameCount − 3)/0x8000`, so 2 to 10 of `smoke 1`'s twelve; the first
frame is held the full 7 ticks and every frame after it
`half + rand·half/0x8000`, so 3 to 5.

Per tick, at `0x475620`–`0x475660`, also identical to §4 **except for one
number**: `x += windX × 8`, `z += windZ × 8`, and

- `y += gravity × 16` (`0x475640`), where damage smoke uses `× 4` (`0x475380`).

**A vent's steam rises four times as fast as a damaged unit's smoke.** That is
the whole visual difference between the two.

### While decoding this: what makes a burning tree smoke

Not needed for vents but found on the way, and it fills in a gap in §4, whose
claim that every call site of every smoke spawner is accounted for is not quite
right — `0x472810` has six, not one. One of the five §4 does not list is
`0x4243CF`.

The per-tick loop at `0x4241A8` walks the 0x800-entry, 48-byte-stride array at
`globals+0x1420B` through the list head at `globals+0x14213`. A record is
either a falling 3-D feature (physics, `0x424214`) or a 2-D sprite feature
playing an animation, and the sprite ones that have bit 0 of `instance+0x2F`
set are *burning*: `0x4233A0` is what sets it, and the same routine plays the
`treeburn` sound (`0x502EE8`). A burning feature emits one white-smoke puff
**every third tick** (`0x4241B6`–`0x4241C7` divides the game time by 3), at a
random point in the middle half of its sprite (`0x424345`–`0x4243B5`), on
**layer 5**. When its burn animation runs out it is replaced by the feature
named at `featdef+0xF6`.

### Placement: what makes a spot legal for a geothermal plant

The footprint check is `0x47D4B6`–`0x47D769`, walking the unit's parsed yardmap
(one byte per cell at `unitdef+0x14E`) against the map cells (13 bytes each).
The yardmap byte's bits:

| Bit | What it checks |
|---|---|
| `0x01` | terrain flag `cell+0xC & 2` |
| `0x02`/`0x04` | occupied by another unit |
| `0x08` | contributes to the footprint's min/max ground height |
| `0x10` | contributes to the water clamp |
| `0x20` | the feature under it must not be `blocking` (bit 6) |
| `0x40` | the feature under it must not be `indestructible` (bit 9) |
| `0x80` | **geo**: note that a vent is wanted, and whether one is here |

The geo bit is the only one that does not reject on its own. At `0x47D68F` it
raises "this unit wants a vent", and at `0x47D708`–`0x47D711`, if the feature
occupying that cell has the geothermal bit, it raises "found one". The verdict
is at the end, `0x47D75B`–`0x47D769`: **a unit that wanted a vent and did not
find one is refused; one that never asked is not checked at all.** One matching
cell is enough — it does not require every geo cell in the yardmap to be over a
vent.

### What RWE does

Already right, and now checked against the binary rather than assumed:
`geothermal` is parsed into `FeatureDefinition`, `GameSimulation::addFeature`
stamps `geoGrid` over the feature's footprint, and `canBeBuiltAt` refuses a
`yardMapContainsGeo` unit unless `containsAnyGeoMatch` finds one of its `Geo`
cells over a stamped tile — the same "one match is enough" rule the original
uses. `computeFeaturePosition` already puts a feature at the centre of its
footprint on the ground, which is the point `0x423F8D` computes.

Added here: `GameScene::spawnGeoVentSteam` puts out one `smoke 1` puff per vent
every `geoVentSteamIntervalTicks` = 5 ticks at `geoVentSteamRiseRate` = 2 world
units of lift, which is the original's gravity × 16 on the 112 nearly every map
ships with, against the 0.5 the rest of RWE's smoke uses for × 4. This is
render-side and uses `rand()` through the existing `spawnSmokePuff`, because
the original's vent steam is not simulation: nothing about it feeds back into
the game, the emitter is not part of unit or feature state, and the trigger is
a plain tick count rather than a script or a random roll. Nothing was added to
`GameHash_util.cpp` or `dump_util.cpp` for the same reason.

Not ported:

- **The layer.** RWE depth-tests particles against world Y rather than placing
  them in ten hand-ordered buckets (§5), so the vent's layer 4 has no
  equivalent. In practice this puts the steam behind a geothermal plant built
  over it either way.
- **The `blocking`/`indestructible` guard on the geo grid.** RWE only stamps
  `geoGrid` for a feature that is non-blocking *and* indestructible as well as
  geothermal; `0x47D708` tests the geothermal bit alone. Every vent in the
  shipped data is both, so the two agree on real data, and the extra conditions
  also guard the metal grid next to it.
- **The burning-feature smoke** above, and the `treeburn` sound with it.
- **Downwind drift**, as §90 already records for the rest of the smoke: RWE has
  no map wind, so a vent's plume goes straight up.

---

## 16. Wakes, thrust, and the small unit flags

The five `emit-sfx` types RWE had never implemented. Wakes are behind every ship
in the game, so this is the most visible of the remaining gaps.

### The dispatch, `0x480EB0`

`EmitSfx(pieceIndex, sfxType)` takes two arguments. Before it does anything else
it asks whether the local player can see the unit (`0x465AC0`, tested at
`0x480EEA`) and **returns doing nothing if not** — so the whole SFX system is
client-side and no part of it is simulation state. That matches where RWE
already puts it.

It then takes the emitting piece's **first two transformed vertices**, adds each
to the unit's world position with the z negated (`0x480F59`–`0x480FD8`), and
jumps through the table at `0x481128`:

| Id | Type | Handler | Spawner |
|---|---|---|---|
| 0 | `Vtol` | `0x481000` | `0x472330` |
| 1 | `Thrust` | `0x48101F` | `0x472330` |
| 2 | `Wake1` | `0x48103E` | `0x472430` |
| 3 | `Wake2` | `0x48105B` | `0x472430` |
| 4 | `ReverseWake1` | `0x481078` | `0x472430` |
| 5 | `ReverseWake2` | `0x481095` | `0x472430` |

`SFXTYPE_POINTBASED` (256) falls off the end of the dispatch and does nothing at
all.

### All four wakes are one routine with two knobs

The four call sites differ in **exactly two** things: which of the two vertices
is pushed first, and whether the second argument is 16 or 8.

- `Wake1` → `(vertex0, vertex1, 16)`, `Wake2` → `(vertex0, vertex1, 8)`
- `ReverseWake1` → `(vertex1, vertex0, 16)`, `ReverseWake2` → `(vertex1, vertex0, 8)`

So **"reverse" is nothing but swapping the two vertices**, which turns the drift
round. There is no test on speed, on throttle, or on the direction of travel
anywhere in the engine — which of the four a ship uses is entirely its script's
decision, and the script knows because the engine tells it through
`setSFXoccupy` (see the `waterline` section).

`Init` at `0x474760` normalises the difference between the two points and scales
it by 32768 in 16.16 (`0x4747CC`–`0x474819`), so a dot drifts **half a world
unit a tick whatever the distance between the vertices** — a modeller putting
them further apart makes no difference to the speed. The `16`/`8` is the colour
ramp period, and the life is six of those, 96 or 48 ticks. So a Wake1 dot
travels 48 world units in its life and a Wake2 dot 24.

Each call puts out **two dots: one immediately and one on the following tick**
(`0x474872` emits, then `0x474AEE` sets the next due time to now + 1, and the
emitter's end time is also now + 1, so it fires once more and never again).
There is no cooldown; the repetition rate is whatever `sleep` the ship's script
uses.

The spawn point is jittered by `rand()*7/0x8000 − 3` — a **whole number of world
units in [−3, +3], drawn independently for x, y and z** (`0x4749D9`, `0x474A00`,
`0x474A27`). Note the y: the scatter is not confined to the horizontal.

Per tick (`0x474580`) a dot moves by its velocity with **no gravity and no
wind**, and steps its palette index one place every `rampPeriod` ticks. It dies
either at its end time or, per `0x474720`, **the moment the terrain under it is
at or above sea level** — which is what makes a wake stop cleanly at a shoreline
instead of running up the beach behind the ship.

### `Thrust` is `Vtol` with one number changed

Both go to the spawner at `0x472330` with the vtable at `0x4FD5D8`, on render
layer 7, and the only difference is the constant: **6 for `Vtol`, 7 for
`Thrust`** (`0x481000` versus `0x48101F`). That number is used twice in `Init`
at `0x4742C0` — as the divisor for the per-tick drift (`0x47432C`–`0x47438B`)
and as the emitter's own lifetime (`0x4742CB`) — so a thrust plume is one puff
longer and each puff moves slightly more slowly. Same `flamestream` sequence,
same layer, same class.

### A correction to §4

§4 records that the wake emitter draws `smoke 1`, on the strength of the palette
ramp being loaded at `0x474A7F`. The handle **is** written into the puff at
`+0x00`, but none of the three routines that touch a wake puff — the per-tick
step `0x474580`, the render `0x4745E0` or the death test `0x474720` — ever reads
it. `0x4745E0` builds a one-pixel rectangle and calls `0x4BF6F0`, the generic
palette-index rectangle fill, exactly as the nanolathe spray does at `0x473B1D`.
**A wake dot is a single screen pixel of a solid palette colour, not a sprite.**

§4's slot map is unaffected — the ordering argument for `smoke 1` at
`globals+0x147CF` stands on its own from the loader at `0x429879`–`0x4298B8`.
Only the corroborating remark about the wake using it is wrong.

The colours are palette entries **97–103**, seven water blues, stepped from
palest to deepest: `(203,227,255)`, `(175,207,255)`, `(151,179,255)`,
`(123,151,255)`, `(103,127,255)`, `(83,107,239)`, `(63,91,227)`. Read out of the
shipped `palettes/PALETTE.PAL`; entry 161 in the same file is `(171,231,127)`,
which matches the nanolathe colour already in RWE and confirms the format.
Submarine bubbles use the same class and the same seven entries walked the other
way, starting at 103 with a step of −1 (`0x472584`, `0x4725A2`).

### What RWE now does

All five types are routed. Wakes are one function taking a reverse flag and a
ramp period; `Thrust` is the existing VTOL emitter with the divisor passed in.
The dot's colour steps discretely along the seven blues instead of fading, its
drift is the original's half a unit a tick instead of a guessed third, its life
is 96 or 48 ticks instead of a guessed 120, the jitter is whole units on all
three axes instead of fractional ones on two, the second dot is laid a tick
later rather than alongside the first, and a dot now dies when it drifts over
land.

Two things are left alone. RWE draws its dot as a small world-space quad rather
than a literal screen pixel, which is the same choice it already made for the
nanolathe spray. And the emission rate is still the script's, which is the
original's behaviour anyway.

### The small unit flags

Six flags that the bit tables above name but that had never been followed to
their readers. The parse sites are all in the FBI parser's long run of boolean
keys, where the helper `0x4C46C0` leaves its answer in `eax` and the
mask-and-shift follows immediately, so the §82 pipeline trap does not apply and
each bit is unambiguous:

| Key | String | Read at | Shifted at | Bit |
|---|---|---|---|---|
| `isairbase` | `0x503BC4` | `0x42C5DC` | `0x42C5F1` `shl eax,0x9` | `def+0x241` 9 |
| `noshadow` | `0x503B24` | `0x42C7DC` | `0x42C7EA` `shl eax,0x19` | `def+0x241` 25 |
| `digger` | `0x503AF0` | `0x42C880` | `0x42C895` `shl eax,0x1e` | `def+0x241` 30 |
| `upright` | `0x503B50` | — | `0x42C746` | `def+0x241` 20 |
| `norestrict` | `0x503620` | — | `0x42CB4B` | `def+0x245` 15 |
| `cantbetransported` | `0x5039DC` | `0x42CBB1` | `0x42CBBF` `shl eax,0x13` | `def+0x245` 19 |

Counted over the 189 FBIs of the extracted data RWE actually loads, which is
the base game plus Core Contingency: `isairbase` 4 (ARMASP, CORASP, ARMCARRY,
CORCARRY), `noshadow` 15, `upright` 28 set and 3 explicitly cleared,
`norestrict` 6, `digger` 2 (ARMAMB, CORTOAST), `cantbetransported` 1
(CORSUMO). The last two are worth the correction: earlier notes recorded that
no shipped unit sets either, which is true of the base game on its own and not
of the data on disk.

### `isairbase`: a damaged aircraft goes home

The definition bit is cached onto the instance as a unit is set up.
`0x485AE7`–`0x485B03` takes `def+0x241 & 0x200`, shifts it left twenty-one and
drops it into a cleared bit 30 of `unit+0x110`.

Each player keeps a **list** of its own air bases rather than searching for
them. `0x40AB96` onwards is the per-player sweep that also counts units by
type, and three tests put a unit on the list at `0x40ABE4`:

```
40abc0  mov  eax,[def+0x241]
40abc6  test al,0x40                ; builder
40abca  test ah,0x2                 ; isairbase
40abcf  test BYTE [unit+0x10e],bl   ; bl is 1, set at 0x40AAD0
40abe4  call 0x408F30               ; push_back onto the vector at player+0x25
```

**`unit+0x10E` bit 0 is the on/off state.** `0x403010` ACTIVATE calls
`0x48B090(1, 1)` and `0x403040` DEACTIVATE calls `0x48B090(1, 0)`; that routine
writes the byte at `0x48B0D8`, and unit init zeroes it at `0x485B7F`. The same
byte and bit gates the `istargetingupgrade` accumulator two instructions later
at `0x40ABFE`, which is what settles that it means "switched on" rather than
anything to do with visibility or ownership. So a pad that has been turned off,
or one still going up and so never activated, is on nobody's list and nothing
lands on it.

The query is `0x40B530(playerIndex, position, radius, out)`. It walks that
player's list, repeats the same three tests at `0x40B578`/`0x40B580`/`0x40B589`
— the last against a literal `0x1`, which is what pins the bit — and keeps
every unit inside the radius. The distance is flat: `0x40B5A0` reads only the
x at `unit+0x6A` and the z at `unit+0x72`, multiplies each difference by itself
as a 64-bit product and shifts thirty-two off (`0x40B5BB`–`0x40B5E3`), so the
comparison at `0x40B5E9` is in whole world units squared. **`y` is never
read**, so an aircraft is not pushed out of range by its own cruise altitude.

Seven callers, and **all seven pass the same two numbers**: `0x41055F`,
`0x4109B9`, `0x410F95`, `0x412613`, `0x412B91`, `0x413ADC`, `0x4153E3`. Each is
preceded by the identical health gate,

```
410525  mov  eax,[def+0x1fa]        ; max hit points
41052b  shr  eax,0x2
41052e  lea  ecx,[eax+eax*2]        ; (max >> 2) * 3
410531  cmp  edx,ecx                ; edx is WORD [unit+0x108], current health
410533  jae  <skip>
```

so the threshold is **three quarters of maximum with the quarter truncated
first**, and an aircraft sitting exactly on it is not hurt enough. The radius
pushed is `0xf00` at every site and the query squares it at `0x40B542`:
**3840 world units**.

If the returned vector is not empty — `0x40C560` is its size — the caller picks
**one at random, not the nearest**. `0x4B6C30(n)` returns zero without touching
the generator at `0x51FC88` when `n` is below two, and otherwise steps it once
and takes the remainder (`0x4B6C8C div edi`, `0x4B6C90 mov eax,edx`). The
chosen unit becomes the target of a mission built from the string at
`0x501B94`, **`VTOL_LANDING`** (`0x4105B9`, `0x41267B`).

Four more readers hang off the same flag:

- **On arrival**, `0x411E63`–`0x411ED3`: if the aircraft is below full health
  (`WORD unit+0x108 < DWORD def+0x1FA`) and the mission's target is a
  `builder && isairbase` whose build progress `unit+0x104` is exactly the float
  at `0x4FCC40`, the aircraft is handed a further mission built from
  `0x501C24`, **`SELFREPAIR`**.
- **The cursor**, `0x43EA83`: the selected unit `canfly` (bit 11) with an
  `isairbase` unit under the pointer gives cursor 13.
- **The right-click**, `0x43F735`–`0x43F75E`, `0x43F959`–`0x43F977` and
  `0x43FAEF`–`0x43FB19`: three separate arms of the order dispatcher test the
  same pair and all jump to `0x43FB1B`, the `VTOL_LANDING` mission.
- **Cargo**, `0x48AD0D`–`0x48AD1F`: a unit taken aboard something is unlinked
  from the world by `0x4384A0` **unless** the thing holding it is an air base,
  which is why aircraft parked on a carrier stay selectable.

### `cantbetransported`, and the whole of `CanTransport`

`0x489A90(this = transport, candidate)` in order:

```
489aa3  candidate def+0x245 bit 19  -> cantbetransported: reject
489abe  transport def+0x245 bit 8   -> canload required
489acb  walk transport+0x8A, next at +0x8E, owner back-pointer at +0x86,
489aec  counting against BYTE transportdef+0x22B; >= rejects
489afe  candidate must still exist
489b0b  WORD candidatedef+0x14A <= BYTE transportdef+0x22A
489b24  candidate unit+0x110 & 3 == 2 -> airborne: reject
489b39  a transport that cannot fly also wants WORD candidatedef+0x1C0 < 0
```

Two offsets fall out of that and both need a second site, because the FBI
parser pipelines its stores. `def+0x22A` and `def+0x22B` come from the same run
of keys as `waterline`: the strings pushed are `0x503DC8` "waterline",
`0x503DB8` "transportsize" and `0x503DA4` "transportcapacity", and under the
§82 rule their values land at `0x42C259`, `0x42C26E` and `0x42C284`. That makes
`def+0x22C` `waterline`, which §88 already had from elsewhere and which is the
check that the pipeline is being read the right way round, `def+0x22A`
**`transportsize`**, and `def+0x22B` **`transportcapacity`**.

`def+0x14A` is not parsed from the definition at all. `0x42CD5D` copies
`WORD [+0x4]` and `WORD [+0x6]` out of the **movement class** record into
`def+0x14A` and `def+0x14C`, and the record's fallback builder `0x440340` —
used when the FBI names no movement class — fills `+0x4` from the key at
`0x505484`, `FootPrintX`, and `+0x6` from `0x505478`, `FootPrintZ`, each store
immediately after its own call rather than pipelined. So the size test is
**the candidate's footprint X against the transport's TransportSize**.

That listing stops three checks early, and one conclusion drawn from it was
wrong: the full decode (§30) finds a mobility test after the airborne one --
the candidate's mover pointer at `unit+0x00` must be non-null, so buildings
never ride -- then a submerged test (the candidate's top, `y` plus the model
height at `def+0x16E`, must be above sea level) and a fully-built test. What
remains true: nothing asks whether the candidate is an *aircraft*, only
whether it is airborne right now, so a landed plane may be carried.

### `noshadow` and `digger` are both shadow-pass flags

Two passes, `0x459288`–`0x4592B6` and `0x4594A2`–`0x4594CA`, and they open
identically: a global option `WORD [0x511DE8+0x37F06] & 4`, then bit 25 of
`def+0x241`, then skip the draw. `digger` is tested only in the second, at
`0x4594D0`, where it selects a path through `0x45A470` and changes one
constant:

```
4594f2  shr eax,0x1e / and al,0x1
4594f7  neg al / sbb eax,eax        ; 0 or -1
4594fb  and eax,0x4b                ; 0 or 75
4594fe  add eax,0x32                ; 50 or 125
```

**It is not a terrain flag.** All ten of its read sites are in that one pass.

### `upright` and `norestrict`

`upright` has exactly one reader, `0x48A8BF`, inside the per-tick ground
placement `0x48A870`. Set, the unit stays vertical and takes its height from a
single sample under the centre; clear, `0x48A8CD` falls through to `0x48A938`
and thence `0x48A490`, which samples four rotated footprint corners and writes
`WORD unit+0x68` pitch and `WORD unit+0x64` roll from the slope.

`norestrict`'s four readers — `0x44C15F`, `0x44C4EA`, `0x44C73A`, `0x44CA59` —
are all the Unit Restrictions screen, which skips definitions carrying the bit
so a host cannot switch them off.

### What RWE now does with them

- **`isairbase` is in.** `findAirBaseToLandOn` in `UnitBehaviorService_util.cpp`
  is the original's predicate and query together: below three quarters health
  with the quarter truncated first, the owner's own `Builder` `IsAirBase` units
  that are activated and alive, within 3840 flat world units, and a draw taken
  with a modulo that skips the generator entirely when there is only one
  candidate — which every peer has to agree about or the simulations part
  company. The choice is remembered in `NavigationStateMovingToLandingSpot`,
  which is what stops it being re-rolled every tick the way the original's
  one-off mission swap does.
- **`cantbetransported` is in**, in the load handler and in the AI's ferry
  candidate filter, so the AI cannot book a passenger the simulation will then
  refuse and leave the transport hovering over it for ever.
- **`noshadow` is in**, as `unitCastsShadow` in `GameScene_util.cpp`.

And what was already right: RWE's transport eligibility had the capacity count,
the alive test and a size test, and `CanLoad` already gated the order button.
Its size test measures `max(footprintX, footprintZ)` where the original
measures footprint X alone — of the twelve shipped units with a non-square
footprint every one is a building, so the two readings never disagree on real
data, and the difference has been left where it is. RWE also refuses a
candidate that is not mobile, that `canFly`, or that is itself a transport,
none of which the original asks; those are older choices and were left alone
too.

### Not ported

- **`digger`.** RWE has both shadow passes now (§100), but the constant the
  flag swaps is not a projection constant at all: 50 and 125 are the height
  buffer's bias, `height = modelY + (digger ? 125 : 50)`, which
  `TOTALA-EXE-SHADING.md` §22 reads out of the span filler. RWE has no
  per-drawable height buffer — it has a real depth buffer — so there is still
  nothing for the flag to select.
- **`upright`.** RWE does not conform anything to the ground: `UnitState`
  carries a `roll` for the aircraft bank and no pitch at all. The flag chooses
  between two ground-placement routines neither of which RWE has, so honouring
  it would mean writing terrain conforming first.
- **`norestrict`.** There is no Unit Restrictions screen for it to hide a unit
  from.
- **The `VTOL_LANDING` cursor and the right-click.** RWE's `CursorType` has no
  landing cursor and no sprite loaded for one, and a player cannot yet send an
  aircraft to a pad by hand — only the idle path does. The three dispatcher
  arms above are where that would go.
- **`SELFREPAIR`.** What the pad does once the aircraft is on it was not
  followed past the mission push at `0x411ECE`, so RWE's pads still mend
  nothing. Everything that gets the aircraft there is in; this is the piece
  left.
- **The air base list itself.** RWE walks the unit map where the original keeps
  a per-player vector. The answer is the same and the walk only runs while a
  damaged aircraft has nothing else to do.

## 17. Jamming, stealth and cloaking

Three keys that change who can see or shoot whom. They are one section because
the original answers all three in one routine — the per-tick visibility pass at
`0x467440` — and because all three land in the same place in RWE: the radar
query that `GameSimulation::updateVisibility` builds and `canDetectUnit` reads.

### The field offsets

Read off the FBI parser under the pipeline rule §82 describes — a key's value is
stored *after the next key's push* — and cross-checked against the
definition-copy routine at `0x42B68F`–`0x42B6DF`, which moves the same run of
words with the same widths.

| FBI key | Offset | Pushed at | Stored at |
|---|---|---|---|
| `sightdistance` | `WORD def+0x202` | `0x42C395` | `0x42C3B3` |
| `radardistance` | `WORD def+0x204` | `0x42C3AA` | `0x42C3C9` |
| `sonardistance` | `WORD def+0x206` | `0x42C3C0` | `0x42C3DF` |
| `mincloakdistance` | `WORD def+0x208` | `0x42C531` | `0x42C557` |
| `radardistancejam` | `WORD def+0x20A` | `0x42C3D6` | `0x42C3F5` |
| `sonardistancejam` | `WORD def+0x20C` | `0x42C3EC` | `0x42C40B` |
| `init_cloaked` | `def+0x241` bit 4 | `0x42C466` | `0x42C491` |
| `stealth` | `def+0x241` bit 8 | `0x42C4D8` | `0x42C503` |
| `cloakcost` | float `def+0x1DA` | `0x42C4FE` | `0x42C516` (`fst`) |
| `cloakcostmoving` | float `def+0x1DE` | `0x42C526` | `0x42C542` (`fstp`) |

Two things fall out of that listing that the priorities note did not have.

**`cloakcostmoving` defaults to `cloakcost`.** The `fst` at `0x42C516` leaves the
parsed `cloakcost` on the FPU stack, `0x42C51C` truncates it to an integer, and
`0x42C525` pushes *that* as the default argument for the `cloakcostmoving` read.
A unit with a cost and no moving cost pays the same either way — which is the
Cloakable Fusion Reactor, and it never moves.

**`Cloakable` is not a key.** There is no such string anywhere in the binary.
`0x42CA5A`–`0x42CA96` sets `def+0x245` bit 13 from `cloakcost > 0.0` (the
constant at `0x4FD210` is zero), and that bit is what the Cloak_On mission
handler tests before it will do anything. None of the shipped FBIs writes
`Cloakable` either, so a build that honours only the key gives the button to
nothing. The neighbouring bits in that dword are `canresurrect` 11, `cancapture`
12, `candgun` 14, which settles the ambiguity §B of the priorities note left
there.

`mincloakdistance` defaults to **80** when the FBI is silent: `0x42D135` tests
the stored word for zero and `0x42D13F` writes `0x50`.

### The unit instance

| Field | Meaning | Settled by |
|---|---|---|
| `unit+0xB0` | tick before which cloaking is refused | `0x4676E0`, read `0x4017F8` |
| `unit+0x10E` bit 0 | switched on | `0x48B090` |
| `unit+0x10E` bit 2 | **cloaked** | `0x48B090` |
| `unit+0x110` bits 2–3 | movement rate band, 0 = stopped | `0x43DB2C` |
| `unit+0x110` bit 8 | radar contact | `0x467937` |
| `unit+0x110` bit 9 | sonar contact | `0x4678FF` |
| `unit+0x110` bit 10 | jammed | `0x46796D`, `0x46798D` |
| `unit+0x110` bit 11 | **cloak wanted** | `0x40308E` / `0x4030BE` |
| `unit+0x110` bit 12 | **an enemy is inside `mincloakdistance`** | `0x4676E8` |

Bits 2–3 are the move-rate band rather than anything to do with cloak: the
routine at `0x43DACF`–`0x43DB3D` writes them from the unit's speed band at the
same time as it calls the COB functions `StopMoving` (`0x50523C`), `StartMoving`
(`0x505230`), `MoveRate1` (`0x50520C`) and `MoveRate2` (`0x505218`). Non-zero
therefore means moving, which is how the cloak cost picks between its two
numbers.

### Turning cloak on

Cloak is a **mission**, not a flag the button writes. The ground mission table at
`0x4FC490` holds `Cloak_On` at record `0x4FC4F4` (handler `0x403070`, display
"Cloaking") and `Cloak_Off` at `0x4FC50D` (handler `0x4030A0`, display
"Decloaking"). Both handlers do one thing: check `def+0x245` bit 13 and set or
clear `unit+0x110` bit 11.

A fresh unit is seeded from the definition in the same routine that seeds the
standing orders — `0x485CBC`–`0x485CCF` copies `def+0x241` bit 4 (`init_cloaked`)
into `unit+0x110` bit 11.

### The drain

Inside the per-unit economy routine, `0x4017CB`–`0x401872`:

- the whole block is skipped when the owning player's type byte `player+0x73` is
  3;
- `unit+0x110` bit 11 must be set and bit 12 clear, and `unit+0xB0` must not be
  greater than the current tick (`0x4017F8`, against `[ds:0x511DE8 + 0x38A47]`);
- the cost is `cloakcostmoving` when `unit+0x110 & 0xC` is non-zero and
  `cloakcost` otherwise (`0x401806`), and it is **truncated to a whole number** by
  `0x4E43A0` at `0x401824` before it is spent;
- it is compared against the player's energy stock `player+0x8C` and, if the
  stock covers it, subtracted there and then and added to the unit's own
  energy-used accumulator `unit+0xC0`. If it does not, **nothing is taken and the
  unit simply does not cloak** — there is no partial payment and no stall;
- either way `0x48B090(unit, 4, cloaked)` writes `unit+0x10E` bit 2, which on a
  change fires COB event `0xE` / `0xF` and ORs render flag `0x10000` into every
  piece of the model.

### The visibility pass, `0x467440`

Runs per tick, and computes flags for **one** viewing player — the index it reads
from `ds:0x511DE8 + 0x2A43` at `0x46745B`. Four loops over the unit list.

**Loop 0, `0x467499`.** For every live unit: clear `unit+0x110` bit 12. Then, if
the unit is the viewer's own or an ally sharing vision, set bits 8 and 9;
otherwise clear bits 8, 9 and 10. So the radar picture is rebuilt from nothing
every tick, and a unit you own is never off it.

**Loop 1, `0x467521`.** Each of the viewer's live units that is **switched on**
(`unit+0x10E` bit 0) and names a `radardistance` or a `sonardistance` becomes a
source. It calls `0x47E890(position, max(radar, sonar) << 16, &visitor)`, which
walks a 128-world-unit cell grid over the bounding box and then does a real
circular test — `d² <= r²` in whole world units at `0x47E9E4` — before calling
the visitor. The visitor is `0x467840`, and it:

- skips units belonging to the viewer;
- **skips any unit whose definition sets `stealth`** (`0x467881`–`0x46788C`);
- sets bit 9 when the unit's `y` is at or below sea level and
  `d² <= sonardistance²`;
- sets bit 8 when `y + def+0x16E` (the model's height) is at or above sea level
  and `d² < (radardistance + 2 × the DETECTOR's unit+0x70)²`.

`unit+0x70` is the high word of the 16.16 `y` at `+0x6E`, so it is `floor(y)` in
whole world units — and it is read once per *source* at `0x467565`, before any
contact exists. The visitor never touches a contact's `+0x70`. An earlier
reading of §2 had this as the contact's altitude; it is not.

The lift is also capped, which is easy to miss because the cap is computed in
the caller. `0x46757F`–`0x467593` takes `max(RadarDistance, SonarDistance)`,
shifts it into 16.16, and passes it as the sweep radius, and `0x47E9E4` refuses
to call the visitor outside it. So the effective radius is
`min(lift, max(radar, sonar))` — see §2.

The split explains what sonar is for: a submerged unit is only ever a sonar
contact, a unit standing clear of the water only ever a radar one, and a
half-submerged one can be both.

**Where the two bits are then read**, which is not symmetric and is the whole
of why they are two bits and not one:

- **bit 9 (sonar) is read by `0x465AC0`**, the can-see predicate, at
  `0x465B38`. It is a veto lifted rather than a contact granted: with bit 9
  clear, a unit whose model top (`y + def+0x16E`) is below sea level — the byte
  at `global+0x1427F`, shifted into 16.16 — returns 0 at `0x465B50` without the
  fog grid being consulted. With bit 9 set the unit simply carries on to the
  same line-of-sight test everything else faces. So sonar does not let you
  shoot at something you cannot see; it is what stops a submarine being
  invisible to the destroyer standing over it. Every unit in the shipped data
  that can fight submarines carries sonar reaching at least as far as it can
  shoot — the shortest is `ARMROY`/`CORROY` at 305 against 330 of sight — and a
  sonar station's 1180 buys picture, not targets;
- **bit 8 (radar) is read by nothing in `0x465AC0` at all.** It reaches the
  minimap draw at `0x466DC0`, and it appends to the fallback candidate list in
  `0x40AA40` at `0x40AB3F`, which `0x40AD80` only walks behind the Targeting
  Facility gate — see §17a. A radar contact is a blip; without `ARMTARG` or
  `CORTARG` on the field it is never a target.

**Loop 2, `0x4675EC` — the jammer.** Every live unit **not owned by the viewer**
that is switched on and names a `radardistancejam` calls the same `0x47E890` with
that radius and the visitor at `0x467960`, which clears bit 8 and sets bit 10 on
everything it reaches. `sonardistancejam` does the same through `0x467980` for
bit 9. So:

- a jammer **hides units, it does not fake contacts**. There is no false-blip
  code anywhere near it;
- it hides *everything* inside the bubble, its own side included, not just its
  owner's units — the visitors do not look at who owns what;
- it must be **switched on**; both jammers in the shipped data set `onoffable=1`;
- it does **not** jam its owner's own picture, because the loop skips units owned
  by the viewing player. It does jam an ally's, which reads as a quirk of the
  original rather than a considered decision;
- it runs after the detection loop, so jamming always wins over radar;
- it touches only bits 8 and 9. A jammer does nothing at all to a unit you can
  actually see: line of sight is `unit+0x9C`, and loop 4 sets it afterwards.

**Loop 3, `0x467690` — `mincloakdistance`.** For every live **cloakable** unit
whose owner is of type 1 or 2, `0x40B0D0(owner, &position, mincloakdistance)` asks
whether any live enemy of that owner is within that distance in the map plane. If
one is, `unit+0xB0` is set to the current tick **+ 0x5A (90 ticks, three seconds)**
and `unit+0x110` bit 12 is set. Bit 12 is cleared again at the top of the next
tick's loop 0, so it is the timestamp that does the work: an enemy walking past a
cloaked Commander decloaks it, and it stays decloaked for three seconds after the
enemy leaves. `mincloakdistance` does **not** refuse to cloak in advance and it is
not a range at which anything is revealed — it is a proximity fuse on the unit's
own cloak.

### What a cloaked unit is invisible to

`unit+0x10E` bit 2 is read in exactly five places:

- `0x465AE8` — the "can this viewer see this unit" predicate. Own units pass at
  `0x465AD7`; a cloaked one returns 0 before anything else is looked at;
- `0x40729C` — the computer player's nearest-enemy search (`0x4071F0`), which
  skips cloaked units;
- `0x4390E2`, `0x45937D`, `0x459779` — drawing: the mincloak range ring, and the
  two sprite paths that shade a cloaked unit differently.

It is **not** read by the weapon target scan. Neither `0x40AD80` (gather enemies
in radius), `0x40B7B0` (choose one) nor `0x49ABB0` (can this weapon engage that
unit) looks at it.

An earlier reading of this page concluded from that that "a cloaked unit
standing inside an enemy gun's range is still shot at". **That is wrong, and it
is wrong in the direction that matters.** The scan does not need to read the
cloak bit, because a cloaked unit never reaches it: the list `0x40AD80` walks is
built by `0x40AA40`, which puts every candidate through `0x465AC0` first, and
`0x465AC0` rejects cloak at `0x465AE8` — `test BYTE PTR [ebx+0x10E],0x4`,
return 0 — before it looks at anything else. Cloak hides a unit from your eyes,
from the AI's search *and* from the turret. The three call sites above are
evidence about where the test lives, not about whether it happens.

The bit is confirmed as cloak independently at `0x401872`. The routine there
checks `unit+0x110` bit 11 (cloak wanted) and bit 12 (suppressed by
`mincloakdistance`), picks `CloakCost` `[def+0x1DA]` or `CloakCostMoving`
`[def+0x1DE]` according to `unit+0x110 & 0xC`, tries to debit the player's
energy at `[unit+0xEC]+0x8C`, and calls `0x48B090(unit, 4, paid)` — so
`unit+0x10E` bit 2 is set only when the cloak was actually paid for.

### §17a — the target-acquisition chain

Worth writing down once, because its absence is what produced the near-miss
above and nearly produced a second one:

```
0x40B7B0   weapon auto-acquire: choose a target
  └─ 0x40AD80(playerIdx, &centre, radius, out)   called at 0x40B82F, 0x40B85F
       reads [playerIdx*4 + 0x5119C0], the per-player scratch object
       loop 1 walks the vector at obj+0x5   (begin [obj+0x9], end [obj+0xD])
       loop 2 walks the vector at obj+0x15  (begin [obj+0x19], end [obj+0x1D])
                — only if loop 1 found nothing, and only if [obj+0x79] is set
  └─ 0x40AA40  rebuilds both vectors, per player, every 30 ticks
       (the maintenance entry is 0x40AD20: rebuild when
        world tick [global+0x38A47] >= [obj+0xED] + 0x1E)
       walks the global unit array [global+0x14357]..[global+0x1435B], stride 0x118
       rejects: [unit+0x110] & 0x10000000 clear (not alive), or bit 14 set
       enemy branch:
         push unit; push player; call 0x465AC0    ← 0x40AB11, the can-see gate
         test eax,eax; je skip-append             ← nothing unseen is appended
         ...then, outside that gate, [unit+0x110] bit 8 appends to list B
```

So the candidate set the weapon scan chooses from is **`0x465AC0`-filtered**,
and the "three tests only" in `0x40AD80` is the tail of a longer filter, not the
whole of one.

`0x465AC0(player, unit)` — `stdcall`, two arguments, the *second* push is the
unit — in full:

```
465ACE  if (unit+0x96 == player)            return 1     ; own unit
465AE8  if (unit+0x10E & 0x4)               return 0     ; cloaked
465AFD  def = unit+0x92
        A = unit+0x6A + def+0x15E     ; x
        B = unit+0x6E + def+0x16E     ; top of the model
        C = unit+0x72 + def+0x166     ; z
465B38  if (!(unit+0x110 & 0x200))                       ; not a sonar contact
465B4C      if (B < global+0x1427F << 16)  return 0      ; fully under water
        ; the fog grid, at the four corners of the footprint in turn
465B6A  if (lit(A,          B,          C         )) return 1
465BDB  if (lit(A+def+0x176, B,          C         )) return 1
465C73  if (lit(A+def+0x176, B-def+0x17A, C+def+0x17E)) return 1
465D19  return lit(A,        B-def+0x17A, C+def+0x17E)
```

`lit(x, y, z)` indexes the fog grid at `(x >> 5, (z − y/2) >> 5)`, the usual
cabinet skew, bounds-checked against `[player+0x80]`/`[player+0x84]`. It has
two backends chosen by `global+0x14281 & 2`: the per-player byte grid at
`[player+0x7C]` when set, and otherwise a call to `0x408090`, which reads the
shared word-bitmask grid at `[global+0x14273]` and tests the bit for the
viewing player index at `global+0x2A43`. `0x4658E0` is a sibling predicate for
a rectangle with the same two backends.

Four things follow that are worth stating plainly, because each has been got
wrong at least once:

- **there is no radar term** — `unit+0x110` bit 8 is never read here;
- **cloak is rejected second**, before anything positional;
- **the sonar bit is read**, but only to lift the underwater veto — see "The
  visibility pass" above, and §18, which had this right;
- **the footprint is probed four times**, not once, so a large unit is seen if
  any corner of its extent is lit.

Two details of the second list are easy to miss and both matter:

- **list B is the radar picture, and it is gated on the Targeting Facility.**
  `[obj+0x79]` is cleared at the top of every rebuild (`0x40AA80`) and set at
  `0x40AC06` only when the player owns a live, switched-on unit whose definition
  sets **`istargetingupgrade`** — `[unitdef+0x241]` bit 10, tested at
  `0x40ABF3`/`0x40ABF9`. With no such unit the flag stays zero and `0x40AD80`
  never walks list B at all. That is the Core Contingency Targeting Facility
  (`ARMTARG`/`CORTARG`), and it is the whole mechanism by which the original
  lets units shoot at what only radar can see;
- even with one, list B is a **fallback**: `0x40AE85`–`0x40AEA1` walks it only
  when the output vector is still empty, so anything actually visible is
  preferred over any radar contact.

`istargetingupgrade` is parsed at `0x42C5F7` (string at VA `0x503BB0`, `and
eax,1; shl eax,0xA` into `[unitdef+0x241]`, with `and ch,0xFB` clearing the bit
first). It was at one point thought to be a field the v3.1 engine parses and
never reads — a negative from searching for `test <mem>,0x400` against the
definition. That search could not have found it: the reader loads the dword
into a register first and tests `dh,0x4`. There is exactly one reader in
`.text`, and it is the one above.

### Where the ranges are drawn

`radardistancejam` and `sonardistancejam` are read by three drawing routines
besides loop 2 — `0x4392F8`/`0x439332` (the range display), `0x466FFC`/`0x46703F`
(minimap) and `0x46760D`/`0x467633` (main view) — and `mincloakdistance` by
`0x4390D2`, `0x439205` and `0x4676B9`. The jammed area and the mincloak radius
are both drawn as circles.

### The shipped data

Counted over `rev31/units`: `CloakCost` on four units — `armcom` and `corcom`
200, `armsnipe` 780, `armckfus` 450; `CloakCostMoving` on three — 1000, 1000,
1100; `mincloakdistance=40` on the same three that move. `RadarDistanceJam` on
six: 400, 420, 490, 500, 700, 730. `Stealth=1` on two, the stealth fighters.
**No `SonarDistanceJam`, no `init_cloaked` and no `Cloakable` anywhere.**

### What a cloaked unit looks like

Bit 2 of `unit+0x10E` reaches the screen in one place, and it does not touch the
model at all: it changes the routine that lays the finished unit down.

A unit is drawn by `0x459200`. Everything about it — every piece, the weapons
hanging off it, the brightness passes at `0x4BA1B0` and `0x4B96E0` — goes into a
bitmap of its own at `[this+0x10]`, in the GAF frame layout (`+0` width, `+2`
height, `+4`/`+6` offsets, `+8` the transparent index, `+0x10` the pixels). The
last thing the routine does is blit that bitmap to the screen, and it picks
between two blitters on the cloak flag:

```
459776: mov  edx,[ebp+0xc]              ; the unit
459779: test BYTE PTR [edx+0x10e],0x4   ; cloaked?
459780: jne  0x4597ba                   ;   -> 0x4B8500
459782: mov  eax,ds:0x511de8
459787: mov  cl,BYTE PTR [eax+0x14280]  ; the draw-everything-see-through toggle
45978d: test cl,cl
45978f: jne  0x4597ba
459791: ...  call 0x4B7F90              ; the ordinary blit
4597ba: ...  call 0x4B8500              ; the translucent one
```

`0x45937D` is the same test again on the routine's other path — the one taken
when `[ebp+0x14]` is set — with the same pair of calls. The arguments are
identical on both sides: same bitmap, same position, same destination. The only
difference is which blitter runs.

**The two blitters.** `0x4B7F90` ends in `0x4CBE70`, a plain masked copy —
`if (src != transparentIndex) dst = src`. `0x4B8500` ends in `0x4CBF2C`, which
takes one extra argument, `[gfx+0xC0]` where `gfx` is the graphics global
`ds:0x51FBD0` that `0x4B6220` returns, and does this instead:

```
4cbf99: mov al,[esi]        ; the unit's pixel
4cbf9b: cmp al,[ebp+0x18]   ; the transparent index
4cbf9e: je  0x4cbfae
4cbfa0: mov ebx,eax
4cbfa2: shl ebx,0x8
4cbfa5: mov al,[edi]        ; what is already on the screen
4cbfa7: add ebx,edx         ; edx = the table
4cbfa9: mov al,[eax+ebx*1]  ; table[src * 256 + dst]
4cbfac: mov [edi],al
```

`dst = table[src][dst]`: a 64 KB lookup on the pair of colours. The compressed
path, `0x4CC057`, does the same with the same table.

**The table.** `0x4BA5C0` allocates `0x10000` bytes tagged **"ALPHA TABLE"**
(`0x50A430`) and stores the pointer at `gfx+0xC0`. Its four neighbours are the
SHADE TABLE (`gfx+0xC4`, `0x2000`), the LIGHT TABLE (`gfx+0xC8`, `0x2000`), the
GRAY TABLE (`gfx+0xCC`, `0x100`) and the BLUE TABLE (`gfx+0xD0`, `0x100`).
`0x4BA772` fills the alpha one: for every pair `(i, j)` it averages the two
palette entries a channel at a time —

```
4ba7c0: mov al,BYTE PTR [ebp-0x2]   ; pal[i].r
4ba7c3: mov cl,BYTE PTR [edi-0x2]   ; pal[j].r
4ba7c6: add eax,ecx
4ba7d4: sar eax,1                   ; (r_i + r_j) / 2
```

— and calls `0x4BA9D0`, a nearest-colour search by squared RGB distance over the
256 entries, to snap the average back into the palette. `i == j` is short-cut to
`i` at `0x4BA7A7`.

So a cloaked unit is drawn **exactly halfway between itself and whatever is
behind it**. Not a stipple, not a palette ramp, not a swapped mesh: an even 50%
blend of the whole silhouette, applied once per covered pixel because the unit
was composited into a bitmap of its own before any of it reached the screen.

**Who sees it.** Nobody but the owner, and the owner always does. `0x465AE8`
returns zero for anyone else's cloaked unit before it looks at line of sight, so
such a unit never reaches the draw list — there is no partial reveal at close
range and no shimmer to catch. `mincloakdistance` does not reveal anything: it
drops the cloak, and the unit is then simply solid again. And the blit choice at
`0x459779` has no ownership test in it, because anything that has got that far
and is still cloaked is your own. Cloak is not invisibility; it is invisibility
to everyone else and see-through to you.

**The global at `[0x511DE8+0x14280]`** forces every unit through the translucent
blitter. `0x495703` cycles it 0..4 from a key handler and `0x4915BA` zeroes it
when a game starts, so it is a debug view rather than a setting.

**Nothing happens on the transition.** No sprite, no flash, no puff. What the
original does raise is a notification, and it is **not** a COB event: `0x48B173`
calls `0x47F780(unit, 0xE, 0)` when the flag comes on, `0x48B1A5` calls it with
`0xF` when it goes off. `0x47F780` is gated on the unit belonging to the local
player and looks the event up in a table of 24-byte records whose caption sits
at `[0x5086E8 + N*24]` and whose sound name sits at `[0x5086E4 + N*24]`:

| N | sound | caption |
|---|---|---|
| `0xE` | `cloak` | `Cloaked` |
| `0xF` | `uncloak` | `Visible` |

The COB calls in that same routine belong to other bits of `unit+0x10E`
entirely: `Activate`/`Deactivate` on bit 0 (`0x501280`, `0x501274`) and
`StartBuilding`/`StopBuilding` on bit 3 (`0x5050F4`, `0x505104`), all through
`0x4B0940`, which takes a **name**, not a number. There is no cloak entry, and
no shipped script has one to answer it with — between them `ARMCOM`, `CORCOM`,
`armsnipe` and `ARMCKFUS`, the only four units with a `CloakCost`, name nothing
but the ordinary set.

**And `0x10000` is not a piece render flag.** `0x48B17D`–`0x48B19C` walks
`unit+0xA2`, which is the list of **missions** attached to the unit — each node
is `+0x4` owner, `+0x8` next, `+0xC` the mission, and the mission is built at
`0x43A0C0` with the one-slot vtable `0x4FD2C8` — and calls that one virtual,
`0x438870`, which is nothing but `mission->[0x4E] |= arg`. `+0x4E` is the
mission's pending-event word; the executor reads it at `0x43B805`, and
`0x43B840` tests `0x10000` and answers it by calling `0x48A0F0(unit, 0..2)`,
which clears each of the three weapons' aim and runs the COB `TargetCleared`
(`0x508D58`). Cloaking makes a unit forget what it was aiming at. It has nothing
to do with drawing.

**The one extra thing on screen.** `0x4390D2`–`0x439106`: when the selected
unit's `mincloakdistance` is non-zero **and the unit is actually cloaked**, the
range display draws a circle of that radius around it in colour
`[0x511DE8+0xDDA]` — the distance at which an enemy will break the cloak.

### What RWE does with this

Implemented: the six FBI fields and the derived `cloakable`; `stealth` keeping a
unit off radar and sonar; the jammer clearing radar and sonar contacts inside its
radius when it is switched on and not the viewer's own; the radar/sonar split by
sea level; the `mincloakdistance` proximity fuse with its three-second tail; the
cloak drain, truncated, moving versus still, all-or-nothing, through the ordinary
consumption path; and a cloaked unit dropping out of `canSeeUnit` and
`canDetectUnit`. The CLOAK button, which was drawn and wired to nothing, now
sends a command and lights up while the order stands — it records the request
rather than the cloak, so a unit an enemy has just walked past keeps its order
and cloaks again three seconds later.

A cloaked unit now also looks like one. The renderer's own predicate asks the
same three questions in the same order as `0x465AE8` — whose it is, whether it
is cloaked, and only then whether the ground under it is lit — so someone else's
cloaked unit is not drawn, not shadowed, not clickable and not sprayed at, and
what can be shot at and what can be seen agree. Your own is drawn at the ALPHA
TABLE's even half-blend with what is behind it. And the `cloak` and `uncloak`
sounds, which had been parsed out of `sound.tdf` and preloaded all along without
ever being played, now go off on the transition for the unit's owner.

Deliberately not ported:

- **Cloak being invisible to the eye but not to a gun.** RWE's target scan runs
  through `canDetectUnit`, which is already a deliberate difference from the
  original (§9), so a cloaked unit falls out of targeting as well. Matching the
  original here would mean a cloaked Commander still being shot at by everything
  it walks past, which is not what the key is for.
- **The single-viewer visibility pass.** The original computes these flags for
  the local player only and stores them on the unit; RWE computes them per
  player, which is what a deterministic simulation with more than one human in it
  needs.
- **The `player+0x73 == 3` exemption** on the drain, and the type 1-or-2 gate on
  the `mincloakdistance` loop. Both are about the original's spectator and
  script-driven player types, which RWE does not have.
- **The half-blend is alpha, not a lookup table.** There is no 8-bit palette to
  snap an average back into, so RWE asks the blend hardware for the same
  arithmetic: `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` at exactly `0.5`. What it does
  not get for free is the original's guarantee of one average per covered pixel,
  which falls out of compositing the unit into its own bitmap first — blending
  the model straight into the frame would average a pixel again for every
  polygon of the unit stacked over it, and the thick parts of a Commander would
  come out nearly solid. So the cloaked units are held back until every solid
  one is down, then drawn twice: once with colour writes off to lay the depth,
  once with `GL_EQUAL` and depth writes off so only the nearest fragment blends.
  Holding them to the end is also what lets what stands behind them show
  through, which the original gets from painting back to front.

Still unported from this reading:

- **The caption line.** `Cloaked` and `Visible` are what the original puts up
  beside the sound; RWE has nowhere to put a unit notification's text yet, so
  only the sound plays.
- **The mincloak ring** on a selected cloaked unit (`0x4390D2`). RWE has no
  range-circle display for anything yet — the jammer and radar rings are in the
  same position — so this waits on that rather than on anything about cloak.
- **`0x10000` clearing the unit's aim** when it cloaks. It is decoded above and
  it is simulation rather than presentation; RWE's cloaked unit keeps its
  target, which given that RWE also drops a cloaked unit out of targeting
  altogether would only matter to the cloaking unit's own gun.
- **The debug see-everything-translucent toggle** at `[0x511DE8+0x14280]`.
- **Bit 10, "jammed".** The original marks a unit a jammer has erased separately
  from one that was never seen. Nothing in RWE reads it, and nothing in the
  original appears to either beyond the flag itself.
- **The jammer's effect on an ally's radar.** RWE follows the original in
  skipping only the viewer's own jammers, so allies do jam each other; it is
  recorded here in case it ever looks like a bug.

## 18. Where a radar contact is drawn, and where it is not

### The main view never sees one

The world render does not walk the unit list. It consumes a list rebuilt once a
frame and reads it at `0x4697B7` for the Z buckets and again at `0x469C16` for
the health-bar pass; every `0x118`-stride walk inside `0x468CF0`–`0x469D1F` is
over that list. The list lives at `world+0x1435F` with its count at
`world+0x14367`, allocated at `0x485590` under the tag `"HOT UNITS"`
(`0x508BC8`).

`0x48BAE0` builds it. Per unit: skip an empty slot (`0x48BB3E`), cull against
the screen rectangle `world+0x37E27` (`0x48BBD2`–`0x48BC3B`), admit the
viewer's own unit unconditionally (`0x48BC42`), and otherwise

```
48bc56  call 0x465AC0        ; the can-see predicate
48bc5b  test eax,eax / je skip
48bc63  ...                  ; append WORD unit+0xA8
```

`0x465AC0` **never reads the radar bits**. It tests own-unit (`0x465AD4`),
cloak (`unit+0x10E` bit 2, `0x465AE8`), the sonar bit for a submerged unit
(`0x465B38`), and then the line-of-sight grid or the explored bitmask directly
(`0x465B5C`, `0x465C04`, `0x408090`). A unit whose only claim on the viewer is a
radar contact is therefore not in the list, is not drawn, and is not pickable:
the 3D cursor pick `0x48CD80` iterates the same list (`0x48CDBD`–`0x48CE97`).

There is one HUD response, and it is not in the viewport. The info panel's
"unit under cursor" branch `0x46AEE0` calls `0x465AC0` at `0x46AF3D` and on
failure falls to `0x46B6F3`, which picks the prefix `"R: "` (`0x50786C`) or
`"S: "` (`0x507870`) on `unit+0x110` bit 9 and prints `"Unidentified object"`
(`0x507858`). So hovering a blip names it as unidentified rather than showing
nothing — but the hover can only come from the minimap.

### The minimap draws one exactly like anything else

`0x466DC0`, called once a tick for the local player only (`0x465072`, guarded
at `0x46506A`) and again after a fog-mode rebuild (`0x48191F`). The gate is

```
466e64  eax = unit+0x110
466e6a  test ah,0x3          ; bit 8 or bit 9
466e6d  jne draw
466e6f  owner == viewer ? draw : skip
```

and that is the only place that reads those bits raw *for drawing*. It is not
the only reader in the binary: `0x40AA40` reads bit 8 at `0x40AB3F` —
`shr eax,0x8; test bl,al`, a shift-and-test rather than a `test ah,imm`, which
is why a sweep for the latter form missed it — to append to the fallback
candidate list that the Targeting Facility unlocks. See §17a.

The
sprite is the GAF animation `radlogo` (`world+0x147DF`, loaded by name at
`0x42990D`), frame = the owner's colour byte, blitted at `0x466ECC`–`0x466F0B`.
In the shipped `anims/FX.GAF` it is ten frames, every one **4×4 pixels**,
hotspot (1,1): a two-by-two block of the player's colour inside a ring of
palette index 89, with the corners transparent. Nothing between `0x466E4E` and
`0x467135` distinguishes a radar contact from a unit in plain sight — same
sprite, same size, same colour.

Position is `x·mmW/worldW` and `(z − y/2)·mmH/worldH` (`0x466E83`–`0x466EB7`),
the same projection the 3D view uses, so an aircraft's blip sits above its
ground position by half its altitude.

The hovered unit additionally gets `radlogohigh` (`world+0x147E3`, 6×6, hotspot
(2,2)) — a ring around the dot — and a selected unit gets its radar, sonar, jam
and weapon range rings (`0x466F42` onwards).

**The blink is not about radar.** `0x466EB9` skips the blip on alternate phases
only when `unit+0xFA` is non-zero:

```
466eb9  mov al,BYTE [unit+0xfa]
466ec1  je draw                        ; timer zero -> always drawn
466ec3  test BYTE [world+0x142f1],0x1  ; the global blink phase
466eca  je skip
```

`unit+0xFA` is a **just-damaged** timer, set to `0xF0` (240 ticks, eight
seconds) by `0x467950` from the damage applier `0x489CE0` at `0x489D8E`,
decremented per tick at `0x48ADF0` and cleared on unit init at `0x485C12`. The
phase bit `world+0x142F1` is flipped every eight ticks by `0x466580`
(`0x4665A9`–`0x4665BB`), called per tick from `0x4955E5`. So a blip flashes at
roughly two hertz for eight seconds after the unit is hit, whether it is a radar
contact or a unit you can see, and a quiet radar contact does not flash at all.

### Nothing stored says "radar only"

`0x467440` recomputes the picture once a tick for the local player alone
(`0x46745B`) and writes the result into `unit+0x110`: loop 0 clears bits 8–10
for everyone else and sets 8 and 9 for own and vision-sharing units
(`0x4674EF`, `0x4674F6`), loop 1 sets bit 9 for sonar and bit 8 for radar
(`0x4678FF`, `0x467937`), loop 2 lets jammers clear them, and **loop 4 sets bit
8 again from line of sight** (`0x46780E`).

That last one matters: **bit 8 is not "radar contact", it is "on my picture at
all"**. The difference between radar and sight is not stored anywhere — it is
recomputed on demand by calling `0x465AC0`, which is what the render list does.
The AI does the same, building two lists in `0x40AA40`, one gated on can-see
(`0x40AB11`) and one on bit 8 (`0x40AB3F`).

This also corrects §17, which says "line of sight is `unit+0x9C`". There is no
such field: loop 4 biases its cursor by `lea esi,[edi+0x74]` at `0x46770D`, so
its `[esi+0x9C]` is `unit+0x110` and its `or ebp,0x100` at `0x46780E` is setting
bit 8 of the same flags word everything else uses.

### What RWE was doing

`GameScene::renderWorldUi` drew, for every radar-only contact, a coloured
minimap dot and a white box outline in world UI space. That is an RWE
invention with nothing behind it in the original, and it is gone. RWE's minimap
already draws radar contacts through `unitIsDetectableByLocalPlayer`, which is
the right gate, and already rings the hovered one — so the minimap half was
right all along and only the main view needed taking away.

### Not ported

- **The damage flash.** RWE's minimap dots do not blink. The timer and the
  eight-tick phase are decoded above; there is no `unit+0xFA` equivalent in the
  simulation and adding one is a bigger change than the complaint asked for.
- **"R: Unidentified object".** RWE's info panel has no unknown-contact
  state.
- The minimap does **not** test cloak in the original (`0x466E4E`–`0x466ECC`
  never reads `unit+0x10E`, and the radar visitor `0x467840` skips only
  `stealth`), so a cloaked enemy inside your radar range still shows a blip
  there while being invisible in the world. That reads as an oversight rather
  than a design decision and RWE does not copy it.

## 19. The order panel, and which flag gates which button

Every side loads one panel — `ARMGEN.GUI` or `CORGEN.GUI`, via `0x41B0F0` —
carrying every order button there is. The game then takes away or greys out
what the current selection cannot use. There is no per-unit order panel.

### `def+0x245` in full

§9 gave bits 0–8, 10 and 11 of this dword and called bit 4 `canattack`. Here is
the whole of it. The parser's boolean helper leaves its result in `eax`, so the
key pushed immediately before the `call 0x4C46C0` owns the `shl` immediately
after it, and the pipeline trap of §82 does not apply.

| Bit | Key | `shl` at | Second site |
|---|---|---|---|
| 0 | `mobilestandorders` | `0x42C8DB` insert | `0x48D104`, under a name compare against `Standing_MoveOrder` |
| 1 | `firestandorders` | `0x42C8FF` | `0x48D0D7`, likewise for `Standing_FireOrder` |
| 2 | `onoffable` | `0x42C8BE` | `0x403010` ACTIVATE / `0x403040` DEACTIVATE |
| 3 | `canstop` | `0x42C92B` | **none** — the order panel is its only reader |
| 4 | `canattack` | `0x42C94A` | `0x43F154`, `0x401F98` |
| 5 | `canguard` | `0x42C970` | `0x43E615`, `0x43F4C7` |
| 6 | `canpatrol` | `0x42C996` | `0x43E5DE` picks cursor 7 over cursor 19 |
| 7 | `canmove` | `0x42C9C3` | `0x43FE03`, `0x44019D` |
| 8 | `canload` | `0x42C9E2` | `0x4067C4` the load handler, `0x489ABE` the transporter half of the predicate |
| **9** | **no key: a copy of bit 10** | `0x42CA3D`–`0x42CA4F` | `0x4899CC`, the can-repair predicate |
| 10 | `canreclamate` | `0x42CA0F` | `0x48996C` can-reclaim, `0x43FA13` the RECLAIMUNIT mission |
| 11 | `canresurrect` | `0x42CA2E` | `0x43FF46` → the `RESURRECT` mission |
| 12 | `cancapture` | `0x42CA72` | `0x4042CF` the CAPTURE handler; also used as a cheap "is this a commander" |
| **13** | **no key: `cloakcost > 0`** | `0x42CA93` | `0x403080` CLOAK_ON, `0x4676AE` in the cloaked-unit render |
| 14 | `candgun` | `0x42CABA` | `0x43F7EE` → the `ATTACKSPECIAL` mission |
| 15 | `norestrict` | `0x42CB4B` | `0x44C165`, the restriction table UI |
| 16 | `wacky` | `0x42ADC0` (first-pass loader only) | `0x46D33C` — read, meaning unrecovered |
| 17 | `showplayername` | `0x42CB77` | `0x46AF64` |
| 18 | `commander` | `0x42CB96` | only jointly with 17 at `0x46AF56` |
| 19 | `cantbetransported` | `0x42CBBF` | `0x489AA3`, the passenger half of the load predicate |
| 20–22 | `selfdestructcountdown` | `0x42CBFA`, default 5 at `0x42CC13` | `0x40202D` |

Two entries in that table are not keys at all and are the reason two buttons
have no FBI field behind them:

```
42ca3d  and ah,0xfd        ; clear bit 9
42ca40  shr edx,1
42ca42  and edx,0x200      ; bit 10 -> bit 9
42ca4d  or  edx,eax
```

**Bit 9 is `canreclamate` copied**, and `0x4899CC` — the routine that decides
whether a unit may repair another — is what reads it. So REPAIR and RECLAIM are
the same flag twice and always appear together. Likewise **bit 13 is
`CloakCost > 0`** (`0x42CA5A`–`0x42CA96`, comparing the float at `def+0x1DA`
against the zero at `0x4FD210`); there is no `Cloakable` key in the binary at
all, which is why honouring one gives the button to nothing.

The layout is confirmed away from the parser by the field-by-field definition
copy at `0x42BAC1`–`0x42BCAD`, which moves bits 0–19 as twenty separate one-bit
members and then `0x700000` as a single three-bit group, and stops there.

Shipped-data check, 189 FBIs: `canload` is exactly `armatlas`, `armtship`,
`cortship`, `corvalk`; `cancapture` and `candgun` are exactly the two
commanders; `canresurrect`, `wacky`, `selfdestructcountdown` and `cloakable`
are named by nothing at all.

### A mixed selection: ANY, not ALL

`0x41B2E0` walks the local player's units (stride `0x118`, from `player+0x67`
to `player+0x6B`), skipping any without `unit+0x110` bit 4 — selected. It keeps
two different kinds of accumulator, and the difference is the whole answer.

**The ten capability bits are a plain OR.** `0x41B49F`–`0x41B524` is ten
repetitions of `shr edi,N / test cl,1 / je skip / mov <slot>,esi` with
`esi = 1`, and **nothing ever clears a slot**:

| bit | flag | address |
|---|---|---|
| 7 | `canmove` | `0x41B4A1` |
| 3 | `canstop` | `0x41B4AE` |
| 4 | `canattack` | `0x41B4BC` |
| 5 | `canguard` | `0x41B4C9` |
| 6 | `canpatrol` | `0x41B4D7` |
| 8 | `canload` | `0x41B4E4` |
| 9 | repair | `0x41B4F2` |
| 12 | `cancapture` | `0x41B4FD` |
| 10 | `canreclamate` | `0x41B50B` |
| 14 | `candgun` | `0x41B518` |

So a button is offered when **any** selected unit names it. Boxing a solar
collector in with a squad of Peewees does not cost the Peewees their move
button, and one transport in the box puts LOAD up for the lot.

**The four stateful toggles are a sentinel accumulator**, which is the shape
§9 already described for the two mode buttons: fire orders start at 4, move
orders at 4, cloak and on/off at 3; a unit that does not name the flag is
skipped entirely (`0x41B403`, `0x41B42C`) so it cannot drag the shared state;
the first offerer's value is taken; a later disagreement collapses to 3
(`0x41B420`, `0x41B449`) or 2. The sentinel means "nobody offered it" and greys
the button out (`0x41A243`, `0x41A280`).

The cloak accumulator has a bug worth not copying: `0x41B485`–`0x41B497` does
not compare at all, so a **second** cloakable unit sets "mixed" even when the
two agree. On/off, three instructions away at `0x41B893`, does compare.

Results are packed into `world+0x37EC0` (move-order 0–2, cloak 3–4, on/off 5–6,
canmove 7, canstop 8, canattack 9, canguard 10, canpatrol 11, canload 12,
canreclamate 13, cancapture 14, repair 15), `world+0x37EBE` bits 12–14 (fire
order) and `world+0x37EC2` bit 0 (candgun).

### Hidden versus greyed, and the slot LOAD and BLAST share

`0x41A120` is the enable pass — seventeen hardcoded name lookups, each followed
by one bit test. Almost everything is **greyed**, via `0x4A1200(page, idx, 1)`
which sets bit 0 of `WORD [ctrl+0x13C]`: MOVE at `0x41A2E8`, STOP `0x41A310`,
ATTACK `0x41A338`, DEFEND `0x41A35F`, PATROL `0x41A387`, and RECLAIM, REPAIR
and CAPTURE the same way.

LOAD and BLAST are the exception, because **they are the same slot**: in
`ARMGEN.GUI` `ARMLOAD` and `ARMBLAST` are both at `xpos=64, ypos=317`, same
width and height. `0x41A409` resolves it on the ORed `canload` bit —

- nothing can load: LOAD is made inactive (`0x4A03F0(page, idx, 0)`, which
  writes `BYTE [ctrl+0x29]`), UNLOAD is greyed, BLAST is greyed unless
  `candgun`;
- something can load: **BLAST is made inactive** and LOAD and UNLOAD stand.

So selecting a commander together with an Atlas costs you the D-gun button.

One trap for anyone reimplementing this: `0x49FE60` finds a control by
`strstr`, not by name equality, so `"MOVE"` also matches `ARMMOVEORD` and
`"LOAD"` also matches `ARMUNLOAD`. It only works because the shipped GUI files
happen to list the short name first.

### What RWE was doing

`GameScene::createOrdersPanel` gated LOAD, UNLOAD, BLAST and CLOAK, and only
when **exactly one unit was selected**; a selection of two or more got the raw
`ARMGEN.GUI` with every button on it, which is the reported complaint. It also
gated LOAD on having transport capacity rather than on `canload` (the same four
units in the shipped data, but not the same rule), and gated nothing at all on
`canstop`, `canpatrol`, `canattack`, `canmove`, `canguard`, `canreclamate` or
`cancapture` — four of which RWE was not even reading out of the FBI.

It now builds the list of selected definitions and applies
`selectionOffersOrderButton`, which is the OR above.

> **Ported, 2026-09-02** ("Buttons can be greyed out, and the order panel
> greys instead of hiding", `3f1a6c12`). This list used to open with a bullet
> saying RWE had no disabled state for a `UiStagedButton` and removed a button
> the selection could not use instead of drawing it dim. It has one now:
> `UiStagedButton::setEnabled(false)` draws the button's greyed face and makes
> it ignore every event, `UiFactory` honours a gui file's `grayedout=1`, and
> `GameScene::applyOrderButtonGating` greys everything and hides only LOAD and
> BLAST, which is exactly the split above (`0x41A412`, `0x41A471`).
>
> **The greyed face is not the original's arithmetic, and does not need to
> be.** The original darkens the gadget's whole rectangle in place, running it
> through SHADE row 12 at level -20 — a measured 0.82x on luminance (§99).
> RWE draws the frame the artists put in the button's own GAF one past the
> pressed frame; every shipped button carries one, and the factory had been
> extracting it and throwing it away all along. So the two agree because the
> artwork was drawn to agree, not because the code does the same sum: what
> the original computes at run time, the artists had already painted. A mod
> whose GAF has no such frame gets no dimming at all here, where the original
> would still have darkened it — the one case where the two can be told
> apart.

### Deliberately not ported

- **The cloak accumulator's disagreement bug** at `0x41B485`.
- **`canresurrect`, `wacky` and `selfdestructcountdown`.** No shipped unit
  names any of them and RWE has no resurrect order.
- **`canstop`.** It is parsed and honoured for the button, but the original's
  own STOP mission builder at `0x43F82C` does not check it, so the flag gates
  the button and nothing else.

## 20. Who may reclaim, and who may not be reclaimed

`canreclamate` is flags word B **bit 10** (parser: push `"canreclamate"`
`0x503A74` at `0x42C9EF`, `shl eax,0xa` at `0x42CA0F`, store `0x42CA1A`).

### One key, two capabilities

The parser does something easy to miss. Immediately after storing bit 10 it
**mirrors it into bit 9** (`0x42CA3B`-`0x42CA4D`:
`mov edx,eax; and ah,0xfd; shr edx,1; and edx,0x200; or edx,eax`). Bit 9 is
the *repair* capability, and the two are read by different routines:

| Routine | Address | Tests |
|---|---|---|
| `CanReclaimTarget(reclaimer, target)` | `0x489960` | bit 10 at `0x48996C` |
| `CanRepair(...)` | `0x4899B0` | bit 9 at `0x4899CC` |

So a single FBI key governs both, and **`workertime` is never consulted by
either**. RWE had been gating both on `workerTime` alone.

That is not a distinction without a difference. Sixteen units in the base game
set `CanReclamate=1` -- the Arm and Core commanders, construction kbots,
vehicles, aircraft and ships. **Twenty-one more have a non-zero `WorkerTime`
with the bit clear**: every factory, both air repair pads, both carriers, and
CORSOLAR. Every one of those could reclaim and repair in RWE and cannot in the
original. No unit has the bit set with a zero worker time.

The same bit is tested again in the order-issue dispatch at `0x40477A` and
`0x404B6E`, which is where the refusal is announced: `0x50164C` "That unit
cannot be reclaimed" and `0x501638` "Reclamation failed".

### The target side

`0x489960` makes two further checks on the target once the reclaimer has
passed:

- **`target+0x110 & 3 == 2`** rejects (`0x48997B`-`0x489981`). The low two bits
  of `unit+0x110` are a physical-mode field that is *not* the movement or
  firing mode at bits 18-21. Value 1 is "on the ground" -- the per-tick ground
  placement routine `0x48A870` requires it at `0x48A8AE` -- and value 2 is
  tested here, in `0x401C48` where it gates allocating an air-movement
  structure for a `canfly` unit, and in the `setSFXoccupy` cascade at
  `0x43DB7A`. Everything points at 2 meaning **airborne**, which would make
  this "you cannot reclaim a unit that is in the air" -- but that is inference
  from three call sites, not a decode of the writer, which was not found.
  **Not implemented on that basis.**
- **the target's `cancapture`** (flags B bit 12) rejects (`0x48998F`,
  byte-verified as `F6 C4 10`). The capture handler mirrors it at
  `0x4042E3`-`0x4042F5` with `0x501618` "That unit cannot be captured". Only
  ARMCOM and CORCOM set the key, so what the rule amounts to in play is that
  **a Commander can capture, and can itself be neither captured nor
  reclaimed**.

Both the reclaimer gate and the `cancapture` rule are implemented; finishing
somebody's half-built structure is construction rather than repair and is
deliberately left outside the gate.

### `selfdestructcountdown`, and why RWE's five seconds was already right

Flags B bits 20-22, parsed at `0x42CBC8`. The part worth recording is the
**absent** case at `0x42CC07`: `and edx,0xffdfffff; or edx,0x500000` sets the
field to **5**. So the original's own default is five seconds and RWE's
hardcoded five is already correct -- there was nothing to change.

An explicit `0` is *not* "use the default": `0x402053` tests the field and
detonates immediately with no announcement. Otherwise the reader `0x402010`
counts down one step a second (`push 0x1E` = 30 ticks into `0x439E80` at
`0x4020F6`), announcing five, four, three, two, one, zero from a message table
based at `0x5086D8`, and then waits a further `rand(0..14)` ticks
(`0x402117`) before dealing **30000** damage (`0x40213C`) -- which is exactly
the armour-bypass threshold at `0x489BD1`, tying it to the D-gun finding in
the damage section. Only six message-table entries exist for a three-bit
field, so countdowns of 6 or 7 would index past the array; nothing sets them.

What RWE still lacks is the six spoken announcements, the random slop before
the blast, and the explicit-zero case. The timing is right.

### `healtime`, `WORD def+0x200`

Parsed at `0x42C379`, stored `0x42C388`. One reader, `0x48AF3D`, in the
per-unit per-tick loop, and it is not a per-tick heal: the whole block runs
only when the game tick is a multiple of eight (`test BYTE PTR [..],0x7` at
`0x48AF5E`) and then adds `healtime * 8 / 30`, truncated -- a `shl eax,0x3`
followed by a divide-by-thirty done with the reciprocal `0x88888889`.

So the key is hit points a **second**, delivered in eight-tick steps. Only
the two commanders set it, at 27, which is seven points every eight ticks or
26.25 a second. Implemented, with the granularity kept because it is what the
health bar visibly does; the original also charges the mending against the
owner's stores through `0x41BD10`, and RWE does not, because no other repair
in RWE costs anything and making this the one exception would be stranger
than leaving it free.

Immediately above it in the same block sits the drowning rule, recorded here
because it is easy to miss: once a second, a unit whose `WORD unit+0x70` is
at or below the water level and whose definition is **not** `canhover` takes
`[gamerules+0xD50]` damage of type 11 (`0x48AF19`-`0x48AF32`).

## 21. Stockpiled weapons and interception

Eight weapons in the shipped data carry `stockpile`, and none of them fires out
of the economy the way the rest do. The round is built beforehand, at a price,
and the launcher spends it.

### The unit's weapon records

Three of them, twenty-eight bytes each, starting at `unit+0x04`. The fire loop
walks them with `lea esi,[edi+0x1f]` and `add esi,0x1c` (`0x49E1B8`,
`0x49E54C`), so the offsets fall out as:

| Field | Record | Weapon one |
|---|---|---|
| weapon definition pointer | `+0x0C` | `unit+0x10` |
| reload countdown, `WORD` | `+0x14` | `unit+0x18` |
| rounds in the magazine, `BYTE` | `+0x1A` | `unit+0x1E` |
| flags — bit 0 aiming, bit 1 enabled, bit 4 may auto-target | `+0x1B` | `unit+0x1F` |

The magazine byte is the whole of a launcher's stockpile. The order menu shows
it as `N +M` where `M` is what is still on order (`0x419A2B` formats `%d` and
then ` +%d` from the count and from `0x439D80`, which totals the outstanding
orders), and the UI only ever looks at **weapon one** (`[unit+0x1E]` hard-coded
at `0x419A33`). Both launchers that ship — `ARMSILO`/`CORSILO` and
`ARMAMD`/`CORFMD` — put their missile in `Weapon1`, so that is not a limitation
anyone would notice.

With nothing in the magazine the label starts empty and the suffix is printed at
the end of it, so an empty launcher with three on order reads `" +3"` with the
leading space still on. `0x419A42` writes the terminator and the append at
`0x419A5A` measures a length of zero; nothing goes back for the space.

### Where the button is, and what raises it

Not on the general orders panel. It is a gadget on the launcher's own **build
page**, and the shipped GUI files say so in as many words:

```
name=ARMMAKENUKE;   ...   commonattribs=8;   // Flag this as a weapon build button
```

That comment is in `ARMSILO1.GUI` as distributed. `commonattribs` is the byte at
`element+0x2A` that the readout loop at `0x4199B0` dispatches on — bit 2 for an
ordinary build queue's `+N`, bit 3 for this `N +M` — and `0x41AE80` stamps a 4
into the same byte when it builds a page's buttons itself.

Six units ship a page: `ARMSILO1`, `CORSILO1`, `ARMAMD1`, `CORFMD1`, `ARMEMP1`,
`CORTRON1`. **All six say `Builder=0` in their FBI**, so anything that gates the
page load on that flag never finds them. RWE did, which is why the control could
not be reached.

The click handler is `0x419B00`, `f(name, unit, count)`:

- it plays `addbuild` for a positive count and `subbuild` otherwise
  (`0x419B21`–`0x419B33`);
- it runs **strstr** for `"MAKENUKE"` and then for `"MAKEANTI"` over the gadget
  name (`0x419B3C`, `0x419B4E`, through `0x4E49B0`) and on either hit issues a
  `BUILDWEAPON` command with unit-type index 0 and the count (`0x419B97`);
- otherwise it looks the name up as a unit type and issues `MOBILEBUILD` or
  `BUILDINGBUILD` (`0x419B61`–`0x419B87`).

The substring test is not decoration. The six do not share a prefix:
`ARMSILO1` names its button `ARMMAKENUKE` and `CORSILO1` `CORMAKENUKE`, but
`ARMEMP1` says `EMPMAKENUKE` and `CORTRON1` `TRONMAKENUKE`. A prefix or an
equality rule silently loses the last two.

### Building a round, `0x402B70`

The order handler is a three-state machine on `[order+0x05]`, re-scheduled by
`0x439E80` rather than run every tick:

- **state 0, `0x402CB4`** — nothing queued, so the order is finished and popped;
  or the magazine is at **200** (`cmp BYTE PTR [eax+0x1e],0xc8`) and the order
  waits **300** ticks and looks again; otherwise the progress is zeroed and the
  state advances.
- **state 1, `0x402BD4`** — one step of the build, below.
- **state 2, `0x402BB3`** — the magazine goes up by one, the outstanding count
  down by one, and `0x41C150` refreshes the button.

The build step advances **five ticks at a time** and the total time is the
weapon's own `reloadtime` — `WORD wdef+0xE4`, which the parser has already
multiplied by 30:

```
402bd4  mov eax,[esi+0x3e]        ; P, ticks already paid for
402be0  mov ax,[edi+0xe4]         ; T, reloadtime in ticks
402bdb  lea ecx,[eax+5]           ; Q = min(P + 5, T)
402c01  fmul [edi+0xc4]           ; metalpershot
402c0f  call 0x4e43a0             ; trunc(P * metalpershot / T)
402c20  call 0x4e43a0             ; trunc(Q * metalpershot / T)
402c2b  sub eax,ebp               ; the difference is what this step costs
402c42..402c4c                    ; the same again for energypershot at +0xC0
402c6b  call 0x4011c0             ; ask the economy for it
402c72  je 0x402c9c               ; refused -> no progress, retry in 10 ticks
402c76  mov [esi+0x3e],ebx        ; paid -> progress = Q
402c88  call 0x439e80             ; and again in 5
```

Truncating the **running total** at each end rather than truncating the step is
the whole trick: it makes the run come to exactly the TDF number however
awkwardly the cost divides by the tick count. Replaying it against the shipped
data gives a `NUCLEAR_MISSILE` 5400 ticks (180 s), 2000 metal and 180000 energy
to the unit, and the anti-nuke 3600 ticks, 200 metal and 10000 energy. A naive
`cost / steps` charged per step loses 46% of a nuke's metal and **all** of an
anti-nuke's, because 200 metal over 720 steps rounds down to nothing.

`0x4011C0` is the ordinary per-unit resource request — it books the demand at
`unit+0xBC+0x04` and `+0x1C` and only accepts it if both resources are satisfied
— so a launcher on a stalled economy stops where it is and keeps the part-built
round rather than going into debt.

### Firing, `0x49E3D5`

The per-weapon fire routine settles the price before it does anything else:

```
49e3d5  test stockpile            ; a stockpiled weapon needs a round
49e3e4  mov al,[esi-0x1]          ;   magazine != 0
49e3ed  mov ecx,[edi+0xec]        ; otherwise the player
49e3f3  fld [ecx+0x8c] ; fcomp [ebx+0xc0]   ; stored energy >= energypershot
49e406  fld [ecx+0x98] ; fcomp [ebx+0xc4]   ; stored metal  >= metalpershot
49e420  je ...                    ; short of either -> the weapon does not fire
49e447  test stockpile
49e459  dec cl                    ;   spend a round, and set NO reload timer
49e51f  call 0x4012a0             ;   otherwise take the two out of the stores
```

Two things follow that RWE did not have. **`metalpershot` is real and is weighed
the same way `energypershot` is**, and **a weapon that cannot pay does not fire
at all** — the original never lets the shot go and takes the debt afterwards.
And a stockpiled weapon gets no reload timer whatever: `0x49E463` jumps straight
over the calculation, because the wait was the build.

`0x49E4F2` then sets `unit[0xBA] |= commandfire ? 0x800 : 0x400`, which is how
the rest of the frame is told a shot went off.

### The reload time is not just `reloadtime`

The branch a non-stockpiled weapon takes at `0x49E468` is worth recording on its
own, because RWE uses the flat TDF number:

```
reloadTicks = ((120 - 20 * hp / maxdamage) * ((100 - 6 * tier) * reloadtime / 100)) / 100
```

with `tier = min(5, kills / 5)` as everywhere else, `hp` the word at
`unit+0x108` and `maxdamage` the dword at `def+0x1FA`. A veteran of 25 kills
reloads in 70% of the time; a unit at the point of death takes 120%.

**Ported since**, as `computeReloadTicks` in `UnitBehaviorService_util`, and it
is safe to apply to every weapon in the game because the two terms cancel
exactly at full health with no kills: `(120-20) * (100*T/100) / 100` is `T`, so
an undamaged rookie gets the flat TDF number it always got. The commander's
disintegrator is the case with a round number behind it — `reloadtime 1.2` is 36
ticks, and a healthy commander gets 36 back. Tests in `src/rwe/sim/dgun.test.cpp`.

### `commandfire`

Bit 26 of `wdef+0x111`, and it means one thing: the weapon is never given a
target by the machine. The auto-target scan skips it (`0x40643F`, `0x407131`,
`0x40FDE5`) and so does the return-fire path, which takes an "is this an
explicit order" argument and only consults the bit when that is zero
(`0x408A88`–`0x408A96`). The D-gun, the nukes and the bombs carry it.

**Correction to an earlier reading here: the anti-nukes do not.** `AMD_ROCKET`
and its Core twin name no `commandfire` at all, and neither does anything else
with `interceptor`. That is not an oversight in the data, it is the whole
mechanism — an anti-nuke has to engage without being told, and the flag that
would stop it is simply absent. Counted over `rev31`'s `Weapons/*.TDF` the flag
appears ten times: both nuclear missiles, both big EMP rounds' launch weapons,
the four bombs and the two disintegrators.

### `coverage`, `targetable`, `interceptor`

`coverage` is `wdef+0xE0` (`0x42E539`, checked against the pipeline rule) and
belongs to the interceptor; `targetable` (bit 29) belongs to the thing being shot
at. The chain is:

1. **Acquisition, `0x49D120`.** An `interceptor` weapon does not look for a unit
   at all: the auto-target scan calls this instead (`0x408B31`) and aims at what
   it returns (`0x48A0A0`). It refuses outright if the magazine is empty
   (`mov cl,[ebp+ecx*4+0x1e]`), then walks the live projectiles and takes the
   first that is not the launcher's own, whose weapon has `targetable`, and
   whose **aim point** — `proj+0x28`, not its current position — is within
   `coverage` of the launcher. The test is a **square**, `|dx| <= coverage` and
   `|dz| <= coverage` done as an unsigned compare against `2 * coverage` in 16.16
   units, and **Y is not looked at**. A last pass over every projectile's `+0x56`
   makes sure nothing else has already claimed it, so two anti-nukes never waste
   themselves on one missile.
2. **Launch, `0x49DC17`.** Vertical launch again, but the target projectile is
   fetched first and **the launch is abandoned if there is none**. `0x49CC20`
   stores it at `newproj+0x56`.
3. **Flight, `0x49B47A`.** The aim point routine returns `target + 4`, the
   target's live position, ahead of any unit target — the case §7 recorded as
   decoded but unfillable.
4. **Detonation, `0x49B106`.** The collision check gives an interceptor an extra
   clause: if it is within its own `areaofeffect` of the target projectile it
   goes off there and then.
5. **The kill, `0x49A664`.** This is the piece that was missing. When a
   projectile explodes, *if its own weapon has `interceptor`*, the routine walks
   every live projectile and detonates each one within `areaofeffect` of the
   blast. It does not re-check `targetable`; anything in the blast dies. That is
   how the nuke comes apart, and it is also why one anti-nuke can clear a salvo.

An `interceptor` or `targetable` projectile is also drawn differently on the
minimap (`0x46720E` tests both bits at once).

Four weapons carry `targetable` and they are the four a launcher stockpiles:
`NUCLEAR_MISSILE`, `CRBLMSSL`, `ARMEMP_WEAPON` and `CORTRON_WEAPON`. Two carry
`interceptor`, both `coverage=2000`, and both `areaofeffect=96`.

### Three details the five steps above leave out

Read out of the same routines while porting them, and each one changes what the
code has to do:

- **The acquisition skips by owner, not by launcher.** `0x49D179` compares the
  projectile's owner byte `proj+0x66` against the unit's `unit+0xFF`, so a
  launcher ignores everything its own *player* fired, not merely its own rounds.
- **Both radius tests are strictly inside.** The proximity fuse at `0x49B1A4`
  and the blast at `0x49A757` both branch away on `jge`, so a projectile exactly
  `areaofeffect` away survives.
- **The blast is a sphere; the coverage is a square.** `0x49A6B8`–`0x49A755`
  squares all three differences and compares against `areaofeffect²`, where the
  coverage test at `0x49D18D` compares x and z separately and never reads y. The
  two are not the same shape and it is easy to write them as though they were.

`0x4E49B0`, which the order-panel handler uses on the button name, is strstr and
not strcmp: it scans the haystack for the needle's first character and returns
null on failure. That matters because the click handler's dispatch reads as a
name comparison and is not one.

### `antiweapons`

Bit 29 of `def+0x241`, pushed at `0x42C84C`. Two units set it, and they are the
two anti-nuke launchers. Nothing in the simulation reads it, and the
interception above keys entirely off the weapon flags -- but it is not unused,
which this section said until the minimap was read. **Its one reader is the
minimap render** (§25): a selected unit with `antiweapons` draws a dashed
coverage ring for each of its `interceptor` weapons, radius `coverage - 512`.
So the flag is a *display* flag. Still not ported: RWE draws the detection
rings but not the coverage ring.

### A correction to §82: `holdtime` does have a reader

`WORD wdef+0xFE` is read at `0x499E81` and `0x49C8D6`, both on the path that
retires the projectile the camera is following (`globals+0x142F7`), and stored
into `globals+0x1434B`. It is how long the view holds on the impact — the nuke
cam. Nothing in the simulation reads it.

### Decoded but not ported

- **`antiweapons`**, as above.
- **The 300-tick wait on a full magazine** is implemented, but nothing in RWE
  can reach 200 rounds in practice.

### Ported since

- **The queue button.** The launcher's build page is now loaded whether or not
  the unit is a `Builder`, the button is matched by substring, and the readout
  is rebuilt every frame — a magazine fills on its own with no command to hang
  a refresh on, which is the one way it does not behave like a build queue.
- **Interception, all five steps.** `interceptor`, `targetable` and `coverage`
  reach `WeaponDefinition`; a projectile carries the target slot; the
  auto-target scan hands an interceptor weapon the projectile search instead of
  the unit one; the launch is abandoned without a target; the aim point comes
  from the target projectile ahead of any unit; the proximity fuse and the blast
  that detonates every projectile in it are both in. `src/rwe/sim/interception.test.cpp`.

  Two places where RWE differs deliberately. The engagement gate on a weapon
  with `interceptor` is `coverage` rather than `maxRange`, checked against the
  target's aim point every tick rather than against its position, because the
  original's ordinary range check never runs for these weapons — the two that
  ship say `range=32000`, which is larger than any map. And a caught projectile
  is marked dead before its own explosion is run, so an interceptor caught in
  another interceptor's blast cannot chain back into it; the original has no
  such guard and no data that would exercise it.
- **Per burst, not per shot.** The cost check, the spend and the round out of
  the magazine all sit in the per-weapon routine that runs once a tick, and a
  burst is expanded afterwards from the projectile's own `+0x60` counter, so the
  original pays for a burst once. RWE charges each shot of a burst separately,
  as it always has for `energypershot`. Nothing in the shipped data can tell the
  difference: the four weapons with a burst — the flamethrower and the three
  EMGs — name no `energypershot`, no `metalpershot` and no `stockpile`.

## 22. The D-gun

There is less to it than the name suggests. `ARM_DISINTEGRATOR` — and its Core
twin — is an ordinary weapon:

```
rendertype=3;  lineofsight=1;  turret=1;  beamweapon=1;  noexplode=1;
commandfire=1;  model=dgun;
range=240;  reloadtime=1.2;  weapontimer=4;  weaponvelocity=200;
areaofeffect=48;  energypershot=400;  firestarter=70;
[DAMAGE] { default=30000; }
```

Every part of what it does comes out of that.

- **The damage.** 30000 is exactly the figure the armour cut-out at `0x489BD1`
  tests against (`cmp edi,0x7530`), so the shot goes through a `DamageModifier`
  that would otherwise take most of it off. Nothing anywhere in the binary
  special-cases the weapon; the number is the mechanism. RWE already has the
  cut-out (`ArmourBypassDamage`), so this half was in place.

  **30000 is the `rev31` figure.** `totala1` — the 1.0 data — says `default=5500`
  for both disintegrators, which is *below* `0x7530` and so does **not** bypass
  `DamageModifier`: against the 1.0 data an armoured target really does halve
  the D-gun. The "the number is the mechanism" reading still holds; it is just
  that the number was raised past the cut-out by a patch, and anyone replaying
  this arithmetic against 1.0 files should expect the other answer.
- **The cost.** 400 energy, no metal, weighed against the player's *stored*
  energy before the shot and taken as it leaves (`0x49E3ED`, `0x49E51F` — see
  the section above). A commander with a flat battery cannot D-gun.
- **The button.** `candgun` is bit 14 of `def+0x245`, pushed at `0x42CAA3` and
  masked with `shl eax,0xe` at `0x42CABA` — verified, and set by the two
  commanders and nothing else. `commandfire` on the weapon is what keeps the
  commander from disintegrating passers-by; `candgun` on the unit is what puts
  BLAST on its order menu. RWE was showing BLAST for any unit with a
  command-fire weapon, which would have put it on a nuclear silo.
- **The wreck.** There is no D-gun rule here either. `0x4864B0` kills a unit with
  the *damage type* as its cause (`unit+0xF5`, set from the damage descriptor at
  `0x489DAC`); causes 4, 5 and 9 leave nothing and skip the death script
  outright — **cause 9 is now named: it is a nanoframe decaying out, §92** —
  cause 7 forces a wreck, and everything else — the D-gun included —
  asks the unit's own COB `Killed` (the name is at `0x508BE8`, called through
  `0x4B0BC0` at `0x4865C3`) and takes the corpse level from what the script
  writes back into the local it is handed, 0 meaning none. The severity handed
  to the script is `clamp(1, 100, (100 * overkill / maxdamage + unit[0xF7]) / 2)`
  (`0x48655E`–`0x4865A4`), and a 30000-point hit on a 3000-point commander pins
  it at 100. So a disintegrated unit leaves nothing because its own script says
  so at that severity, not because the weapon asked.
- **The render type.** `rendertype=3` is the ordinary model type, which RWE
  already handles. The `ProjectileRenderTypeMindgun` TODO in
  `GameScene_util.cpp` is `rendertype=2`, which belongs to `MINDGUN` — a
  100-damage `unitsonly` beam that nothing in the shipped data uses. It is not
  the D-gun, and finishing it would change nothing.

### Decoded but not ported

- **The `Killed` severity formula** above. RWE runs `Killed` but does not feed it
  the original's overkill-derived severity, so what a script chooses to leave
  behind may differ.
- **`unit+0xF7`**, the second term in that severity, is unidentified.
- **The damage-type death causes** 4, 5 and 7 are decoded as a set but not
  individually named; nothing was traced far enough to say which weapon or
  event produces each. **Cause 9 is settled**: the `GetBuilt` mission fires
  `DamageUnit(self, self, 30000, 9)` at a frame that has decayed away, which is
  why that cause leaves nothing and skips the script — see §92.

## 23. The streaming economy

Metal and energy are not spent tick by tick. Consumers ask every tick, and once
a second the whole player's asking is added up and settled against one number.
That is what makes a shortfall feel like everything slowing down together
rather than some things stopping.

### The tick order

The sim step increments the game tick at `0x4954BD` and then runs its phases,
of which the per-player pass `0x464F80` is one. Inside that pass, each player
reaches `0x465077`:

```
465077  mov eax,[edi+0xf0]              ; the player's next economy tick
465083  cmp eax,[ecx+0x38a47]           ; against the game tick
465089  ja  0x4655a6                    ; not yet -> skip the rest of this player
46508f  add eax,0x1e                    ; +30
```

`global+0x38A47` is the game tick counter. `player+0xF0` is pushed thirty ticks
ahead every time it fires, so the settle at `0x46555A -> 0x401360` runs **once a
second per player**, staggered by whatever each player's counter started at. The
gate the call itself sits behind (`0x46554F`, a word at `global+0x39239` that
must be negative) is initialised to `0xFFFF` at `0x498199` and only ever moved by
the endgame sequences, so in ordinary play it is always open.

Everything between the settles just accumulates. `0x401360` then, in this order:

1. Sweeps the player's units, adding each one's `EnergyUse`, `EnergyMake`,
   `MetalMake`, extraction, wind and tidal for the second, and totalling the
   four numbers each unit carries.
2. Adds the player-level block at `player+0xEC`, which has the same layout.
3. Computes the two throttle fractions, `0x401A4D`.
4. Writes the leftover back as the stockpile and clamps it to storage,
   `0x401AB3`.
5. Sweeps the units again to hand back the unpaid remainder as debt and clear
   the accumulators, `0x401B37`.

### The per-unit block, `unit+0xBC`

Twelve floats, six per resource. The player-level block at `player+0xEC` is the
same layout with the display copies dropped.

| Offset | Energy | Metal |
|---|---|---|
| produced this second | `+0xBC` | `+0xD4` |
| asked for this second | `+0xC0` | `+0xD8` |
| granted this second | `+0xC4` | `+0xDC` |
| still owed | `+0xC8` | `+0xE0` |
| produced, last second (display) | `+0xCC` | `+0xE4` |
| asked for, last second (display) | `+0xD0` | `+0xE8` |

Unit stride is `0x118` (`0x401900`).

### The throttle, `0x401A4D`

A consumer asks through `0x4011C0`, which takes the block in `ecx` and an
energy and a metal amount:

```
4011c0  fld [ecx+0xc]   ; energy still owed
4011e0  test ah,0x41    ; owed <= 0 ?
4011e6  je  0x401218    ; no -> refuse, return 0
4011e8  fld [ecx+0x24]  ; metal still owed
4011f6  je  0x401218    ; same test, same refusal
4011f8..40120f          ; granted += both; return 1
```

**Nothing here looks at the stockpile.** The only gate on a consumer is whether
it is still paying off a previous shortfall, and the demand is recorded for the
display either way. Once a second `0x401A4D` runs twice, energy then metal, on
the totals:

```
S = stockpile + everything produced this second
D = everything still owed
N = everything granted this second

if D <= S: debtFraction = 1        ; R = S - D
else:      debtFraction = S / D    ; R = 0
if N <= R: newFraction  = 1        ; stockpile = R - N
else:      newFraction  = R / N    ; stockpile = 0
```

and `0x401B37` gives each unit back what it did not get paid for:

```
owed' = granted * (1 - newFraction) + owed * (1 - debtFraction)
```

So the answer to "who gets what" is: **a single global fraction per resource,
identical for every consumer**. Not first come first served, and no priority
between building, reclaiming and repairing — they are all the same call.
Debt is paid before anything new, and a debt too big to clear means nothing new
is paid at all this second.

The felt behaviour is a duty cycle rather than a slowdown. A builder that asked
for more than the player could afford is refused outright on the following
ticks, does no work at all, and resumes when its debt reaches zero — but because
the fraction is global, every consumer's duty cycle has the same shape, so at
the scale a player watches it looks like the whole base running at the same
reduced rate.

### Build rate, `0x41BA60`

Called as `(builder, target, amount)`. Every caller passes the same amount
(`0x402A09`, `0x403E43`, `0x404139`, `0x414235`, `0x414656`):

```
4029db  mov eax,0x88888889
4029e6  mov cx,[edx+0x1fe]     ; the BUILDER's workertime
4029ed  imul ecx               ; the standard signed divide by 30
4029fc..402a04                 ; amount = (float)(workertime / 30)
```

`workertime / 30` is an **integer** division, truncating. An ARMCK rated at 80
builds at 2, not 2.67 — a quarter of its rated worker time is thrown away by the
rounding — and anything below 30 would build at nothing at all.

`unit+0x104` is the target's remaining build fraction, counting **down** from
1.0 to 0.0; zero means finished, which is why `0x4016C5` tests it before
crediting `EnergyMake` and storage. One call does:

```
41bacd  fild [ecx+0x1ea]       ; the TARGET's buildtime
41bad3  fdivr [esp+0x28]       ; amount / buildtime
41bad7  fsubr st,st(1)         ; progress - that
41bae4..41bb1e                 ; clamped into [0, 1]
41bb32  fsub [esp+0x24]        ; delta = the fraction actually applied
41bb36  fld [ecx+0x186]        ; buildcostenergy * delta
41bb3c  fld [ecx+0x18a]        ; buildcostmetal  * delta
41bb72  call 0x4e43a0          ; hit points are trunc(progress * maxdamage),
41bb7d  call 0x4e43a0          ;   differenced across the step
41bc5a  lea ecx,[ebp+0xbc]     ; charged to the BUILDER's own block
41bc60  call 0x4011c0
41bc67  je  0x41bcaa           ; refused -> no progress at all this tick
```

So the per-tick spend is `buildCost * (workerTime / 30) / buildTime` for each
resource, the whole job takes `buildTime * 30 / workerTime` ticks, and the
answer to "what happens to progress when the spend is throttled" is: **nothing
happens to it**. Progress is applied in full on the ticks the request is
accepted and not at all on the ticks it is refused. The throttle acts on the
resources; the debt it leaves behind is what stops the builder next tick.

A negative amount runs the same routine backwards: `0x41BBA1` refunds
`buildcostmetal * delta` into the builder's metal production. Energy is not
refunded.

Replaying this against real FBI data, an ARMCOM (`WorkerTime=300`) building an
ARMSOLAR (`BuildTime=2495`, `BuildCostMetal=145`, `BuildCostEnergy=760`) takes
250 ticks — 8.33 seconds — and drains 17.43 metal and 91.38 energy per second,
which are the numbers the original shows.

### What counts as production

FBI keys, from the parse block at `0x42C27F`–`0x42C372` (each key's value is
stored by the instruction after the *next* key's push, so the pairing has to be
made call-to-store):

| Key | Offset | Type |
|---|---|---|
| `energymake` | `def+0x1C2` | float |
| `energyuse` | `def+0x1C6` | float |
| `metalmake` | `def+0x1CA` | float |
| `extractsmetal` | `def+0x1CE` | float |
| `windgenerator` | `def+0x1D2` | float |
| `tidalgenerator` | `def+0x1D6` | float |
| `cloakcost` | `def+0x1DA` | float |
| `cloakcostmoving` | `def+0x1DE` | float |
| `energystorage` | `def+0x1E2` | float |
| `metalstorage` | `def+0x1E6` | float |
| `buildtime` | `def+0x1EA` | dword |
| `workertime` | `def+0x1FE` | **word** |
| `makesmetal` | `def+0x22D` | byte |
| `buildcostenergy` | `def+0x186` | float, from an int |
| `buildcostmetal` | `def+0x18A` | float, from an int |

`buildcostenergy` and `buildcostmetal` are confirmed at a second site,
`0x42AD40`; `workertime` and `buildtime` by the chain that lands `sightdistance`
on the `def+0x202` already in §82.

**There is no `MetalUse` key.** `metaluse` does not appear in the binary at all,
only the display string `UNITMETALUSE`. Metal is spent by building, by weapons
and by nothing else.

The generation block at `0x4013AC` reads, for a unit that is finished and alive
(`unit+0x110` bit 28):

- If the unit has a switch (`unit+0x110` bit 29) it must be **on**
  (`unit+0x10E` bit 0) or it does nothing at all. If it has no switch, its
  `EnergyUse` is charged only while it is on *or* moving (`unit+0x110` bits 2–3,
  `0x401620`).
- `EnergyUse` **below zero is production**, not consumption (`0x401429`). This is
  how a solar collector works: ARMSOLAR has `EnergyUse=-20` and `EnergyMake=0`,
  so switching it off does stop it. Above zero it is charged through the same
  request path as everything else, and whether the request was accepted is the
  "powered" flag that gates what follows.
- Then exactly one of four, as an if/else chain: `extractsmetal` (times the
  metal under the footprint), `makesmetal`, `windgenerator`, `tidalgenerator`.
  The first two are gated on that powered flag; **wind and tidal are not** —
  they are what makes the power.
- `windgenerator` is multiplied by `global+0x37EDE` (`0x401575`), which
  `0x490D40` sets to `windspeed / global+0x37EC8`, and `0x37EC8` is `0x1388` =
  **5000**, set at `0x4918ED`. `0x490D5E` clamps that ratio to **1.0**, so a map
  whose wind blows harder than 5000 gains nothing by it.
- `tidalgenerator` is multiplied by `global+0x14267` (`0x4015DF`), the map's
  `tidalstrength`.
- `EnergyMake`, `MetalMake` and both storage figures are added at `0x4016C5`
  only when `unit+0x104` is zero, so **a unit still under construction produces
  nothing and stores nothing**. They are not gated on the switch.

A computer player is handicapped on every one of these: `0x40144E` multiplies
its production by 0.7 or 0.5 depending on `global+0x37EEE` (0 gives 0.5, 1 gives
0.7, anything else full), and the same pair of constants appears at each
production site including the starting stockpile at `0x465451` and reclaim at
`0x4026B9`.

### Storage and overflow

`player+0xA4` and `player+0xA8` are zeroed at the top of the settle and rebuilt
from the `EnergyStorage` and `MetalStorage` of every finished unit
(`0x40179B`–`0x4017CB`). A per-player base is added on top at `0x401988` when
`player+0x149` bit 0 is set, from `player+0xDC` and `player+0xE0`, which
`0x4661B1` and `0x4661C7` read from a save or scenario as `PlayerEnergyStorage`
and `PlayerMetalStorage`.

Overflow is dropped and counted: `0x401AB3` clamps the stockpile to the cap and
adds the excess to a lifetime waste total at `player+0xCC` / `player+0xD4`.

### Reclaim and death metal

Reclaiming a **feature** pays both resources in one lump from the feature
definition (`0x423907`, energy from `featdef+0xEC`, metal from `featdef+0xF0`),
into the reclaimer's own production, handicap and all.

Reclaiming a **unit** pays `trunc((1 - progress) * buildcostmetal)` at
`0x402666`, metal only, in one go, and then kills the target. Only the share
that was actually built comes back.

There is no death metal as such: a unit that dies leaves a wreck to be
reclaimed. The one exception is `0x486CAD`, which credits the **killer** with
`(1 - progress) * buildcostmetal` when the killing damage type nibble is `0x50`
— the D-gun.

### Weapons and cloak

`energypershot` and `metalpershot` (`wdef+0xC0` / `+0xC4`) and the cloak cost do
**not** go through the throttle. They are taken straight out of the stockpile
if it covers them and refused if it does not: `0x401220`, `0x401260` and
`0x4012A0` read the player through the block's back pointer at `block+0x30` and
subtract from `player+0x8C` / `player+0x98` there and then. Cloak, at
`0x40182F`, truncates its cost to an integer first and books the amount as
demand for the display.

### What RWE now does

RWE settles once a second already, and its build arithmetic was already right:
`workerTimePerTick = workerTime / 30` with the same integer division, and a
per-tick spend of `buildCost * workerTimePerTick / buildTime`. `EnergyMake`,
`MetalMake`, `ExtractsMetal`, `MakesMetal`, the "under construction produces
nothing" rule, the storage recompute, the overflow clamp and
`MaxUtilizableWindSpeed = 5000` all matched.

What did not, and now does:

- **The throttle.** RWE served consumers first come first served against the
  stockpile as it stood at that moment, and a consumer that could not be served
  simply lost the work. It now grants every request whose unit is out of debt,
  settles the second against one fraction per resource, and carries the
  remainder as debt on the unit that asked. `settleResourcePool` is `0x401A4D`
  and `UnitState::addResourceDelta` is `0x4011C0`.
- **Tidal generators**, which were not implemented at all: `TidalGenerator` was
  not parsed and the map's `tidalstrength` never reached the simulation.
- **The wind cap** was applied to the map's maximum wind speed at load rather
  than to the generation factor, which left a map whose *minimum* wind is above
  5000 with an inverted range to draw a speed from.
- **`isSufficientlyPowered`** was computed after the metal a unit makes had
  already been credited, so a metal maker was gated on the previous second's
  answer. The original decides it from the same second's energy draw.
- The make-and-use pass now runs **before** the settle rather than after it, so
  a generator's output pays for the work beside it in the same second instead of
  the next one.

### Not ported, and why

- **The AI production handicap.** RWE has its own `resourceBonusFor`, which
  scales a computer player's income up for Brutal rather than down for easy and
  medium. Replacing one tuning knob with another is not a compatibility
  question, and RWE's AI is not the original's.
- **The per-player base storage.** The original's comes from
  `PlayerEnergyStorage` and `PlayerMetalStorage` on the player, gated by a flag
  read out of a save or scenario file; where a skirmish gets its own values from
  has not been found. RWE keeps giving the commander the side's starting
  stockpile as storage instead.
- **Reclaiming a unit** is instantaneous in the original, metal only, and
  RWE's is gradual and pays energy too. Changing it would be a gameplay
  decision rather than a correction, and `0x402640` is reached from one caller
  that has not been identified.
- **`MetalUse`** is an RWE extension with no counterpart in the original. It is
  still parsed and charged; no stock unit sets it, so it costs nothing to keep.
- **The if/else chain** between extraction, `MakesMetal`, wind and tidal. RWE
  runs all four independently. No unit in the game data sets more than one of
  them, so the two agree on real data.
- **The lifetime production, demand and waste totals** at `player+0xAC` through
  `player+0xD4`. They are statistics for the end-of-game screen, which RWE
  does not have.

## 24. Small systems: hit density, regrowth, kamikaze, paralysis, move rate

Five small keys that the shipped data sets and RWE parsed but never read. Four
of them turned out to have real machinery behind them. One of them does not
exist in the binary at all.

### `hitDensity` is not read by the original — the pass-through guess is wrong

The string `hitdensity` **does not occur anywhere in `TotalA.exe`**, in any
case, and not in `TAE.EXE` or any of the shipped DLLs either. The feature TDF
parser at `0x4224C9`–`0x422B30` pushes every key it reads as a literal — the
same `push <keystring>` / `call 0x4C46C0` pipeline the FBI parser uses — and
`hitdensity` is not among them. 559 features name it; the engine of this build
ignores every one.

So §90's note that it is "very likely the pass-through chance for projectiles
hitting features" is **refuted**, not merely unconfirmed.

What the original actually does with a shot and a feature is at `0x49B2B3`,
inside the per-projectile collision check `0x49B090`:

```
49b2b3  mov dx, WORD PTR [ebx+0x8]     ; the map square's feature slot
49b2bd  cmp dx, 0xfffb                 ; >= -5 is a marker, not an index
49b2dc  cmp dx, 0xfffe                 ; -2: follow the back-reference at
49b2e7  ...                            ;     [sq+0xa]/[sq+0xb] to the anchor
49b31b  ecx = featureDefs + (idx << 8) ; a feature definition is 0x100 bytes
49b32e  mov al, BYTE PTR [ecx+0xfa]    ; the definition's height
49b334  mov dl, BYTE PTR [ebx+0x6]     ; the square's ground height
49b337  add eax,edx                    ; the top of the feature
49b339  movsx edx, WORD PTR [esi+0xa]  ; the shot's y
49b33f  jle miss                       ; above the top -> straight through
```

That is the whole test: same map square, and the shot below the feature's top.
There is no roll and no density. A smudge, whose definition height is zero, is
therefore transparent by geometry rather than by chance, and a rock is solid.
RWE already does exactly this (`GameSimulation.cpp`, `projectileCollides`), so
**nothing was implemented for this entry** — only `hitdensity.test.cpp`, which
fires the same shot at the same rock at each of the four densities the shipped
data actually uses (0, 5, 10, 100) and requires it to stop every time.

Two map-square facts fell out and are worth keeping: a square record is **13
bytes** (`0x481550` indexes `base + 13*(y*width + x)`), and a square is 16 world
units across (`0x4815A0` takes `pos >> 20` of a 16.16 coordinate).

### Feature regrowth: `reproduce` is a percentage, and the shipped data is all zero

Both keys are parsed at `0x422A13` / `0x422A27` into **bytes** at `feat+0xFC`
and `feat+0xFD`, and both have a reader — the tail of the per-tick world update
at `0x424050`:

```
4240a3  ecx = [globals+0x14257]         ; the sweep cursor
4240a9  dec ecx                         ; one square a tick, walking backwards
4240bc  jns 0x4240d7                    ; ran off the bottom?
4240be  cursor = width*height - 1       ; reload and idle this tick
4240e9  ax = WORD [sq+0x8]              ; a feature type must stand here
4240f7  test BYTE PTR [sq+0xc],0x1      ; and it must be the anchor square
42410f  push 0x64 ; call 0x4b6c30       ; rand(100)
42411a  cl = BYTE PTR [esi+0xfc]        ; `reproduce`
424122  jge nothing                     ; rand >= reproduce -> no seed
42414a  dl = BYTE PTR [esi+0xfd]        ; `reproducearea`
424158..424178                          ; dx, dy = rand(area) - area/2
42417c  call 0x481550                   ; the destination square
424189  cmp WORD PTR [ecx],0x0          ; a unit on the SOURCE square vetoes it
42418f  cmp WORD PTR [eax+0x8],0xffff   ; the destination must be truly empty
4241a3  call 0x423c50                   ; plant the parent's own type there
```

So `reproduce` is a **percentage out of 100, not a flag**, the period is one
full sweep of the map (width × height ticks — about two and a quarter minutes on
a 64×64-square map), the seed is a uniform offset in a `reproducearea` box
either way, and what stops a wood covering the map is that the destination
square's feature slot must be exactly `-1`: not blocked, not a continuation of
somebody else's footprint, not already planted.

**Every feature in the shipped data sets `reproduce=0`.** All 83 that name the
key set it to zero, in `rev31`, `totala1` and `totala2` alike, with
`reproducearea=6` beside it. In stock Total Annihilation a cleared forest never
grows back; the machinery is there and the data switches it off. RWE now
implements the rule (`GameSimulation::updateFeatureRegrowth`) so a mod can use
it, and `FeatureDefinition::reproduce` became an `unsigned int` to carry the
percentage.

One deliberate difference. The original derives the row from the cursor by
dividing by the map's **height** (`0x424142`) where every other square lookup in
the binary divides by the width — `0x481550` and the feature placer `0x423CCD`
both index `y*width + x`. On a square map the two agree; on any other map the
original plants seeds in the wrong row and, when the map is wider than it is
tall, drops them off the end entirely. RWE divides by the width.

### Kamikaze: an ordered run, not a proximity fuse

`kamikaze` is bit 28 of `def+0x241` (parsed `0x42CB18`, stored `0x42CB1D`) and
`kamikazedistance` the word at `def+0x218` (parsed `0x42CB29`, stored
`0x42CB32`). Two units in the shipped data set them: the Roach at 40 and the
Invader at 80. Neither has a weapon of any kind.

The mechanism is an **order**, not an automatic detonation:

- `0x43F38A` is the command-name resolver. Given a unit whose definition has bit
  28, an attack turns into the mission named `ATTACK_KAMIKAZE` (`0x5053AC`), so
  the player has to point the thing at something.
- The mission handler is `0x403260`. In its first state (`0x403336`) it reads
  `WORD [def+0x218]`, **clamps it up to a minimum of 16** (`0x403364`), and
  plants a move-to-target sub-order with that arrival radius.
- In its second state (`0x4032B4`), on arrival, it allocates a fresh order named
  `SELFDESTRUCT` (`0x501520`) and hands it to the unit (`0x4032F7`). The blast
  is therefore the ordinary self-destruct one — `SelfDestructAs`, `CRAWL_BLAST`
  rather than the smaller `CRAWL_BLASTSML` the unit explodes as when something
  else kills it.
- The two other readers of `def+0x218` are cosmetic: `0x4391B6` draws the
  selected unit's range ring and `0x4393E6` labels a debug circle with the
  literal string `kamikazedistance`.

There is one thing not ported. The original also lets a kamikaze *chase*:
`0x407025` tests bits 16 and 28 of `def+0x241` together inside the
"I have been attacked" reaction, and `0x40B901` skips the usual reachability
test for one. RWE's own auto-targeting needs a weapon to pick a target at all,
so a Roach still only goes where it is sent.

### Paralysis: a stun with no damage, and it stacks

`paralyzer` is bit 7 of `wdef+0x111` (parsed `0x42EB95`), and the game data's
own comment in `WEAPONS.TDF` says what it means outright:

> `paralyzer = Weapon will stun the enemy for a length of time described in the damage field, time=ticks.`

The binary agrees, and adds the detail that matters most:

```
499e20  ecx = [wdef+0x111]
499e26  shr ecx,7 ; and cl,1
499e32  inc ecx                     ; damage TYPE = paralyzer ? 2 : 1
499e37  call 0x489bb0               ; armour, both veterancies, then the message
...
489deb  cmp BYTE PTR [msg+0x8],0x2  ; type 2 -> the paralysis branch
489def  jne 0x489eb1                ;           which never returns to
489eb5  sub WORD PTR [esi+0x108],ax ;           the hit-point subtraction
```

So a paralyzer hit takes **no hit points at all**. It is the only thing that
makes the EMP missile's `[DAMAGE]` table — 1800 against every CORE unit —
sensible: 1800 ticks is sixty seconds, not eighteen hundred points.

The rest of the branch:

- `0x489E39` — `immunetoparalyzer`, **bit 26 of `def+0x241`** (parsed
  `0x42C7FA`), skips the whole thing. Both commanders set it.
- `0x489E69` — if the victim is **already** stunned, the new duration is *added*
  to what is left rather than replacing it.
- `0x402D33` — the order handler clamps the total to `0x708` = **1800 ticks, 60
  seconds**, which is exactly what one EMP missile buys anyway.
- `0x402D46`–`0x402D5F` — entering the stun clears all three weapons' aim.
- `0x402D10` — the stun *is* an order, sleeping for the duration in front of the
  unit's own orders, so a unit resumes what it was doing when it comes round.
- The duration is the damage number **after** armour class lookup,
  `DamageModifier` and both veterancy adjustments, since it is the `WORD` at
  `msg+5` that `0x489BB0` wrote.
- Targeting: a paralyzer weapon declines a target that is already stunned
  (`0x408AFF`, `0x40B972`, testing bit 4 of `unit+0x10E`).

Not ported, and marked as inference in the code: exactly what a stunned unit
*can* still do. The paralysed flag itself is read at only those two targeting
sites and has no handler in the flag setter `0x48B090`, so everything that stops
a stunned unit acting in the original is a consequence of the sleeping order
holding its order slot. RWE takes that literally — a stunned unit stops moving,
stops firing, stops building and keeps its orders — but the equivalence is
reasoning about the order machine, not a gate read out of the binary.

Two conditions in the paralysis branch are **networking, not game rules**, and
were deliberately not ported: `[victim+0x110]` bit 28 set with bit 14 clear is
the general "alive and finished" test, and `[victim+0x96][0x73] ∈ {1,2}` is the
same player-type check that gates declaring a unit dead at `0x489ED1` and
sending the damage message at `0x489C99` — it means "this machine owns the
simulation of that unit".

### `MoveRate1` / `MoveRate2`: three speed bands, and why the Atlas needs the first

The two keys are read at `0x42C1EF` and `0x42C217` into 16.16 values at
`def+0x1AE` and `def+0x1B2`, and the **default supplied to the reader is
`MaxVelocity * 2`** (`0x42C1E6` and `0x42C206` both load `[def+0x192]` and shift
it left one). That is the whole reason this matters: a threshold at twice the
unit's top speed is one it can never cross, so a unit that names neither key is
in the first band the whole time it is moving.

The band machine is `0x43DA70`, called from the movement update:

```
43da87  eax = DWORD [mov+0x20]      ; the unit's current speed
43da8c  ...                         ; zero (and no turn) -> band 0
43da9a  cmp eax, [ecx+0x1ae]        ; <= MoveRate1  -> band 1
43daa9  esi = [ecx+0x1b2]           ; <= MoveRate2  -> band 2, else band 3
43dac7  ecx = ([unit+0x110] >> 2) & 3   ; the band it was in
43dacf  je  done                    ; unchanged -> run no script at all
43dad5  push 0x50523c               ; new band 0 -> "StopMoving"
43dadf  test al,0xc                 ; old band 0 -> "StartMoving" first
43dafa..43db1c                      ; then "MoveRate1"/"2"/"3"
43db35  store the new band in bits 2-3 of [unit+0x110]
```

Seven units name `moverate1` (ARMFIG, ARMHAWK, ARMLANCE, CORTITAN, CORVENG and
CORVAMP at 8; CORVALK at 1) and one names `moverate2` (CORVALK at 2). Only five
of the two hundred shipped scripts define any `MoveRate` function at all:

- **ARMATLAS** and **CORVALK** define all three. Each one signals its flame
  thread dead, restarts `ProcessFlames`, and sets a static to 1, 2 or 3;
  `ProcessFlames` loops on `emit-sfx 0` from the three thruster pieces every 67
  ms while that static is 1 or 2, and `StopMoving` sets it to 0. The Atlas names
  no threshold, so it lives in band 1 and it is `MoveRate1` that lights it — the
  §4 finding, now with the reason attached.
- **ARMFIG**, **ARMHAWK** and **CORVENG** define only `MoveRate2`, and all three
  bodies are word-for-word the same: a one-in-ten roll, guarded by a static so
  it cannot re-enter, then `turn base around z-axis` to 21845, 32768 and back to
  0 — a **barrel roll**. With `MoveRate1=8` against a `MaxVelocity` of 10 or 11,
  that is a fighter rolling occasionally once it is up to speed.

RWE now runs the same machine (`UnitBehaviorService::updateMoveRateBand`), with
the band kept on `UnitState::moveRateBand`. One difference: the original reads
its movement object's speed word, and treats a unit that is turning on the spot
with no linear speed as moving (`0x43DA8E` also tests `WORD [mov+0x24]`); RWE
measures the distance the unit actually covered this tick and calls anything
under a tenth of a unit stopped.

### What a blast does to a feature, `0x4244B0`

> **Ported, 2026-09-11** (B4 #45). The blast gate in `doProjectileImpact` is
> `!indestructible` and nothing else; the damage added is the weapon's default
> damage, unscaled; the feature breaks to its `featuredead` the moment what it
> has taken reaches its `damage`; the feature reader defaults a missing
> `damage` to zero; and a feature stood up by the blast in progress is not met
> again by it. `wreckage.test.cpp` pins each. Not ported: the fire branch
> below, which RWE still handles with its own chance roll in
> `tryIgniteFeaturesInRadius`.

The area walkers (`0x455496` from the blast at `0x4554xx`, and `0x49A626`)
call `0x4244B0(cellRecord, x, z, wdef)` for each map square in the blast. The
routine loads the globals pointer and falls into `0x4244B6`, which is why no
call to the latter address exists. In order:

1. `[globals+0x37F2F] & 8` must be set. That word is the console-toggle word
   (`NoShake` is its bit 4, §"Screen shake"); bit 3 is **`TreeDeath`**
   (handler `0x416E30`, record at `0x502118`), and the session set-up at
   `0x430E90` turns it on. `TreeDeath 0` makes every feature immune.
2. The square must hold a feature: `[cell+0x8]` below `0xFFFB`.
3. Its definition (`[globals+0x1426F] + index * 0x100`) must not be
   `indestructible` (`[featdef+0xFE]` bit 9). **That is the whole of the
   gate.** `blocking`, `reclaimable` and the rest are never read here, so a
   rock, a bush, a wreck and a scar decal are all fair game.
4. In a multiplayer session (`0x435100` = 3) a peer that is not the local
   authority (`[player+0x97]` bit 0 clear) does not apply the damage; it packs
   `(x, z, wdef+0x10A)` into a type `0xF`/`0x6` message and sends it
   (`0x450030`, `0x44FDB0`, `0x451BC0`). The rest is the local path.
5. **Fire first.** If the definition is `flamable` (bit 4), the weapon's
   `firestarter` byte (`wdef+0x10B`) is non-zero, and the feature is not
   already burning (`[cell+0xC]` bit 0), the feature is set alight
   (`0x4233A0`, §"Burning") and takes **no damage**. No chance roll happens
   here; whatever `firestarter`'s percentage means, it is not tested on the
   way in.
6. Otherwise the weapon's default damage, the word at `wdef+0xD4` (the
   `default` key, parsed at `0x42EF9A`-`0x42EFA9`), is added to what the
   feature has taken: `[cell+0xA]` for a feature that is not burning, or the
   burn record's `[+0x26]` (the 48-byte table at `globals+0x1420B`, found by
   the index in `[cell+0xA]` and checked against `[+0x28]`/`[+0x2A]`) for one
   that is. There is no distance falloff and no `edgeeffectiveness`; those are
   for units.
7. If the total reaches `[featdef+0xEA]`, the definition's `damage`, the
   feature is removed and its `featuredead` stood up (`0x423550(x, z, 0)`).

**The default `damage` is zero.** The feature parser (`0x422A20`) reads every
integer key through the integer-with-default helper `0x4C46C0`, passing the
same register as the default for the whole block, and that register is zeroed
once at `0x4226F4`. `damage` comes off the stack at `0x422A87` straight into
`featdef+0xEA`. So the 145 shipped features that omit the key (the `LightScar`
and `CarScar` decals among them) go on the first hit, and the 378 that say
`damage=20000` (the `Sl-RockScar` family) take the hit like anything else and
in practice never fall. RWE's reader used to default the key to 1, which for
the blast was the same thing and for reclaim work was a floor it did not need.

**On the flag word's other bits**, since they had to be read to find bit 3:
`Drop` is bit 0, `ShareMetal` bit 1, `0x416E00`'s command bit 2, `TreeDeath`
bit 3, `NoShake` bit 4, `Clock` bit 6, `0x417030`'s bit 7, `0x417060`'s bit 8,
`0x417090`'s bit 9, `ShootAll` bit 10.

## 25. The detection rings on the minimap

Sections 25 to 29 read the same part of the game -- what the interface draws
over the world and on the minimap -- and share a piece of notation. `cfg` is
the game-state block whose pointer lives at `0x511de8`: a single
`malloc(0x3924D)` allocated and zeroed at `0x41D920`, so every `cfg+` offset
below is a byte displacement into that one allocation. Unit structures are
`0x118` bytes in a flat array between `[cfg+0x14357]` and `[cfg+0x1435B]`.
World positions are 32-bit fixed point with 16 fractional bits, so `[unit+0x6A]`
is x, `+0x6E` is y and `+0x72` is z, and the *integer* parts are the words at
`+0x6C`, `+0x70` and `+0x74`.


The whole minimap - TA's own code calls it the *radar screen*; the allocation
tags are `"RADAR PICTURE"`, `"RADAR MAPPED"`, `"RADAR FINAL"`,
`"HOT RADAR UNITS"` - is redrawn each frame by `0x466DC0`. That routine walks
the entire unit array once, plots each unit's dot, and, for units that are
selected, draws its detection rings. **The rings are drawn only on the minimap.
There is no ring for `sightdistance`, `radardistance` or `sonardistance` in the
3D view at all**; the 3D view has a different and much smaller set of circles,
described at the end of this section.

### Which ranges get a ring

Four, and a fifth family for anti-missile units. Read straight off the unit
type struct:

```
466f42   mov    edx,DWORD PTR [ebx+0x110]
466f48   shr    edx,0x4
466f4b   test   dl,0x1                       ; unit is SELECTED
466f4e   je     0x46713b                     ; -> no rings at all
466f54   test   BYTE PTR [ebx+0x10e],0x1     ; unit is switched ON
466f5b   jne    0x466f70
466f5d   mov    eax,DWORD PTR [ebx+0x92]     ; unit type
466f63   test   BYTE PTR [eax+0x245],0x4     ; onoffable=1
466f6a   jne    0x46707c                     ; off + onoffable -> skip the four
466f70   mov    ecx,DWORD PTR [ebx+0x92]
466f76   mov    cx,WORD PTR [ecx+0x204]      ; radardistance
466f7d   test   cx,cx
466f80   je     0x466fb3
466f82   mov    eax,DWORD PTR [esp+0x14]     ; the colour table, cfg+0xDCB
466f88   movsx  ecx,cx
466f8b   mov    dl,BYTE PTR [eax+0xa]        ; colour = cfg[0xDCB + 0x0A]
466f8e   movsx  eax,WORD PTR [esi+0x142eb]   ; minimap width in pixels
466f95   imul   eax,ecx
466f98   push   edx                          ; colour
466f9a   idiv   DWORD PTR [esi+0x1422b]      ; / map width in world units
466fa4   push   eax                          ; radius, in minimap pixels
466fa5   push   ebp                          ; centre y
466fa6   push   edi                          ; centre x
466fa7   push   edx                          ; the minimap surface
466fa8   call   0x4c0070                     ; DrawCircle
```

The same block repeats verbatim for three more fields, then a fifth, different
one:

| Field | Type offset | Colour byte | Where |
|---|---|---|---|
| `radardistance` | `+0x204` | `cfg[0xDCB+0x0A]` = `cfg+0xDD5` | `0x466F70` |
| `sonardistance` | `+0x206` | `cfg[0xDCB+0x0A]` = `cfg+0xDD5` | `0x466FB3` |
| `radardistancejam` | `+0x20A` | `cfg[0xDCB+0x0C]` = `cfg+0xDD7` | `0x466FF6` |
| `sonardistancejam` | `+0x20C` | `cfg[0xDCB+0x0C]` = `cfg+0xDD7` | `0x467039` |
| anti-missile `coverage` | weapon `+0xE0` | `cfg[0xDCB+0x0F]` = `cfg+0xDDA` | `0x46707C` |

**`sightdistance` (`+0x202`) does not get a ring.** It is read one slot away
from `radardistance` and is conspicuously skipped. Nor does `mintime`,
`maneuverleashlength`, `builddistance` or anything else. A unit can draw up to
four rings at once - a unit with radar, sonar and both jammers would show four
concentric circles in two colours - and a plain Radar Tower shows exactly one.

Those five field offsets are confirmed twice over: the FBI parser at
`0x42C395`-`0x42C40B` stores them in that order (remembering that the parser's
store lands *after* the next key has been pushed, so `sightdistance`->`+0x202`,
`radardistance`->`+0x204`, `sonardistance`->`+0x206`,
`radardistancejam`->`+0x20A`, `sonardistancejam`->`+0x20C`), and the debug
overlay at `0x439202` independently labels the same offsets with the literal
strings `"sight"`, `"radar"`, `"sonar"`, `"radarjam"`, `"sonarjam"` at
`0x505188`, `0x505180`, `0x505178`, `0x50516C`, `0x505160`. `+0x208` sits in
the gap and is `mincloakdistance`, labelled `"mincloak"`.

### The on/off gate

`[unit+0x10E] & 1` is the unit's activated flag and `[type+0x245] & 4` is
`onoffable=1` (FBI key `onoffable` at `0x503AE4`, inserted at bit 2 of the dword
at `+0x245` by `0x42C8AE`). A radar tower the player has switched off is
`onoffable` with the activated bit clear, and the branch at `0x466F6A` jumps
past all four rings. **Switching a radar off makes its ring disappear.** The
anti-missile coverage ring is not behind that gate - the jump lands at
`0x46707C`, after the four but before the coverage block.

### The anti-missile coverage rings

```
46707c   mov    eax,DWORD PTR [ebx+0x92]
467082   mov    ecx,DWORD PTR [eax+0x241]
467088   shr    ecx,0x1d
46708b   test   cl,0x1                      ; antiweapons=1
46708e   je     0x46713b
467094   lea    edx,[ebx+0x10]              ; &unit.weapon[0], stride 0x1C
467097   mov    DWORD PTR [esp+0x20],0x3    ; three weapon slots
4670a3   mov    eax,DWORD PTR [esp+0x18]
4670a7   mov    ecx,DWORD PTR [eax]         ; the weapon definition
4670a9   mov    edx,DWORD PTR [ecx+0x111]
4670af   shr    edx,0x1e
4670b2   test   dl,0x1                      ; interceptor=1
4670b5   je     0x467121
4670b7   movsx  eax,WORD PTR [esi+0x142eb]
4670be   mov    ecx,DWORD PTR [ecx+0xe0]    ; the weapon's `coverage`
4670c4   sub    ecx,0x200                   ; minus 512
4670ca   imul   eax,ecx
4670ce   idiv   DWORD PTR [esi+0x1422b]
```

`antiweapons` is bit 29 of the dword at `[type+0x241]` (FBI key at `0x503AF8`,
inserted by `0x42C86F`); `interceptor` is bit 30 of `[weapon+0x111]` (weapon
TDF key at `0x5040D4`, inserted at `0x42EAAC`); `coverage` is the dword at
`[weapon+0xE0]` (weapon TDF key at `0x5042C8`, stored at `0x42E540`, confirmed
by the debug label `"weapon %d - coverage"` at `0x5051AC`). The `-512` is
literal and unexplained - I read it as TA shrinking the drawn circle by half a
map square so it does not overstate the guaranteed intercept area, but that is
inference.

If the byte at `[unit.weaponSlot+0xE]` is non-zero the ring goes through
`0x4C01A0` instead of `0x4C0070` - the same circle, but **dashed**: 32 segments
of which alternate ones are drawn, with the starting parity taken from
`[cfg+0x142F1] & 1`, the minimap's blink bit. The dashes therefore swap over on
each blink, giving a chase pattern. The solid form is used otherwise.

### Shape, and how the world radius becomes pixels

A **circle**, not a diamond, and emphatically not the LOS shape. TA's actual
line of sight is the octant-mirrored raster in `gamedata/LOS.TDF` (tables for
radii 1 through 12, `numlines` growing 2, 4, 4, 6, 8, 10, 12, 12, 14, 16, 18,
20); the minimap ring makes no attempt to match it. It is a plain 32-gon:

```
4c0087   mov    ebx,0x800                  ; angle step: 0x10000 / 32
4c008c   lea    ebp,[edi+eax*1]            ; first point = (cx + r, cy)
4c0095   push   edi                        ; r
4c0096   push   ebx                        ; angle
4c0097   call   0x4b7123                   ; -> r*cos
4c00a7   add    esi,ecx                    ; x = cx + r*cos
4c00a5   push   edi
4c00a6   push   ebx
4c00a9   call   0x4b70ef                   ; -> r*sin
4c00c3   add    edi,ecx                    ; y = cy + r*sin
   ... clip, then 0x4CC7AB = 1-pixel Bresenham line, colour = arg5 ...
4c017b   add    ebx,0x800
4c0183   cmp    ebx,0x10000
4c018d   jle    0x4c0091
```

So: **32 straight segments, one pixel wide, solid, closed** (the step runs from
`0x800` to `0x10000` inclusive, bringing the last vertex back to the start).
`0x4CC7AB` is a hand-written Bresenham that pokes single bytes into the
surface; there is no thickness and no antialiasing.

The radius conversion shows it is a circle in *minimap pixel* space, not world
space:

```
radiusPx = range * [cfg+0x142EB] / [cfg+0x1422B]
```

`[cfg+0x142EB]` is the minimap width in screen pixels and `[cfg+0x1422B]` the
map width in world units - the same pair used to place the unit dots
(`0x466E83`) and, in reverse, the view rectangle (`0x466B70`). Every ring uses
the **horizontal** scale, including for its vertical extent; the vertical scale
`[cfg+0x142ED]/[cfg+0x1422F]` is used for the *centre* but never the radius. On
a map whose minimap is not scaled identically in both axes the ring is
therefore slightly wrong vertically, and TA does not care.

The centre is the unit's minimap dot:

```
mx = ux          * [cfg+0x142EB] / [cfg+0x1422B]
my = (uz - uy/2) * [cfg+0x142ED] / [cfg+0x1422F]
```

with `ux`, `uy`, `uz` the integer parts at `[unit+0x6C]`, `+0x70`, `+0x74`.
Note the `z - y/2`: TA's minimap is in the same sheared space as the 3D view,
not a true plan view.

### Which units, and enemies

The loop is over *all* units in the world, and each one that is (a) drawn on
the minimap at all and (b) has `[unit+0x110] & 0x10` set gets its rings. Bit 4
of `[unit+0x110]` is the selection flag - the box-select routine at `0x48DB80`
sets and clears exactly that bit (`or edx,0x10` / `and edx,0xFFFFFFEF`) while
sweeping the unit range. So **every selected unit draws its rings, not just
one.** Ten selected radar towers give ten circles.

Enemy units are gated earlier, at `0x466E5C`, before the position is even
computed: unless the global reveal flag is on, a unit that is not the local
player's and does not have `[unit+0x110] & 0x300` (the visible / on-radar bits)
is skipped entirely. An enemy unit you can see and have clicked on would reach
the ring code, and nothing there re-checks ownership - so in principle
selecting a visible enemy radar shows its coverage. I did not confirm that TA
ever sets the selection bit on a unit you do not own; treat that last step as
unverified.

### The 3D view's own circles - a different thing entirely

`0x4390A0` does draw circles in the world, but not these. It is one of five
handlers hung off the *order overlay* pass (next section), it runs **only while
Shift is held**, and in a retail build it draws just two things:

* `mincloakdistance` (`[type+0x208]`), and only if the unit is currently
  cloaked (`[unit+0x10E] & 4`, the bit the visibility test at `0x465AC0` keys
  off). Colour `cfg+0xDDA`.
* For a `kamikaze` unit (bit 28 of `[type+0x241]`, FBI key `kamikaze` at
  `0x503A20`, inserted at `0x42CB12`), a ring whose radius sweeps outward and
  restarts every 60 ticks:

```
439137   mov    si,WORD PTR [eax+0xd6]        ; the weapon's areaofeffect
43913e   mov    eax,DWORD PTR [ecx+0x38a47]   ; global tick counter
439144   mov    ebp,0x3c
439149   div    ebp                           ; edx = tick % 60
43914b   shr    esi,1                         ; areaofeffect / 2
439157   imul   edx,esi
43915a   shl    edx,1
43915c   mul    edx                           ; \ divide by 30
43915e   shr    edx,0x5                       ; /
439161   cmp    edx,0x8                       ; floor of 8
439168   cmp    ebp,esi                       ; ceiling of areaofeffect/2
```

  i.e. `r = clamp((tick % 60) * areaofeffect / 60, 8, areaofeffect/2)` - it
  expands to the blast radius over the first thirty ticks and then sits there
  for thirty more. Followed by one static ring of `kamikazedistance`
  (`[type+0x218]`) or `sightdistance` depending on a per-unit condition.

Everything else that routine can draw - sight, radar, sonar, both jammers,
mincloak, builddistance, maneuver leash, kamikazedistance and all three weapon
ranges, each with its name printed beside it - is behind `[cfg+0x391BF]`, a
developer flag, and is unreachable in a shipped game. That is where the label
strings `"sight"/"radar"/"sonar"/"radarjam"/"sonarjam"/"mincloak"/"build
distance"/"maneuver"` at `0x505144`-`0x505190` live. They are useful as an
independent check on the field offsets and nothing else.

The world circles are drawn by `0x438EA0`, worth a note because it is *not* the
flat minimap circle: it samples the ground height under every vertex (`call
0x485070`, then `max(unitY, groundHeight)`) so the ring drapes over terrain,
and its segment count is proportional to radius -
`segments = (int)(r * 2*pi * 0.125)` from the doubles `6.28318530717958` at
`0x4FD2B0` and `0.125` at `0x4FD2B8`, i.e. one segment per eight world units of
circumference.

### The colours: what I could and could not read

Every colour above is a byte fetched at runtime from a small table at
`cfg+0xDCB`, indexed as `cfg[0xDCB + n]`. The ones in play are `n = 0x0A`
(radar and sonar), `n = 0x0C` (both jammers), `n = 0x0F` (anti-missile
coverage), plus `n = 0x01`/`0x03`/`0x09`/`0x0A` for the 3D selection brackets.

**Since resolved -- see §50.** The scan found no stores because the writer
addresses the table from a different base: `0x4AC7D0` fills it as
`cfg+0x519+0x8B2`, nearest-matching `GUIPAL.PAL` into the screen palette at
game-screen init. GUIPAL's first sixteen entries are the standard VGA text
palette, so the slots resolve exactly: `0x0A` (radar and sonar) is VGA light
green, landing on palette 233 = (83,223,79); `0x0C` (both jammers) is VGA
light red, landing on palette 211 = (255,71,0) -- the jammer rings are
orange-red, not any green; `0x0F` (anti-missile coverage) is white.

What I can say: `palettes/PALETTE.PAL` puts TA's green ramp at indices 232-239,
`232 = (123,255,119)` bright down to `239 = (11,31,0)` near black, and the
waypoint artwork in the next section is painted from `232`-`236`. A green ring
is consistent with that ramp. The cheap way to settle the exact index is to
break on `0x466FA8` in a debugger and read `edx`.

### Implementation spec - minimap rings

For each selected unit, in the minimap render:

1. Skip unless the unit is drawn on the minimap at all (own unit, or
   visible/on-radar).
2. Centre: the unit's existing minimap dot position.
3. Unless the unit is switched off while being `onoffable`, draw a circle for
   each of `radardistance`, `sonardistance`, `radardistancejam`,
   `sonardistancejam` that is non-zero. Skip `sightdistance`.
4. Radius in minimap pixels is `range * minimapWidthPx / mapWidthWorldUnits`
   for all four, using the horizontal scale even for the vertical extent.
5. Draw a closed 32-segment polygon, one pixel wide, solid. At these sizes the
   flat facets are visible top and bottom of a large ring - that is correct;
   do not substitute a smooth circle if you want to match.
6. Radar and sonar share one colour; the two jammers share a second. Pick two
   greens from the 232-239 ramp until the indices are recovered; radar/sonar
   should be the more prominent.
7. Separately, for a unit with `antiweapons=1`, draw one ring per weapon slot
   whose weapon has `interceptor=1`, radius `(coverage - 512)` scaled the same
   way, in a third colour. Dash it - 16 of 32 segments, parity flipping on the
   minimap blink - to match exactly.
8. Draw nothing in the 3D view for sight/radar/sonar. TA's only 3D circles are
   the cloaked-unit `mincloakdistance` ring and the kamikaze pulse, both gated
   on Shift being held.

---

## 26. The marching waypoint trail

Everything in this section is drawn by one pass, `0x48CC30`, called from the
frame render at `0x469BFC` - and gated:

```
469bdc   push   0xf9
469be1   call   0x4c1b80          ; IsKeyDown
469be6   test   eax,eax
469be8   je     0x469c01          ; nothing drawn
469bea   mov    edx,DWORD PTR ds:0x511de8
469bf4   add    edx,0x142f3       ; the view context
469bfa   push   edx
469bfb   push   eax
469bfc   call   0x48cc30          ; the order overlay pass
```

`0x4C1B80` is a `GetKeyState` wrapper with an alias table; `0xF9` resolves
through the byte table at `0x4C1C6C` and the jump table at `0x4C1C48` to case 5
at `0x4C1BB5`, which is `push 0x10; call GetKeyState` - **`VK_SHIFT`**. So the
answer to "only while shift is held, or whenever a unit with a queued order is
selected" is unambiguous: **only while Shift is held**. Release it and the
waypoints, the trail, the target brackets and the 3D range circles all vanish
in the same frame.

### The pass

`0x48CC30` walks the local player's units and, for each live one, calls
`0x439B30` with a bitmask of which overlays to draw and a flag:

```
48cd15   cmp    esi,DWORD PTR [edi]        ; is this the view's current unit?
48cd17   je     0x48cd4a                   ;   -> mask 0x1F, flag 1
48cd19   mov    ax,WORD PTR [esi+0xa8]     ; the unit's own index
48cd20   cmp    ax,WORD PTR [edx+0x37e9c]
48cd27   je     0x48cd4a                   ;   -> mask 0x1F, flag 1
48cd29   cmp    ax,WORD PTR [edx+0x2cba]
48cd30   je     0x48cd4a                   ;   -> mask 0x1F, flag 1
48cd32   shr    ecx,0x4
48cd35   test   cl,0x1                     ; merely selected?
48cd38   je     0x48cd3e
48cd3a   push   0x0                        ;   -> mask 0x1F, flag 0
```

`0x439B30` walks the unit's order list (`[unit+0x5C]`, chained through
`[order+0x4A]`) and for each order looks up its type byte `[order+0x4]` in a
runtime table at `[0x512344]`, 25 bytes per entry (the divide-by-25 magic
`0x51EB851F >> 3` at `0x43BCAD` confirms the stride). The dword at `+0x0C` of
the entry is a mask of which of five handlers to run:

| Bit | Handler | What it draws |
|---|---|---|
| `0x01` | `0x438C00` | the corner bracket around the order's target |
| `0x02` | `0x4394E0` | **the waypoint icon and the trail to it** |
| `0x04` | `0x4399F0` | a flat 16-segment ring around the order's target |
| `0x08` | `0x439740` | the waypoint icon alone, no trail |
| `0x10` | `0x4390A0` | the unit's range circles (once per unit) |

An order type gets a trail if its table entry has bit 1, a bare marker if it
has bit 3. I did not enumerate the table - it is built at runtime through the
vector at `0x512344`/`0x512348`/`0x51234C` and I ran out of thread to pull.

The **flag** matters. `0x4394E0` draws the waypoint icon first and then:

```
43951b   test   ebp,ebp          ; the flag
43951d   je     0x43972f         ; -> icon only, no trail
```

So the marching trail is drawn for the *current* unit (and for the one or two
units named by the globals at `[cfg+0x37E9C]` and `[cfg+0x2CBA]`, which are
unit indices - `0x48CC6C` multiplies them by `0x118` to make pointers), while
every other selected unit gets its waypoint icons but no trail between them.
That those two globals are "unit under cursor" and "primary selection" is
inference; that only one or a few units get the trail is decoded.

### The artwork

`anims/CURSORS.GAF`, sequence **`pathicon`**. Loaded at `0x429E66`:

```
429e66   push   0x503430          ; "pathicon"
429e6b   push   esi               ; the CURSORS.GAF handle
429e72   call   0x4b8d40
429e83   mov    DWORD PTR [edx+0x148d3],eax
```

(the store lands one call later than the push, the usual shape in this binary),
and `[cfg+0x148D3]` is exactly the pointer the trail loop picks up at
`0x4395A8`.

One frame, 11x11, hotspot `(5,5)`, transparency index 9. Decoded, the pixels
are a four-armed star - a plus with diagonal spurs:

```
  .   .   .   .   .  79   .   .   .   .   .
  .   .   .   .  79 232  79   .   .   .   .
  .   .   .  79 233 233 233  79   .   .   .
  .   .  79   .  79 234  79   .  79   .   .
  .  79 233  79  79 235  79  79 233  79   .
 79 232 233 234 235 236 235 234 233 232  79
  .  79 233  79  79 235  79  79 233  79   .
  .   .  79   .  79 234  79   .  79   .   .
  .   .   .  79 233 233 233  79   .   .   .
  .   .   .   .  79 232  79   .   .   .   .
  .   .   .   .   .  79   .   .   .   .   .
```

Index 79 is `(11,11,0)`, a near-black outline; 232-236 are the top of TA's
green ramp, `232 = (123,255,119)` at the tips fading inward to
`236 = (27,127,11)` at the centre. The icon is therefore *brightest at its
extremities and dark in the middle* - a small green sparkle, not a filled blob.
The colour is baked into the artwork; there is no palette lookup or remap on
this path.

### Spacing and the march

This is `0x4394E0` after the marker has been drawn. `arg4` is a scratch
position carrying the running "previous point": `0x439B30` seeds it with the
unit's own current position and `0x439740` overwrites it with each order's
position as it goes, so the chain is *unit -> order 1 -> order 2 -> ...*.

```
439531   mov    eax,DWORD PTR [ebp+0x38a47]  ; global tick counter
439537   sub    eax,DWORD PTR [ebx+0x46]     ; minus the order's timestamp
43953a   xor    ebx,ebx
43953c   cmp    ebx,eax
43953e   sbb    ebx,ebx
439542   and    ebx,eax                      ; age = max(tick - issued, 0)
   ... dx, dy, dz; length = (int)sqrt(dx^2+dy^2+dz^2) ...
439587   cmp    esi,0x10000
439593   jl     0x43972f                     ; segment under 1.0 world unit: skip
439599   mov    eax,ebx
43959b   mov    ecx,0x1e
4395a1   idiv   ecx                          ; edx = age % 30
4395a8   mov    ebp,DWORD PTR [ebp+0x148d3]  ; the pathicon anim
4395b2   lea    ecx,[edx+edx*2]              ; 3 * (age % 30)
4395b5   shl    ecx,0x14                     ; << 20
4395b8   imul   ecx                          ; \
4395ba   add    edx,ecx                      ;  > / 30
4395bc   sar    edx,0x4                      ; /
4395ce   mov    ecx,edx                      ; phase, in 16.16 world units
4395f7   cmp    ecx,esi
4395fb   jge    0x43972f                     ; phase past the end: nothing
```

and then the loop, one iteration per icon:

```
439631   mov    eax,ebp                      ; distance along the segment
439633   mov    ecx,0x10
439639   call   0x4e44f0                     ; << 16, 64-bit
43964a   call   0x4e4440                     ; / length  -> t in 16.16
   ... for each of x,y,z:  d * t >> 16, added to the saved "from" ...
4396d3   movsx  eax,WORD PTR [esp+0x46]      ; y
4396d8   movsx  ecx,WORD PTR [esp+0x4a]      ; z
4396dd   movsx  edx,WORD PTR [esp+0x42]      ; x
4396e2   sar    eax,1
4396e4   sub    ecx,eax                      ; sy = z - y/2
4396ed   sub    ecx,[view+0x30]              ;    - scrollY
4396f2   sub    edx,[view+0x2c]              ; sx = x - scrollX
4396f8   add    ecx,0x20
4396fb   add    edx,0x80
439701   mov    eax,DWORD PTR [esi+ebx*8+0x28]  ; frame ebx of the anim
43970d   call   0x4b7f90                     ; blit
439712   lea    eax,[ebx+0x1]
439717   mov    cx,WORD PTR [esi]            ; frame count
43971b   idiv   ecx
439723   mov    ebx,edx                      ; next icon: next frame
43971d   add    ebp,0x300000                 ; next icon: +48.0 world units
439725   cmp    ebp,DWORD PTR [esp+0x64]
439729   jl     0x439631
```

Reading the constants off:

* **Spacing is a constant 48 world units.** `0x300000` in 16.16 is exactly
  `48.0`. It is not a division of the segment into equal parts - a long segment
  just gets more icons, and the last gap before the endpoint is whatever is
  left over.
* **The march is a scroll, not a frame cycle.** The phase is
  `((age % 30) * 3 * 0x100000) / 30`, i.e. `(age % 30) * 1.6` world units,
  sweeping `0` to `46.4` and wrapping - exactly one icon spacing per 30 ticks.
  TA's tick is 1/30 s, so **the icons crawl along the path at 48 world units
  per second, one full spacing per second**, and the wrap is seamless because
  the sweep equals the spacing.
* The frame index does advance - once per tick from the age, and once more per
  icon along the line - but `pathicon` has exactly one frame, so both are
  no-ops and the icon never changes. TA plainly intended an animated icon here
  and shipped a still one.
* The phase runs *forward* from the earlier waypoint, so the icons appear to
  flow from the unit toward its destination.
* `age` comes from `[order+0x46]`, the tick the order was issued, so each
  segment of a long queue has its own phase and they do not march in lockstep.
  (The same timestamp drives the target bracket in `0x438C00`, which animates
  its size over the first ten ticks of an order's life.)

### Geometry

The point is a **straight 3D linear interpolation** between the two order
positions, y included - the trail does not sample the terrain and does not
follow it. Over level ground the order positions are on the ground and the
trail hugs it; across a valley the icons cut straight through the air. Contrast
the range circles in `0x438EA0`, which explicitly call the height lookup at
every vertex.

The projection is TA's ordinary world-to-screen, the same three lines used by
the selection brackets and the range circles:

```
sx = x - scrollX + 0x80
sy = z - y/2 - scrollY + 0x20
```

The `+128` / `+32` is a constant origin offset of TA's world render surface and
carries no meaning for RWE - use your own projection.

### The waypoint markers are not the trail icon

The endpoints are drawn by `0x439740`, and they are **the mouse cursor
artwork**, not `pathicon`:

```
4397e2   xor    edx,edx
4397e4   mov    dl,BYTE PTR [esi+0x4]           ; the order type
4397e7   lea    edi,[edx+edx*4]
4397ea   mov    edx,DWORD PTR ds:0x512344
4397f0   lea    edx,[edx+edi*4]
4397f3   mov    dl,BYTE PTR [edi+edx*1+0x10]    ; entry+0x10 = the icon index
4397f7   test   dl,dl
4397f9   jne    0x439811                        ; 0 -> no icon at all
   ...
43997a   mov    ecx,DWORD PTR [edi+ecx*4+0x1487f]  ; the anim for that index
43999f   mov    eax,DWORD PTR [edi+0x38a47]        ; global tick
439993   mov    ax,WORD PTR [ecx+0x2c]             ; ticks per frame
4399a8   shl    esi,1                              ; doubled
4399aa   div    esi
4399b0   mov    si,WORD PTR [ecx]                  ; frame count
4399b3   div    esi
4399b5   mov    ecx,DWORD PTR [ecx+edx*8+0x28]     ; the frame
4399bb   call   0x4b8500                           ; blit
```

`[cfg+0x1487F + i*4]` is the array of cursor animations, loaded from
`CURSORS.GAF` at `0x429C9F`-`0x429E94`. Recovering the load order (again
remembering the store lands one call late) gives the whole index map:

| Index | Sequence | Index | Sequence |
|---|---|---|---|
| 0 | *(never assigned - no icon)* | 11 | `cursorreclamate` |
| 1 | `cursorattack` | 12 | `cursorload` |
| 2 | `cursorairstrike` | 13 | `cursorunload` |
| 3 | `cursortoofar` | 14 | `cursormove` |
| 4 | `cursorcapture` | 15 | `cursorselect` |
| 5 | `cursordefend` | 16 | `cursorfindsite` |
| 6 | `cursorrepair` | 17 | `cursorred` |
| 7 | `cursorpatrol` | 18 | `cursorgrn` |
| 8 | `cursorpickup` | 19 | `cursornormal` |
| 9 | `cursorteleport` | 20 | `cursorhourglass` |
| 10 | `cursorrevive` | 21 | `pathicon` |

A queued Move therefore shows an animating `cursormove` at the waypoint, an
Attack order `cursorattack`, Patrol `cursorpatrol`, Guard `cursordefend`,
Reclaim `cursorreclamate`, and so on - the marker *is* the cursor you clicked
with, animating in place. Unlike the trail these markers are keyed off the
**global** tick, so they all animate in lockstep, at **half** the animation's
nominal rate (`tick / (2 * frame0.duration)`, then modulo the frame count).
`cursormove` has 8 frames, `cursorattack` 10, `cursorpatrol` 14,
`cursordefend` and `cursorload` 16 each.

Where the order names a target unit rather than a point, `0x439740` uses that
unit's live position, falling back to a snapshot cached in `[order+0x32]` /
`[order+0x34]` with the flag `0x200000` in `[order+0x42]` once the target stops
being visible - so the marker freezes where you last saw the thing rather than
tracking it through fog.

Two more overlays land at the same target while Shift is held, worth knowing so
they are not mistaken for part of the trail: `0x438C00` draws the four-cornered
selection bracket sized from the target's model bounds, and `0x4399F0` draws a
flat 16-segment ring of radius `(int)(footprintRadius * 0.89)` (the double
`0.89` at `0x4FD2C0`; radius `32` when the order has no target unit), colour
`cfg+0xDD7`.

### Implementation spec - waypoint trail

For `GameScene.cpp`, while Shift is held and for the primary selected unit:

1. Build the chain of points: the unit's current position, then each queued
   order's position in turn.
2. For each consecutive pair, if the 3D distance is under 1.0 world unit, draw
   nothing.
3. `age = currentTick - orderIssuedTick`, floored at 0.
4. `phase = ((age % 30) * 48) / 30` world units, i.e. `1.6 * (age % 30)`. If
   `phase >= segmentLength`, draw nothing for this segment.
5. For `d = phase; d < segmentLength; d += 48`, place an icon at
   `from + (to - from) * d / segmentLength` - a straight 3D lerp, no terrain
   sampling.
6. The icon is `CURSORS.GAF` / `pathicon`, one frame, 11x11, drawn centred on
   its hotspot `(5,5)`, colour baked in (green ramp 232-236, outline 79). No
   line, no dashes - the "line" is only the row of icons.
7. At each waypoint draw the cursor animation for that order type
   (`cursormove`, `cursorattack`, `cursorpatrol`, `cursordefend`,
   `cursorrepair`, `cursorreclamate`, `cursorcapture`, `cursorload`,
   `cursorunload`, ...), frame `(globalTick / (2 * frameDuration)) %
   frameCount` so all markers stay in sync. Some order types have no icon;
   those draw nothing at the waypoint.
8. Other selected units that are not the primary get the waypoint icons but
   **no** trail between them.
9. For the full TA look, also bracket and ring the order's target while Shift
   is down.

---

## 27. The building placement box

### Where the footprint comes from, and why it is not the yardmap

The box is the unit's **footprint**, and the footprint has nothing to do with
`YardMap`. `FootprintX`/`FootprintZ` are not FBI keys at all in the parser's own
list — `0x42BF6C`-`0x42CF53` walks a hundred-odd key names and neither appears.
What happens instead is at `0x42CD00`: the FBI parser looks up `movementclass`,
and if the unit names one it copies the fields out of that `MOVEINFO.TDF` class;
if it does not, it runs **the movement-class parser over the unit's own FBI
section**:

```
42cd06  push 0x503990            ; "movementclass"
42cd16  call 0x4c48c0
42cd1d  test eax,eax
42cd1f  je   0x42cd2f            ; no movement class named
42cd24  call 0x440420            ;   -> look the class up in MOVEINFO
42cd43  jne  0x42cd5d
42cd51  call 0x440340            ;   -> else parse FootPrintX/Z, MaxSlope, ...
42cd5d  mov cx,[eax+0x4]         ;         out of the FBI section itself
42cd61  mov [ebp+0x14a],cx       ; def+0x14A = footprint X, in cells
42cd68  mov dx,[eax+0x6]
42cd6c  mov [ebp+0x14c],dx       ; def+0x14C = footprint Z, in cells
```

`0x440340` is the shared reader — `FootPrintX` at `0x505484`, `FootPrintZ`,
`MaxWaterDepth`, `MinWaterDepth`, `MaxSlope`, `BadSlope`, `MaxWaterSlope`,
`BadWaterSlope` — which is why buildings, which never name a `movementclass`,
still get a footprint out of their FBI.

The **yardmap is stretched onto that footprint, not the other way round**
(`0x42CF3E`-`0x42D071`). The loop runs `footprintZ` rows of `footprintX` columns
and walks the `YardMap` string one character per cell, except that it stops
advancing when the next character is NUL:

```
42d040  mov cl,[esi+0x1]
42d045  test cl,cl
42d047  je  0x42d04a            ; last character -> do not advance
42d049  inc esi
```

So ARMSILO's `YardMap=ooooooooo` — nine characters — fills a 5x5 grid, all open,
and `FootprintX=5` is what the box is drawn from. **The two really do disagree in
the shipped data** and the footprint wins.

One cell is **sixteen world units**. The definition's bounding box falls straight
out of the same routine at `0x42D079`:

```
42d079  movsx ecx,WORD PTR [ebp+0x14a]   ; footprintX
42d082  neg   eax
42d08b  shl   eax,0x14                   ; * 0x100000
42d091  sar   eax,1                      ; / 2      -> -footprintX * 0x80000
42d093  mov   [ebp+0x15e],eax            ; def+0x15E = min X = -footprintX * 8.0
42d0a5  mov   [ebp+0x166],eax            ; def+0x166 = min Z = -footprintZ * 8.0
42d0b5  mov   [ebp+0x16a],eax            ; def+0x16A = max X = +footprintX * 8.0
42d0c5  mov   [ebp+0x172],eax            ; def+0x172 = max Z = +footprintZ * 8.0
```

Values are 16.16, so `0x80000` is 8.0 — a half-extent of `footprint * 8`, a full
extent of `footprint * 16`. **No margin of any kind is added anywhere.**

### The box under the cursor, `0x4197D0`

Command mode lives in the byte at `globals+0x2CC3`; **mode `0x0E` is "place a
building"**, set at `0x41AB89` when a build button is clicked. While that mode is
live and bit 1 of `globals+0x2CC6` is set, every mouse move calls `0x4197D0`
(`0x491CDB`, `0x499241`), which recomputes the box:

```
4197dd  mov cx,[edx+0x2cc4]       ; the unit type being placed
4197ef  mov eax,[edx+0x1439b]
4197f5  add esi,eax               ; esi = its definition (stride 0x249)
4197f7  lea eax,[edx+0x2caa]      ; globals+0x2CAA = cursor world position, 16.16
4197ff  mov edi,[esi+0x14a]       ; footprintX in the low word, footprintZ high
419805  mov eax,[ecx]             ; cursor X
419811  shl edi,0x13              ; footprintX * 0x80000
419817  sub eax,edi               ;   X - footprintX*8
41981e  add eax,0x80000           ;   + 8.0
41982a  sar eax,0x14              ;   / 16.0   -> the cell index
41982f  mov [esp+0xc],ax          ; cellX
419840  mov [esp+0xe],cx          ; cellZ
419849  shl eax,0x4
41984f  mov [edx+0x2c92],eax      ; x0 = cellX * 16
41985e  mov [edx+0x2c9a],ecx      ; z0 = cellZ * 16
419881  shl ecx,0x4
419886  mov [eax+0x2c9e],ecx      ; x1 = x0 + footprintX * 16
419894  add edx,[eax+0x2c9a]
41989a  mov [eax+0x2ca6],edx      ; z1 = z0 + footprintZ * 16
```

The `+ 8.0` before the shift is a round-to-nearest, so the box snaps to the
sixteen-unit build grid with the *cursor* at the centre of the footprint rather
than at a corner.

It then asks whether the site is legal and where the building would stand:

```
4198c6  call 0x47d2e0            ; can this definition be built at this cell?
4198d1  and  al,0x1
4198d3  shl  al,0x6
4198e1  mov  [ecx+0x2cc6],dl     ; bit 6 of globals+0x2CC6 = the site is OK
4198ec  test BYTE PTR [eax+0x2cc6],0x40
4198f3  je   0x4198fc
4198f5  call 0x47c780            ;   OK -> the levelled height 0x47D2E0 recorded
4198fe  call 0x47d820            ;   not OK -> just sample the terrain
419918  mov  [ecx+0x2c96],eax    ; y0
419925  mov  [edx+0x2ca2],eax    ; y1  (the box is flat)
```

`0x47D2E0` returns **1 for a buildable site** (`0x47D800 mov eax,1`, against
`0x47D812 xor eax,eax`) and as a side effect stores the levelled build height as
a byte in the global at `0x51E684`; `0x47C780` is nothing but
`mov eax,ds:0x51e684; ret`. So **bit 6 set means the site is good**, which the
click handler at `0x498F86` confirms independently:

```
498f86  test BYTE PTR [eax+0x2cc6],0x40
498f8d  je   0x499016            ; bit clear -> refuse
498f98  call 0x419670            ; bit set   -> issue the order
498f9f  push 0x509660            ; "oktobuild"
```

### Drawing it, inside the world render at `0x469E0B`

The box is drawn by the world render `0x468CF0` itself, sharing its code with the
drag-select rectangle. Both are **world-space rectangles**, not screen ones; TA
draws the map orthographically with height only shifting things upward, so an
axis-aligned world rectangle is an axis-aligned screen rectangle:

```
469e13  mov edi,[ecx+0x2c96]     ; y0 (a byte height)
469e19  mov eax,[ecx+0x2c9a]     ; z0
469e1f  mov edx,[ecx+0x1431f]    ; camera X
469e25  mov esi,[ecx+0x2c92]     ; x0
469e37  sar edi,1                ; y0 / 2
469e39  sub eax,edi              ; z0 - y0/2
469e41  sub esi,edx              ; x0 - camX
469e4b  sub eax,ebp              ;   - camZ
469e59  add esi,0x80             ; + 128
469e5f  add eax,0x20             ; + 32
```

so `screenX = worldX - camX + 128` and `screenY = worldZ - height/2 - camZ + 32`.
**One world unit is one pixel**, the viewport origin is (128, 32), and a height
contributes half its value upward. Then:

```
469e6b  cmp bl,0xe               ; command mode 0x0E?
469e70  mov cl,[ecx+0x2cc6]
469e76  and cl,0x40
469e7d  and ecx,0x6
469e80  add ecx,0x4              ; -> 10 if the site is OK, 4 if it is not
469e83  mov ebp,ecx
469e87  mov ebp,0xf              ; not build mode -> 15 (the drag box)
469e94  mov bl,[ebp+ecx*1+0x0]   ; colour = guicolours[index]
469ec5  call 0x4bf8c0            ; rectangle (x0,y0)-(x1,y1)
469eda  inc edi ... dec ecx      ; inset by one pixel all round
469f1e  call 0x4bf8c0            ; rectangle again, same colour
```

`0x4BF8C0` draws a one-pixel rectangle outline out of four clipped lines
(`0x4BEA20` clips, `0x4CC7AB` draws). Two nested calls one pixel apart means the
placement box is **exactly two pixels thick, both pixels the same colour**. The
drag-select box goes through the same code but its second, inner ring is drawn in
`guicolours[0]` instead; that one is two-tone.

So there is **one** cursor box, not several: no dash pattern, no size change, no
third state. Valid and invalid differ only in colour.

The colour table is a byte array at `globals+0xDCB`, handed to the render frame at
`0x468D49` (`lea ebx,[eax+0xdcb]`; `mov [esp+0x74],ebx`). Indices in use across
the UI: 0 and 15 the drag box, **4 the invalid placement box, 10 the valid one**,
1/3/9/10 the queued build box of the next section, 9 and 12 the order path lines,
14 the minimap view rectangle.

**I could not find where that table is filled, and I want to be plain about
that.** The globals block is malloc-ed and zeroed once at `0x41D956`, and nothing
in `.text` writes `globals+0xDCB`..`+0xDDA`: a raw scan of every byte, word and
dword store with a `disp32` in that range finds none, there are no runs of
`mov byte [reg+disp32], imm8` anywhere in the image, and every one of the nine
`lea`/`add` instructions that computes the address (`0x4183D6`, `0x466DD4`,
`0x467EC6`, `0x468625`, `0x468D49`, `0x46902F`, `0x46A452`, `0x46ACF4`,
`0x46B9E0`) is a reader. Either it is written through a pointer aliased from
somewhere I did not follow, or through a base register holding `globals + K` with
a compensating displacement. **The palette entries are therefore unknown** — that
is ignorance, not a guess I am dressing up. The next thing to try is a runtime
memory dump: attach to the game with the placement box on screen and read sixteen
bytes at `globals+0xDCB`, rather than more static reading.

### The build-grid overlay is a different thing

`0x418310`, called from the world render at `0x468DBA`, walks the visible map
cells in sixteen-unit steps and draws a coloured grid over them. It is gated on
`globals+0x14280 == 1` and its cell states come from the **selected unit's
movement class** (`def+0x1B6`, two bits per cell, `0x4185D0`-`0x4185FA`), not from
the build site, so this is the pathability overlay and not part of placement. Its
three colours are the byte table at `0x4FCC68` = `04 0E 0A`, that is
`guicolours[4]`, `[14]`, `[10]`. Recorded only so it does not get mistaken later
for a placement grid.

### A line of buildings

There is no drag-out line. `0x498F70` is the click: after issuing, it tests a
modifier flag on the event and either **stays in mode `0x0E` with bit 5 of
`globals+0x2CC6` set** so the next click places another, or drops back to mode 1
and sends `STOP`:

```
498fa9  test BYTE PTR [esi+0x8],0x4
498fb2  je   0x498fc0
498fb4  or   BYTE PTR [eax+0x2cc6],0x20   ; keep placing
498fc0  mov  BYTE PTR [eax+0x2cc3],0x1    ; otherwise leave build mode
```

Each click is an independent order and each gets its own box, drawn by the routine
in the next section. A row of five buildings is five boxes, each with its own
animation stamp, and nothing joins them.

### Implementation spec

For `GameScene::renderBuildBoxes` and the `hoverBuildInfo` block around
`GameScene.cpp:1246`:

- The footprint is `FootprintX`/`FootprintZ` — RWE already gets this right through
  `computeFootprintRegion` — and **not** the yardmap. Sixteen world units per
  cell; `MapTerrain::HeightTileWidthInWorldUnits` is already 16.
- The box is `[cellX*16, cellX*16 + footprintX*16]` by
  `[cellZ*16, cellZ*16 + footprintZ*16]`. No margin. RWE matches.
- Snap with round-to-nearest about the cursor:
  `cell = floor((cursor - footprint*8 + 8) / 16)`, not truncation.
- Line width is **2 px, both pixels one colour**; RWE's
  `drawBoxOutline(..., 2.0f)` is right. There is no thicker "confirmed" variant to
  add.
- Valid uses `guicolours[10]`, invalid `guicolours[4]`. The palette entries are
  unknown, so RWE's green and red are as good a stand-in as anything.
- The box height should be **the levelled build height computed over the whole
  footprint**, and the same value should be stored on the order, so the box does
  not jump vertically the instant the order is issued. RWE currently uses
  `terrain.getHeightAt(centre)` for both, which is close but is not the same rule.

---

## 28. The placement animation

The animation is real, it belongs to the **queued build order** and not to the
cursor box, and it is drawn by `0x438C00`. `0x439B30` walks a selected unit's
order queue, looks each order's type up in the descriptor table at `ds:0x512344`
and dispatches on a flags dword at `+0x0C`: bit 0 draws the build-site box
(`0x438C00`), bit 1 a waypoint marker (`0x4394E0`), bit 2 the sixteen-segment
dashed move line (`0x4399F0`), bit 3 a target marker (`0x439740`).

### The box

`0x438C00(surface, view, order, ...)` reads the order's unit type from
`order+0x36` and its target position from `order+0x22`, and builds the rectangle
straight out of the definition's bounding box, the same footprint*8 half-extents
as the previous section:

```
438c23  mov ebp,[edx+0x22]        ; order position X
438c26  mov edi,[edx+0x2a]        ; order position Z
438c41  mov edx,[eax+0x15e]       ; def min X
438c4d  add edx,ebp               ; -> world x0
438c4f  mov esi,[eax+0x16a]       ; def max X
438c59  mov edx,[ecx+0x4]
438c5c  add edx,ebx               ; position Y + def min Y -> world y0
438c66  mov edx,[eax+0x166]       ; def min Z  -> world z0
438c6c  mov eax,[eax+0x172]       ; def max Z  -> world z1
```

and projects it with exactly the mapping of the previous section. The order's own
position is the snapped centre of the placement box (`0x41970A`:
`(2*cell + footprint) * 0x80000`, that is `cell*16 + footprint*8`) and its Y is
the same levelled height byte the cursor box used (`0x419721`), so **the queued
box lands on precisely the pixels the cursor box occupied.** Nothing moves and
nothing resizes at the moment of placement.

### The clock

```
438cb7  mov eax,[edx+0x38a47]     ; the game tick
438cc5  mov ebx,[edx+0x46]        ; the tick the order was created
438cca  sub eax,ebx               ; age
438cde  cmp edx,eax               ; clamp at zero
438ce0  sbb edx,edx
438ce6  and edx,eax
438ce8  cmp edx,0xa
438ceb  jae 0x438cfb              ; clamp at ten
438cfb  mov DWORD PTR [esp+0x48],0xa
```

`order+0x46` is stamped with `globals+0x38A47`, the 30 Hz simulation tick, by the
order constructor itself (`0x43A12E`, `0x43A471`) — that is, at the moment you
click. So `t = clamp(now - createdTick, 0, 10)`: the animation runs for **ten
ticks, one third of a second**, once, and then holds. It does not repeat while the
building is under construction and it does not restart when construction begins.

```
438d07  sub ecx,ebp              ; width  = x1 - x0
438d0e  imul ecx,edx             ; * t
438d11  imul                     ; 0x66666667 ...
438d1c  sar edx,0x2              ; ... / 10
438d2b  mov ebx,edx              ; dx = width  * t / 10
438d3d  mov [esp+0x48],edx       ; dy = height * t / 10
```

**Linear, no easing, no overshoot**, integer-truncated toward zero. The travel is
a fraction of the box, not a fixed distance: `t/10` of the full width and of the
full height respectively.

### The eight lines

Two colours, chosen once from a flag on the unit the order belongs to
(`order+0x0E`, set by the order constructor at `0x43A09F`):

```
438d45  mov eax,[edx+0xe]        ; the ordering unit
438d48  mov ecx,[eax+0x110]
438d53  shr ecx,0x4
438d56  test cl,0x1              ; bit 4 of unit+0x110 = the unit is selected
438d5b  mov dl,[eax+0xdce]       ;   set   -> A = guicolours[3]
438d61  mov al,[eax+0xdd5]       ;             B = guicolours[10]
438d71  mov cl,[eax+0xdcc]       ;   clear -> A = guicolours[1]
438d77  mov dl,[eax+0xdd4]       ;             B = guicolours[9]
```

Bit 4 is the selection flag: the loop at `0x419755` that issues a build order
skips every unit without it, and the cursor routine at `0x48D2A7` uses the same
test to collect the units it is deciding a cursor for.

Then, with `(x0,y0)-(x1,y1)` the screen rectangle:

| # | Colour | Geometry |
|---|---|---|
| 1 | A | vertical at `x = x0 + dx - 1`, from `y0-1` to `y1+1` |
| 2 | A | vertical at `x = x1 - dx + 1`, from `y0-1` to `y1+1` |
| 3 | A | horizontal at `y = y0 + dy - 1`, from `x0-1` to `x1+1` |
| 4 | A | horizontal at `y = y1 - dy + 1`, from `x0-1` to `x1+1` |
| 5 | B | vertical at `x = x0 + dx`, from `y0` to `y1` |
| 6 | B | vertical at `x = x1 - dx`, from `y0` to `y1` |
| 7 | B | horizontal at `y = y0 + dy`, from `x0` to `x1` |
| 8 | B | horizontal at `y = y1 - dy`, from `x0` to `x1` |

all through `0x4BE950(surface, x1, y1, x2, y2, colour)`. The excerpt for the first
two, which fixes both the argument order and the one-pixel offsets:

```
438d89  lea ecx,[ebx+ebp*1]      ; x0 + dx
438d91  lea edx,[edi+0x1]        ; y1 + 1
438d98  dec ecx                  ; x0 + dx - 1
438d99  push eax                 ;   colour A
438d9a  lea ebx,[esi-0x1]        ; y0 - 1
438d9d  push edx
438d9e  push ecx
438d9f  push ebx
438da4  push ecx
438da5  push ebx                 ;   surface
438daa  call 0x4be950
438db7  sub eax,ecx              ; x1 - dx
438dc1  mov [esp+0x18],eax
438dc5  inc eax                  ; x1 - dx + 1
438dce  call 0x4be950
```

The stack bookkeeping is worth a warning. `0x438DC1` writes `[esp+0x18]` after a
`push`, so it lands on the slot `0x438D34` used for `dx`, not on the slot
`0x438D94` used for `x0+dx`. Read it without accounting for that push and lines 5
and 6 come out as verticals drawn at `x = dx`, which is nonsense, and was my first
reading.

### What it looks like

**They are not corner brackets and they are not points.** They are four
full-length lines, two vertical and two horizontal, that sweep across the box.
What the eye reads as corners are the four crossing points, and each carries tails
running out to the box edge, so the figure is a hash whose inner rectangle shrinks
and grows. There are no short legs, so there is no leg length to give.

- `t = 0`: `dx = dy = 0`. The lines sit at `x0-1`/`x0` and `x1`/`x1+1` — the box,
  one pixel proud of the footprint.
- `t = 5`: `dx = width/2`. Both verticals are at the centre and both horizontals
  are at the centre — a plus sign.
- `t = 10`: `dx = width`, so `x0+dx = x1` and `x1-dx = x0`. **The pairs have
  crossed over** and land back on the box edges, now occupying `x0`/`x0+1` and
  `x1-1`/`x1` — exactly the footprint.

Full box, collapse to a cross at the centre over five ticks, expand back to the
footprint over five more. That is the effect the user describes as the four
corners sliding inward toward the centre and then returning to the edges.

On the outline changing width: **the line weight does not change.** Both boxes are
two pixels. What does change, and what I think accounts for the impression, is
three things together. The animation first frame is one pixel larger than its
last; the settled queued box carries one-pixel nubs past each corner, because the
colour-A arms overhang by one, which the cursor box does not have; and the colour
goes from uniform `guicolours[10]` to two-tone `guicolours[3]` inside and
`guicolours[10]` outside. The outer pixel keeps the same table entry as the valid
cursor box, which is why the transition reads as continuous rather than as a swap.
**I looked for a third outline with a different thickness and there is not one**:
the only other callers of the rectangle primitive `0x4BF8C0` are the minimap view
rectangle (`0x466B5E`) and a debug window (`0x467F6C`), and the only other line
clusters in that part of the binary are the pathability overlay (`0x418310`) and
the remaining order-queue markers.

The move-order marker `0x4394E0` has an animation of its own on the same
`order+0x46` stamp but over **thirty** ticks (`0x43959B mov ecx,0x1e`). I did not
decode it, and it is not this one.

### Implementation spec

In `GameScene::renderBuildBoxes`, replace the single `drawBoxOutline` per build
order with the eight-line figure, in world-UI space where one world unit is one
pixel:

- Give `BuildOrder` a `GameTime createdAt`, stamped when the order is created.
- `t = std::clamp(currentTime - order.createdAt, 0, 10)` in ticks.
- `dx = (width * t) / 10`, `dy = (height * t) / 10`, integer, truncated toward
  zero. `width` and `height` are `footprint * 16` world units.
- Draw the eight one-pixel lines of the table above: colour A for the four that
  carry the one-pixel overhang, colour B for the four that do not.
- B is the same colour as the valid placement box, so RWE current green is fine;
  A is a shade different.
- Draw it for every queued order for as long as the order is in the queue. The
  animation is self-limiting because `t` saturates at 10 and the figure settles on
  the footprint.
- Do not restart `t` when construction starts; nothing in the original does.

---

## 29. The nuclear silo stockpile and build progress

Most of the mechanism is already in the stockpiled-weapons section of this
document, and what follows is largely a set of negatives. They are decoded
negatives, not guesses.

### Where the count is shown

Only in one place: **the caption of the weapon-build button on the launcher own
build page.** `0x4199B0` rebuilds the caption of every gadget on the page each
refresh, dispatching on `commonattribs` (`element+0x2A`; the element stride is
`0x15B` and the caption buffer is `element+0xB6`):

```
4199f8  test al,0x4              ; bit 2: an ordinary build queue
419a1e  push 0x502660            ;   "+%d"
419a2b  test al,0x8              ; bit 3: a weapon build button
419a33  mov al,[ebp+0x1e]        ;   the magazine byte of weapon one
419a39  call 0x439d80            ;   how many are still on order
419a42  mov BYTE PTR [esi],0x0
419a48  push 0x50266c            ;   "%d"
419a67  push 0x502664            ;   " +%d"
419a6f  call 0x4e42b0
```

The caption is N for the finished rounds with a space, a plus and M appended for
the outstanding orders, so `3 +2`. With an empty magazine the `"%d"` is skipped
entirely and the string starts empty, so the append lands at offset zero with its
leading space still attached and the button reads a space, a plus and the count.

The gadget is identified by `commonattribs=8`, and `ARMSILO1.GUI` says so in as
many words:

```
name=ARMMAKENUKE;  xpos=0;  ypos=27;  width=64;  height=64;
attribs=32;  commonattribs=8;   // Flag this as a weapon build button
```

`CORSILO1.GUI` is the same with `CORMAKENUKE`. Both silos put their missile in
`Weapon1`, and `0x419A33` hard-codes `[unit+0x1E]`, the magazine byte of weapon
record one, so that is the only slot the readout can ever show.

### Where the progress of the round being built is shown

**Nowhere.** There is no percentage, no bar, and no third state on the button. I
looked in four places and all four come up empty.

**1. The footer RELOAD1/RELOAD2/RELOAD3 rectangles.** These are the obvious
candidate: three per-weapon regions in `SIDEDATA.TDF`, one for each of a unit
three weapons, at `x1=132 y1=450 x2=148 y2=458` and two ten-pixel steps below.
They are parsed at `0x43249A` into `sidestruct+0x1A8`, `+0x1B8`, `+0x1C8`:

```
43249a  push esi
43249f  push 0x50477c            ; "RELOAD%d"
4324b9  call 0x431950
4324bf  add edi,0x10             ; three of them, sixteen bytes each
```

and are then **never read**. The side struct is `globals+0x37F3D + side*0x232`,
the rectangle block starts `0x4A` into it, and its sibling rectangles *are* read
through that base in the ordinary way: `DAMAGEBAR` at `side+0x152` is loaded at
`0x46B04C` and `0x46B2DC`, `UNITNAME` at `side+0x142` at `0x46AFD4`. There is no
`[reg+0x1F2]`, `[reg+0x202]` or `[reg+0x212]` anywhere in `.text` that is not a
unit-definition field on an unrelated struct. The reload readout is dead data, in
the same class as `sortbias`.

**2. The unit info panel.** `0x46AD90`-`0x46B400` draws the whole footer for the
selected unit and its entire vocabulary is: `"%s  M:%d E:%d"` for a build target,
the unit name, the damage bar off `unit+0x108` against `def+0x1FA`, `"+%.1f"`,
`"+%.0f"`, `"-%.1f"` and `"-%.0f"` for the four resource rates, and
`"%d %s - %s"` and `"%d %s"` for kills and Veteran. No stockpile, no reload, no
percentage.

**3. A format string.** The only doubled percent sign in the binary is the netstat
line `"pS=%4d pR=%4d (S=%d/%4d, R=%d/%4d) C=%3d%%"`, and the only `"%d/%d"`
(`0x505710`) has a single caller, `0x441674`, in the mission and save list.

**4. maxstockpile.** It does not exist. A case-insensitive scan of the whole image
for `stockpile` finds exactly one occurrence, `0x5040EC`, and that is the
**weapon TDF** flag, bit 28 of `wdef+0x111`. There is no FBI key, no cap field,
and no per-unit maximum other than the hard-coded `0xC8` = 200 that `0x402CB4`
compares the magazine against before parking the order for 300 ticks.

### The fields

| What | Where |
|---|---|
| finished rounds | `unit+0x1E`, a BYTE: weapon record one magazine (records are `0x1C` apart from `unit+0x04`; the field is `+0x1A` within a record) |
| rounds still on order | not stored; `0x439D80` totals the outstanding `BUILDWEAPON` orders on demand |
| progress on the current round | `order+0x3E`, a dword of **ticks already paid for** |
| the total | `wdef+0xE4`, `reloadtime` already multiplied by 30 |
| the flag | `stockpile`, bit 28 of `wdef+0x111` |

Progress advances five ticks at a time (`0x402BD4`) and each step cost is the
difference of two truncated running totals. For `NUCLEAR_MISSILE` the total is
5400 ticks, 180 seconds, so a percentage would be `order+0x3E * 100 / wdef+0xE4`.

### Implementation spec

To match the original exactly, RWE already does everything there is to do once the
N and the plus-M caption is in: the original shows the finished count and the
queue length on the build button and nothing else at all.

To answer the question the user actually asked, how far through the current
missile the silo is, that is **an addition, not a restoration**, and it belongs in
the "where RWE deliberately differs" section rather than being presented as TA
behaviour. The numbers to use:

- Progress fraction is `ticksPaid / (reloadTime * 30)`, where `ticksPaid` is RWE
  equivalent of `order+0x3E` on the `BuildWeapon` order and `reloadTime` is the
  weapon own field. It advances in steps of five ticks and stalls outright when
  the economy refuses a step, so it is not a smooth ramp: draw it as a bar or a
  whole-number percentage, not as an interpolated animation.
- The natural home, and the one Humongous evidently intended, is the
  RELOAD1/RELOAD2/RELOAD3 rectangle of the selected unit side data: three stacked
  eight-pixel strips down the left of the footer, shipped in both sides
  `SIDEDATA.TDF`, parsed by the original and used by nothing. Filling them in
  costs nothing the original was doing with the space.
- The caption on `ARMMAKENUKE` stays exactly `"%d"` plus `" +%d"`, including the
  stranded leading space when the magazine is empty.

---

## 30. The FBI keys, and which of them the exe actually reads

The 3.1 exe's string table contains `transportcapacity`, `transportsize`,
`cantbetransported` and `canload`. It does **not** contain
`transportmaxunits` anywhere -- that was the 1.0 key, and in the 3.1 exe it is
dead data. The 3.1 patch rewrote the FBIs to `transportcapacity`, leaving the
old key behind in a few files.

Parser sites, with the S:30 pipeline rule applied (a key's value is stored
after the *next* key's push):

```
42c239  push esi              ; default 0
42c23a  push 0x503dc8         ; "waterline"
42c243  mov  [ebp+0x1ba],ax   ; <- turnrate (previous key)
42c24a  call 0x4c46c0
42c24f  push esi
42c250  push 0x503db8         ; "transportsize"
42c259  mov  [ebp+0x22c],al   ; <- waterline
42c25f  call 0x4c46c0
42c264  push esi
42c265  push 0x503da4         ; "transportcapacity"
42c26e  mov  [ebp+0x22a],al   ; <- transportsize
42c274  call 0x4c46c0
42c279  push esi
42c27b  push 0x503d98         ; "energymake"
42c284  mov  [ebp+0x22b],al   ; <- transportcapacity
```

| Key | Offset | Default | Notes |
|---|---|---|---|
| `transportsize` | `def+0x22A`, byte | 0 | read at `0x4067D4`, `0x41125F`, `0x489B0B` |
| `transportcapacity` | `def+0x22B`, byte | 0 | read **only** at `0x489AEC` |
| `canload` | bit 8 of `def+0x245` | 0 | `shl eax,0x8` at `0x42C9E2`, between `canmove` (bit 7) and `canreclamate` |
| `cantbetransported` | bit 19 of `def+0x245` | 0 | `shl eax,0x13` at `0x42CBBF`, after `commander` (bit 18) |
| `waterline` | `def+0x22C`, byte | 0 | agrees with the doc's independent reading -- the pipeline is aligned |

`cantbetransported` is a bit of **`def+0x245`**, not `def+0x241` (the task
brief's "bit 19 of def+0x241" conflated the two flag dwords). The reader at
`0x489A9D` (`mov eax,[esi+0x245]; shr eax,0x13; test al,1`) settles it, and
`0x42CBA6`/`0x42CBBF` in the parser agree. Only `CORSUMO` and `CORKROG` set it
in the shipped data.

Also load-bearing here, from the movement-record copy at `0x42CD5D`:

| Field | Offset | Source |
|---|---|---|
| footprint X / Z | `def+0x14A` / `+0x14C`, words | movement class record `+0x4`/`+0x6`, **overriding** the FBI's own FootprintX/Z when a `movementclass` is named |
| `maxwaterdepth` | `def+0x1BE`, word | record `+0x8` |
| `minwaterdepth` | `def+0x1C0`, word, **signed** | record `+0xA` |
| `maxslope` | `def+0x228`, byte | record `+0xC` |
| `maxwaterslope` | `def+0x229`, byte | record `+0xE` |
| model height | `def+0x16E`, 16.16 (integer part is the word at `+0x170`) | recursive max-Y over the 3DO's vertices and children, `0x4CB5F0`, stored at `0x42D7CA` |
| footprint Z in world units | word at `def+0x180` = integer part of the 16.16 at `+0x17E` (footprintZ*16) | `0x42D10A` block |

**Record defaults matter.** Both the per-FBI fallback record (`0x4402E0`) and
the 32 static MOVEINFO class records (initialiser `0x440230`, records at
`0x512358`, stride 0x20) start as: `maxwaterdepth = 10000` (0x2710),
`minwaterdepth = -10000` (0xD8F0), all four slopes `255`. `0x440340` then
overlays only the keys the TDF/FBI actually names. So `TANKHOVER3` (no
MaxWaterDepth, no MinWaterDepth) leaves hover tanks with maxwaterdepth 10000
and minwaterdepth -10000 -- which is what lets them be dropped onto any water
-- while `BOATS4` (MinWaterDepth=6) gives ships a *positive* minwaterdepth,
which is what locks them out of ground transports (02). A unit's FBI-level
`MaxWaterDepth=0` (e.g. `ARMANAC`) is **ignored** when a movementclass is
named; the class wins.

### Unit instance fields used below

| Field | Offset |
|---|---|
| mover object (null for buildings) | `unit+0x00` |
| position x/y/z, 16.16 | `unit+0x6A`/`+0x6E`/`+0x72` |
| packed current cell (x<<16 or z) | `unit+0x76` |
| footprint X (low word) / Z, copied from `def+0x14A` at spawn (`0x485AAA`) | `unit+0x7E` dword |
| **carrier** (the transport holding this unit), 0 if none | `unit+0x86` |
| **passenger list head** (units I am carrying) | `unit+0x8A` |
| next passenger in my carrier's list | `unit+0x8E` |
| unit definition / player | `unit+0x92` / `+0x96` |
| COB context | `unit+0x9A` |
| unit id word | `unit+0xA8` |
| build fraction, float, 1.0f when complete (`0x485B27` stores `0x3f800000`) | `unit+0x104` |
| COB **busy** flag | bit 1 of the byte at `unit+0x10F` |
| placement state, bits 0-1 of `unit+0x110`; 2 = airborne | written by the SetPosition family (`0x43DA26`, `0x48B69E`) |
| **hidden-while-carried**, bit 17 of `unit+0x110` | set iff attached to piece -1 (`0x48AC99`) |
| attach piece byte | `unit+0xF9` |

---

## 31. Load eligibility -- `0x489A90`, `CanLoadUnit(transport, candidate)`, in full

Called from every order-builder arm that can produce a pickup (`0x43F70C`,
`0x43F980`, `0x43FB3D`) and from the two cursor/default-action choosers
(`0x43E7E0`, `0x43EAAF`). This is the *only* reader of `transportcapacity`
anywhere in the binary.

```
489a90  ebx = candidate, esi = candidate def
489a9d  mov eax,[esi+0x245]; shr eax,0x13; test al,1
        -> cantbetransported set: reject
489ab2  edi = transport def
489ab8  mov eax,[edi+0x245]; test ah,0x1
        -> canload clear: reject
489acb  walk transport+0x8A (next +0x8E), count entries whose +0x86 == transport
489aec  mov cl,BYTE [edi+0x22b]          ; transportcapacity
489af2  cmp edx,ecx; jl ok               ; count >= capacity: reject
489afe  cmp DWORD [ebx],0x0              ; candidate's mover
        -> null (a building): reject
489b0b  movzx dx,BYTE [edi+0x22a]        ; transportsize
489b13  cmp WORD [esi+0x14a],dx          ; candidate footprint X
        -> footprintX > transportsize: reject
489b24  mov eax,[ebx+0x110]; and eax,3; cmp al,2
        -> airborne: reject
489b39  mov eax,[edi+0x241]; test ah,0x8 ; transport canfly?
489b42  jne 489b56                       ; an aircraft skips the next check
489b44  cmp WORD [esi+0x1c0],0x0; jl 489b56
        -> ground/hover/sea transport and candidate minwaterdepth >= 0: reject
489b56  ecx = candidate y (unit+0x6E) + candidate def+0x16E (model height)
489b71  cmp ecx, seaLevel<<16 (byte [map+0x1427F])
        -> top at or below the waterline: reject
489b7d  fld [ebx+0x104]; fcomp 1.0f      ; build fraction
        -> not exactly complete: reject
489b98  accept
```

In words, a transport may load a unit iff:

1. the candidate is not `cantbetransported`;
2. the transport has `canload=1`;
3. it currently carries **fewer than `transportcapacity` units** -- a flat
   headcount. **Nothing anywhere weighs a unit's size against capacity: a big
   unit consumes exactly one slot.** `transportsize` never touches the count;
4. the candidate is mobile (has a mover -- buildings are out). TOTALA-EXE.md's
   existing note on this routine reads `0x489AFE` as "candidate must still
   exist" and concludes "nothing asks whether it is mobile"; `unit+0x00` is
   the mover pointer (TOTALA-EXE-MISSIONS.md S:2), so this *is* the mobility
   check, and that sentence should be corrected;
5. the candidate's **footprint X** <= the transport's `transportsize` (both
   sides in footprint cells; the candidate's footprint comes from its
   movement class when it names one);
6. the candidate is not airborne;
7. **a transport that cannot fly** additionally requires the candidate's
   `minwaterdepth < 0` -- i.e. sea and hover transports refuse anything that
   *needs* water (ships, subs). Air transports skip this;
8. the candidate's top -- position y plus model height -- is **above sea
   level**. This is what excludes submerged submarines and a Triton crawling
   the seabed, for every transport type;
9. the candidate is fully built.

The existing partial decode stops at (7); (8) and (9) are new.

Note what is *not* here: no ownership or alliance test, no `floater`, no
`canhover`, no mass, no check that the candidate is an aircraft or not.

> **Corrected 2026-09-10** (§103). This used to add "(the UI only offers the
> cursor on the player's own units)", which is not true of the original: none
> of the five `CanLoadUnit` call sites applies an ownership test either, and
> the LOAD cursor arm at `0x43E7D3` will hand back `cursorpickup` over an
> enemy. The own-units rule is RWE's, and is recorded in §88 as such.

### Why the folklore comes out the way it does

With the shipped data (`transportsize=3` on all six transports):

- **Ships**: every surface ship is `BOATS4`/`BOATS5`/`BOATS6` (footprint 4-6)
  -- too big for any transport, air included; and their positive
  `minwaterdepth` locks them out of sea/hover transports regardless.
  "Air transports cannot pick up ships" is footprint, not a special case.
- **Submarines**: `BOATD3` is footprint 3, so a sub passes the size test for
  an *air* transport -- but a submerged sub fails (8). A sub whose top pokes
  above the surface in shallow water is genuinely liftable by the exe's rules.
- **Hovercraft**: `TANKHOVER3` (Anaconda, Snapper, Skimmer, Scarab...) is
  footprint 3, minwaterdepth -10000, and floats with its top above water -- so
  hovercraft on open water are loadable by air transports **and** by the
  Bear/Turtle. `TANKHOVER4` (the hover transports themselves) is footprint 4:
  too big for anything.
- All ordinary vehicles/kbots (footprint 2-3) fit everything.

---

## 32. Capacity and size -- the six transports, as the 3.1 exe sees them

Effective values under the GOG install (`rev31.gp3` overriding `ccdata.ccx`
overriding `totala1.hpi`; `transportmaxunits` is unparsed):

| Unit | Name | transportsize | transportcapacity | dead `transportmaxunits` | Effective |
|---|---|---|---|---|---|
| ARMATLAS | Atlas | 3 | **5** | (1.0 file had none) | **1** -- see below |
| CORVALK | Valkyrie | 3 | 1 | 1 | 1 |
| ARMTSHIP | Hulk | 3 | **20** | 20 | 20 |
| CORTSHIP | Envoy | 3 | **5** | 24 | **5** |
| ARMTHOVR | Bear | 3 | **5** | 6 | **5** |
| CORTHOVR | Turtle | 3 | **5** | 6 | **5** |

Three corrections to the folk numbers the task brief carried:

- **Bear and Turtle load 5, not 6.** The 6 lives in the dead key.
- **The Envoy loads 5, not 24** (the Hulk really is 20). Both `rev31` and
  `ccdata` agree; whether nerf or typo, it is what the parsed key says.
- The Atlas FBI says capacity **5**, but an air transport can never hold more
  than one: `VTOL_Pickup`'s preamble aborts the mission outright while
  anything is attached (`0x41121E`: `mov ecx,[esi+0x8a]; test ecx,ecx; jne`
  -> return 8). Capacity is only consulted at order time by `0x489A90`;
  the **1-at-a-time rule for aircraft is hard-coded in the mission**, not
  data. (Under the 1.0 exe the Atlas presumably keyed off its absent
  `transportmaxunits`; under 3.1 the FBI was patched to 5 and the hard-coded
  rule does the limiting.)

A transport with no `transportcapacity` key parses as capacity 0 and can never
be ordered to load (0 < 0 fails) -- mods beware.

---

## 33. Which mission an order produces

In the order-type dispatcher (jump table `0x4401EC`, same function as S:3 of
TOTALA-EXE-MISSIONS.md; `esi` = orderer's def, `edi` = target unit):

**LOAD** (`0x43F701`): requires a target unit and `0x489A90` to pass, then

```
43f719  canfly (def+0x241 bit 11) ?  "VTOL_PICKUP"  : "GROUND_PICKUP"
```

**UNLOAD** (`0x43F735`):

```
43f735  if canload && canfly && target unit is an isairbase (targetdef+0x241 bit 9)
             -> "VTOL_LANDING"           ; landing on a carrier/pad, not an unload
43f764  if !canload -> no mission
43f76f  canfly ? "VTOL_UNLOAD" : "GROUND_UNLOAD"
```

The right-click default-action arm (`0x43F962`-`0x43F99F`) does the same
CanLoad -> pickup selection, so right-clicking a friendly unit with a loaded
cursor is the same order. **Hover transports are simply the `canfly=0` branch:
Bear and Turtle run `Ground_Pickup`/`Ground_Unload`, exactly like the Hulk and
Envoy.** There is no hover-specific mission.

Ground mission table rows (base `0x4FC490`, 25-byte records):

| # | Name | Handler | Display |
|---|---|---|---|
| 14 | `BeCarried` | `0x402FC0` | "Being transported" |
| 34 | `Ground_Pickup` | `0x406780` | "Loading" |
| 35 | `Ground_Unload` | `0x406900` | "Unloading" |

VTOL table rows (from TOTALA-EXE-MISSIONS.md S:1): `VTOL_Pickup` -> `0x4111B0`
("Loading"), `VTOL_Unload` -> `0x411560` ("Unloading").

---

## 34. `Ground_Pickup` -- `0x406780`, the crane flow (sea *and* hover)

Handler args (unit, mission, flags); jump table `0x4068E8`, six states.
Preamble: no target, or `flags & 8` (target lost) -> announce "Transport
mission failed" (`0x5016D8`), return 8.

```
state 0 (0x4067b1)  transport def canload required (else return 7)
                    movzx cx,BYTE [transportdef+0x22a]   ; transportsize
                    cmp WORD [target+0x7e],cx            ; target footprintX
                    -> too big: say "Unit is too large to transport", return 8
                    announce-once "Loading unit", return 1
state 1 (0x40685d)  0x438730(unit, mission, 8): while COB busy flag
                    (unit+0x10F bit 1) is set, sleep on wake mask 8|4; clear -> advance
state 2 (0x40680f)  start COB "TransportPickup"(targetUnitId)   ; 0x4B0A70, arg = WORD [target+0xa8]
                    play sound 0xc; mission+0x36++ (attempt count)
                    wake timer 15 ticks (0x439E80), return 1
state 3 (0x40685d)  wait for the script's busy flag to clear again
state 4 (0x40686f)  if target+0x86 != 0  -> attached: return 5 (done, delete mission)
                    if mission+0x36 >= 3 -> return 9 (park: rand(30)+30 tick timer, restart)
                    else install a ground move goal at the target's position,
                    tolerance 0 (0x438930(mission; &target+0x6a, 0)),
                    wake mask = 0xE8, return 1
state 5 (0x4068b1)  cancel the goal (0x4388D0(0)), return 0 -> state 0
```

The shape of it: **try the crane first; if the script could not reach, walk at
the target and try again on arrival (or on move failure -- mask 0xE8 wakes on
either), up to three attempts per cycle.** The engine never moves the
passenger itself -- the COB script does everything (see 10), and the actual
attachment happens when the script executes `ATTACH_UNIT`. The engine merely
polls `target+0x86`.

The footprint check here and in `VTOL_Pickup` reads the *instance* copy
`unit+0x7E`, which `0x485AAA` fills from `def+0x14A` at spawn -- the same
number `0x489A90` checks.

---

## 35. `Ground_Unload` -- `0x406900`

Jump table `0x406A88`, four states. Preamble: `flags & 8` -> announce
"Unloading process is proceeding non-optimally", return 8.

```
state 0 (0x40693e)  canload required
                    0x489690(&mission+0x12, [transport+0x8a]):
                        target := current head of the passenger list
                    no passenger -> return 5 (done)
                    announce-once "Unloading"
                    packed = (mission+0x22 & 0xffff0000) | (mission+0x2A >> 16)
                             ; (intX<<16)|intZ of the ordered drop point
                    start COB "TransportDrop"(passengerId, packed)
                    mission+0x36++; wake timer 15; return 1
state 1 (0x4069de)  wait for the busy flag to clear (0x438730 mask 8)
state 2 (0x4069f0)  if passenger+0x86 != transport -> dropped: sound 0xd, return 5
                    if mission+0x36 >= 3 -> return 9
                    tol = (transportdef+0x241 bit 12, canhover)
                          ? int(WORD [transportdef+0x180] * 1.5)   ; footprintZ*16*1.5
                          : 0
                    install move goal at mission+0x22 with that tolerance,
                    wake mask 0xE8, return 1
state 3 (0x406a76)  return 0 -> restart
```

Two things worth staring at:

- **`canhover` is bit 12 of `def+0x241`** (parser `shl eax,0xc` at
  `0x42C727`, keyed by the `canhover` push at `0x42C701`) -- a new S:30 entry.
  A hover transport approaching its drop point is allowed to stop
  **1.5 x footprintZ x 16 world units short** (96 for the 4-footprint
  Bear/Turtle) instead of reaching the exact spot -- it is a hovercraft parked
  on water reaching over the beach with its boom. Sea transports and anything
  else use tolerance 0. The 1.5 lives as a double at `0x4FC958`.
- One successful drop ends the mission (return 5). Unloading a full Hulk is
  the order layer re-issuing: the `Standby` default mission (`0x405FE0`)
  re-executes the unit's current order (`0x43B700`/`0x43B1F0`) each time the
  mission list drains, and an empty transport's next `Ground_Unload` returns
  5 immediately from state 0. (How the order is finally marked consumed was
  not traced.)

The engine does **not** test drop legality here -- `DROP_UNIT` does (08).

---

## 36. `VTOL_Pickup` (`0x4111B0`) and `VTOL_Unload` (`0x411560`)

### VTOL_Pickup, jump table `0x41153C`, six states

Preamble, every tick: no target or `flags & 0x10048` -> "Transport mission
failed", return 8. Then the **submersion re-test** (`0x4111D5`-`0x4111F9`):
`targetY + targetdef+0x16E <= seaLevel<<16` -> failed, return 8 -- a target
that dives after the order is aborted mid-mission. Then `transport+0x8A != 0`
(already carrying) -> return 8.

```
state 0 (0x411241)  canfly required; footprintX vs transportsize
                    -> too big: "Unit is too heavy to transport" (the message
                       says heavy; the test is the footprint), return 8
                    announce "Loading"; weapons to mission control (0x4898B0(3));
                    if sitting on a pad (unit+0x86) detach (0x48AAC0(u,0,-1,2));
                    activate (0x48B090(1,1));
                    if the mover is landed (mover+0x2E & 3 == 1): set mode 2
                    (take off), goal at own position, altitude cruisealt/2,
                    wake mask |= 0xE0
                    return 1
state 1 (0x41132f)  goal at the target unit (0x44E190), altitude = cruisealt,
                    tolerance 0x30 (48 wu); wake mask 0x100E8; return 1
state 2 (0x41138a)  announce "Preparing for transport"; mission+0x36 = -1;
                    call COB "QueryTransport"(&mission+0x36) (0x4B0BC0)
                    -> the script returns the grab piece; wake 0x100E8; return 1
state 3 (0x4113cb)  h = targetdef+0x16E                  ; target model height
                    start COB "BeginTransport"(h)        ; script drops its hook by h (10)
                    (0x456200: broadcast the same call to remote players - display only)
                    resolve the piece's offset (0x43DEF0(unit, mission+0x36));
                    goal at the target, altitude = -(piece y-offset)
                    -> descend until the hook piece sits at the target's top;
                    wake 0x100EA; return 1
state 4 (0x411479)  if flags & 0x42 -> run "EndTransport", return 8 (aborted)
                    0x48AAC0(target, transport, mission+0x36)  ; ATTACH at the piece
                    sound 0xc; goal at own position, altitude cruisealt,
                    wake |= 0xE0; return 1                     ; climb away loaded
state 5 (0x4114fe)  return 5 - done
```

### VTOL_Unload, jump table `0x411828`, four states

Preamble: empty passenger list -> return 5.

```
state 0 (0x41159d)  canfly required; target := head of passenger list (0x489690);
                    announce "Unloading";
                    goal at mission+0x22 (the ordered point), altitude cruisealt,
                    tolerance 0x140 (320 wu); wake 0xE8; return 1
state 1 (0x411635)  drop-legality test (see below) at mission+0x22 for the
                    passenger's def; fail -> "Unable to unload unit", return 9
                    goal at mission+0x22, altitude = WORD [passengerdef+0x170]
                    (the passenger's model height - descend until the slung
                    unit touches the ground); wake 0xE8; return 1
state 2 (0x4116f2)  flags & 0x40 (move failed) -> return 9
                    re-run the same legality test; fail -> "Unable to unload
                    unit", return 9
                    run COB "EndTransport" (0x4B0940, fire and forget)
                    0x48AAC0(passenger, 0, -1, mode 1)   ; detach, landed
                    sound 0xd; goal at own position, altitude cruisealt;
                    wake 0xE0; return 1                  ; climb away
state 3 (0x4117fe)  sound 0xd; return 5
```

The legality test call, both states (`0x41163B`-`0x41168F`):

```
cellX = (mission.x - (footprintX << 19) + 0x80000) >> 20  ; centre the footprint,
cellZ = likewise                                          ; round, to cell coords
0x47DB70(passengerDef, 0, packed cells, 1)
```

---

## 37. The drop-legality test `0x47DB70`, and `ATTACH_UNIT` / `DROP_UNIT`

`0x47DB70(def, occupantTag, packedCellXZ, mode)` -- "may a unit of this
definition stand here". Mobile units (`bmcode` != 0; buildings divert to the
build-placement test `0x47D2E0`) walk every footprint cell (map cell records,
stride 13 bytes) and require, per cell:

```
47dc78  word cell+0x08 occupancy: 0xFFFF free; 0xFFFE -> resolve the building
        root cell (bytes +0xA/+0xB) and require it free; else a feature id ->
        its record's byte +0xFE bit 6 (blocking) must be clear
47dcfd  word cell+0x00 (occupying unit id stamp) must be 0 or == occupantTag
47dd10  byte cell+0x06 (low corner)  >= seaLevel - maxwaterdepth   ; not too deep
47dd24  byte cell+0x05 (high corner) <= seaLevel - minwaterdepth   ; deep enough
47dd31  slope = high - low; if slope > maxslope (def+0x228):
            legal only if the cell is underwater (low < seaLevel) AND
            slope <= maxwaterslope (def+0x229); else reject
```

All four numbers come from the movement class (with the 10000/-10000/255
defaults of 01), so:

- a **tank** (`TANKSH2`, maxwaterdepth 12) can be dropped on land or in water
  up to 12 deep, and nowhere deeper -- "tanks no";
- a **hover tank** (`TANKHOVER3`, defaults) can be dropped on any water and on
  land up to slope 12 -- "hovercraft yes";
- a **ship or sub** could be dropped only where the water is at least its
  minwaterdepth -- "ships/subs yes" -- though nothing in the shipped data can
  actually carry one;
- an **amphibian** (Triton/Crock, maxwaterdepth 100+) goes anywhere shallower
  than that.

`floater` and `canhover` are **not consulted**; the water rules are entirely
maxwaterdepth/minwaterdepth/slopes. The air path calls this test directly
(07). The ground path gets it for free: the COB `DROP_UNIT` handler
(`0x4813B0`, vtable slot +0x3C off the dispatch at `0x4B1B58`) refuses to
release a unit onto an illegal cell:

```
4813e0  unit must be alive, and attached to this unit
4813fd  0x47DB70(passengerDef, passengerId, passenger+0x76 (its current packed
        cell - i.e. wherever the boom has swung it), 1)
481419  fail -> do nothing (the unit stays attached; the mission retries)
48141b  ok   -> 0x48AAC0(passenger, 0, -1, mode 1)   ; detach, landed
```

`ATTACH_UNIT` (slot +0x38, body ending `0x4813A6`) resolves the unit, requires
it alive and either unattached or already attached **to this unit** (that is
what lets a script re-attach the same unit to a different piece), then calls
`0x48AAC0(unit, self, piece, mode)`.

### `0x48AAC0` -- attach/detach, the real state change

Validates (unit alive and not dying; **a unit that is itself carrying
something cannot be attached** -- `0x48AAE3`; carrier alive, not the unit, and
not itself carried), then broadcasts a 7-byte type-0xA network message and
applies it locally at `0x48AB70`:

```
48ac57  detach path (carrier == 0): clear +0x86/+0x8E, clear hidden bit 17,
        re-insert into the spatial grid (0x47CB40)
48ac70  attach path: unit+0x86 = carrier; push onto the head of the carrier's
        +0x8A list (LIFO); unit+0xF9 = piece;
48ac99  bit 17 of unit+0x110 (hidden) := (piece == 0xFF)
            ; attach-unit to piece -1 is how sea/hover transports hide cargo
            ; inside the hull; the Atlas attaches to a real piece, so its
            ; cargo dangles visibly
48ace1  mover mode bits (mover+0x2E & 3) := the mode argument
            ; pickup-detach passes 2 (airborne), drop-detach passes 1 (landed)
48acf3  if the unit's player is human/remote and the carrier is NOT an
        isairbase (def+0x241 bit 9): 0x4384A0(unit)
            ; flush the mission list and hand the unit "BeCarried"
            ; (pads skip this - aircraft on a carrier deck keep their orders)
```

On the attach path the unit was first removed from the spatial grid
(`0x47CB00` at `0x48AC5D`) if it was unattached.

`BeCarried` (`0x402FC0`) is two states: take all weapons under mission control
(`0x4898B0(3)` -- weapons hold), then sleep in 10-tick pokes until `unit+0x86`
clears, whereupon it returns 5 and the unit falls back to its default mission.

---

## 38. The carried state, damage, and dying with the transport

While carried, a unit is: off the spatial grid, hidden if attached to piece
-1 (the render loops at `0x459423` etc. skip `unit+0x110` bit 17), weapons
held, missions flushed to `BeCarried`. Because every weapon-target search and
splash collection runs over the spatial grid (TOTALA-EXE.md S:6/S:12), a
carried unit **cannot be hit by anything** while aboard -- the reasoned
consequence of the grid removal; no per-check "is carried" test exists or is
needed.

When the transport dies, the kill handler (the death-message path around
`0x4867B0`):

```
4867ba  if the dying unit is itself attached: detach (0x48AAC0(u,0,-1,1))
4867d0  while the passenger list is non-empty:
4867fe      0x489BB0(killer, passenger, 0x7530, deathKind, 0)
                ; 30000 damage through the normal pipeline - at or over
                ; 0x7530 armour is ignored (TOTALA-EXE.md S:6), so this
                ; kills anything the game can carry
486818      0x48AAC0(passenger, 0, -1, 1)     ; then detach it
```

So: **passengers are not damageable while carried, and all of them die when
the transport does** -- killed by 30000 armour-piercing damage, credited to
the transport's killer, then detached (their wrecks land at the death spot).

---

## 39. The hover transport flow -- what the Bear and Turtle actually are

`ARMTHOVR.COB`/`CORTHOVR.COB` are structural copies of the sea transports'
scripts (`ARMTSHIP.COB`): pieces `boom1..4`, `magnet`, `link`; functions
`BoomCalc`, `BoomExtend`, `BoomReset`, `BoomToPad`, **`TransportPickup`**,
**`TransportDrop`**. There is no `QueryTransport`/`BeginTransport`/
`EndTransport` -- nothing of the Atlas in them. Decompiled (Bear, functions 11
and 12; get-value ids 9 = UNIT_XZ, 10 = UNIT_Y, 11 = UNIT_HEIGHT,
16 = GROUND_HEIGHT, set-value 6 = BUSY):

```
TransportPickup(u):
    BoomCalc(UNIT_XZ(u), UNIT_Y(u) + UNIT_HEIGHT(u))   ; aim the crane; sets a
    if !static4: return                                ; success flag - out of
    BUSY = 1                                           ; reach fails silently
    BoomExtend()
    move link piece to y = -UNIT_HEIGHT(u); ATTACH_UNIT(u, link, 0)
    BoomToPad(); ATTACH_UNIT(u, -1, 0)                 ; swing in, hide in hull
    BoomReset(); BUSY = 0

TransportDrop(u, packedXZ):
    BoomCalc(packedXZ, GROUND_HEIGHT(packedXZ) + UNIT_HEIGHT(u))
    if !static4: return
    BUSY = 1
    BoomToPad(); move link to y = -UNIT_HEIGHT(u); ATTACH_UNIT(u, link, 0)
    BoomExtend()                                       ; swing out to the point
    DROP_UNIT(u)                                       ; engine checks 0x47DB70
    BoomReset(); BUSY = 0
```

The `BUSY` value is the engine's `unit+0x10F` bit 1 -- exactly what
`0x438730` polls between mission states. The Atlas COB, for comparison:
`QueryTransport` returns piece 1 (`link`); `BeginTransport(h)` is one
instruction, `MOVE_NOW link y -> -h`; `EndTransport` folds the arms back.

So the answer to "how do Bear/Turtle load" is: **crane-style, via
`Ground_Pickup`/`Ground_Unload` and COB `TransportPickup`/`TransportDrop`,
identically to the Hulk/Envoy** -- the only hover-specific behaviour in the
whole path is the 1.5x-footprint unload arrival tolerance of 06 (`canhover`
bit) and, of course, that a hovercraft's mover can park on water next to its
cargo. If RWE's hover transports do nothing today, the missing piece is not a
new mechanism: it is (a) hover movement getting the transport within boom
reach, and (b) the same crane flow the sea transports already run.

FBI notes: Bear/Turtle are `canload=1`, `transportsize=3`,
`transportcapacity=5`, `canhover=1`, `MovementClass=TANKHOVER4`,
`DefaultMissionType=Standby`.

---

## 40. Implementation spec for RWE

Definitions used below: `fpX(u)` = footprint X in cells (movement-class
override included); `height(u)` = model max-Y; `seaLevel` from the map;
`minWD(u)`/`maxWD(u)` = the movement parameters with defaults
**maxWD = 10000, minWD = -10000** when neither the FBI nor the class says
otherwise; `carried(t)` = number of units attached to `t`.

### Data

| Transport | capacity | size | notes |
|---|---|---|---|
| Atlas | 5 (data) -> **1 effective** | 3 | air rule below |
| Valkyrie | 1 | 3 | |
| Hulk | 20 | 3 | |
| Envoy | **5** | 3 | not 24 |
| Bear | **5** | 3 | not 6 |
| Turtle | **5** | 3 | not 6 |

Parse `canload` (the button/eligibility gate -- do not key off
`transportCapacity > 0`) and `cantbetransported`. Ignore `transportmaxunits`
everywhere.

### Load eligibility (order time and cursor), all transports

```
canLoad(t, u):
    !u.def.cantBeTransported
    && t.def.canLoad
    && carried(t) < t.def.transportCapacity      // flat headcount
    && u is mobile                                // no buildings
    && fpX(u) <= t.def.transportSize
    && u is not airborne
    && (t.def.canFly || minWD(u) < 0)             // sea+hover refuse ships/subs
    && u.position.y + height(u) > seaLevel        // nothing submerged
    && u fully built
```

No team check in the sim; RWE should keep its UI-level own-units-only rule.

### Air transports (canFly): capacity is 1, hard

Refuse to begin a pickup while anything is attached, regardless of
`transportCapacity`. Abort the pickup mission the moment the target's top
sinks to or below sea level. Flow: climb to cruisealt/2 if landed -> fly to
48 wu of the target at cruisealt -> `QueryTransport` (script returns the hook
piece) -> `BeginTransport(height(u))` (script lowers the hook by the cargo's
height) -> descend to altitude = height(u) (equivalently, -hookPieceYOffset)
-> attach at the piece -> climb to cruisealt -> done. RWE's existing Atlas
flow already approximates this; the numbers above (48 wu approach tolerance,
cruisealt/2 takeoff, descend-to-cargo-height) are the original's.

### Air unload legality (per footprint cell, footprint centred on the click)

```
cellFree                                          // no unit, no blocking feature
&& cellLowCorner  >= seaLevel - maxWD(u)          // not too deep for the cargo
&& cellHighCorner <= seaLevel - minWD(u)          // deep enough (ships/subs)
&& (slope <= maxSlope(u)
    || (cellLow < seaLevel && slope <= maxWaterSlope(u)))
```

Expressed in RWE's current fields: a unit with `maxWaterDepth` w may go where
`seaLevel - terrainHeight <= w`; a unit with `minWaterDepth` m >= 0 only where
the water is at least m deep; `floater`/`canHover` play **no part** -- a hover
tank passes because its effective maxWD is the 10000 default, so RWE must not
substitute `canHover` for that default. Test before descending and again
before releasing; on failure say "Unable to unload unit" and back off
(rand 30-60 ticks) rather than cancelling the order. Fly to within 320 wu of
the point at cruise altitude first, then descend to altitude = height(u),
detach in *landed* state, climb away. One unit per mission; re-issue while
cargo remains.

### Ground/hover transports: the crane flow

One mission shape for Hulk, Envoy, Bear, Turtle:

- **Pickup**: check size (announce "Unit is too large to transport" on
  failure); wait for the COB busy flag; run `TransportPickup(cargoId)`; wait
  busy clear; if not attached, path toward the cargo (tolerance 0 -- let the
  mover get as close as it can; wake on arrival *or* failure) and retry, three
  attempts then back off. The script's reach check (`BoomCalc`) is the real
  range gate -- the engine has none.
- **Unload**: pick the head of the passenger list; run
  `TransportDrop(cargoId, packedDropXZ)` with packed = (intX<<16)|intZ; wait
  busy; if still attached, move toward the drop point -- arrival tolerance
  `1.5 x footprintZ x 16` wu if the transport is `canHover`, else 0 -- and
  retry x3. `DROP_UNIT` itself must enforce the same per-cell legality as the
  air unload (at the cargo's current cell, its own id counting as free) and
  silently keep the unit aboard when it fails. One unit per mission; the
  order re-issues until empty.

### The carried state

On attach: remove from collision/targeting, hide iff attached to piece -1
(keep Atlas cargo visible), stop weapons, flush orders to a BeCarried idle
(unless the carrier is a repair pad), set the passenger's mover to
landed/airborne per the detach mode. On carrier death: deal each passenger
30000 armour-ignoring damage credited to the carrier's killer, then detach.
A unit carrying cargo can itself never be picked up.

---

## 41. Loose ends

- The exact mechanism that consumes an UNLOAD *order* once the transport is
  empty was not traced (the `Standby` handler `0x405FE0` re-issues the current
  order via `0x43B1F0`; an empty transport's unload mission returns 5
  immediately, so the loop terminates behaviourally either way).
- Wake-mask bit meanings are used as opaque constants above (0xE8, 0x100E8,
  0xE0; bit 0 = timer, bit 2 = set by the busy-wait, bit 6 = move failed,
  bit 16 = the clear-weapon-targets event of the framework's step 5). A full
  decode of the event word `unit+0xBA` remains open.
- `0x43DEF0` (piece offset resolution) was read only closely enough to see
  VTOL_Pickup negate the piece's y-offset into a goal altitude; the
  BeginTransport arithmetic (hook at -height, goal altitude +height) makes the
  intent unambiguous, but the routine itself was not fully decoded.
- Whether the 1.0 exe read `transportmaxunits` was not checked (no 1.0 binary
  at hand); the 3.1 exe certainly does not.

---

## 42. The CD player object

The music engine is one C++ object -- the class name `SJE_CdPlayerClass` is in
the binary at file offset 0x109bd0 -- hanging off the global game object:
`[ds:0x511de8 + 0x10]`. Its fields, recovered from the accessors:

| Offset | Meaning |
|---|---|
| `+0x00` | MCI device open flag |
| `+0x14` | aux device id for `auxSetVolume` |
| `+0x20` | target CD volume (0-0xFFFF) |
| `+0x1fc` | music mode, 0-4 (setter `0x4CE7A0`) |
| `+0x200` | number of tracks on the CD, from `status cdaudio number of tracks` |
| `+0x204` | user-selected track (setter `0x4CE580` clamps to track count; the TRACKNUM display / CDNEXT / CDPREV drive this) |
| `+0x208` | track currently playing, from `status cdaudio current track` |
| `+0x20c` | "we started a play" flag |
| `+0x210` | CD identity (volume serial of the disc, read at `0x4CDA00` via `0x4BB190`/`0x4BB260`) |
| `+0x214` | **track type array**: 100 bytes, one per track, indexed by 1-based MCI track number (`type[track]`, byte). Getter `0x4CE7E0`, setter `0x4CE7C0`, bulk-load `0x4CE3E0` (copies `count` bytes from a buffer into `+0x215`, i.e. buffer[0] -> track 1) |
| `+0x278` | **situation state**, 0-4 (setter `0x4CE690`, getter `0x4CE680`) |
| `+0x27c` | music enabled flag (setter `0x4CEDC0`; when cleared, stops the CD) |
| `+0x284` | volume fade step per timer tick (negative during fade-out) |
| `+0x28c` | completion callback, set to `0x490FE0` (the pump, below) |

MCI plumbing: all control goes through `mciSendStringA` with the literal
strings at file 0x109b58-0x109cc8 (`status cdaudio number of tracks`,
`status cdaudio mode` -- whose answer is strcmp'd against `playing` --
`play cdaudio from %i` + ` to %i` + ` notify`, `stop cdaudio`, and so on). A
play command always carries `notify`; the MM_MCINOTIFY handler (0x4CE130
area) refreshes status via 0x4CDA00 and then calls the completion callback,
which is how the playlist advances.

---

## 43. Registry footprint

Everything is stored under **HKEY_CURRENT_USER\Software\Cavedog
Entertainment\Total Annihilation**. The generic accessor at 0x4B6880 does
`RegCreateKeyExA(HKEY_CURRENT_USER, "Software")`, then "Cavedog
Entertainment", then the section name the caller passes ("Total
Annihilation" at 0x5032E8), with KEY_READ (0x20019) or KEY_WRITE
(0x20006) picked by a read/write flag. Note this 3.1-era GOG exe uses **HKCU**,
not the HKLM path older documentation gives -- the constant pushed at
0x4B68B6 is 0x80000001. (GOG's win32.dll reads the same HKCU path.)

The music-related values, from the settings load at 0x4305A2-0x430760 and
the save at 0x4313B3-0x431476:

| Value | Type | Backs | Default when absent |
|---|---|---|---|
| `musicmode` | DWORD | `[game+0x37f14]` bit 0 -- CD music **on/off** (the "CD Music Off/On" toggle) | on |
| `cdmode` | DWORD | `[game+0x37f16]` -- the music **mode**, 0-4 | **4** (situational) |
| `musicvol` | DWORD | `[game+0x37f10]` -- music volume | 0x20 (32) |
| `CDLISTS` | BINARY, 0xAA0 bytes | the per-CD track-type lists (next section) | zeroed |

Despite the names, `musicmode` is the on/off flag and `cdmode` is the mode.
The strings sit together in the file at 0x102b20 (`cdmode`) and 0x102b28
(`musicmode`).

There is also a **dead** routine at 0x42F910 that writes ten DWORD values
named `track0`..`track9` (sprintf of `track%d` at 0x102968) from a 10-byte
type array -- no call site anywhere in the binary. It is the leftover of an
older per-track persistence scheme, superseded by CDLISTS. Do not implement
it.

---

## 44. Track types and the CDLISTS table

The type of each track is one byte, and the values are exactly the cycle
positions of the TRACKTYPE gadget in MUSIC.GUI
(`text=Building|Battle|Victory|Defeat|Unused;`, `stages=5`):

| Value | Label |
|---|---|
| 0 | Building |
| 1 | Battle |
| 2 | Victory |
| 3 | Defeat |
| 4 | Unused |

The GUI handler reads/writes the byte with no translation (0x45C452: gets
`type[selected]` via 0x4CE7E0 and stuffs it straight into the cycle gadget;
0x45C561 writes the cycle stage back via 0x4CE7C0), so cycle index = type
value.

**Constructor default** (0x4CE260): the whole 100-byte array is filled with
`type[i] = (i % 4) + 1`:

```
4ce287:  mov  BYTE PTR [esi+0x214],bl      ; type[0] = 1
4ce28f:  mov  eax,ecx                      ; i
         cdq / xor / sub / and 3 / ...     ; i % 4 (signed)
4ce29d:  inc  al                           ; + 1
4ce29f:  mov  BYTE PTR [ecx+esi*1+0x214],al
4ce2a6:  inc  ecx
4ce2a7:  cmp  ecx,0x64                     ; 100 entries
```

i.e. a cycle of Battle, Victory, Defeat, Unused -- never Building. This is only
the fallback for an unrecognized CD; on such a disc in situational mode the
Building state finds no track and music simply stops.

**CDLISTS** is an MRU list of 20 CDs, 0x88 bytes each (20 x 0x88 = 0xAA0),
living at 0x51E828 and written verbatim as the REG_BINARY value. Entry
layout (from the search loop at 0x4910C0 and the new-entry writer at
0x491190):

| Entry offset | Meaning |
|---|---|
| `+0x00`-`0x1f` | never written by the code I found (junk/padding) |
| `+0x20` | CD identity dword (`[obj+0x210]`, the volume serial) |
| `+0x24` | track types, `type[1..100]` as bytes (buffer[0] = track 1) |

The pump 0x490FE0 runs at init and on every MCI notify. It looks the current
CD id up in the list; on a hit it moves that entry to slot 0 (MRU) and loads
its types into the object via 0x4CE3E0. On a miss it shifts the list down
(dropping the oldest), writes a new slot-0 entry, **and if -- and only if --
the disc looks like the TA game disc, applies the shipped default types**:

- `status cdaudio number of tracks` must answer exactly **16** (0x491148),
- `status cdaudio type track 1` must answer something other than `audio`
  (0x4CE460 -- i.e. track 1 is the data track of a mixed-mode disc).

The default buffer, built on the stack at 0x490FF3-0x491037, is **seven
1-bytes followed by nine 0-bytes**, applied to tracks 1-16:

- tracks 1-7 -> type 1 (**Battle**) -- track 1 being the unplayable data track,
- tracks 8-16 -> type 0 (**Building**),
- **no track defaults to Victory, Defeat, or Unused.**

Saving: 0x490F80 copies `type[1..count]` back into slot 0's +0x24 area and
writes the whole 0xAA0 blob to CDLISTS (write helper 0x42F960, REG_BINARY).
It is called when leaving the music settings screen (0x49173D).

### Mapping to the GOG files

GOG replaces the CD with music/<n>.mp3 plus an MCI shim (win32.dll, a
winmm proxy). Its `play cdaudio from %i` handler parses the track number and
builds the filename as `"music/" + itoa(n) + ".mp3"` with **no offset**
(number stored at its 0x10009030, filename assembled at 0x100013FE), so
<n>.mp3 *is* MCI track n. The shim's own auto-advance wraps in 2..17,
consistent with track 1 being the data track. So the shipped default translates
directly:

| CD track / mp3 | Title | Default type |
|---|---|---|
| 1 | (data track -- typed Battle but not playable audio) | Battle[1] |
| 2 | Brutal Battle | **Battle** |
| 3 | Fire And Ice | **Battle** |
| 4 | Attack!!! | **Battle** |
| 5 | Warpath | **Battle** |
| 6 | The March Unto Death | **Battle** |
| 7 | Ambush in the Passage | **Battle** |
| 8 | Forest Green | **Building** |
| 9 | Death And Decay (file duplicates 0.mp3) | **Building** |
| 10 | Stealth (file duplicates 1.mp3) | **Building** |
| 11 | Licking Wounds | **Building** |
| 12 | Futile Attempt | **Building** |
| 13 | On Throughout the Night | **Building** |
| 14 | Desolation | **Building** |
| 15 | Charred Dreams | **Building** |
| 16 | Where Am I | **Building** |
| 17 | Blood of the Machines | *(not on the 16-track disc the default recognizes; GOG extra)* |

[1] The data track genuinely carries type 1 in the default table (buffer byte 0
is 1 and 0x4CE3E0 maps buffer[0] -> track 1). The random pick below can
therefore land on it; on real hardware MCI just fails to play it and the next
notify re-rolls. An implementation should simply not include it.

The battle/building split lands exactly on the aggressive-sounding titles
(2-7) versus the calm ones (8-16), which is good evidence the alignment is
right. 0.mp3/1.mp3 are duplicates of tracks 9/10 that the original logic
never addresses; 17.mp3 is outside the recognized TOC. Treat 17 as a
Building track if you want it in the rotation (it is a calm track).

---

## 45. The five music modes

`[game+0x37f16]` / registry `cdmode`, copied into the object (+0x1fc) at
scene start. The chooser (0x4CDB40) dispatches on it through the jump table
at 0x4CE00C:

| Mode | Handler | Behaviour when the current track ends (or the chooser is poked) |
|---|---|---|
| 0 | 0x4CDBEF | nothing new is ever started; when the current play finishes, `stop cdaudio` and clean up |
| 1 | 0x4CDCC4 | sequential: next = current+1 (wrap to 1 past the end), and it issues one MCI play from that track **through the last track** |
| 2 | 0x4CDD7B | random: `rand() % count + 1`, play that one track |
| 3 | 0x4CDDFD | repeat: keep the user-selected track (+0x204) playing; if something else is playing, switch to it |
| 4 | 0x4CDE96 | situational, by track type (next sections) |
| - | 0x4CDB5D | before any of that: if the situation state is 4, `stop cdaudio` and reset; if the state is 2 or 3 (Victory/Defeat), jump straight to the type-matched handler regardless of mode |

The TRACKMODE cycle in MUSIC.GUI is `Play All|Random|Repeat|Custom`
(4 stages) and the code maps it as **mode = stage + 1** (0x45D156: the
gadget is set to mode - 1). Mode 0 is not on the dial; it exists for
completeness/off. The out-of-the-box mode is **4, Custom** -- the situational
system is TA's default.

The TRACKTYPE gadget is only enabled when music is on **and** mode is 4
(0x45D239: `test [0x37f14],1` / `cmp [0x37f16],4`).

---

## 46. The situation state

`[obj+0x278]`, values matching the track types: 0 Building, 1 Battle,
2 Victory, 3 Defeat, 4 = silence/stopped. The setter 0x4CE690(newState):

1. If unchanged, return.
2. Save the currently-playing track into `0x51FF20[oldState]` -- a per-state
   resume table that is **written and never read** (only xref is the write at
   0x4CE6AF). Dead.
3. Record the new state.
4. If mode != 4 **and** the new state is not 2/3, stop there -- the state is
   bookkeeping only. Otherwise:
   - if the old state was 4 (silence): restore volume and call the chooser at
     once -- no fade;
   - else start a **fade-out**: step = -volume/18 every 2 ticks (timer set at
     0x4CE770; the imul by 0xC71C71C7 then `sar 2` is signed /18). When
     the fade reaches zero (0x4CE5E0): volume 0, and if the new state is 0
     (Building) arm a one-shot **120-tick** timer before calling the chooser
     (0x4CE64D), otherwise call the chooser immediately.

Ticks here are the game clock: 0x4B6340 returns
`timeGetTime() * R / 1000` where R is the tick rate at `[[0x51FBD0]+0xe8]` --
30 at Normal game speed (inference from the game-speed system; the timer
service 0x4B63F0 counts these down). So the fade is 18 x 2 = 36 ticks,
about **1.2 s**, and Building music resumes **4 s** after the fade ends.

**Who sets the state.** Every call site of 0x4CE690, exhaustively:

| Site | State |
|---|---|
| 0x491477 | 0 at game-scene init (music starts in Building) |
| 0x494F8C | 0 or 1 from the battle/peace evaluator (next section) |
| 0x49848B | 0 on the display-mode-change/resume path |
| 0x4910A9, 0x49EC2E | restores a saved state around CD re-init / disc change |
| 0x41ED7E, 0x426462, 0x460602, 0x491B86 (helper 0x491B60, called from six places), 0x4996BE, 0x49986B | **4** -- menus, frontend screens, and both endgame paths |
| 0x4175F6 | the **MusicMode console command** (table entry at .data file 0x10041C): sets the state to its numeric argument |

The endgame handlers (0x499603 region: `mov edi,4` ... `push edi`) silence the
music when the game ends -- **they do not set Victory (2) or Defeat (3)**.
States 2 and 3 are reachable *only* through the debug console command. The
chooser and the GUI fully support Victory/Defeat track types, but no gameplay
event ever triggers them; the feature was built and never wired up. (This
matches the long-standing community observation that the Victory/Defeat
settings do nothing.)

Also for the record: the string `battlestart` (0x104440) is the name of the
start button in the multiplayer battle room GUI. It has nothing to do with
music.

---

## 47. The battle/peace evaluator, 0x494E70

Runs from the in-game per-frame loop (called at 0x4999A1). Gates:
`[game+0x2a44]` bit 2 must be set (set at mission start
0x4269B0/0x426BA4, cleared by the endgame music-off helper 0x491B60),
not paused (`[game+0x38d75]` bits), and it early-outs unless at least
**30 ticks (about 1 s)** have passed since its last full run (0x51F2F8 holds
the last run time).

State it keeps:

- 0x51E710: a ring of **30 slots**, one per second, cleared at game start
  (0x4919F7). Index at 0x51F2DC. Each run advances the ring and zeroes the
  new slot.
- 0x51F2FC: seconds since the last state change (reset to 0 on a change).
- 0x5091D0: the last state the evaluator chose.

**What feeds the ring** -- AddBattleActivity(n) at 0x494FF0 adds n to the
current slot. Exactly two call sites:

- 0x489DE6, in the weapon-damage application path: **+1** when a unit takes
  a hit and either the attacker's owner or the victim's owner is the local
  player (0x489DC6-0x489DE4, local player index at `[game+0x2a42]`).
- 0x4869EB, in the unit-death path: **+5** when the dying unit's killer
  (`[unit+0xf4]`, the owner of the last unit that damaged it) is the local
  player.

**The decision**, once per second, and only when at least 10 s have passed
since the last change (0x494ED7: `cmp eax,0xa; jle skip`):

- Let sum30 = sum of all 30 slots (last ~30 s), sum5 = sum of the 5 most
  recent slots (last ~5 s).
- Currently Building (state 0) -> **switch to Battle** if
  `sum30 > 50 || sum5 > 30`, **and** the local player's unit count
  (`word [game + 331*p + 0x1ca7]`, the counter the debug overlay labels
  "Total Units", p = local player) is **> 30** (0x494F53). A commander
  skirmish with a handful of units keeps the peace music no matter how hot it
  gets.
- Currently Battle (state 1) -> **revert to Building** if
  `sum30 < 10 && sum5 == 0 && at least 60 s since entering Battle`
  (0x494F69-0x494F82).
- On a change: setState, reset the 10 s / 60 s counter.

The evaluator always runs (in any mode); the setter just ignores its result
unless mode is 4, so flipping to Custom mid-game picks up the current
situation.

---

## 48. The chooser in situational mode, 0x4CDE96

Poked at every MCI notify (track finished), after every fade, and at scene
start. With the state in s:

1. If MCI reports `playing` and `type[currentTrack] == s`, do nothing.
2. Otherwise pick a track: `r = rand() & 0xF`; walk forward from the current
   track, wrapping past the track count to 1, and take the **(r+1)-th track
   whose type equals s** -- a uniform-ish random pick among the tracks of the
   wanted type, with a scan budget of (r+1) x count steps.

```
4cde96:  call rand ; ebx = eax & 0xF
4cdf48:  inc  ecx / wrap to 1                ; walk forward
4cdf54:  mov  al,[ecx+edx*1+0x214]           ; type[track]
4cdf5b:  cmp  eax,ebp / jne                  ; == state?
4cdf5f:  dec  ebx / jle found
4cdf64:  dec  esi / jg loop                  ; budget = (r+1)*count
```

3. From the chosen track, count how many **consecutive** tracks (no wrap)
   share the type, and issue a single `play cdaudio from c to c+n notify`
   (0x4CDF6B-0x4CDF8B, play routine 0x4CEB60). With the default table a
   battle pick plays through the rest of tracks 2-7 in disc order before the
   next notify re-rolls.
4. If the budget runs out with no track of type s on the disc:
   `stop cdaudio` -- silence until the next state change.
5. Restore the target volume (0x4D00D0, an `auxSetVolume` on both channels).

Victory (2) and Defeat (3) route into this same type-matched pick regardless
of mode (checked before the mode dispatch, 0x4CDBC2), which is how they
*would* have played had anything set them.

The `CDPlay <n>` / `CDStop` console commands (0x4167F0/0x416810) are thin
wrappers over play-one-track and stop.

---

## 49. What RWE should implement

A playlist of named tracks, each typed Building/Battle/Victory/Defeat/Unused,
with these defaults (GOG names):

- **Battle**: Brutal Battle, Fire And Ice, Attack!!!, Warpath, The March Unto
  Death, Ambush in the Passage.
- **Building**: Forest Green, Death And Decay, Stealth, Licking Wounds,
  Futile Attempt, On Throughout the Night, Desolation, Charred Dreams,
  Where Am I -- and Blood of the Machines if we want the whole OST in rotation
  (it was not on the disc the original's default table covers).
- **Victory / Defeat**: empty by default, exactly like the original.

Behaviour (all times at Normal speed; internally these are 30 Hz ticks and
scale with game speed in the original):

1. **Start of a battle scene**: state = Building; pick a random Building track
   and play it. Menus and the frontend: no music (the original stops the CD
   there).
2. **Track selection**: uniform random among tracks of the current state's
   type. When a track ends, pick again from the same type. (The original's
   forward-scan from the current track and its consecutive-run playback are
   CD artifacts; random-per-track is the faithful simplification -- note the
   original *can* repeat the same track back-to-back, since the scan can lap.)
3. **Battle detection**, evaluated once per second in-game, not while paused:
   - Keep a 30-slot one-second ring of "battle points": **+1** whenever a unit
     belonging to, or attacked by, the local player takes a weapon hit; **+5**
     whenever the local player's damage kills a unit.
   - Building -> Battle when (points in last 30 s > 50 **or** points in last
     5 s > 30) **and** the local player owns more than 30 units.
   - Battle -> Building when points in last 30 s < 10 **and** last 5 s = 0
     **and** Battle has held for at least 60 s.
   - After any switch, no new switch for 10 s.
4. **On a switch**: fade the current track out over about 1.2 s (18 steps of
   -vol/18 every 2 ticks). Then, entering Building, wait a further 4 s
   (120 ticks) of silence before starting the Building track; entering Battle,
   start the Battle track immediately after the fade.
5. **Game end (win or lose)**: fade out and stop the music. Do **not** play
   the Victory/Defeat types -- the original never does; those states exist only
   behind its debug console command. (If we ever want them: they bypass the
   mode check and play a random track of the matching type.)
6. **Modes**, if we surface them: Off, Play All (album order, wrapping),
   Random (any track), Repeat (one track), Custom (the above). Default Custom.
   The situational evaluator runs regardless of mode but only Custom acts on
   it. If a wanted type has no tracks in Custom: silence until the state
   changes.
7. **Settings that persist**: music on/off, mode, volume, and the per-track
   type list (the original keys the list to the disc identity in CDLISTS;
   RWE has one fixed "disc" and needs just one list).

### Loose ends, labelled

- The per-state resume table (0x51FF20) is dead code in the exe -- the
  original never resumes a track where it left off; every entry into a state
  re-rolls.
- The first 0x20 bytes of a CDLISTS entry are never touched by any code I
  found; unknown, probably unused.
- The "> 30 units" gate reads the counter the debug overlay labels "Total
  Units"; I found its increments (unit creation, 0x486187/0x486322) and
  its use as the live count against the unit limit, but not the decrement
  site -- some computed addressing I did not chase. Its role as "current unit
  count of the local player" is solid from the limit checks and the player-
  elimination checks that read it.
- R = 30 ticks/second for all timings is inferred from the game-speed system
  (Game Speed normal = 30 fps); the arithmetic in 0x4B6340 is exact
  (`ms * R / 1000`), and R at other speed settings scales the music timings
  with it.

---

## 50. The interface colour table: found, and it was never a table of constants

Sections 25 and 27 said the byte array at `cfg+0xDCB` has no writer anywhere in
`.text`, and left the palette entries unknown. The scan was right and the
conclusion was wrong: **nothing writes displacement `0xDCB` because the writer
addresses the table as `+0x8B2` from `cfg+0x519`.** The table is real, it is
found, and it is not a hand-written list at all - it is **computed at runtime
by nearest-colour matching `palettes/GUIPAL.PAL` against the screen palette**.

The chain, all verified in the disassembly:

- `0x42A400` (called from the in-game init `0x491200` at `0x491378`) loads
  `palettes/PALETTE.PAL` and copies all 256 four-byte entries verbatim into
  `cfg+0x143A7` (`0x42A415`-`0x42A420`, a rep movs). `0x497FD4` later hands
  that same block to `0x4BA200` to set the hardware palette, so `cfg+0x143A7`
  is the screen palette, in file byte order (r,g,b,0).
- `0x498109`-`0x498148` (the game-screen init; the function that also sets the
  viewport origin `0x37E27`=0x80, `0x37E2B`=0x20 - the (128,32) of section 27)
  builds the path `palettes/guipal.PAL` (strings `0x5031BC`/`0x5031C8`/
  `0x5031D0`), loads it through the VFS (`0x4BBE50`), and calls
  **`0x4AC7D0(cfg+0x519, cfg+0x143A7, guipalData)`**.
- `0x4AC7D0` does two things. First it copies the 256 GUIPAL entries to
  `dst+0xB2` (= `cfg+0x5CB`). Then, for **each of the 256 GUIPAL entries**, it
  scans all 256 screen-palette entries for the minimum of |dr|+|dg|+|db|
  (initial best `0x98967F`, strict less-than, so the first index wins ties)
  and writes the winning index to `dst+0x8B2` - which is **`cfg+0xDCB`**. One
  byte per entry, 256 bytes: the table actually runs `cfg+0xDCB..0xECA`, and
  the UI only ever reads the first sixteen.

So `guicolours[n]` = "the PALETTE.PAL index nearest to GUIPAL entry n". And the
first sixteen GUIPAL entries are nothing exotic: they are the **standard
VGA/CGA 16-colour text palette** (black, blue, green, cyan, red, magenta,
brown, light grey, dark grey, then the four brights, yellow, white) - which is
why every `colorf=` in the `.GUI` files is a small number, and why index 4 is
the "invalid" colour (VGA red) and 10 the "valid" one (VGA bright green).
Entries 16+ of GUIPAL are a grey ramp and further ramps; only 0-15 matter here.

Replaying the arithmetic of `0x4AC7D0` exactly (same L1 metric, same tie rule)
against the shipped `PALETTE.PAL` and `GUIPAL.PAL` gives, for the whole
16-entry logical palette:

| n | VGA meaning | GUIPAL RGB | palette index | RGB on screen |
|---|---|---|---|---|
| 0 | black | (0,0,0) | **0** | (0,0,0) |
| 1 | blue | (0,0,170) | **4** | (0,0,128) |
| 2 | green | (0,170,0) | **2** | (0,128,0) |
| 3 | cyan | (0,170,170) | **6** | (0,128,128) |
| 4 | red | (170,0,0) | **213** | (171,23,0) |
| 5 | magenta | (170,0,170) | **5** | (128,0,128) |
| 6 | brown | (170,85,0) | **203** | (167,27,0) |
| 7 | light grey | (170,170,170) | **85** | (171,171,171) |
| 8 | dark grey | (85,85,85) | **90** | (91,91,91) |
| 9 | bright blue | (85,85,255) | **9** | (84,84,252) |
| 10 | bright green | (85,255,85) | **233** | (83,223,79) |
| 11 | bright cyan | (85,255,255) | **32** | (115,255,223) |
| 12 | bright red | (255,85,85) | **211** | (255,71,0) |
| 13 | bright magenta | (255,85,255) | **253** | (255,0,255) |
| 14 | yellow | (255,255,85) | **194** | (247,227,103) |
| 15 | white | (255,255,255) | **255** | (255,255,255) |

Cross-checks, all of which land right side up:

- Drag-select box: [15] outer, [0] inner - white with a black inner line,
  which is exactly what the band box in TA looks like.
- Health bar (`0x46B0BD`/`0x46B0DE`): [0x0A] fill, [0x04] remainder -
  green/red.
- Pathability overlay {4, 14, 10} - red / yellow / green, a traffic light.
- Queued build box of a **non-selected** constructor: [1]/[9] - navy and
  bright blue. The queues of other constructors really do show blue in TA.
- Radar rings (`cfg[0xDCB+0x0A]`) - the familiar bright green, palette 233;
  the open question in section 25 about the ring colour is settled by the
  same table.
- The screenshot of live queued boxes shows a bright-green outer line with a
  distinctly **teal-tinted darker inner line** - that teal is `guicolours[3]`
  = (0,128,128), dark cyan, and it is not producible from the pure-green ramp
  at all. The screenshot colours are blended by rescaling so they are
  corroboration, not proof; the proof is the arithmetic above.

Two corrections to the earlier write-ups while here. First, `0x4C13F0` is a
*getter* of the colour-table slot of the blitter context (`ctx+0x210`), not an
installer; `0x46ACF5` merely stashes `cfg+0xDCB` in a local so the frame code
can pull single colour bytes out of it, and the one call that does store to
`ctx+0x210` (`0x4C13D0`, called once at `0x491598`) passes the literal `0xFE`.
Second, the front end calls the same mapper at `0x426503` but with a **null**
screen-palette pointer, which would read addresses 0-2 - dead code or
Win9x-tolerated; the in-game call at `0x498148` is the one that fills the
table the placement code reads.

---

## 51. Which line is darker: the inner - but only where there are two colours

Read straight from the drawing code, geometry first:

- **Cursor placement box** (`0x469E8C`-`0x469F1E`): first `0x4BF8C0` rectangle
  at (x0,y0)-(x1,y1), then `inc edi/inc esi/dec edx/dec ecx` - the second
  rectangle is **inset by one on all four sides**. In build mode (`0x2CC3` ==
  0x0E) the second call pushes the **same** colour register ebx, so the cursor
  box is two pixels of one colour: `guicolours[10]` valid, `guicolours[4]`
  invalid. **There is no darker line on the cursor box.** Only the drag box
  branch (`0x469F0B`) swaps the inner ring to `guicolours[0]` - white outside,
  black inside, darker within.
- **Queued build box** (`0x438C00`): the four colour-A lines settle at
  x0+1/x1-1 (and the same in y), the four colour-B lines at x0/x1 - so at
  rest **A is the inner line and B is the outer line**. A is `guicolours[3]`
  (selected) / [1] (not selected), B is `guicolours[10]` / [9]. With the
  values from section 30: selected = dark cyan (0,128,128) inside, bright
  green (83,223,79) outside; unselected = navy (0,0,128) inside, bright blue
  (84,84,252) outside.

The user report that the darker line is on the inside is therefore
**confirmed for the queued box and the drag box, and does not apply to the
cursor box**, which is uniform. The visual continuity noted in section 28
still holds: the outer pixel of the queued box is the same palette entry (233)
as the whole cursor box, so the moment of ordering reads as the inner pixel
darkening to teal, not as a new box.

## What RWE should draw

All values are PALETTE.PAL RGB, two-pixel frames, stated inner then outer:

- **Cursor box, valid site**: inner **(83,223,79)**, outer **(83,223,79)** -
  both pixels palette 233. No two-tone.
- **Cursor box, invalid site**: inner **(171,23,0)**, outer **(171,23,0)** -
  both pixels palette 213. Not a pure red.
- **Queued box, constructor selected**: inner **(0,128,128)** (palette 6, dark
  cyan), outer **(83,223,79)** (palette 233, bright green).
- **Queued box, constructor not selected**: inner **(0,0,128)** (palette 4,
  navy), outer **(84,84,252)** (palette 9, bright blue).
- Bonus, the band/drag select box: inner **(0,0,0)** (palette 0), outer
  **(255,255,255)** (palette 255).

---

## 52. The unit model draw path

The whole chain, for every unit (ground pass, air pass and the third pass at
`0x46A762` all funnel into the same routine):

| Address | Role |
|---|---|
| `0x45AC20` | draw one unit: refresh the per-piece working vertex buffers, apply COB piece transforms (`0x45B030`), then draw |
| `0x458810` | per model instance: compute the animation-freeze flag, then loop the flattened piece list (stride `0x36`) |
| `0x459200` | variant taken when the unit has a rotation — transforms vertices, then the same per-piece draw |
| `0x4584D0` | **per piece: project vertices, loop primitives, dispatch to the rasterizers** |
| `0x4C7580` | textured quad rasterizer (dest, frame, points, uvArray) |
| `0x4C7310` | its span fill, dispatching on texture width to `0x4CD896`/`0x4CD8DA`/`0x4CD91E`/... |
| `0x4C0310` | flat-colour n-gon rasterizer (dest, points, count, colourIndex) |

Projection in `0x4584D0` (`0x458513`-`0x45855F`), fixed point 16.16 into the
unit's scratch bitmap:

```
screenX = (x >> 16) + 0x80
screenY = ((zbase - z) >> 16) - ((y >> 16) >> 1) + 0x20
```

i.e. the documented `screenY = z - y/2` painter projection, nothing new.

Per primitive (stride `0x20`, the raw 3DO primitive struct with offsets
relocated to pointers at load), the dispatch at `0x4585D7`:

| Condition | What happens |
|---|---|
| flags bit 0 set (`+0x1C & 1`) | flat colour: `0x4C0310` with the primitive's ColorIndex, any vertex count |
| else, vertex count != 4 | **skipped entirely** — a textured triangle or n-gon is never drawn |
| else (textured quad) | pick the texture frame (below), then `0x4C7580` |

And at `0x458568`: **if the piece declares a selection primitive
(header `+0x0C` != -1), primitive 0 is skipped** — the loop simply starts at
index 1. The index stored in the header is not consulted for the skip. On
every stock construction-unit model the selection plate *is* primitive 0, so
this is invisible; on the wreckage models (`1x1D.3do`, `2x2A.3do`, ..., where
`selprim` is 20, 44, 109...) the original genuinely drops one real textured
face and draws the actual selection plate as a flat colour-0 quad. RWE
skipping `prims[selectionPrimitiveIndex]` instead is the saner reading —
worth listing as a deliberate difference, not "correcting".

There is **no backface culling** anywhere in this path — no cross product,
no winding test. A quad facing away from the camera still rasterizes; its
vertex order is reversed on screen, so it appears with its texture mirrored.
Closed models never show this; single-sided decorative quads do.

Painter order only: pieces in tree order, primitives in file order, later
draws overwrite earlier ones. No depth buffer, consistent with section 5 of
TOTALA-EXE.md.

---

## 53. There is no lighting. None.

What was checked, so this negative is worth something:

- `0x4584D0` computes screen x,y per vertex and nothing else. No normal is
  ever formed; the only cross-product-flavoured code near the model system
  is the explosion-debris builder at `0x421700`, which is motion, not light.
- `0x4C7580` carries exactly x, u, v per span edge. The span table rows hold
  left/right x, u, v — no intensity channel exists in the data structures.
- The width-specialised inner loops (`0x4CD896` and siblings) are a raw
  copy: `dest[x] = tex[(v>>16)*W + (u>>16)]`. No table indirection, no
  transparency key, nothing.
- The flat filler bottoms out in `rep stos` with the colour byte
  (`0x4C069B`/`0x4C0798`).

So: no lambertian term, no sun direction, no ambient floor, no palette-row
shading, no gouraud. The light level of every unit texel is exactly 1.0.

**The shading you see in the game is baked into the data.** The stock
models bind different brightness variants of the same texture per face
orientation — ARMSOLAR's dishes use `metal3a`/`metal3b`/`metal3c`/`metal3d`
(the same rivet plate at four bakes) and `Arm01b`/`Arm01c`/`Arm01d` (the
logo at three), assigned by the artist according to which way the face
points. One of its textures is literally named `32XGouraud` — a painted
gradient, 10 team-colour frames, standing in for the vertex shading the
engine does not do.

RWE's shader term (`unitTexture.frag`, currently
`0.72 + 0.36 * (0.5 + 0.5 * dot(N, normalize(-1.3, 1.0, 0.3)))`, buildings
only, and historically `0.58 + 0.6*dot`) has **no counterpart in the
original**. Whatever constants it uses, it is shading on top of textures
that were already shaded by hand, which is why mirrored faces carrying the
same texture come out unequal in RWE and equal in TA.

---

## 54. The palette tables — real, loaded, and not for models

The engine does have palette shading machinery; it just never touches the
3DO path. Loaded at `0x429340` from `palettes\` via the VFS, allocated per
display object by named stubs (names in `.data`: `ALPHA TABLE`,
`SHADE TABLE`, `LIGHT TABLE`, `GRAY TABLE`, `BLUE TABLE`):

| File | Size | Slot | Shape | Content (measured) |
|---|---|---|---|---|
| `PALETTE.PAL` | 1,024 | — | 256 x RGBA | the palette itself |
| `PALETTE.ALP` | 65,536 | `+0xC0` (`0x4BA5C0`) | 256 x 256 | alpha blend: `out = mix(dst, src)` remap |
| `PALETTE.SHD` | 8,192 | `+0xC4` (`0x4BA610`) | **32 rows x 256** | brightness ramp, row 0 = black, row 16 ~ identity, row 31 ~ 1.55x |
| `PALETTE.LHT` | 8,192 | `+0xC8` (`0x4BA660`) | 32 rows x 256 | additive light ramp, row 0 = identity, row 31 ~ 1.5x |
| (computed) | 256 | `+0xCC`/`+0xD0` | 1 row | grey / blue remaps |

Consumers pass `table + (level << 8)` — one 256-entry remap row per light
level — into the *sprite* blitters (e.g. `0x4B847A`, `0x4B84AE`, feeding
`0x4CBF2C`/`0x4CC3D0`). Those are the GAF-sprite paths: terrain, shadows,
fog, UI. Partially traced only — the point that matters here is the model
rasterizers reference none of them.

The `zbuffer` FBI key is parsed (`0x42C5A6`, into `unitdef+0x241` bit 7)
and no consumer of that bit was found — same status as `sortbias`: dead in
this build. No `ThreeD`-style per-vertex mode exists in the renderer.

---

## 55. Texture lookup and frame selection

At 3DO load (`0x42A2C0` reads `objects3d\%s.3do`, `0x4CB590` relocates
offsets to pointers, `0x42A140` fixes up textures in place), each primitive
with a texture name has the name resolved against the GAFs from the
`textures\` directory (list at `ctx+0x148E3`). The primitive's runtime
fields are then:

| Field | Meaning after fixup |
|---|---|
| `+0x10` | single-frame texture: pointer to frame-0 pixels. Multi-frame: current animation frame number |
| `+0x18` | multi-frame: pointer to the GAF entry (frame table at entry `+0x28`, 8 bytes per frame) |
| `+0x1C` bit 0 | "draw as flat colour". Set in the *file* for every stock untextured face (3,287 of 3,287 checked); set by the loader, with ColorIndex forced to `0xD1` grey, when a texture name cannot be resolved |
| `+0x1C` bit 1 | multi-frame texture — frame selected at draw time |
| `+0x1C` bit 2 | the entry has exactly **10 frames**: a team-colour texture |

At draw (`0x4585EC`):

- team texture: frame = the owning player's colour index
  (`player+0x96`), via `0x4B7F30`;
- other multi-frame: the current animation frame via `0x4B7EE0`, except
  when the freeze flag passed down from `0x458810` is set, which pins
  frame 0. (The flag comes from unit state — for `0x20000000`-flagged units
  from `unit+0x10E` bit 0, otherwise from root-piece header word `+0x20`
  being zero. Reading it as "texture animation runs only while the unit is
  active" fits, but the state bits were not chased further — inference.)
- single-frame: the pointer stored at load, no lookup at all.

Note `+0x14`/`+0x18`/`+0x1C` in the *file* are tool leftovers (garbage) for
textured faces except that bit 0 of `+0x1C` is reliably meaningful across
the stock data. The exe tests **only bit 0**; RWE's parser treats the whole
dword as `isColored`, which happens to agree on stock data (garbage values
are nonzero) but differs in principle.

---

## 56. The quad mapping — decoded from `0x4C7580`

When called with a null uv array (the only way the model path calls it),
the rasterizer builds the default at `0x4C75EB`, straight from the frame
header (width word at `+0`, height at `+2`):

```
uv[0] = (0,     0)        ; vertex 0 <- texture top-left
uv[1] = (w-1,   0)        ; vertex 1 <- top-right
uv[2] = (w-1, h-1)        ; vertex 2 <- bottom-right
uv[3] = (0,   h-1)        ; vertex 3 <- bottom-left
```

in the primitive's vertex-index order, exactly one copy of the texture
stretched across the quad. Inclusive texel coordinates: the last column and
row of texels land exactly on the far edges. There is no tiling, no
winding-dependent flip, no rotation by normal — a mirrored-on-screen
(back-facing) quad simply comes out mirrored because the same corners apply
to the reversed shape.

Then the crucial part: the quad is **scan-converted as a quad**. The code
finds the topmost vertex, walks the two edge chains (indices decrementing on
one side, incrementing on the other, `0x4C775E` / `0x4C789F`), interpolates
x, u, v linearly *along each edge* per scanline into a span table, and fills
each span interpolating u, v linearly *across* it. That is a screen-space
bilinear-style warp over the whole quad: continuous everywhere, no diagonal,
and on a trapezoid the texture fans smoothly from the wide edge to the
narrow one.

---

## 57. Why ARMSOLAR looks wrong in RWE

RWE's corner assignment in `meshFrom3do` (`src/rwe/mesh_util.cpp`) is
vertex 0 = topLeft, 1 = topRight, 2 = bottomRight, 3 = bottomLeft — **the
same as the exe** (and the same as Spring's known-good `3DOParser.cpp`).
The atlas rectangle preserves GAF row order, so orientation is right too.

The divergence is the next line: RWE splits the quad into two triangles
(`t0 = v2,v1,v0`, `t1 = v3,v2,v0`) and lets OpenGL interpolate affinely per
triangle. On a parallelogram that is exact. On anything else the two
triangles disagree, and the mapping kinks along the v0-v2 diagonal.

ARMSOLAR is made of trapezoids — the four fixed panels of the pyramid
(`CorSol1a`, prims 11-14 of `base`: top edge ~12.5 wide over a ~31.7
bottom edge) and the big outer face of each dish. `CorSol1a` is a 32x64
grid of solar cells with strong vertical bars, so the kink is impossible to
miss: in TA the bars cross each panel in clean straight rows that fan
slightly; in RWE each panel shows the bars breaking direction along the
diagonal. A side-by-side software render of both algorithms on the closed
model (same pose, same camera) reproduces RWE's crease exactly on the
triangle side and TA's clean stripes on the scanline side —
`armsolar_ta_vs_tri.png` in this directory, left = TA quad interpolation,
right = the affine split.

For the record, the mapping on those panels: with `CorSol1a` being 32 wide
x 64 tall and vertex 0 at the panel's bottom-left, u (the 32-texel axis)
runs *up* the panel and v (the 64-texel axis) runs *across* it — the
texture is authored sideways and the bars therefore streak horizontally
across the face in the original.

---

## 58. Where RWE diverges, item by item

| # | RWE today | The exe | Verdict |
|---|---|---|---|
| 1 | textured quads split into two affine triangles | scanline quad interpolation, seamless | **the ARMSOLAR bug — fix** |
| 2 | shades buildings with `0.72 + 0.36*(0.5+0.5*dot(N,L))`, `L = norm(-1.3, 1.0, 0.3)` | no lighting whatsoever | fix (or record as deliberate) |
| 3 | draws every primitive including the selection plate | skips primitive 0 whenever a selection primitive is declared | fix — but skip `prims[selprim]`, see above |
| 4 | textured triangle / n-gon falls through to the flat-colour path with a garbage colour | never drawn at all | fix (stock data has zero of these; mods will) |
| 5 | `isColored` = whole `+0x1C` dword truthy | bit 0 only | align while touching the parser; agrees on all 3,287 stock faces |
| 6 | backface culling | none — backfaces drawn mirrored | keep RWE's culling (deliberate difference; closed stock models are unaffected) |
| 7 | missing texture drawn flat via atlas miss fallback | flat grey `0xD1` (209) | cosmetic, align if convenient |

---

## 59. Implementation spec for RWE

**Lighting** (`shaders/unitTexture.frag`, `unitBuild.frag`):

- The faithful constants are: ambient **1.0**, directional **0.0** — i.e.
  delete the term. `lightIntensity = 1.0` for every unit and building,
  shaded exactly like mobile units already are (the `shade == false` path).
  Keep the sea-level `waterTint` and cloak `alpha` — those model different
  original mechanisms (the ALPHA table blends), not lighting.
- If some directional cue is wanted for depth-reading on RWE's free camera,
  that is a deliberate difference and belongs in TOTALA-EXE.md section 51
  with its constants — but the flat look is the original one, and the art
  carries its own shading (`metal3a`-`d`, `Arm01b`-`d` are per-orientation
  bakes).

**Texture mapping** (`src/rwe/mesh_util.cpp`, `meshFrom3do`):

- Keep the corner assignment exactly as is (v0 TL, v1 TR, v2 BR, v3 BL) —
  it is verified correct against `0x4C75EB`.
- Replace the two-triangle emission for textured quads with a **bilinear
  patch**. TA interpolates u,v along the projected edges per scanline; for
  the near-orthographic projections both engines use, that equals the
  bilinear patch over the four corners (position and uv share the same
  weights, `P(s,t) = sum wi(s,t)*Pi`,
  `w = {(1-s)(1-t), s(1-t), st, (1-s)t}`), and affine projection commutes
  with those weights. Two options:
  1. *Mesh-side (recommended, no shader work):* tessellate each textured
     quad into an NxN grid of the patch — positions and uvs both from the
     bilinear weights — and triangulate the cells. For a parallelogram this
     is exact at any N; N=1 there. Choose N from corner disagreement, e.g.
     N=4 when `|(v1-v0) - (v2-v3)|` exceeds a couple of world units, else 1.
     Grid lines land exactly on TA's mapping; interior error is O(1/N^2)
     and invisible at N=4 on a 32-texel face.
  2. *Shader-side (exact):* pass the four corner uvs plus the vertex's
     patch coordinates, do inverse-bilinear per fragment. Exact, but a new
     vertex format and a divergence-prone shader for no visible gain over
     N=4.
- Skip `prims[o.selectionPrimitiveIndex]` in `meshFrom3do` (and in
  `polygonEdgesFrom3do`) when the index is present. Stock buildings put the
  plate at index 0 and the exe skips index 0 unconditionally; skipping the
  declared index also handles the wreck models the exe gets slightly wrong.
- Drop textured primitives whose vertex count is not 4 instead of letting
  them fall into the colour path; the original never draws them, and the
  colour they'd get from RWE today is uninitialised tool memory.
- While in the parser: read `isColored` as `(unknown3 & 1) != 0` so the
  flat-colour gate matches the bit the exe tests.

**Verification**: re-render ARMSOLAR (closed) and compare against
`armsolar_ta_vs_tri.png` left panel — the `CorSol1a` bars must cross each
trapezoid panel in unbroken rows with no diagonal crease, and the base
plate quad (primitive 0) must not appear.

---

## 60. Which keys open what

The in-game keyboard dispatch is at `0x495e90`: the key code is fetched from
a ring buffer (`0x4c1ab0`), `key - 9` indexes a byte table at `0x496694`
(0xF0 entries) which selects one of 40 cases in a jump table at `0x4965f4`.

The key codes are TA's own. The VK-to-code translator (around `0x4c1fb9`
to `0x4c21cb`, feeding the ring buffer via `0x4c1b20`) shows the encoding:

| TA code | Key |
|---|---|
| `0x09` | Tab |
| `0x1b` | Escape |
| `0xAA`-`0xC3` | Ctrl+A .. Ctrl+Z -- corrected by the keyboard decode (sections 72-73): the translator formats CTRL_%c and polls VK_CONTROL; the earlier Shift reading was wrong |
| `0xC5`-`0xCD` | Shift+1 .. Shift+9 (`'1'+0x94`) |
| `0xE2`-`0xED` | F1 .. F12 (Shift+Fn = `0xCE`-`0xD9`) |
| `0xF8` | Pause |

The cases that matter here:

- **Tab (`0x09`), case at `0x496133`**: query the game type
  (`0x435100(session)`; 1 = campaign, 2 = skirmish, 3 = multiplayer --
  inference from how the three values gate briefing/save/gray logic).
  If multiplayer, and the tab bar isn't locked out (`[game+0x37ebe]&4`
  clear), call `0x495010` -- **toggle the TABMENU bar**. If *not*
  multiplayer, fall straight through to the F2 case below -- in single
  player **Tab opens the GAME OPTIONS menu directly**; there is no tab bar.
- **F2 (`0xE3`), case at `0x496165`**: if Shift is not held and no menu is
  already open (`[game+0x37ebe]&1` clear), call `0x460cc0` -- open the GAME
  OPTIONS menu -- and set `[game+0x37ebe] |= 1` ("menu open"). No game-type
  check: F2 works in multiplayer too. (With Shift held the case instead
  records something to `+0x391b9` -- unrelated, not decoded further.)
- **Escape (`0x1b`), case at `0x495ed4`**: if `[game+0x37ebe]&1` (a menu is
  open) -- clear the bit and close the stored options panel (name kept in a
  buffer at `game+0x37ea0`, destroy via `0x4a9660`). Otherwise ESC is the
  order-reset/deselect key: first press resets the current order mode by
  pressing the `STOP` gadget of the main panel (string `"STOP"` at
  `0x502714`, gadget lookup `0x49fe60` + `0x4a6a40`), a further press
  deselects all units (`0x48bd00`) and closes floating panels
  (`0x491d70(1)`).
- **Pause (`0xF8`), case at `0x496099`**: toggle `[game+0x38a51] & 1` -- the
  pause flag -- and broadcast the new state to the other players (a message
  built with leading byte `0x19`, formatted through `0x44fdb0(3, ...)` and
  sent via `0x451df0`). No game-type check; the pause key is the same in
  single and multiplayer.
- **`+`/`=` and `-`/`_`** (`0x496570` / `0x496512`): game speed up/down by
  1, clamped to 1..20 (`0x38a4b` is the desired speed), refused for
  watchers; applied via `0x490df0(speed, 1)`.

**Does opening the menu pause?** Yes, in single player. `0x460cc0` (GAME
OPTIONS open) ends with: if game type != 3, `[game+0x38a51] |= 1`
(`0x460e00`). The menu's close path (`0x460a33`) clears the bit again --
only when in-game and not multiplayer. Opening the Save (`0x49306d`) and
Load (`0x493330`) dialogs sets the same bit, and the options screens keep
it set (`0x45d0a1`). In multiplayer nothing menu-related pauses; only the
Pause key does, and it is broadcast.

---

## 61. TABMENU.GUI -- the multiplayer drop-down bar

File: `D:\RWE-extract\totala1\guis\TABMENU.GUI`.

Panel `HEADER`: 510x33 at (130, -33) -- it lives above the top edge and
slides down over the map (the negative `ypos` idiom; ALLIES/SHARE/CONTROL
use the same trick with y = -372/-429/-402). `crdefault=CANCEL`,
`escdefault=CANCEL`, `defaultfocus=OPTIONS`. With the 128-wide unit panel
on the left, 130+510 spans exactly to the right edge of a 640-wide screen.

| Gadget | Pos (in panel) | Size | Label | Quickkey |
|---|---|---|---|---|
| `OPTIONS` | (8,6) | 120x21 | "Options Menu" | O |
| `ALLIES` | (132,6) | 120x20 | "Allies" | A |
| `CANCEL` | (191,6) | 114x20 | (blank, `active=0`) | Backspace |
| `SHARE` | (255,6) | 120x20 | "Share" | S |
| `CONTROL` | (379,6) | 120x20 | "Control" | (none) |

No GAF carries TABMENU art (no `TABMENU.GAF`, no entry in
`commongui.GAF`), so the bar renders with the engine's default panel/button
drawing -- text buttons on the standard background, exactly what RWE's
UiFactory already produces for a GUI with no bitmap.

**Toggle, `0x495010`**: plays UI sound `"BigButton"` (ALLSOUND.TDF ->
`butmain1`). State lives in `[game+0x2bee]` bits `0xe0`: if any are set the
bar is up -- clear them and destroy the panel; otherwise set bit `0x20`,
load `TABMENU.GUI` (via `0x4aa8f0`, flags `0x800`), install the click
callback `0x494740`, then set button availability:

- Count the *other* live, non-watching players (10 player slots, stride
  `0x14b`, base `game+0x1b63`; a player is skipped when dead or when
  `[player+0x9b] & 0x40` -- the watcher bit).
- If game type != 3, or the local player is a watcher: ALLIES, SHARE and
  CONTROL are all disabled (`0x4a0570(gui, name, 0)`).
- Else ALLIES and SHARE are enabled iff at least one other such player
  exists; CONTROL additionally requires `[game+0x2c74]&1` clear and
  `0x457a50() != 0` (not decoded further).

`OPTIONS` is never disabled. Finally `0x4a81e0(gui, 0x40)` -- the slide-in
animation -- runs.

**Click dispatch, `0x494740`** (each button plays sound `"BigButton"`):

| Gadget | Action |
|---|---|
| panel-close event (`gadget+0x60 == -1`, i.e. CANCEL/ESC) | clear `[0x2bee]&0xe0`, destroy the bar |
| `OPTIONS` | `[game+0x37ebe] \|= 1`; `0x460cc0` -- open GAME OPTIONS |
| `SHARE` | `0x4936f0` -- SHARE.GUI (Transfer Resources: metal/energy sliders with player list, "Chosen Units", "Map Info") |
| `CONTROL` | `0x4466b0` -- CONTROL.GUI (Reject Player list, "No Watching\|Watching OK") |
| `ALLIES` | `0x4478b0` -- ALLIES.GUI (per-player ally checkboxes, "Allied Victory Off\|On") |

The three child dialogs all slide down from above the screen (negative
`ypos` in their GUI files). Also of note: bare key `h` (case `0x4964ae`)
opens the Share dialog directly in multiplayer.

---

## 62. The GAME OPTIONS menu -- ARMOPT.GUI / COROPT.GUI

Opened by F2 anywhere, Tab in single player, or TABMENU OPTIONS in
multiplayer. `0x460cc0` loads the literal name `"ARMOPT.GUI"` through the
GUI loader `0x4aa8f0`, which substitutes the local player's side prefix
(the same mechanism as `"%sMAIN2.GUI"`); art comes from `anims/ARMOPT.GAF`
(one entry, `OPTBG`, 128x354).

Panel: 128x352 at **(0, 128)** -- it sits exactly over the left-hand unit
panel, with the (paused, still rendered) game visible everywhere else.
There is no snapshot and no dimming at this stage. `crdefault`,
`escdefault` and `defaultfocus` are all `OK`.

| Gadget | Pos | Size | Label | Quickkey |
|---|---|---|---|---|
| `LOADGAME` | (13,22) | 96x31 | "Load Game" | L |
| `SAVEGAME` | (13,64) | 96x31 | "Save Game" | S |
| `PREFS` | (13,105) | 96x31 | "Options" | O |
| `MISSION` | (13,147) | 96x31 | "Briefing" | B |
| `HELP` | (13,189) | 96x31 | "Help" | -- |
| `EXIT` | (13,231) | 96x31 | "Exit" | E |
| `OK` | (14,291) | 96x31 | "Resume" (assoc=78, closes) | R |

There is also a `TEXT` label gadget reading "GAME OPTIONS".

On open (`0x460cc0`): SAVEGAME and LOADGAME are **grayed out in
multiplayer** (`0x49fdf0`/`0x4a1200` with `gametype==3`); in skirmish they
stay enabled (skirmish games can be saved). If game type is 2 or 3 the
MISSION button's text is replaced with **"Settings"** (string `0x506da4`).
Then the slide-in `0x4a81e0(gui, 0x40)` runs and, if single-player, the
pause flag is set.

**Click dispatch, `0x4609b0`** (buttons play UI sound `"Options"` ->
`butoptn`):

| Gadget | Action |
|---|---|
| close event (OK/Resume, CR, ESC) | destroy panel, free any options-screen surfaces, clear pause (if in-game, not MP), clear `[0x37ebe]&1` |
| `LOADGAME` | `0x4931d0` -- Load dialog (LOADGAME.GUI; pauses) |
| `SAVEGAME` | `0x493060` -- Save dialog (SAVEGAME.GUI; pauses) |
| `PREFS` | `0x460160` -- the in-game options screens (section 06) |
| `HELP` | load HELP.GUI (flags `0x1881`), draw bitmap `"dhelp"`, callback `0x45fac0` |
| `MISSION` | campaign: load BRIEFING.GUI, hide its `MOREBAR`/`TextRegion` gadgets, draw `"igmbrief"`, callback `0x45f770`. Skirmish/MP ("Settings"): `0x45f1d0` -- the settings summary screen ("Max Units:", "Starting Energy:", "Line of Sight:", ... strings at file `0x1050cc`+) |
| `EXIT` | `0x4608b0` -- the exit menu (section 04) |

**There is no Restart and no Pause button on this menu** -- Restart lives
inside the Exit menu, and pause is implicit (single player) or a key
(multiplayer).

`OPTION.GUI` (a 426x410 centred "GAME OPTIONS" with "End Mission",
"Preferences", "Mission Objective") is referenced nowhere in the exe -- a
leftover earlier design. The shipped menu is the side-panel ARMOPT.

---

## 63. The exit flow -- EXITMENU.GUI, YESORNO.GUI, RESTART.GUI

`0x4608b0` loads `EXITMENU.GUI` (150x155 at (279,117), a small centred
menu), callback `0x460800`:

| Gadget | Label | Action |
|---|---|---|
| `MAINMENU` | "Exit to Menu" | confirm mode 0 |
| `EXITGAME` | "Exit Game" | confirm mode 2 |
| `RESTART` | (text set at runtime) | restart dialog |
| `CANCEL` | "Cancel" | close |

On open: in campaign/skirmish the RESTART button is enabled and given the
text **"Restart"** (`0x4c5740(0x506d5c)`); in multiplayer it stays blank
and dead. In multiplayer, when `[game+0x2bee]&0x10` is set (the
battle-already-decided flag -- inference), "Exit to Menu" is disabled.

Confirmations go through `YESORNO.GUI` (`0x460680`, callback `0x4605c0`):
its `TITLE` gadget gets one of

- mode 0 ("Exit to Menu"): `"Surrender this battle and return to main menu?"`
- mode 2 ("Exit Game"): `"Surrender this battle and exit to Windows?"`, or
  just `"Exit the Battle"` when `[0x2bee]&0x10` (nothing left to
  surrender).

Yes (CHOICE1): mode 0/1 -> tear down to the front-end menu
(`0x491b60`/`0x490b30(1)`); mode 2 -> set `[game+0x3923b]|=4` and quit to
Windows (`0x491c60`). The buttons are named `CHOICE1`/`CHOICE2` and the
handler installs them as the panel's cr/esc defaults.

RESTART (`0x4604a0`) loads `RESTART.GUI` (252x221 at (287,106)): bitmap
`"drestart"`, the mission name is written into `MISSIONNAME`(1), and the
dialog offers "Adjust Difficulty" (Easy|Medium|Hard), Cancel, Restart;
callback `0x460340`.

---

## 64. The Save/Load dialogs

Only touched for completeness: `0x493060` (save) and `0x4931d0` (load) set
the pause bit on open and use `SAVEGAME.GUI`/`LOADGAME.GUI`. Not decoded
further here.

---

## 65. The in-game options screens -- PREFS.GUI + the RT pages

This is the front end's STARTOPT composite, re-skinned for in-game use.
The shared plumbing:

**The frame chooser, `0x45cfc0`.** One function serves both worlds: if
`[game+0x2a44]&4` (the "we are in a game" flag) it loads **`PREFS.GUI`**
and sets `[game+0x37ebe]|=1`; otherwise it loads **`STARTOPT.GUI`**. So
`PREFS.GUI` *is* the in-game STARTOPT. It also grays the frame's MUSIC tab
when no CD device exists (`[game+0x10]->[0] == 0`), and at its end
(`0x45d09c`) re-asserts the pause flag when in-game and not multiplayer.

**PREFS.GUI** (frame): 128x354 sidebar at **(0, 126)** -- the same place
ARMOPT occupied. Background gadget `IGOPT` -> `commongui.GAF` entry `IGOPT`
(128x354, one frame). `crdefault=escdefault=defaultfocus=PREV`.

| Gadget | Pos | Label | Quickkey |
|---|---|---|---|
| `SOUND` | (13,24) | "SOUND" | S |
| `MUSIC` | (13,66) | "MUSIC" | M |
| `SPEEDS` | (13,108) | "INTERFACE" | I |
| `VISUALS` | (13,150) | "VISUALS" | V |
| `PREV` | (13,251) | "OK" | O |
| `CANCEL` | (13,293) | "Cancel" | C |

(The tab gadgets have `assoc=30` -- radio-style; the exe highlights the
active tab with `0x4a1110(gui, name, 1)`.)

The `Igoptsoux/Igoptmusx/Igoptvisx/Igoptintx.pcx` files in `bitmaps/` are
**not referenced by the exe** (no such strings exist in the binary) and the
shipped GAF has only the single IGOPT frame -- they are source art for
per-tab sidebar variants that never shipped. Same for `PREFS.GAF`'s
`PREFSBG` entry.

**Opening a page** (SOUND `0x45de30`, MUSIC `0x45d7c0`, VISUALS
`0x45e5e0(0)`, SPEEDS `0x45ed50`; dispatched from the frame callback
`0x45fc60` on tab clicks): each one

1. calls `0x45cfc0` (frame reload -- PREFS or STARTOPT),
2. destroys the previous page panel (`0x45ce80`),
3. if `[0x37ebe]&1` (in-game) loads the **RT** GUI -- `SOUNDSRT.GUI`,
   `MUSICRT.GUI`, `VISUALRT.GUI`, `SPEEDSRT.GUI` (flags `0x200`); else
   loads the front-end GUI (`SOUNDS`/`MUSIC`/`VISUALS`/`SPEEDS`) *and*
   draws that page's full-screen backdrop bitmap (`optsound4x`,
   `optmusic4x`, `optvisual4x`, `optinterface4x` via `0x4288d0`),
4. installs the page's click callback (sound `0x45da90`, music `0x45d280`,
   visuals `0x45e100`, speeds `0x45ead0`),
5. wires the sliders: gadget`+0x13c` = range, `+0x140` = position,
   `+0x144` = a live callback that fires as the knob moves.

So in-game the composite is: **PREFS sidebar (0,126) + RT page panel at
(128,128), 150x352** -- the page background is the matching 149x354 frame
from `commongui.GAF` (`SOUNDSRT`, `MUSICRT`, `VISUALSRT`, `SPEEDSRT`,
resolved by the background gadget's name as usual; note the visuals GUI's
background gadget is named `VISUALSRT`). Together they fill x = 0..278;
the rest of the screen shows the squish animation of section 07 (ending
black).

**RT pages vs front-end pages** -- the gadget sets are the same except:

- Every RT page adds `RESTORE` ("Restore Defaults") and `UNDO`
  ("Undo Changes") at y=269/304.
- `VISUALRT` drops the screen-resolution control (`VIDSLDR`/`VIDVAL`
  "640x480"/`VIDTEXT`) -- the exe only wires video-mode enumeration and
  SELVMODE.GUI in the front-end branch (`0x45e6a4` runs only when
  `[0x37ebe]&1` is clear). **You cannot change resolution in-game.**
- Minor label drift ("Unit Chat" vs "Unit Text Acks", "Text Delay (secs)"
  vs "Screen Text Delay", TEST "TEST" vs "Sound Test").

Page contents (RT versions):

- **SOUNDSRT**: `MODE` "Off|Mono|3D" (Sound Mode), `FXVOL` slider (range
  64, live callback `0x45bde0`), `SPEECH` "Off|Medium|Full" (Unit Sounds),
  `TEST` -- plays `sounds\explode.wav` (string at file `0x104fd8`).
- **VISUALRT**: `GAMMA` slider, `SHADING` Off|On, `ANTI` Off|On
  (antialias), `BSHADOWS` Off|On (building shadows).
- **SPEEDSRT**: `GAME` slider -- game speed, range 21, live callback
  `0x45c070`, value `[game+0x38a4b]`, shown as "%d Slow/Normal/Fast/...";
  `SCREEN` slider -- scroll speed, range 65, value `[game+0x1434d]`;
  `TXTSCROL` -- text delay "%d secs"; `MAXLINES`; `UNITCHAT`
  "Off|Medium|Full"; `LEFTCLICK` "Left Click|Right Click" (interface
  style). The GAME slider is wired in-game too (speed changes go through
  the same `0x490df0` path as the +/- keys).
- **MUSICRT**: section 09.

---

## 66. The screen-squish transition (in-game only)

`0x460160` (ARMOPT PREFS-button handler), before opening the frame:

1. allocates `"FLIPSURFACE"` at the full screen size and copies the
   current framebuffer into it -- a snapshot of the game as it looked,
2. allocates a cleared 480x300 `"BKUPSURFACE"`,
3. sets `0x512fe4 = 1`, which arms a per-frame routine `0x45ffb0`,
4. snapshots every option value (section 08),
5. slides the frame in (`0x4a81e0(gui, 0xc0)`).

`0x45ffb0` runs each frame while the options are open: a counter
(`0x512fec`) advances 0x15/frame to 0x115 and the FLIPSURFACE snapshot is
redrawn progressively squeezed (a growing crop `0x512ff0 += 6`/frame,
rectangles built around x=127 -- the sidebar edge -- and the screen bottom
`0x1df`), with the 351x21 `LIGHTBAR` sprite from `commongui.GAF` drawn
across it during the collapse. The net effect is the familiar CRT-style
"game screen collapses behind the panel" wipe; the end state is a black
field behind the sidebar + page. (The rectangle interpretation is partly
inference; the snapshot/LIGHTBAR/collapse mechanics are decoded fact.) The
surfaces are freed when the options close (`0x45ff50`).

In the front end the same handler draws the `options4x`/`opt*4x` bitmaps
instead; no game snapshot is involved.

---

## 67. Settings lifecycle -- live apply, Undo, Restore, OK, Cancel

On options open, `0x460160` copies the whole settings block
(`game+0x37ee6`, 0x52 bytes -> `0x512f18`) plus the current CD track
(`0x4ce5a0` -> `0x512fd9`) and all 100 per-track CD types (`0x4ce7e0(i)`
-> `0x512f75[100]`).

- **Every control applies immediately** -- sliders through their `+0x144`
  live callbacks (volume audibly changes while dragging), stage buttons in
  their click handlers (`0x4cdb40` music enable, `0x4ce7a0` track mode,
  etc.). Nothing waits for OK.
- **UNDO (per RT page)** restores the entry snapshot for that page's
  values and re-applies them to the running systems (music page:
  `0x45d477`; the frame CANCEL uses the same restore code).
- **RESTORE ("Restore Defaults")** applies hard-coded defaults (music page
  `0x45d53c`: speech volume 0x20, track mode 4 = Custom, music on).
- **OK (the `PREV` gadget)**: `0x430f00` -- writes every setting to the
  registry under key `"Total Annihilation"` (a long series of
  `0x4b6a50(key, valueName, value)` calls) -- then the panel closes via
  its default-close path. Plays sound `"Options"`.
- **CANCEL** (frame): plays `"Previous"`, restores *all* pages' values
  from the snapshot -- volumes, gamma (value and ramp), shading toggle (it
  re-toggles the renderer if the bit changed), track mode, track types,
  game speed (`0x38a4b/0x38a4d`), scroll speed, text delay, chat levels --
  then closes without touching the registry (`0x45fd73`-`0x45ff28`).

Closing the options in-game returns to the game directly (the GAME
OPTIONS sidebar is not restacked), and the close path clears the pause
flag.

---

## 68. MUSICRT -- the CD music panel

> **Ported in part, 2026-09-11** (`3da7c98a`). TRACKMODE works: Play All
> walks the album in order and wraps, Random takes any track, Repeat plays
> the current track again, and Custom, the default, is the situational music;
> the evaluator keeps counting in every mode but only Custom acts on it.
> CDNEXT and CDPREV step Play All and Repeat through the album. The mode is
> saved to `rwe.cfg` as `music-mode`. TRACKTYPE and TRACKNUM are not ported
> (fork issue #20).

Layout (panel 150x352 at (128,128), background GAF entry `MUSICRT`):
`NOTRAK` "Off|On" (CD music on/off), `MUSICVOL` slider, `TRACKMODE`
"Play All|Random|Repeat|Custom", CD transport `CDPREV CDSTOP CDPLAY
CDNEXT` (16x16 buttons at y=151), `TRACKNUM` (y=178), `TRACKTYPE`
"Building|Battle|Victory|Defeat|Unused" (y=202), RESTORE, UNDO.

State: `[game+0x37f14]&1` = music enabled; `[game+0x37f16]` = track mode
1-4; `0x512fe0` = the panel's current track number.

Decoded behaviour (populate/sync `0x45d130`, dispatch `0x45d280`, display
refresh `0x45c3f0`, per-frame track watcher `0x45d0c0`):

- **`TRACKNUM` displays the current CD track number** -- `sprintf("%d")`,
  or the string **"NO DISC"** when the track is 0. It is display-only (no
  click case in the dispatch) and grays out when music is off. A per-frame
  watcher (`0x45d0c0`) keeps it in sync while the CD advances on its own.
- **`TRACKTYPE` shows the current track's type** (stage =
  `getTrackType(curTrack)`, `0x4ce7e0`) and **clicking it cycles the stage
  and assigns that type to the current track** (`0x4ce7c0(curTrack,
  stage)`, at `0x45d753`) -- confirmed: it retypes the track as
  Building/Battle/Victory/Defeat/Unused. The types live in a 100-entry
  table (snapshotted for Undo). TRACKTYPE is only enabled when music is on
  **and** the mode is Custom (`0x45d234`); in any other mode it sits
  grayed, showing the type read-only.
- **`TRACKMODE` cycles Play All -> Random -> Repeat -> Custom**
  (`[0x37f16]` = 1..4, applied via `0x4ce7a0`). Choosing Repeat pins the
  current track (`0x4ce580` re-asserted on every refresh); choosing Custom
  un-grays TRACKTYPE and immediately re-applies the current track's type.
  Custom is the mode in which track types matter (the game picks tracks by
  their assigned type), and it is the default -- Restore Defaults sets 4.
- **Transport**: CDPLAY plays the panel's track (`0x4ceb60(track,1)`);
  CDSTOP stops (`0x4ced40`) and resets the display to track 1; CDNEXT and
  CDPREV step with wrap-around at the disc's track count (`0x4ce450`).
  All four, plus TRACKMODE and MUSICVOL, gray out when NOTRAK is Off.
- `NOTRAK` toggles the enable bit and calls `0x4cedc0(on)`.

The front-end `MUSIC.GUI` is identical minus RESTORE/UNDO (plus a "CD
Music" label); both are driven by the same handlers -- the only fork is
which GUI file loads.

---

## 69. GAMMA.GUI and other leftovers

`GAMMA.GUI` (333x128 centred, "Gamma Correction" with one slider,
"Set"/"Previous Menu") is referenced **nowhere** in the exe -- no
`"GAMMA.GUI"` string exists in the binary. The live gamma control is the
`GAMMA` slider gadget on VISUALS/VISUALRT. GAMMA.GUI hangs off nothing; a
leftover. Same status: `OPTION.GUI` (section 03), the `Igopt*.pcx`
bitmaps, `IGOPT0X/1X.PCX`, `igoptionsTEMP.PCX` (section 06).

---

## 70. Pause semantics

The pause flag is `[game+0x38a51] & 1`. It is *only* a sim-tick gate: the
tick scheduler (`0x49527a`-`0x4953ee`) computes how many sim ticks to run
this frame and, when the flag is set, forces that count to zero
(`0x4953d9`) and returns. Everything else -- rendering, mouse scrolling,
GUI animation, chat -- runs normally; while paused the world renderer
draws the `igpaused` banner (117x29, from `IGTITLES.GAF`, alongside
`igvictory` and `igdefeat`) over the view (`0x46a107`).

Set by: the Pause key (toggle, broadcast in MP as message type `0x19`);
opening GAME OPTIONS / Save / Load / the options screens in a
single-player or skirmish game (never when game type is 3). Cleared by the
matching close paths. Bits 1 and 2 of the same word are unrelated (net-lag
and reduced-speed indicators, `0x49535c`).

Multiplayer note (not deeply decoded): the Pause key path has no
game-type branch -- a player can toggle the shared pause and the state is
broadcast; the menus deliberately never pause an MP game.

---

## 71. Implementation spec for RWE

Everything below maps onto `D:\RWE\src\rwe\game\GameScene.cpp` (which
already has `paused` and `guiVisible`), UiFactory (which already builds
GUI files and resolves gadget art from GAFs by name), and the
MainMenuScene options code (which already composites pages over
STARTOPT.GUI).

**Key bindings** (GameScene key handler):

- `Tab`: multiplayer -> toggle the TABMENU bar; single player -> open
  GAME OPTIONS.
- `F2`: open GAME OPTIONS (any game type).
- `Escape`: if a menu/options panel is open, close it (Cancel semantics
  for the options screens); else cancel the current order mode, then
  deselect all.
- `Pause`: toggle `paused`; in MP broadcast it (when RWE gets there).
- Menu open in single player/skirmish => `paused = true`; restore on
  close. Never auto-pause in multiplayer.

**GAME OPTIONS panel** -- build `<side>OPT.GUI` via UiFactory at (0,128),
over the live scene (no dimming, no snapshot; the game keeps rendering
behind it, frozen by `paused`). Buttons:

| Button | RWE action |
|---|---|
| Load Game / Save Game | gray until save/load exists; then the dialogs (both pause) |
| Options ("PREFS") | open the in-game options composite below |
| Briefing / Settings | campaign: BRIEFING.GUI with `igmbrief`; skirmish: a settings summary (can stay grayed initially) |
| Help | HELP.GUI + `dhelp` bitmap (optional) |
| Exit | EXITMENU.GUI flow below |
| Resume (OK; cr/esc default) | close panel, unpause |

Gray Save/Load in multiplayer; retitle MISSION to "Settings" for game
types 2/3.

**Exit flow** -- EXITMENU.GUI at (279,117): "Exit to Menu" -> YESORNO.GUI
("Surrender this battle and return to main menu?") -> GameScene exits to
MainMenuScene; "Exit Game" -> YESORNO ("...and exit to Windows?") ->
quit; "Restart" (campaign/skirmish only, text set at runtime) ->
RESTART.GUI -> reload the mission; Cancel closes. YESORNO's buttons are
CHOICE1 (yes) and CHOICE2 (no); set the TITLE text per mode.

**TABMENU bar** (multiplayer only): TABMENU.GUI at (130, -33 -> 0) with a
slide-down; OPTIONS always enabled, opening GAME OPTIONS; Allies/Share/
Control grayed until those dialogs exist (the original grays them by
player-count/watcher rules anyway). Toggled by Tab; ESC/Backspace closes.

**In-game options composite**: reuse the MainMenuScene composite logic but
with `PREFS.GUI` as the frame (sidebar at (0,126), GAF entry `IGOPT` as
background, tabs SOUND/MUSIC/INTERFACE/VISUALS + OK/Cancel) and the
`*RT.GUI` page at (128,128) -- `SOUNDSRT`, `MUSICRT`, `VISUALRT`,
`SPEEDSRT`, backgrounds resolved from `commongui.GAF` by the background
gadget's name (the visuals entry is `VISUALSRT`). Do **not** offer
resolution switching in-game. The transition can be a simple cut or fade
to black behind the panels; the original's CRT-squish (snapshot +
LIGHTBAR collapse) is cosmetic and optional.

**Settings lifecycle**: apply every change immediately (volume while the
slider drags, gamma, shading, game speed, scroll speed, ...); snapshot all
values on open; per-page "Undo Changes" and frame-level Cancel restore the
snapshot; "Restore Defaults" applies per-page hard defaults; OK persists
(RWE's config file standing in for the registry) and closes. FX volume,
music volume, scroll speed and game speed map directly onto settings RWE
already has; shading/shadows/antialias/gamma as available.

**Music page**: if RWE has no CD-audio equivalent, either load
MUSICRT.GUI faithfully with "NO DISC" in TRACKNUM and the transport
grayed (the original's no-disc presentation), or gray the MUSIC tab as
the original does when no CD device exists -- both are authentic. With a
music backend: TRACKNUM = current track number, TRACKTYPE = current
track's type (click retypes it; enabled only in Custom mode), TRACKMODE
cycles Play All/Random/Repeat/Custom, transport = play/stop/next/prev
with wrap.

**Pause**: keep `paused` a sim-gate only -- camera scrolling, GUI and
rendering continue; draw the `igpaused` sprite from `IGTITLES.GAF` over
the world view while paused.

---

## 72. The key pipeline: translator, ring buffer, dispatch

Keys reach the game through a ring buffer at `game+0xf2` (fetched by
`0x4c1ab0`, pushed by `0x4c1b20`). Plain printable characters go in as their
ASCII codes; everything else goes through the translator at `0x4c1d50`, which
is keyed on the Windows VK code:

```
  4c1d50: sub    esp,0x8
  4c1d54: push   0x11                       ; VK_CONTROL
  4c1d56: call   DWORD PTR ds:0x4fc350      ; GetKeyState
  4c1d5c: and    al,0xfe
  4c1d5e: mov    ecx,DWORD PTR [esp+0x10]   ; the VK code
  4c1d62: neg    ax
  4c1d65: sbb    eax,eax
  4c1d67: lea    esi,[ecx-0x13]
  4c1d6a: neg    eax                        ; eax = 1 if Ctrl is down
  4c1d6c: cmp    esi,0x68
  4c1d6f: ja     0x4c20ea
  4c1d75: xor    edx,edx
  4c1d77: mov    dl,BYTE PTR [esi+0x4c2230] ; VK-0x13 indexes a byte table
  4c1d7d: jmp    DWORD PTR [edx*4+0x4c21d0] ; 24-way jump table
```

The letter and digit paths settle the modifier question. With the Ctrl flag
set, a letter gets `+0x69` (`'A'` becomes `0xAA`) and a digit gets `+0x94`
(`'1'` becomes `0xC5`); without it the letter is lowercased and pushed as
ASCII. An F-key handler is one `neg`/`sbb` idiom:

```
  4c1fca: neg    eax                ; eax = Ctrl flag
  4c1fcc: sbb    eax,eax            ; 0 or 0xFFFFFFFF
  4c1fce: and    al,0xec
  4c1fd0: add    eax,0xe2           ; F1 = 0xE2 plain, 0xCE with Ctrl
```

So `0xCE`-`0xD9` is **Ctrl+F1..Ctrl+F12**. Shift and Alt do not alter the
pushed code at all -- the handlers poll them live through `0x4c1b80`, whose
own jump table (byte table `0x4c1c6c`, targets `0x4c1c48`) maps the special
codes to `GetKeyState` calls. That table is the decoder ring for the
modifiers:

| TA code | Key | Evidence |
|---|---|---|
| `0x09` | Tab | dispatch table |
| `0x0D` | Enter | dispatch table |
| `0x1B` | Escape | dispatch table |
| `0x20`-`0x7E` | printable ASCII | translator / char path |
| `0xAA`-`0xC3` | **Ctrl**+A..Z (`'A'+0x69`) | translator `+0x69` on the Ctrl flag; handler formats `"CTRL_%c"` |
| `0xC5`-`0xCD` | **Ctrl**+1..9 (`'1'+0x94`) | translator `+0x94` on the Ctrl flag |
| `0xCE`-`0xD9` | **Ctrl**+F1..F12 | `neg/sbb` on the Ctrl flag |
| `0xE2`-`0xED` | F1..F12 | translator |
| `0xEE` / `0xEF` | Insert / Delete | translator, VK `0x2D`/`0x2E` |
| `0xF0` / `0xF1` | Home / End | translator, VK `0x24`/`0x23` |
| `0xF2` / `0xF3` | PgUp / PgDn | translator, VK `0x21`/`0x22` |
| `0xF4`-`0xF7` | Left, Up, Right, Down | translator; polled for map scroll |
| `0xF8` | Pause | translator, VK `0x13` |
| `0xF9` | Shift (state poll only) | `0x4c1bb5`: `GetKeyState(0x10)` |
| `0xFA` | Ctrl (state poll only) | `0x4c1bc9`: `GetKeyState(0x11)` |
| `0xFB` | Alt (state poll only) | `0x4c1ba1`: `GetKeyState(0x12)` |

`"CTRL_%c"` (at `0x5094f0`) is the smoking gun for the Ctrl reading; Alt for
`0xFB` is `GetKeyState(VK_MENU)`, and HELP.TDF's "ALT1 - ALT9|Select squad"
line confirms it end to end.

Arrow codes `0xF4`-`0xF7` and Space `0x20` are polled per frame, not
dispatched -- arrows scroll the map, Space is covered with F4 below. Insert,
Delete, Home, End, PgUp and PgDn are produced by the translator but hit the
dispatch's default case: **no in-game binding found** for them.

---

## 73. The dispatch at 0x495e90, all forty cases

The dispatch pops a key, samples Shift into `esi`, and indexes the byte table:

```
  495eab: push   0xf9                       ; Shift
  495eb0: call   0x4c1b80                   ; is it down?
  495eb5: mov    esi,eax
  495eb7: lea    eax,[ebp-0x9]              ; key - 9
  495eba: cmp    eax,0xef
  495ebf: ja     0x4965ce
  495ec5: xor    ecx,ecx
  495ec7: mov    cl,BYTE PTR [eax+0x496694] ; 0xF0-entry case selector
  495ecd: jmp    DWORD PTR [ecx*4+0x4965f4] ; 40-way jump table
```

Every non-default entry, with what the handler was found to do:

| Key(s) | Case target | Action |
|---|---|---|
| Esc | `0x495ed4` | menu open: close it. Else first press: press the main panel's `STOP` gadget (resets the order mode); further press: deselect all (`0x48bd00`) and close floating panels (`0x491d70(1)`) |
| Tab | `0x496133` | multiplayer: toggle the TABMENU bar (`0x495010`); otherwise fall into the F2 case -- GAME OPTIONS |
| Enter | `0x4964fd` | "SmallButton" UI sound, then `0x494050`: open the message bar -- `TALK.GUI` single player, `TALK2.GUI` multiplayer; refused for watchers |
| `!` `#` `*` backquote `~` | `0x496058` | toggle bit 0 of `[game+0x37f06]` and re-derive (`0x430f00`) -- **damage bars** (HELP.TDF line 27; registry value `damagebars`) |
| `+` `=` | `0x496570` | game speed +1, clamped to 20, refused for watchers and while the F11 mode is up; applied via `0x490df0(speed,1)` |
| `-` `_` | `0x496512` | game speed -1, clamped to 1, same guards |
| `,` | `0x496081` | `0x41bf10(1)` -- previous build-menu page for the selected builder, "nextbuildmenu" sound |
| `.` | `0x49608d` | `0x41bde0(1)` -- next build-menu page |
| `1`-`9` | `0x495fb9` | build page or squad select, swapped by Alt and the `SwitchAlt` registry value -- own section below |
| Ctrl+`1`-`9` | `0x495f9d` | `0x48d920(n)`: assign squad n -- every selected unit gets `[unit+0xac] = n`, every unselected member of squad n is removed. "CreateSquad" sound |
| `T` (uppercase) | `0x4964f1` | `0x41c2e0(1)` -- track selected unit |
| `t` | `0x4964e6` | `0x41c2e0(0)` -- track selected unit (the flag's effect inside `0x48c190` not decoded further) |
| `h` | `0x4964ae` | multiplayer only: open `SHARE.GUI` -- resource sharing (`0x4936f0`) |
| `n` | `0x4964d2` | `0x48d4d0` -- "Scroll to the next unit off screen" (HELP.TDF line 20) |
| `\` | `0x49637a` | registry-gated (developer section below); `0x417b50(0,-1)` -- plays something from a buffer at `0x511bd0`, not decoded further |
| F1 | `0x4960ea` | plain: `0x4942e0` -- unit info display for the selected (or pointed-at) unit. With Shift: latch the hovered unit into `0x391b3/0x391b7` -- a **debug text overlay** (`0x467e50` prints unit id, health, flag words as text) |
| F2 | `0x496165` | plain: open GAME OPTIONS (`0x460cc0`) if no menu is up. With Shift: latch hovered unit into `0x391b9/0x391bd` -- second debug overlay (`0x4685a0`) |
| F3 | `0x4961cd` | `0x464000` -- clear the "seen" bit on the message backlog and jump to the unit that last reported (HELP.TDF line 35) |
| F4 | `0x49639d` | toggle bit `0x80` of `[game+0x37f06]` -- the side-panel slide-away, own section below |
| F5-F8 | `0x49603c` | recall camera bookmark 1-4 (`0x41d3f0`): stop tracking, load saved position, snap |
| Ctrl+F5-F8 | `0x496020` | save camera bookmark 1-4 (`0x41d3b0`): current `[game+0x1431f]`/`[+0x14323]` into slot, mark valid |
| Ctrl+F10 | `0x4961d7` | registry-gated: movie-frame recording -- scans `%s\MOVIE*`, starts writing `MOVIE%03i` "FRAM" frames at the "Movie Output Rate" registry rate |
| F11 | `0x4962f2` | registry-gated: toggle bit 1 of `[game+0x3923b]` -- a debug panel with its own sub-keymap (developer section) |
| F12 | `0x4963c4` | `0x463c80` -- zero the chat-message indices at `[game+0x2a3e]`/`[+0x2a40]`: "Clear all chat messages" |
| Pause | `0x496099` | toggle `[game+0x38a51]&1` and broadcast (message lead byte `0x19`) |
| Ctrl+A | `0x4963ce` | `0x48bd50` -- select **all** own units, the whole array, alive and owned, everywhere on the map |
| Ctrl+C | `0x4963fe` | select category `CTRL_C` (the Commander), then `0x41c310`: set the tracked-unit id to it -- select and center |
| Ctrl+D | `0x49641d` | self-destruct: for each selected unit look up its `SELFDESTRUCT` weapon; units without one get the generic path `0x48cf30` |
| Ctrl+S | `0x4964dc` | `0x48c030` -- clear selection, then select every unit in the on-screen list `[game+0x1435f]` |
| Ctrl+Z | `0x496413` | `0x48be00` -- select all units of the same type(s) as the selection |
| Ctrl+B..Y (rest) | `0x4963d8` | the generic category select, next section |

The default case at `0x4965ce` does nothing except, when the F11 debug mode is
active, forward the key to the sub-handler `0x4956c0`.

---

## 74. Ctrl+letter is data-driven: `CTRL_%c` and the FBI Category field

Every Ctrl+letter that is not special-cased (A, C, D, S, Z are; C only half
-- it goes through the same lookup first) lands here:

```
  4963d8: lea    eax,[ebp-0x69]             ; key code back to 'A'..'Z'
  4963db: lea    ecx,[esp+0x10]
  4963df: push   eax
  4963e0: push   0x5094f0                   ; "CTRL_%c"
  4963e5: push   ecx
  4963e6: call   0x4e42b0                   ; sprintf
  4963eb: add    esp,0xc
  4963ee: lea    edx,[esp+0x10]
  4963f2: push   esi                        ; Shift held -> add to selection
  4963f3: push   edx
  4963f4: call   0x48bf30                   ; select all units in category
```

`0x48bf30` selects every own unit whose FBI `Category` line contains that
token. The bindings are therefore **entirely data-driven** -- there is no
hard-coded "Ctrl+B selects builders" anywhere in the exe. The shipped 3.1
unit set defines seven tokens:

| Token | Units carrying it | Meaning (readme.txt / HELP.TDF) |
|---|---|---|
| `CTRL_W` | 68 | all mobile units with weapons except the Commander |
| `CTRL_V` | 61 | all aircraft (VTOL) |
| `CTRL_F` | 48 | all factories (plants) |
| `CTRL_B` | 42 | all construction units |
| `CTRL_R` | 17 | radar, jammers, sonar and their jammers |
| `CTRL_P` | 13 | all aircraft with weapons |
| `CTRL_C` | 6 | the Commanders |

So out of the box: Ctrl+B, Ctrl+F, Ctrl+P, Ctrl+R, Ctrl+V, Ctrl+W select
those groups (Shift adds to the current selection instead of replacing);
Ctrl+C selects and centers; every other Ctrl+letter looks up a category that
no shipped unit has and selects nothing. A mod can invent `CTRL_X` and the
key springs to life.

---

## 75. The digits, Alt, and the `SwitchAlt` registry value

The digit case reads an option bit and polls Alt (`0xFB`):

```
  495fb9: mov    edx,DWORD PTR ds:0x511de8
  495fbf: xor    ecx,ecx
  495fc1: push   0xfb                       ; Alt
  495fc6: mov    cl,BYTE PTR [edx+0x37f07]  ; bit 8 of the display word
  495fcc: test   cl,0x1
  495fcf: je     0x495fef
  ...     ; bit set:   Alt -> build page,  plain -> select squad
  ...     ; bit clear: Alt -> select squad, plain -> build page
```

Default (bit clear): **`1`-`9` switch the selected builder's build-menu page**
("Select the menu for the current unit", HELP.TDF line 32) and **Alt+`1`-`9`
select squad 1-9**. The bit is the registry value `SwitchAlt` under
`Total Annihilation` (loaded at `0x43020a`), which swaps the two -- the
interface preference for group selection on bare digits. Squad select passes
the Shift state into `0x48d9a0`, so Alt+Shift+digit adds the squad to the
current selection. There is no squad 0: `'0'+0x94 = 0xC4` sits between Ctrl+Z
and Ctrl+1 in the table and is unbound, and plain `0` only reaches the
build-page path as an out-of-range page.

Squad membership lives on the unit (`[unit+0xac]`), is exclusive (assigning a
unit to squad 3 removes it from squad 1), and readme.txt section 5 adds the
factory rule: a factory assigned to a squad stamps that squad onto everything
it builds.

---

## 76. F4, Space, and the sliding panel

The display-options word `[game+0x37f06]` is persisted bit by bit in the
registry: bit 0 `damagebars`, bit 1 `Anti-Alias`, bit 2 `Shadows`, bit 3
`VehicleShadows`, bit 4 `FeatureShadows`, bit 5 `Shading`, bit 6
`DitheredFog`, bit 8 `SwitchAlt`. Bit 7 is the odd one out -- no registry
name, toggled only by F4, and read in exactly one place, the panel-slide
updater at `0x4948e0`:

```
  494935: test   BYTE PTR [eax+0x37f06],0x80
  49493c: jne    0x4949e5                   ; bit set: slide pos toward 0x7d
  494942: push   0x20                       ; bit clear: is Space down?
  494944: call   0x4c1b80
```

`0x51f2d8` is a slide position animated between 0 and `0x7d` (125 px, the
side panel width), with "Panel" and "Options" UI sounds at the endpoints.
With the F4 bit set the position runs to `0x7d`; with it clear it runs to 0,
except while Space is held (and the cursor is not on the panel's own gadget),
which drives it to `0x7d` again. Reading: **F4 latches the side panel out of
the way; holding Space slides it away temporarily** to see the map under it
(inference on which endpoint is "hidden" -- the arithmetic and sounds are as
stated, the direction is inferred from the Space semantics). A second updater
at `0x4689c0` runs the same Space logic for another sliding element.

RWE has no equivalent; its Space is the UI's "press the focused button" key
(`UiStagedButton.cpp:198`).

---

## 77. The second layer: screenshots, movies, and the gated debug keys

Ctrl+F9 (`0xD6`) is deliberately absent from the main dispatch -- it is
handled a layer up, in the per-frame routine at `0x4998e0` that also clears
the menu-pause bit:

```
  499915: cmp    edi,0xd6                   ; Ctrl+F9
  49991b: jne    0x49997d
  49991d: call   0x4c1ab0                   ; consume the key
  ...
  499945: push   0x5024fc                   ; "%s\screenshots"
  ...
  499961: push   0x50966c                   ; "SHOT"
  499967: call   0x4cb170                   ; write SHOTnnnn.pcx
```

`0x4cb170` scans the directory for the highest existing number and writes the
next `%s%s%s%04i.pcx` -- so **Ctrl+F9 = screenshot to
`screenshots\SHOTnnnn.pcx`** (HELP.TDF line 38), no gating.

> **Ported, 2026-09-11** (`1b16930e`). `SceneManager` catches Ctrl+F9 above
> every scene, consumes it, and writes `screenshots/SHOTnnnn.pcx` under the
> local data directory, one past the highest number already there. Two
> departures, both in §88: the file is 24-bit where the original's was 8-bit,
> and the picture is taken before the cursor is drawn. Where the original's
> numbering starts is not decoded; RWE's first is `SHOT0000`.

The registry value `Games` under `Total Annihilation` (read at `0x430e43`;
`== 1` sets bit 1 of `[game+0x37f2f]`) gates the developer keys:

- **Ctrl+F10**: movie recording -- each press starts numbering `MOVIE%03i`
  frame dumps, written from the frame pump at `0x4969f7` at the
  "Movie Output Rate" registry rate into "Image Output Directory".
- **F11**: toggles a debug mode (bit 1 of `[game+0x3923b]`) that opens a
  panel, freezes the speed keys, and routes every key through a second
  33-case jump table at `0x4956c0` (targets `0x495810`) -- pokes the hovered
  unit's state, five-press counters, and similar. Not enumerated further;
  it is not a player surface.
- **`\`**: `0x417b50(0,-1)`, plays something from a runtime buffer -- not
  decoded further.
- **Shift+F1 / Shift+F2**: latch the hovered unit for the two debug text
  overlays (`0x467e50`, `0x4685a0`) that print its internals.

---

## 78. The order keys are GUI data: `quickkey`

`a` attack, `m` move, `s` stop and friends are nowhere in the exe's dispatch.
They are `quickkey=` fields on the order-panel gadgets -- the ASCII code of
the key -- and the GUI layer presses the button when that character arrives.
The shipped bindings, from all 149 `.GUI` files:

**ARMGEN.GUI / CORGEN.GUI** (the shared ORDERS panel) and the per-unit
panels agree on one keymap:

| Key | Gadget | Order |
|---|---|---|
| `o` | ORDERS | switch to the Orders menu |
| `b` | BUILD | switch to the Build menu |
| `m` / `M` | MOVE | move |
| `s` / `S` | STOP | stop |
| `p` / `P` | PATROL | patrol |
| `a` / `A` | ATTACK | attack |
| `g` | DEFEND | guard |
| `f` | FIREORD | cycle fire orders (hold fire / return fire / fire at will) |
| `v` | MOVEORD | cycle move orders (hold pos / maneuver / roam) |
| `x` | ONOFF | activate / deactivate |
| `c` | CAPTURE | capture |
| `e` | RECLAIM | reclaim |
| `r` | REPAIR | repair |
| `l` | LOAD | load |
| `u` | UNLOAD | unload |
| `k` | CLOAK | cloak toggle |
| `d` | BLAST | **the D-gun** ("Use the Disintegrator Gun", HELP.TDF line 15) |

The per-unit panels store the movement four as uppercase (65/77/80/83) and
the rest lowercase; the engine treats them alike (HELP.TDF documents them as
plain letters). Shipped-data quirks: `ARMAAP1.GUI` alone gives DEFEND
`quickkey=113` (`q`) -- almost certainly a typo for `g`; the main panel's
SHARE button carries `S`, colliding with STOP. NEXT/PREV build-page arrows
carry `quickkey=0` -- paging is the exe's `,`/`.` binding, not a quickkey.

RWE's quickkey plumbing (`UiFactory::convertQuickKeyToSdlk`, lowercasing
65-132 into SDL keycodes; `UiStagedButton::keyDown` matching) faithfully
reproduces this layer -- **but a quickkey is only as alive as its activation
handler**, and RWE's `GameScene` message chain handles ATTACK, MOVE, DEFEND,
STOP, RECLAIM, REPAIR, PATROL, CAPTURE, LOAD, UNLOAD, FIREORD, ONOFF, CLOAK,
NEXT, PREV, BUILD, ORDERS -- and not `BLAST`, not `MOVEORD`, not `SHARE`. So
in RWE today **`d` (D-gun) and `v` (move orders) are dead keys** even though
the buttons render (OrderButtons.cpp knows both for visibility only).

---

## 79. What RWE binds today

From `src/rwe/game/GameScene.cpp` `onKeyDown`/`onKeyUp` (branch `revival`):

- Tab / F2 toggle the game menu; Esc closes it, else cancels the cursor mode,
  else deselects (no STOP-gadget press, but equivalent in effect).
- Arrows scroll; Shift/Ctrl tracked; `+`/`=`/`-` (and keypad) speed;
  Pause pauses -- all matching.
- F10 debug window, F1 help overlay, backquote (scancode) health bars,
  `t` track, Ctrl+C select-and-track commander.
- Ctrl+A select all **on screen**, Ctrl+S **stop**, Ctrl+D self-destruct,
  Ctrl+Z **attack-ground mode**, Ctrl+W **guard mode**, Ctrl+F **attack
  mode**, Ctrl+P **move mode**.
- Digits: Ctrl+digit bind group, Shift+digit add selection to group, digit
  recall, `0` = a tenth group, Ctrl+Shift+digit = add.
- `SceneManager` binds F11 to the ImGui debug window globally.

---

## 80. The gap table

Key -> what the original does -> RWE status.

| Key / combo | Original action | RWE |
|---|---|---|
| Esc | close menu / press STOP gadget / deselect all + close floating panels | **works** (cancels cursor mode rather than pressing STOP; same effect) |
| Tab | MP: TABMENU bar; SP: GAME OPTIONS | **different** -- always the game menu; no TABMENU bar |
| Enter | message bar (TALK.GUI / TALK2.GUI) | **missing** -- no message/chat bar at all |
| `+` `=` / `-` `_` | speed 1..20 | **works** |
| Pause | pause toggle, broadcast | **works** |
| Arrows | scroll map | **works** |
| backquote `~` `!` `#` `*` | toggle damage bars | **works** (backquote only; the alias keys absent -- harmless) |
| `,` / `.` | previous / next build-menu page | **missing** (PREV/NEXT handlers exist, keys not wired) |
| `1`-`9` | build-menu page 1-9 (default; `SwitchAlt` swaps with Alt) | **different** -- control-group recall |
| Alt+`1`-`9` | select squad (Shift adds to selection) | **missing** -- Alt unused |
| Ctrl+`1`-`9` | assign squad (exclusive membership; factories stamp their products) | **works** in spirit (RWE groups are sets; no exclusivity, no factory inheritance, extra group 0) |
| `t` / `T` | track selected unit | **works** |
| `n` | scroll to next unit off screen | **missing** |
| `h` | MP: SHARE.GUI resource sharing | **missing** |
| F1 | unit info display for selected unit | **different** -- RWE help overlay |
| F2 | GAME OPTIONS | **works** |
| F3 | jump to the unit that last reported | **missing** |
| F4 | latch the side panel away | **missing** |
| Space (held) | slide the side panel away temporarily | **different** -- activates focused UI button |
| F5-F8 | recall camera bookmark 1-4 | **missing** |
| Ctrl+F5-F8 | save camera bookmark 1-4 | **missing** |
| Ctrl+F9 | screenshot `screenshots\SHOTnnnn.pcx` | **missing** |
| F12 | clear all chat messages | **missing** (nothing to clear yet) |
| Ctrl+A | select **all** units | **different** -- selects on-screen only (the original's Ctrl+S) |
| Ctrl+S | select all units **on screen** | **different** -- stops selected units (the original's `s` quickkey) |
| Ctrl+D | self-destruct selection | **works** |
| Ctrl+C | select Commander and center | **works** |
| Ctrl+Z | select all of same type | **different** -- attack-ground cursor mode |
| Ctrl+W | select armed mobiles (`CTRL_W`) | **different** -- guard cursor mode |
| Ctrl+F | select factories (`CTRL_F`) | **different** -- attack cursor mode |
| Ctrl+P | select armed aircraft (`CTRL_P`) | **different** -- move cursor mode |
| Ctrl+B | select builders (`CTRL_B`) | **missing** |
| Ctrl+R | select radar/jammer/sonar (`CTRL_R`) | **missing** |
| Ctrl+V | select all aircraft (`CTRL_V`) | **missing** |
| Ctrl+other letters | category lookup, no-op with stock data | **missing** (moot until modded categories) |
| quickkeys `o b m s p a g c e r l u x k f` | panel buttons | **work** |
| quickkey `v` (MOVEORD) | cycle move orders | **dead key** -- no activation handler |
| quickkey `d` (BLAST) | the D-gun | **dead key** -- no activation handler |
| Shift (held) | queue orders; build-square + cloak-radius ghosts; x5 factory clicks | **works** for queueing and x5; hover ghosts out of scope here |
| Ins/Del/Home/End/PgUp/PgDn | nothing found in-game | n/a |
| Ctrl+F10, F11, `\`, Shift+F1/F2 | registry-gated developer keys | n/a (RWE's F10/F11 debug windows are its own equivalent) |

RWE-only extras, all benign: F10/F11 debug windows, group `0`,
Ctrl+Shift+digit, keypad speed keys.

---

## 81. Implementation list for RWE, in priority order

1. **Re-point the Ctrl+letter family at selection, where the original has
   it.** Ctrl+A = select all everywhere; Ctrl+S = select on screen; Ctrl+Z =
   same type; Ctrl+B/F/P/R/V/W = category token match against the FBI
   `Category` word list, generic `CTRL_%c` style, Shift adding to the
   selection. The current cursor-mode bindings on Ctrl+S/W/F/P/Z are
   inventions that shadow the original meanings; the orders they duplicate
   already have their true keys (`s`, `g`, `a`, `p` quickkeys). This is the
   loudest muscle-memory break in the list.
2. **Wire the two dead quickkeys**: `d` -> BLAST (D-gun -- an activation
   handler that enters a manual-fire attack mode for the dgun weapon) and
   `v` -> MOVEORD (cycle move orders). The buttons already render and gate
   correctly; only activation is missing.
3. **`,` and `.`** -> the existing PREV/NEXT build-page logic. Trivial.
4. **Digit semantics.** Default digits to build-menu page switching, move
   squad selection to Alt+digit, keep Ctrl+digit assignment, and offer the
   `SwitchAlt` preference to swap -- or consciously keep the modern scheme
   and record it in the "deliberately differs" section of TOTALA-EXE.md.
   If squads are kept TA-true: exclusive membership and the factory
   inheritance rule come with them.
5. **Camera bookmarks** Ctrl+F5..F8 / F5..F8 -- small, self-contained,
   pure scene state.
6. **`n`** next-unit-off-screen scroll -- small.
7. **F1 unit info** display (UNITINFO.GUI); move RWE's help overlay to an
   unused key if kept.
8. **The message bar**: Enter to open (TALK.GUI), F12 to clear, F3 to jump
   to the last reporting unit -- one work item, since F3/F12 only mean
   something once messages exist. MP chat and `h` sharing (SHARE.GUI) hang
   off the same UI.
9. **F4 / held-Space panel slide** -- cosmetic, needs the panel to be a
   sliding element first.
10. **Ctrl+F9 screenshot** to `screenshots\SHOTnnnn.pcx` -- nice-to-have.

Not worth porting: the `Games`-registry developer keys (Ctrl+F10 movie mode,
F11 debug panel, `\`, Shift+F1/F2 overlays) -- RWE's ImGui windows already
serve that purpose.

---

## 82. Field offsets

FBI key names are compared at `0x42C129`–`0x42C1C5`, which gives the unit
definition layout:

| FBI key | Offset | Notes |
|---|---|---|
| `maxvelocity` | `def+0x192` | an asymptote set by drag, never clamped |
| `brakerate` | `def+0x19A` | used by the nose re-aim only |
| `acceleration` | `def+0x19E` | |
| `bankscale` | `def+0x1A2` | default `0x10000` = 1.0 |
| `pitchscale` | `def+0x1A6` | not implemented in RWE |
| `turnrate` | `def+0x1BA` | |
| `sightdistance` | `def+0x202` | |
| `mincloakdistance` | `def+0x208` | |
| `buildangle` | `def+0x210` | `WORD`, default 0 — see §8 |
| `builddistance` | `def+0x212` | |
| `sortbias` | `def+0x21A` | parsed, never read — dead |
| `bmcode` | `def+0x22F` | `BYTE`; 1 = mobile, 0 = building |
| `defaultmissiontype` | `def+0x230` | `BYTE` mission id, 0 = none — see §9 |
| `standingmoveorder` | `def+0x241` bits 0–1 | default 2; 0 hold, 1 maneuver, 2 roam |
| `standingfireorder` | `def+0x241` bits 2–3 | default 2; 0 hold, 1 return, 2 at will |
| `mobilestandorders` | `def+0x245` bit 0 | default 0 — offer the move button |
| `firestandorders` | `def+0x245` bit 1 | default 0 — offer the fire button |
| `canattack` | `def+0x245` bit 4 | the rest of that dword is in §9 |

Weapon TDF key names are compared through `0x42E484`–`0x42EFC3`. The pipeline
stores a key's parsed value *after the next key has been pushed*, so pairing a
push address with the store that follows it lands one slot early — the trap that
put `flighttime` at `+0xFA` when it is at `+0xFC`. Float keys are stored with an
`fstp` immediately after their own read and are unambiguous.

| Weapon key | Offset | Notes |
|---|---|---|
| `weaponvelocity` | `wdef+0x68` | `×65536/30`, a cap |
| `startvelocity` | `wdef+0x6C` | `×65536/30` |
| `weaponacceleration` | `wdef+0x70` | `×65536/900` |
| `energypershot` / `metalpershot` | `wdef+0xC0` / `+0xC4` | float |
| `minbarrelangle` | `wdef+0xC8` | float radians, default −11.25°; **not** a word at `+0xFE`, and inert — see §11 |
| `shakemagnitude` / `shakeduration` | `wdef+0xCC` / `+0xD0` | dword; `shakeduration ×30` |
| `areaofeffect` | `wdef+0xD6` | word |
| `edgeeffectiveness` | `wdef+0xD8` | float, default 0.0 |
| `range` | `wdef+0xDC` | default `0x7FFF` |
| `coverage` | `wdef+0xE0` | |
| `reloadtime` | `wdef+0xE4` | word, `×30` |
| `weapontimer` | `wdef+0xE6` | word, `×30` |
| `turnrate` | `wdef+0xE8` | word, `×1/30`, a 16-bit angle per tick |
| `burst` / `burstrate` | `wdef+0xEA` / `+0xEC` | word; `burstrate ×30` |
| `sprayangle` | `wdef+0xEE` | word |
| `duration` / `randomdecay` | `wdef+0xF0` / `+0xF2` | word, `×30` |
| `smokedelay` | `wdef+0xFA` | word, `×30` |
| `flighttime` | `wdef+0xFC` | word, `×30` |
| `holdtime` | `wdef+0xFE` | word, `×30`; no known reader — see §11 |
| `accuracy` | `wdef+0x104` | word |
| `tolerance` / `pitchtolerance` | `wdef+0x106` / `+0x108` | word; a zero `pitchtolerance` falls back to `tolerance` — see §11 |
| `firestarter` | `wdef+0x10B` | byte |
| `rendertype` | `wdef+0x10C` | byte |
| `color` | `wdef+0x10D` | byte |
| `color2` | `wdef+0x10E` | byte |
| flags | `wdef+0x111` | dword, see below |

The flag bits at `wdef+0x111`, read off the parser's shifts: `lineofsight` 0,
`ballistic` 1, `shellweapon` 2, `beamweapon` 3, `vlaunch` 4, `meteor` 5,
`noradar` 6, `paralyzer` 7, `dropped` 8, `startsmoke` 9, `endsmoke` 10,
`soundtrigger` 11, `guidance` 12, `tracks` 13, `unitsonly` 14,
`groundbounce` 15, `waterweapon` 16, `toairweapon` 17, `smoketrail` 18,
`turret` 19, `selfprop` 20, `propeller` 21, `noexplode` 22, `burnblow` 23,
`twophase` 24, `cruise` 25, `commandfire` 26, `noautorange` 27, `stockpile` 28,
`targetable` 29, `interceptor` 30.

Bits 6, 7, 9 and 10 were gaps in an earlier reading of this table and are filled
in above; the `0x10B`..`0x10E` block is likewise itemised rather than given as a
range, because `rendertype` sitting at `0x10C` is what fixes the other three.
What bit 3 and bit 22 actually *do* is §92 — bit 3 is dead code for this
weapon, and bit 22 is the whole of the D-gun.

Projectile instance fields, stride `0x6B`:

| Field | Offset |
|---|---|
| weapon definition pointer | `proj+0x00` |
| position | `proj+0x04` |
| velocity | `proj+0x1C` |
| the point it was fired at | `proj+0x28` |
| heading / pitch | `proj+0x36` / `+0x38` |
| speed | `proj+0x3A` |
| distance to the target at launch | `proj+0x3E` |
| tick fired | `proj+0x42` |
| tick the motor stops | `proj+0x46` |
| target unit | `proj+0x4E` |
| target projectile (interceptors) | `proj+0x56` |
| burst shots left | `proj+0x60` |
| flags, phase counter in bits 4–5 | `proj+0x69` |

Unit instance fields:

| Field | Offset |
|---|---|
| roll | `unit+0x64` |
| heading | `unit+0x66` |
| pitch | `unit+0x68` |
| movement mode | `unit+0x110` bits 18–19 |
| firing mode | `unit+0x110` bits 20–21 |

Useful routines:

| Address | What |
|---|---|
| `0x43D0D0` | bank update |
| `0x43D290` | per-tick air movement |
| `0x468CF0` | world render |
| `0x4697ED` | Z bucketing, 16-unit quantisation |
| `0x469B38` | particle layer 7 draw |
| `0x469B3D` | airborne object pass |
| `0x4827B0` | `UpdateUnitLOS` |
| `0x49B3E0` | pick a projectile's aim point |
| `0x49B520` | step a projectile's heading and pitch toward it |
| `0x49B720` | per-tick projectile update, all five kinds |
| `0x49C920` | set a projectile's motor time |
| `0x49C980` | set a projectile's launch speed |
| `0x49C9C0` | spawn, line-of-sight and self-propelled |
| `0x49CC20` | spawn, vertical launch |
| `0x49CDE0` | spawn, ballistic |
| `0x49D42E` | the launch dispatch |
| `0x4B715A` | atan2 |
| `0x4B7173` | Rotate2D |
| `0x4E43A0` | float to integer, rounding mode set to **truncate** |

Palette ranges that turned up:

| Range | Used for |
|---|---|
| 97–103 | wake / water foam, pale to deep blue |
| 160–175 | construction display colours, sixteen greens palest to near black |
| 161–167 | nanolathe spray, the middle seven of those greens |

---

## 85. The D-gun: `ATTACKSPECIAL`, and what `commandfire` really costs you

> **A computer player's commander fires it by itself.** `commandfire` keeps a
> weapon out of the auto-acquire scan, which is what makes the D-gun a decision
> rather than a gun — but the scan at `0x4089A0` exempts the computer from that
> rule: "`commandfire` weapons do not either, **unless the player is of type
> 2**". Type 2 is the same `player+0x73` byte that exempts an AI from the
> `ShootMe` rule a few tests further down the same scan. So an AI commander
> engages with its D-gun on its own and a human's does not. RWE implemented the
> `ShootMe` half of that scan and not this one, until a play-test reported a
> commander standing and dying under fire with the weapon unused; it now does
> both (`UnitBehaviorService`, and the `[dgun]` cases). The return-fire path
> `0x408A90` skips `commandfire` too and **no exemption has been read there** —
> that is a different routine, and it has not been checked.

`candgun` is capability bit 14, and `0x43F7E8` turns a click in D-gun mode into
the `ATTACKSPECIAL` mission. That test is the whole eligibility rule: it does
not look at the target or the position, so a D-gun order is produced for an
enemy unit, a friendly unit, a feature or bare ground alike.

**The mission has almost no logic of its own.** Its handler, `0x403190`, is six
instructions: it asks `0x43F0E0` for the ordinary ATTACK mission for this unit
and target, rewrites its own mission id to that, and writes the literal `2`
into the mission's weapon-slot word at `0x4031BB`. The service loop re-reads
the id and runs the new handler on the same tick, so the order's name changes
from "Annihilating" to "Attacking" immediately.

What it becomes depends on the target:

| target | mission | handler |
|---|---|---|
| a live enemy unit, mover present | `Attack_Chase` | `0x4034A0` |
| bare ground, or a friendly unit, or the target already gone | `Suppress` | `0x4038A0` |
| no `canattack` | nothing; the order evaporates |

**The weapon is slot 2 by index — `Weapon3` in the FBI — not by scanning for
the `commandfire` bit.** Every call downstream indexes with that word:
`0x49ABB0` for the range test, `0x48A060`/`0x48A0A0` to aim, `0x49ADF0` for the
approach tolerance. The shipped data agrees: `ARMCOM` declares `Weapon1` and
`Weapon3` and no `Weapon2` at all, and `ARMCOM.COB` has `AimTertiary` and
`FireTertiary` with no Secondary anywhere.

**It fires once and the order is over, and that is a `commandfire` rule rather
than a D-gun one.** `0x49E4F2` sets bit 11 (`0x800`) of the unit's event word
for a `commandfire` shot where an ordinary weapon sets bit 10 (`0x400`), and
both `Attack_Chase` (`0x4034AF`) and `Suppress` (`0x4038A7`) open by testing
that bit and returning 5 — delete the mission. The nukes and the bombs end
their orders by exactly the same path.

Other things the decode settles:

- **The approach closes to the weapon's full range**, 240 for
  `ARM_DISINTEGRATOR` — `0x403623` asks `0x49ADF0`, which is just `wdef+0xDC`.
  It is halved and then reduced only after repeated failures (`Attack_Chase`
  sub-states 5–8).
- **The range test is flat.** `0x49AD7E` squares dx and dz in whole world units
  and never looks at y. It also refuses if either party is at or below sea
  level (`0x49ACC3`, `0x49ACEA`).
- **Short of `energypershot`, nothing happens at all.** The cost check's
  failure branch at `0x49E420` jumps to the per-weapon loop tail and raises no
  event, so the unit stands there holding its target and fires on the first
  tick the stores can pay. The order is not lost.
- **The target dying ends it.** `mission+0x16` is the `+0x4` field of a weak
  reference rooted at `mission+0x12` and linked into `target+0xA2`; `0x489740`
  nulls it, and the handler's guard at `0x4034C0` deletes the mission.
- **Nothing turns the unit.** Neither handler writes a heading; the
  disintegrator has `turret=1`, so the arm swings and the body does not.
- **The button is a latching cursor mode**, not an order: `0x419D84` matches
  `"BLAST"` and sets `world+0x2CC3` to 4, and clicking again un-latches it.
  Shift keeps the mode armed (`0x49908C`) and appends rather than replacing
  (`0x43ADC0`). `ARMGEN.GUI` gives `ARMBLAST` `quickkey=100`, so `d` toggles
  it — and `ARMDEFEND` is `g`, which is worth knowing because RWE had `d` on
  guard.
- **No formation offset.** `AttackSpecial`'s record flag word is `0x680`, bit 1
  clear, so every selected commander is sent at the same point rather than
  spread (`0x48D126`).
- **No leash.** `0x43ADC0` passes zero as the sixth argument of `0x43A0C0`, so
  `maneuverleashlength` never applies to an order the player gave.

## 86. Patrol: what makes a unit leave its route

A patrol route is **N separate missions**, one waypoint each, strung on the
unit's mission list. Reaching a waypoint returns 6, and the service loop's
return-6 arm at `0x43B975` unlinks that mission and appends it at the tail —
that is the whole cycling mechanism. `0x43A020` closes the loop by appending
one more waypoint at the unit's *current* position, once, the first time a
patrol mission runs its state 0.

A builder or repairer given a patrol order gets `RepairPatrol` instead
(`0x43F3E6`), and **neither RepairPatrol handler ever calls the acquisition
search** — a repair patrol does not engage.

**The engagement rule is not route-relative at all.** The handlers poll
`0x43B700`, which

1. requires the fire order to be **exactly Fire At Will** — `cmp ecx,0x200000 /
   jne`, so Return Fire shoots back but never leaves the route; then
2. hands over to `0x40B7B0` in mode 0, which gathers candidates in a **flat
   circle of `SightDistance` around the unit's own current position**
   (`0x40B845`, `0x40B848`, and `0x40AD80` where `dy` never enters); and
3. rejects every candidate that fails `0x49ABB0` at `0x40B914` — the weapon
   eligibility test, **which ends in weapon slot 0's own range check**.

So the radius a unit will break off within is **`min(SightDistance, weapon-0
range)`, measured from the unit**. A Thunder breaks off at 350 because its bomb
reaches 1280; a Peewee breaks off at 180 because its EMG is shorter than its
eyes. Nothing reads `maneuverleashlength`, the leg, or the waypoint.

**A sighting becomes a real mission.** `0x43B1F0` prepends an attack mission
and, on `StandingMoveOrder = Maneuver`, a move back to the exact spot where
contact was made behind it, so the list becomes `[Attack] -> [Move home] ->
[Patrol_k] -> ...`. The patrol mission is never touched, so the route resumes
at **the waypoint it was already heading for**. The attack ends when the target
dies, when health drops below three quarters with a repair pad within 3840, or
when the leash trips — `maneuverleashlength` from the contact point, anchored
at `0x43B330` and tested at `0x4034D2` and in every air attack prologue.

For aircraft the mission is the ordinary one: a patrolling bomber that acquires
a ground unit gets a full `AirStrike` (`0x411F50`), handed the target exactly
as an ordered attack would be. Two traps come with that. A patrolling bomber
**ignores enemy aircraft entirely** — `0x43F2AA` skips `AIRSTRIKE` for an air
target and the fall-through produces no mission at all — and **a bomb never
auto-acquires**, because `0x408A7F` excludes `dropped` weapons from the
per-tick scan. The break-off is the only way a bomber ever drops anything.

**Two of the shipped bombers patrol without ever attacking, correctly.**
`ARMTHUND` and `CORSHAD` carry `StandingFireOrder=0`, so `0x43B700` fails on
every poll. `ARMPNIX` and `CORHURC` carry 2 and do engage. This is the same
thirteen units that ship on Hold Fire — the artillery, the nuke silos, the
mines — and it is deliberate.

Cadence: `VTOL_Patrol` polls every 30 ticks, ground `Patrol` every 45–74
(`rand(30)+30` then 15), and never on the tick a waypoint is reached, because
return 6 comes first. The air leg's goal is aimed **320 world units past** the
waypoint with a 336 tolerance (`0x410EC4`–`0x410F21`), which is a 16-unit
arrival radius and a deliberate fly-through; and it carries no altitude
instruction, only the state-0 take-off climb to `cruisealt/2`.

## 87. Pathfinding: one scheduler, a bug-walk, and a unit that never waits

There is one pathfinder in the game -- a singleton at `gm+0x14207`, built by
`0x40E9E0` -- and it is driven from `0x40EB70` as the **first** statement of
the per-tick unit update, before any unit's own update. The pivot that finds
it is the tagged allocation name `"AISearch touched mapentries"` at
`0x50192C`.

### The budget

**1333 work units a tick** (`this+0x48`, set at `0x40EAD3`), divided evenly
among the live players, each banking its share as credit and the scheduler
spending until the pool is empty. The tariff, all read directly:

| Action | Cost |
|---|---|
| Look at one unit slot, whether or not it wants a path | 1 (`0x40ECA2`) |
| Start a search | +100 (`0x40ED6D`) |
| Phase 1, the cheap walk | 1 per cell stepped, **uncapped** |
| Phase 2, the A\* | 1 per node expanded, sliced at 100 (`0x40EEAF`) |

Both numbers are confirmed by a debug console command: `"Search"` at
`0x416780` writes its first argument into `+0x48` and `atof(arg2) * 65536`
into the heuristic weight at `+0x54`.

Because merely *scanning* a slot costs 1, the budget is always fully spent --
a quiet tick sweeps about 1333 unit slots looking for someone who wants a
route.

### It degrades quality, not throughput

Every 150 ticks (`0x40EBA5`) the scheduler counts how many full sweeps of a
player's unit slots it managed and sets the A\* heuristic weight accordingly:
fewer than one sweep gives **9.0**, fewer than two **4.5**, otherwise **1.5**
(`0x18000` in 16.16). Loaded, the search is essentially greedy best-first;
idle, it is weighted A\* at about 1.7 once the already-inadmissible heuristic
(`18*max + 7*min` against step costs of 16 and 22) is counted.

### Two phases

**Phase 1 (`0x40E160`) is not a search.** It is a greedy axis-walk with wall
following -- a bug algorithm -- with no iteration cap at all, run to
completion on the tick the search starts. Its direction tables are at
`0x4FD670`/`0x4FD678`. It produces three things: the best heuristic value it
reached (`this+0x40`), a mark on every cell it stepped on, and an immediate
notification to the mission of whether the destination is reachable
(`0x100`/`0x200`, delivered before any route exists). If it got no closer than
it started, the search is abandoned there and no A\* runs at all
(`0x40E979`).

**Phase 2 is a weighted A\*** over path cells one heightmap square across,
with a 5-direction successor fan after the first node (`0x40EEA8`, so it can
never turn more than 90 degrees in a step and never reverses), step costs
16/22, turn costs `0/40/60/80/100` from `0x4FCA10`, a further 75 for turning
within five cells of the last turn, and +30 for a cell whose passability class
is "tight". Passability is a 2-bit-per-cell table per movement class, kept
current by units stamping themselves in (`0x440830`) rather than by the search
asking. **Unexplored ground reads as passable and free** (`0x40D831`), which
is why the original walks confidently into the fog.

**Nothing is ever truncated.** What is relaxed is the goal: any cell at least
as close as phase 1 managed is marked as a destination (`0x40DCA8`), so a
search for an unreachable place costs one bug-walk rather than an exhausted
open list, and phase 2 always completes at something. The path is emitted as
corners only (`0x40E050`) and the navigator keeps at most **20** of them
(`0x44F0BC`), so a long route is deliberately re-requested part of the way
along.

### The part that matters most: a unit never waits

`Navigator::SetGoal`, `0x44F2A0`, installs a **two-point path** -- where the
unit is standing, then the goal -- and raises "I have a path" alongside "I
want a path" (`0x44F3F2`-`0x44F41B`). The unit is moving on the tick it was
ordered, and the real route overwrites the straight line whenever the
scheduler reaches it. There is a ladder before that: keep the old path if its
tail already satisfies the new goal, or if its endpoint is less than half as
far from the goal as the unit is.

RWE installed nothing and stood still until the search came back, which at
four hundred a side was seconds of an army not moving. That is now fixed, and
it is most of what looked like a budget problem: with the straight line in
place the budget is a quality knob rather than a correctness one.

### Requests

There is **no queue and no coalescing**. The scheduler walks a round robin
over player slots and unit slots, one slot per work unit, asking each unit's
navigator `WantsPath` (`0x44F260`) -- which is rate limited to **once every 60
ticks** per unit. A hundred units given one order produce a hundred separate
searches to a hundred separate points, picked up in slot order. Fairness comes
from the rotation, so a saturated original produces worse paths rather than a
growing backlog.

Repathing has exactly three triggers (`0x44F239`): a new or changed goal, the
path running down to fewer than two waypoints, and the mover's "blocked" bit.

**Aircraft never touch the pathfinder at all** -- `0x438943` tests `canfly`
and installs no goal.

### What RWE cannot copy

`0x43DC4E` gives units belonging to a player of type 3 a stub navigator
holding **three** waypoints, and `0x44F4A0` bit-serialises exactly three plus
the blocked flag. That reads as network replication of paths, which would mean
the original computes routes locally and sends them. RWE is lockstep and
cannot: its budget, its cursor and any adaptive weight would all have to be
hashed simulation state.

### What is ported, and how faithfully

The straight-line stand-in is ported exactly. The first pass and the relaxed
goal are ported in shape rather than instruction for instruction, and the
difference is worth stating plainly.

`BugWalk.cpp` is a greedy walk with wall following, as the original's is, but
its wall trace and its leave test are written from the structure of
`0x40E3D5`-`0x40E445` rather than transcribed: the agent that decoded it could
not make that section byte-exact, and a reimplementation of a routine one does
not have exactly will fail on geometries the original would have walked. Three
consequences follow, and only the first is a real cost:

  - **RWE relaxes its goal more often than the original would**, because the
    walk gives up more often. A relaxed goal means a shorter search horizon
    and another request later, so it trades path optimality for cost. Measured
    on `path_bench` at 400 units the trade is currently favourable in both
    directions: expansions fall from 149,199 to 32,292 over 600 ticks, and
    over 3000 ticks 116 units reach their destination where 96 did before.
  - The walk is used **only to relax, never to refuse**. The original abandons
    a search outright when its walk gets no closer than it started
    (`0x40E979`); doing that here would turn a walk that failed spuriously
    into a unit that will not move, which is a far worse failure than a slow
    search.
  - The walk takes a **step limit** where the original has none. Running out
    of it costs a less relaxed goal and nothing else.

**The slicing is ported too, as of 2026-09-09.** RWE used to stop a search
dead at a thousand expansions and hand back whatever partial route it had,
which the unit walked before asking again; the original has no per-search cap
at all and slices its A\* instead, carrying the same search on next tick
(`0x40EEAF`). RWE now does the same. `AStarPathFinder` is a state machine --
`beginSearch`, `stepSearch(n)`, `takeResult` -- and `PathFindingService` gives
the search whatever is left of the tick's expansion budget and keeps it
exactly where it stopped if that runs out. A search therefore ends at the
goal or at an empty open list and nowhere else, so a partial result now means
one thing only: the place genuinely cannot be reached. The remaining bound is
the map, which is the same bound the original has.

Three things follow, and they are the shape of the original's scheduler
rather than a coincidence:

  - **One search at a time**, because the scratch grid stamps its cells with
    the search they belong to and starting a second search would stamp the
    first one's cells stale. The original is under the same constraint from
    the other end: its pathfinder is a singleton at `gm+0x14207` holding one
    set of map entries, which is why it can slice at all.
  - **A suspended search is checked before it is resumed.** A unit that has
    died, or been given a different order while its search was part way
    through, has no use for the answer, so the search is thrown away and the
    request started again against what the unit wants now.
  - **A save carries it, in five integers.** A half-finished A\* cannot be
    serialised and does not have to be: a search is a pure function of the
    world, the goal, and the cell it started from. The first two are in the
    save already -- the world because it *is* the save, the goal because it
    hangs off the unit's navigation state -- and only the third is lost,
    because a unit whose search is suspended keeps walking its straight-line
    stand-in and is no longer standing where the search began. So the save
    writes down that footprint and how many expansions had been done, and the
    load rebuilds the search from the footprint and runs it forward that far.
    A\* is deterministic, so what comes out is the state that was suspended
    and not an approximation of it. The request needs no name: the search
    always belongs to the one at the head of the queue.

    The obvious alternative -- drop the search on both sides of a save, so
    both timelines start it again from nothing -- is wrong, and it is worth
    saying why, because it looks right. It works for a saved game, where
    nobody can watch both timelines. It breaks a **replay**: keyframes go
    through the same `saveSimulationToJson` during playback, and the
    recording they are being compared against dropped nothing, so every
    keyframe taken while a search happened to be in flight would put the
    playback a few ticks out from the game it is replaying. Measured on
    `path_bench` at 400 units, a search is in flight on about a third of all
    ticks, so that is not a rare case.

The scratch grid's two per-cell indices went from int16 to int32 to pay for
this -- a cell is twelve bytes now rather than eight -- because the static
asserts that made int16 safe were bounded by the thousand-expansion cap, and
a search that can close every cell of a 512x512 map needs more than an int16
addresses.

One number is deliberately not changed with it: the budget stays at 4000
expansions a tick, forty times the original's hundred. Raising or lowering it
is its own question with its own roadmap entry, and doing both at once would
make neither measurable.

Still not ported: the adaptive heuristic weight (it would have to be hashed
simulation state, since RWE is lockstep and the original is not), the
restricted successor fan, the turn and straight-run costs, unexplored ground
reading as free, the 60-tick per-unit cooldown, and the 20-waypoint clamp. RWE
keeps an admissible octile heuristic and an eight-way fan.

**And a warning about the benchmark, found while measuring this.**
`path_bench`'s obstacles were an immobile unit definition with no yard map,
which is an assert in a Debug build and an empty-optional dereference in a
Release one. Every `path_bench` figure recorded before 2026-09-09 was
therefore measured with obstacles that did not reliably obstruct, and none of
them is a valid baseline -- including the "116 units arrive where 96 did"
line the bug-walk entry in the roadmap carries. The definition has a yard map
now.

## 88. Where RWE deliberately differs

Recorded so these do not get "fixed" back later by someone comparing against the
original:

- **Detection is evaluated live, every tick.** The original answers "can this
  player see that unit" out of a snapshot: `0x40AA40` rebuilds each player's
  enemy list only every 30 ticks (`0x40AD20`), so a target can be up to a
  second stale — visible for a second after it has gone dark, and invisible for
  up to a second after it has been lit. RWE calls `canSeeUnit` at the moment it
  matters. The staleness is an artefact of the original's budget, not a
  behaviour worth reproducing, and copying it would mean carrying a
  30-tick-old candidate list in hashed simulation state.
- **No bit-8 fallback list, and no Targeting Facility.** The original's second
  candidate list (§17a) is appended *outside* the can-see gate, on `unit+0x110`
  bit 8 — "on my radar picture at all" — walked only when the first list is
  empty and only when the player owns a unit with `istargetingupgrade`. RWE has
  neither the list nor the flag. Two reasons it should stay that way: the units
  that carry the flag (`ARMTARG`/`CORTARG`) are Core Contingency and are not in
  the shipped data here, and the radar picture the list is built from is
  recomputed for **one** player per tick (§17, the visibility pass), so feeding it into a simulation
  decision would make the outcome depend on who is sitting at the keyboard.
  RWE's radar and sonar contacts therefore reach the minimap (`canDetectUnit`)
  and nothing else; every simulation decision goes through `canSeeUnit`.

- **Mobile units can be shaded, and shading can be switched off by category.**
  The original shades buildings and features and never a mobile unit. The
  shaded rasterizer `0x459C70` itself branches on nothing but the piece's COB
  `SHADE` bit, which is why this entry used to say the original shades
  everything; but it has one caller, the cache renderer `0x4586A0`, and that
  sends only an object with `unit+0x110` bit 29 set -- `bmcode == 0`, a
  building, or the Feature Unit -- to it, and only with SHADING on
  (`0x45873C`, `0x45874A`). Everything else, every tank, aircraft, ship and
  commander, is cached by the unshaded twin `0x459830` (`0x45878B`), whatever
  the option says. Found 2026-09-11. RWE shaded units by default until the
  same day; the default is now Buildings, which is the original's On, and
  the Units and Both stages that shade units are the divergence. RWE's
  VISUALS page carries a four-state Shading switch -- Off, Units,
  Buildings, Both -- and two rwe.cfg keys, `shading-strength-units` and
  `shading-strength-buildings`, blend the measured `PALETTE.SHD` ramp towards
  the unshaded colour. Both default to 40. The shape of the ramp is the
  original's in full -- the per-vertex level, the wrap, the unnormalised
  normals and sun, the row truncated per pixel -- and only its depth is pulled
  in: at 100 the table's snap to the nearest palette entry reads as banding on
  a modern screen and row 0 is a true black, which was tried on 2026-09-08 and
  rejected on sight. Units sat at 25 until 2026-09-11, when a play-test found
  it too faint to see on a commander at all; they went to 40 with the
  buildings. (An earlier version of this entry said both defaulted to 100.
  They never shipped that way; see the comment on `shadingStrengthUnits` in
  `GlobalConfig.h`.)
- **Units are anti-aliased by default, and so is a building's dont-cache
  piece.** The original box-filters a building's cached bitmap and nothing
  else (§101). RWE filters the same, plus two things it did not: everything
  solid that is not the ground, while `anti-alias-units` is on, which it is
  by default since 2026-09-11; and a finished building's dont-cache pieces,
  such as a metal extractor's top, always, because in play they read as part
  of the building. Neither gets the halo, which stays on the cached pieces
  the original's table actually saw.
- **A finished `ZBuffer=0` unit's flat-coloured faces are unshaded.** The
  original leaves such a unit's textured quads raw but still shades its
  flat-colour n-gons (TOTALA-EXE-SHADING.md S:23). RWE draws the whole model
  raw: its mesh does not keep the two kinds of face apart, and the difference
  is nine faces of CORFAV and one of CORTRUCK, the only two units that say
  `ZBuffer=0`.
- **Nanolathe spray lands on the roof**, not inside the model. The original
  samples the landing height inside the model too, which it can afford because
  its spray is composited in a late layer. RWE's is depth-tested so a
  construction aircraft can cover its own beam, so a landing point inside the
  geometry would be swallowed.
- **Exhaust occlusion is depth-tested**, not hand-layered — see §5.
- **A cloaked unit is half-blended with alpha**, not through the original's
  256×256 ALPHA TABLE, and a depth prepass stands in for the private bitmap the
  original composites into. Same 50%, different mechanism — see §17.
- **The fog raster is windowed on the camera.** At one texel per world unit a
  whole 640×640-cell map would be 400 MB, past most drivers' limits, so RWE holds
  a 2.6 MB window a few tiles larger than the view. This is what the original
  effectively does anyway, composing its overlay per visible screen tile.
- **Off-map fog cells read as the nearest on-map cell.** Reading them as "clear"
  leaves the frame's ragged edge with no neighbouring tile to cover it, and a
  strip of map shows through at the border.
- **No pitch**, see §1 (the `BrakeRate` nose re-aim is ported now).
- **A mobile unit's `buildangle` is ignored.** The original overwrites a mobile
  unit's spawn heading with the raw value (§8), which for everything but the ten
  capital ships is zero and so agrees with RWE's half-turn default anyway. RWE
  takes a factory-built unit's facing from the pad's `QueryBuildInfo` instead,
  which is what actually points it out of the yard, so a ship coming off a
  slipway keeps the pad's heading rather than being spun a quarter turn.
- **A gunship's nose follows its flight path**, so it crosses its ring side-on.
  The original does the same — but it does *not* hold its aim regardless of
  where the nose points, which an earlier reading of this claimed. Gunship
  rockets are `turret=0`, so §11 applies to them and the original holds fire
  until the nose is within the weapon's tolerance, which for `vtol_rocket` and
  friends is 8000, about 44°. RWE now does the same.
- **The waypoint trail marches off the global clock.** The original takes each
  segment's phase from the age of the order being drawn (§26), so two orders
  queued a few ticks apart march very slightly out of step. RWE's orders do not
  record when they were issued, and giving them an issue tick would mean
  carrying it through the game hash, the state dump and the network protocol to
  buy an effect nobody can see, so every segment marches together.
- **The placement sweep is timed scene-side.** Same reasoning: the original
  stamps the tick onto the order (`order+0x46`, §28) and RWE notes when a build
  order first appears instead. It is decoration, and decoration does not belong
  in the simulation.
- **The silo readout is an addition, not a restoration.** The original shows the
  stockpile only as the caption on the MAKENUKE button and shows the missile
  under construction nowhere at all -- no bar, no percentage, no format string
  (§29). RWE borrows the `RELOAD1` rectangle, which `SIDEDATA.TDF` defines and
  the original parses and then never reads.
- **The selection plate is skipped by its declared index.** The original
  skips primitive 0 whenever a selection primitive is declared (§51), which
  on the wreckage models drops a real face; RWE skips the index the header
  names instead.
- **Backface culling stays on.** The original has none -- a single-sided quad
  facing away rasterizes with its texture mirrored (§51). Culling matches on
  every closed model and only hides faces the artists never meant to show
  twice.
- **Skewed textured quads are tessellated, not scan-converted.** The original
  interpolates the texture along the quad's own edges per scanline; RWE
  approximates that warp with a 4x4 bilinear patch on non-parallelogram faces
  (§51), which agrees exactly on parallelograms and to within a texel
  elsewhere.
- **Only the heading half of the `turret=0` check is enforced.** The original
  compares the required elevation against the hull's own pitch at `unit+0x68`
  (§11). RWE's simulation has no hull pitch — `UnitState` carries a rotation and
  nothing else — so comparing against a notional zero would be a different rule
  wearing the same name rather than the original's. The heading half is the one
  that stops a unit shooting sideways and backwards, and it is the half that is
  implemented.
- **A feature's reclaim time counts its hit points, and scales with the
  builder.** The original is flat: `15 + (metal + energy)/2` ticks, one tick of
  work a tick, identical for every builder and with no term for the feature's
  own `damage` (S:97). RWE charges `metal + energy + hitPoints/4` at the
  builder's `workerTimePerTick`, which came out of a play-test round and is why
  a boulder takes longer than a bush and a shelled wreck clears quicker.
  Changing it back would move every reclaim time in the game, so it is recorded
  rather than corrected.
- **Circular sight is a computed disc, and it has no floor.** The original's
  Circular mode blits one of ten hand-drawn masks from `anims/vismasks.gaf`,
  radius 5 to 14 cells, so its effective sight is `clamp(SightDistance/32, 5,
  14)` (§2). RWE has no reader for those masks and stamps the disc
  `dx² + dy² ≤ r²` instead, which differs only around the rim. It keeps the
  ceiling of 14 cells and drops the floor of 5: a unit with no `SightDistance`
  sees only the ground it stands on, in every mode. Restoring the floor would
  give blind units 160 world units of sight the moment the option is switched.
- **The interface's palette remaps are alpha blends.** Three things the
  original does by running a screen rectangle through a 32x256 palette-index
  table -- greying a gadget, brightening the selected row of a list box, and
  darkening a marked one -- have no equivalent in a renderer with no palette.
  The list-box highlight reproduces the measured median of `PALETTE.LHT` row
  30 (1.84x) by compositing white at 45%, which has the same shape (largest
  lift on dark pixels, none on white) but not the same per-index behaviour.
  Greying is still not drawn at all (S:19, S:99).
- **The anti-missile coverage ring is gated on the weapon, not on
  `antiweapons`.** The FBI flag is a display flag and RWE has never parsed it;
  the ring is drawn for any unit carrying an `interceptor` weapon instead. The
  two sets are identical in the shipped data -- `ARMAMD` and `CORFMD` -- so
  only a mod could tell (S:25, S:99).
- **A builder walking to its site already says `Nanolathing`.** The original
  runs a move mission first and its footer says `Moving`; RWE's single
  `BuildOrder` covers the walk and the work, so the mission line changes one
  order earlier (S:99).
- **A transport only picks up your own units.** The original applies no
  ownership or alliance test anywhere on the load path: not in `CanLoadUnit`
  (§31), not at any of its five call sites, and not in the LOAD button's
  cursor arm, which will show `cursorpickup` over an enemy (§103). Whether the
  pickup then completes was never traced and never play-tested, so this is a
  house rule kept for want of evidence rather than in defiance of it: the test
  lives once, in `canLoad` in `src/rwe/game/DefaultAction.cpp`, so that
  removing it later is one edit rather than a hunt.
- **A resurrect shows the reclaim cursor.** The original has `cursorrevive`,
  id 10, for it (§103). The base game's `CURSORS.GAF` does not contain that
  sequence — only `rev31.gp3`'s does — and RWE has never loaded it, so the
  reclaim cursor stands in, as it already did for a resurrect order in flight.
- **Right-clicking an enemy still attacks it when nothing better applies.**
  The decode of the right-click chain (`0x43FA00`, §103) lists capture and
  reclaim on an enemy and no attack arm at all, which cannot be the whole
  story and is recorded there as unsettled. RWE keeps an attack arm below the
  two, so a tank right-clicking an enemy shoots at it.
- **A waypoint retires at sixteen world units, not the original's five.**
  `Navigator::Update` compares the squared distance against 25 (`0x44F205`);
  RWE keeps sixteen for an intermediate waypoint and eight for the last, which
  is also `hasReachedGoal`'s own tolerance. Five is fine for a unit on its own
  and costs about a third of the arrivals in a crowd, because a waypoint is a
  cell centre and a unit two cells across cannot always reach within five of
  one another unit is standing on. Measured, at a hundred units in
  `path_bench`: 27 arrivals and 884 searches at five, against 49 and 509 at
  sixteen. §102 has the rest of it.
- **Screenshots are 24-bit, and leave the cursor out.** The original writes
  8-bit PCX from its 8-bit screen; RWE draws in true colour, so it keeps the
  name, the folder, the numbering and the format family and widens the
  pixels. The picture is read back after the scene draws and before the
  cursor and the debug windows go on top; whether the original's included
  its cursor is not decoded (§77).

---

## 89. Keys the original parses and never uses

Worth writing down, because the absence of a reader is not visible from the
data files and each of these looks like something RWE is failing to do.

**`ComputerMetal` and `ComputerEnergy`, from a map's OTA.** Read at `0x436677`
and `0x436695` through the same integer-with-default helper (`0x4C46C0`) as
everything else in that routine, converted with `fild` and stored as floats at
`mapRecord+0xD84` and `mapRecord+0xD60`. Those two stores are the only
accesses to either offset anywhere in `.text`: nothing reads them back, so a
map that asks for a richer computer player gets nothing, and both sides take
the resources the game was started with. RWE parses them and ignores them,
which is the same behaviour by a different route.

**`AIProfile`.** The string sits at `0x50219A` with no reference to it
anywhere in the image, so the original does not parse the key at all.

The caveat on both: an access through a computed address rather than a
constant offset would not show up in that search. Nothing suggests one --
every other field in that record is reached by constant offset -- but it is
the one way this reading could be wrong.

## 90. `AirToAir`: a pursuit, and the twenty units that are not a hop

`0x412D40`, reached from `0x43F2CB` when the weapon is not `dropped` and the
target can fly. RWE had never ported it, so a fighter sent at another fighter
flew the bomber's attack run with the strafing overshoot switched off, which
was the closest thing available.

**First, a correction to this document and to `TOTALA-EXE-MISSIONS.md` §7.**
Both said this mission "hops around its target in twenty-unit steps". There
are no hops. `0x140000` is the length of two probe vectors -- one along the
bearing to the target, one along the aircraft's nose -- whose dot product
(`0x412FDA`, `0x41316D`) asks whether the bandit lies in the forward
half-plane. Nothing in the mission ever moves twenty units. The components are
floored to whole units before multiplying, which puts the boundary between 87
and 92 degrees depending on heading; that wobble is not worth reproducing.

### Two states

**State 0** takes off, announces "Attacking", takes all three weapons, climbs
to half `cruisealt` if it was on the ground, and sets a one-tick timer.

**State 1** is the whole dogfight and is terminal. Every wake it frees all
three weapons, takes weapon 0 back and points it at the bandit (`0x412FC3`) --
and never lets go, because there is no matching clear anywhere in the handler.
Then one of four branches:

| Branch | When | What it installs | Next |
|---|---|---|---|
| **Extend** | the goal was reached and the bandit is *in front* | a point `30 x MaxVelocity` along the nose, receding at `MaxVelocity` | a timer of `rand(30) + 60` |
| **Chase** | no goal event, bandit further than 160 | the bandit's position plus 45 ticks of his velocity, receding at his velocity plus half his top speed | 45 ticks |
| **Hold** | no goal event, bandit within 160 | nothing at all -- it keeps flying what it had | 45 ticks |
| **Break** | the goal was reached and the bandit is *behind*, or three decisions in a row with him off the nose | a separate `VTOL_EVADE` mission, pushed in front | restarts at state 0 |

The off-nose counter (`mission+0x36`) goes up by 45 a decision and breaks at
90, so it takes three consecutive decisions; any one with the bandit in front
resets it.

### The goal is an object that runs away from you

`0x44E740` builds a goal class no other mission uses: a position and a
velocity, whose resolve (`0x44EA60`) **advances the position by the velocity
every time it is asked**, in x and z only. Its satisfaction radius is a
hard-coded 48 with no setter, and the call that looks like an altitude
instruction, `0x44EC10`, is an empty `ret 0x4` -- the `cruisealt` handed to it
is thrown away.

Two consequences worth stating plainly. **The extend can never be reached**:
it starts thirty times the aircraft's top speed ahead and recedes at exactly
that speed, so the leg always ends on its timer. And **the `V/2` term in the
chase is unconditional**, so a hovering or landed bandit still gets a goal
sliding along his heading -- up to 270 units for a Vampire before the 45-tick
refresh.

### The break

`VTOL_EVADE`, `0x413BC0`, whose only producer in the whole binary is this
mission. State 0 draws a side with `rand(2)` and flies ninety degrees that way
for one weapon range; state 1 flies the same way again for **two** (`shl
edi,0x11`); state 2 returns 5 and the mission deletes itself, after which
`AirToAir` starts over at state 0. It is a reversal, not an S: the side is
drawn once and only read afterwards.

### What it does not have

No range test, no angle test and no firing tolerance -- it never reads weapon
0's range at all, and leaves all of that to the weapon update. No health check
and no repair-pad break-off, which all three sibling handlers do have: **a
damaged fighter never goes home.** No fuel. No re-acquisition. And
`mission+0x22` is never refreshed, so the position it carries is stale for
anything that reads it later.

It does have an off-map branch, before the leash is even considered
(`0x412E5E`): a fighter that has left the map is sent 800 units back towards
the middle with its state untouched.

### As ported

`AirMovementStateDogfight` follows the above, with the goal advanced once a
tick in the physics pass where the original advances it on resolve. Three
deliberate departures, all small: the facing test is a clean dot product
rather than the floored one; the break is a phase of the same state rather
than a separate mission pushed in front, since RWE has no mission list to push
onto; and a fighter's nose follows its flight path, where the original leaves
heading to the mover.

Porting it also turned up two things about dying off the map, which an
aircraft can do and a dogfight breaking two weapon ranges off a corner
actually does. `deleteDeadUnits` asserted that a dead unit's footprint lies on
the map, which holds for anything on the ground and not for an aircraft; it
now skips the grid work in that case and keeps the assertion where it is still
an invariant. And leaving `flyingUnitsSet` must not be conditional on any of
that -- the projectile pass walks that set and asks for each unit by id, so an
entry left behind by a dead aircraft is a lookup for a unit that is gone, which
is an assertion in Debug and a bad variant access in Release. `battle_test`
found the second one with sixty fighters a side after the suite had passed;
there is a regression test for it now.

## 91. Still unknown or unported

- TA's **Permanent** LOS mode has not been looked at.
- **Circular** LOS mode (the `vismasks.gaf` stamp) is understood but not
  implemented; RWE always uses True.
- RWE's explored grid is **per-player** rather than the original's one shared
  bitmask with a bit per LOS group. Equivalent until allied vision groups exist.
- `hitDensity` is parsed (100 for solid things, 5–10 for foliage, 0 for smudges)
  and does nothing. It was once guessed to be the pass-through chance for
  projectiles hitting features; that guess is **refuted** — the string does not
  occur in `TotalA.exe` at all, and the collision test at `0x49B2B3` is purely
  geometric. This entry survived here after the refutation was written up and
  is corrected rather than deleted, since the guess is an inviting one to make
  twice.
- **Weapons damage features regardless of their kind.** RWE once switched
  feature damage off for render types 0, 5 and 7 — the laser and lightning
  draws — on the community belief that beams cannot hurt wreckage. Nothing in
  the binary gates damage on `rendertype` (a drawing attribute at `wdef+0x10C`)
  or on `beamweapon` (bit 3 of `wdef+0x111`, one reader, which maintains the
  draw's tail point), and the shipped naval corpses' `damage=24000` only makes
  sense as bought immunity from gunfire that would otherwise clear them. The
  exemption is gone. Collision was always geometric and is unchanged, so a beam
  still stops on a feature — it can now also break it.
- The **strafing pass** (`AirToGround`, `0x412710`) is decoded but not ported:
  RWE's fighters still fly the generic attack run. See the missions document §5.
- **`maneuverleashlength`** is now parsed but not enforced. In the original it
  aborts an attack when the aircraft strays that far from where it was standing
  when the order was given — missions document §8.
- ~~The interface colour table~~ Resolved: `0x4AC7D0` writes it from a
  different base; see §50. The superseded entry read:
- **(superseded) The interface colour table at `cfg+0xDCB` has no writer anywhere in
  `.text`.** Every one of the nine accesses is a read; it is a logical-colour to
  palette remap installed for the blitter. So the exact palette indices for the
  minimap rings (§25), the placement box (§27) and the sweep (§28) are unknown,
  and RWE uses greens of its own choosing. Settling it needs a runtime memory
  dump, not more static reading -- break on `0x466FA8` and read `edx`.
- **The anti-missile coverage ring** (§25) is decoded -- one dashed ring per
  `interceptor` weapon on an `antiweapons` unit, radius `coverage - 512` -- but
  not drawn.
- The exact tick at which the original commits a **bomb release** inside its
  weapon code is still not pinned down; RWE uses its own bombsight.
- **`unit+0x110` bits 2–3.** They pick the loose 2000 default over the tight 150
  when a weapon names no tolerance (§11), and are tested at only three places —
  `0x40458A`, `0x4057D9` and `0x49D899` — none of which says what they mean. RWE
  keeps its own 256 default rather than guess.
- **`holdtime` has no known reader** — see §11. `aimrate` is not a key the
  original recognises at all, so there is nothing there to find.
- **`DefaultMissionType`** is decoded (§9) but not ported. RWE seeds a new
  unit's standing orders from the definition and lets its ordinary idle
  targeting stand in for `Standby`. The sight-range search `0x43B700` behind it
  *is* ported now, as `findEnemyToEngage` — see §86 — so what is left here is
  only the mission the key names.
- **The movement mode is acted on for a break-off, and nowhere else.** A unit
  that leaves its post now carries the leash `0x43B1F0` gives it —
  `maneuverleashlength` from the spot where it saw the target — and Maneuver
  walks back to that spot afterwards, so Maneuver and Roam are no longer the
  same thing. Hold Position is still honoured only in that such a unit is never
  given an attack order to begin with, and the mode is still not consulted
  anywhere else the original consults it.
- **Smoke does not drift downwind.** The vector and the ×8 scaling are decoded
  (§4, §7) but RWE has no map wind, so every puff goes straight up. The lift
  itself is right: RWE's half a unit a tick is the original's gravity × 4 on the
  112 that nearly every map uses, though it will not track a map that sets
  gravity to something else.
- The **explosion smoke** (`0x472630` from `0x420AE1`, three puffs seven ticks
  apart) and the **30-second burning wreck plume** (`0x48644B`) are decoded but
  not ported; RWE's explosions and wreckage do not smoke afterwards.
- **`BadSlope` and `BadWaterSlope` are not parsed.** §95 decodes them: they
  are the movement class's *free* slope threshold, with `MaxSlope` /
  `MaxWaterSlope` above them admitting the cell as "tight" at an extra 30 of
  path cost, and they default to half the corresponding max. RWE reaches the
  same default by hand (`computeRoughSlope`, `maxSlope / 2`) but ignores the
  keys, and only the two hover classes name them in the shipped data --
  `TANKHOVER3` and `TANKHOVER4` set `BadSlope=12` equal to their `MaxSlope`,
  so the original charges a hovercraft nothing for ground RWE calls rough.
  RWE's rough test also uses one threshold above and below the waterline
  where the original picks the dry or the wet pair per cell. Path cost only:
  neither changes what is passable. Left alone because a change to path cost
  moves every route, and that deserves its own pass with `path_bench` and the
  pathing tests watched.
- **The work sounds are played. Ported, 2026-09-10.** Sound slot 11,
  `working` (`reclaim1` in every construction unit's category), is played once
  when reclaim or capture work starts, and slot 16 `capture` when a capture
  finishes -- S:97. Both now have a simulation event to hang off: a new
  `UnitStartedReclaimingEvent` raised on the first tick of actual work by
  feature reclaim, unit reclaim and capture alike, and the `UnitCapturedEvent`
  that already existed, which now carries the captor so slot 16 is the
  captor's sound. Slot 16 stays silent on the shipped data, no category
  setting it.
- **`Resurrect` is decoded and not implemented** -- S:98. No shipped FBI sets
  `canresurrect`, so it would be a mod-only capability, and a new `UnitOrder`
  alternative cannot be added from inside `src/rwe/sim` alone.
- **`beamweapon`'s tail point** (`0x49BBA3`) is decoded and deliberately *not*
  ported — §92. It gives a round a second, trailing point that starts moving
  `duration + 1` ticks after the shot, and it is drawn only by `rendertype 0`.
  No shipped weapon both sets the bit and meets those conditions: the two
  disintegrators are `rendertype=3` and declare no `duration`, so for them the
  original computes the tail and throws it away. Anything that wanted it would
  have to be a mod.

## 92. The D-gun's projectile: `noexplode`, the full-range flight, and who gets hurt

§22 read the weapon's data and found nothing special in it. This is the other
half: what the *projectile* does once it has left the barrel, which is where
the three things a play-test noticed actually come from. None of them is a
D-gun rule. All of them fall out of two flags and one arithmetic.

### `beamweapon` is not what makes the beam

`beamweapon` is bit 3 of `wdef+0x111`, and it has **exactly one reader in the
whole binary**, at `0x49BBA3`, inside the `lineofsight` arm of the per-tick
projectile update. It is not a projectile kind: neither the kind dispatch nor
the launch dispatch ever looks at it, and `ARM_DISINTEGRATOR` is launched and
flown as an ordinary `lineofsight=1` round.

What the bit does is maintain a **second point** on the projectile — a tail that
starts moving `duration + 1` ticks after the shot, so the thing is a line
segment rather than a point. **But `ARM_DISINTEGRATOR` declares no `duration`**
— checked against both `rev31` and `totala1` — and the parser default is 0.0,
so its segment is one tick of travel long. Nothing draws it either: the
head/tail pair is read only in the `rendertype == 0` branch of the draw, and
the disintegrator is `rendertype=3`, which draws the model at `wdef+0x74`.

**For the D-gun the beam-tail machinery is computed and thrown away.** It is
recorded here so nobody chases it again; it is deliberately not implemented.

### It always flies its full range, in a straight line

`0x49C942` sets the round's life to `(range << 16) / weaponvelocity` ticks —
the speed being the 16.16 per-tick figure the parser stored — which is simply
*how many ticks it takes to fly the whole range*. For the disintegrator,
`weaponvelocity=200` is 6.667 units a tick and `range=240`, so the life is
**36 ticks and exactly 240 world units**. The direction is fixed at launch from
the aim point minus the muzzle and never changes: no gravity, no guidance. At
the end of its life it is retired with no explosion.

Two consequences worth stating plainly:

- **`weapontimer` is a fallback, not an override.** `ARM_DISINTEGRATOR` names
  `weapontimer=4`, and it is never used: the original only reaches that branch
  for a weapon with no `weaponvelocity` to divide the range by. RWE had the two
  the wrong way round and gave the D-gun a four-second life.
- **Nothing shortens the flight to suit the target.** The round is not aimed
  *at* a distance, it is aimed *along* a direction, and it flies 240 units
  whether the target was at 30 or at 239. That by itself is most of "the trail
  goes beyond the target".

### `noexplode` makes it detonate without being consumed

Bit 22 of `wdef+0x111`. Collision is tested every tick, and on a hit the
detonation routine runs — and its **first act, `0x499EDE`, is: if `noexplode`,
skip marking the projectile dead.** That is the whole of the flag. The round
keeps its position and its velocity, moves its 6.67 units on the next tick, and
tests the next cell along.

So a disintegrator ploughing into a hillside **detonates once per tick for the
rest of its 36-tick life**, each detonation spawning `explosionart=explode5`
and a full-strength blast.

**That is the trail. It is not one blast but up to thirty-six of them, strung
out along 240 world units** — which is why the weapon is good against a clump
rather than against one unit, and why it goes on hurting things past whatever
it was fired at.

Out-of-bounds is not one of these: that path never reaches the detonation
routine, so a round that leaves the map is simply gone.

### The blast, and who is in it

`areaofeffect=48 > 16`, so every one of those detonations takes the area branch:

- **The blast radius is `areaofeffect / 2`** — `0x49A150`'s `shr eax,1`. Twenty
  four world units for the D-gun, not forty-eight.
- Distance is measured from the blast point to the unit's **bounding box**,
  clamped per axis and then square-rooted, so a unit whose box contains the
  point is at distance zero.
- Falloff is `scale = edge + (1 - edge) * (1 - d/R)^2`, with `d == 0`
  short-circuiting to 1.0. `ARM_DISINTEGRATOR` names no `edgeeffectiveness`, so
  `edge` is zero and the scale is plain `(1 - d/R)^2`.
- Up to twenty distinct units are de-duplicated per blast.

**Friendly fire is unconditional.** There is no allegiance test anywhere on the
damage path. The blast hurts every other unit in radius at full strength and
merely books the result into two separate tallies by owner — which would be
pointless if own-damage did not happen.

**Exactly one unit is exempt: the firer** (`0x49A259`, comparing each candidate
against the projectile's stored firing unit). That exemption is not a nicety,
it is what makes the weapon usable at all — a round that is not consumed by
going off starts detonating from the muzzle outwards, and the first of those
blasts is standing on the commander.

**One asymmetry that matters.** The *collision* test skips same-owner units
(`0x49B1F4`), so the beam is neither stopped nor triggered by a friendly
standing in the way; it flies straight through. But any detonation it *does*
have blasts that friendly at full strength.

### The cooldown

There is no D-gun rule here either. It is `reloadtime` put through the general
scaling at `0x49E468` (§21):

```
reloadTicks = ((120 - 20*hp/maxdamage) * ((100 - 6*tier) * reloadTicks / 100)) / 100
```

with `tier = min(5, kills / 5)`. A healthy commander with no kills gets
`(120-20) * 36 / 100 = 36` ticks — **exactly the 1.2 seconds the TDF asked
for**, because the two terms cancel at full health. Shot up, it climbs to 1.44
seconds at the point of death; at twenty-five kills it falls to 0.84.

`weapontimer` and `energypershot` add nothing to the wait. **`energypershot` is
a gate, not a delay**: short of energy or metal the shot is not taken and *no
reload timer is set*, so the weapon retries on the very next tick. The cost is
taken after the shot.

### On "it damages the firer"

The decode says the opposite in as many words, and no path was found that
breaks the exemption. The report was chased empirically instead, at the range
where the question is sharpest — a commander disintegrating something in the
adjacent map cell, close enough that the very first detonation lands on its own
bounding box (`src/rwe/sim/dgun.test.cpp`, "what actually hurts a commander that
D-guns something at arm's length").

**The beam does not touch its firer**: thirty-six blasts of thirty thousand
went off within a few units of the commander and it took nothing. What *does*
hurt it is **the death explosion of the thing it just killed**. A dying unit's
`ExplodeAs` blast is spawned with no firing unit at all, so it has nobody to
exempt and hurts whatever is standing over the corpse. At one cell's separation
the corpse blast is centred on the commander's own bounding box, so it lands at
distance zero and pays out in full — the whole of `SMALL_UNITEX`'s thirty
points for a Peewee, and rather more for anything bigger.

So the play-test was right about the symptom and the binary is right about the
cause: the firer is exempt from its own beam, and the damage it takes is the
corpse's. `firestarter=70` was the other candidate and is not it — the fires the
trail starts spread between features and never damage units.

### Ported

All of the above except the beam tail, which is dead code in the original for
this weapon:

- `noexplode` reaches `WeaponDefinition` and the projectile update, which now
  applies the impact and lets the round fly on. A `ProjectileDetonatedEvent`
  carries the explosion art and screen shake for a detonation that did not end
  the round.
- A line-of-sight round's life is `range / velocity` and `weapontimer` is the
  fallback behind it, not in front of it.
- The firer is exempt from its own blast; nothing else is.
- `areaofeffect / 2`, the quadratic falloff and the same-owner collision skip
  were already right and are now pinned by tests against the shipped weapon.
- The reload scaling of §21 is implemented as `computeReloadTicks`.

`src/rwe/sim/dgun.test.cpp` builds `ARM_DISINTEGRATOR` by parsing its actual
`WEAPONS.TDF` block rather than transcribing the numbers.

## 93. Abandoned nanoframes decay

A nanoframe nobody is building falls apart. It loses health, the construction
display runs backwards, and eventually the frame is simply gone. RWE did none
of it: a frame placed and abandoned sat there for the rest of the game.

### The field and the mission

`unit+0x104` is a float holding the build fraction still **remaining**: 1.0 the
moment the frame is placed, 0.0 when the unit is finished. Every frame carries a
`GetBuilt` mission (handler `0x402DA0`) whose whole body is a small state
machine on `mission+0x5`:

- **state 0** — disarm the frame's weapons, schedule a timer +300 ticks, go to
  state 1.
- **state 1** — schedule +30 ticks, go to state 2. Nothing is tested here.
- **state 2**, on expiry —
  - event bit 15 set (somebody built on me this period): reschedule +30, decay
    nothing;
  - otherwise: reschedule +11, and decay by calling `0x41BCD0(unit, 11)`.

Event bit 15 is set on the **target** by the builder, at the very top of the
build routine (`0x41BA9D`) and *before* the economy is consulted. A builder the
economy has just refused still holds the frame's decay off, which is what keeps
a stalled base from eating its own construction sites.

So the timing has two shapes:

- **placed and never touched** — first decay at tick 330, eleven seconds after
  the frame appears, then one every 11 ticks;
- **built and then abandoned** — while a builder is on the job the timer runs on
  a 30-tick cycle, so the first thing lost comes 30 ticks after the last build
  tick, and every 11 thereafter.

### The rate: one energy-point of the build cost per tick

`0x41BCD0` computes `buildtime * n / buildCostEnergy` with `n = 11`, negates it,
and hands it to the ordinary build routine — there is no separate decay routine.
That routine does `newRemaining = remaining - amount / buildtime`, clamped to
[0,1]. **The build time cancels exactly**, and what is left is:

> A nanoframe decays at one energy-point of its own build cost per tick. From a
> built fraction `p` it takes `p * BuildCostEnergy` ticks to vanish.

Against the shipped rev31 data:

| unit | BuildCostEnergy | MaxDamage | full decay | hp lost |
|---|---|---|---|---|
| ARMSOLAR | 760 | 326 | 760 ticks (25.3 s) | 12.9 hp/s |
| ARMPW | 697 | 250 | 697 ticks (23.2 s) | 10.8 hp/s |
| ARMLAB | 1130 | 2690 | 1130 ticks (37.7 s) | 71.4 hp/s |
| ARMFUS | 36058 | 3100 | 36058 ticks (20 min) | 2.6 hp/s |

That spread is the felt behaviour and is not to be smoothed out: a cheap frame
melts in under half a minute, a half-built fusion plant effectively never goes
away.

Hit points need no separate rule. Because the decay goes through the build
routine, the routine's own `trunc(fraction * maxdamage)` recompute takes them
back off by the same difference that put them on, floored at zero.

### The end, and death cause 9

When the remaining fraction reaches 1.0 the frame kills itself with
`DamageUnit(self, self, 30000, cause 9)`. Cause 9 is one of the three the death
routine at `0x4864B0` short-circuits (see §22): **no wreck, no `Killed` script,
no death animation** — the unit is taken off the board. So cause 9 can now be
named: it is the nanoframe decaying out.

### And any unit killed while under construction leaves nothing

Found in the same routine and unrelated to decay: `0x4865D2` clears the corpse
flag outright whenever the remaining build fraction is non-zero, after the
`Killed` script has run and before the wreck would be spawned. Shoot a half-built
factory and there is nothing to reclaim, whatever killed it and whatever its
script asked for.

### The reverse animation is not a separate thing

The construction display takes the remaining fraction × 255 and dispatches on
five bands (§3). Decay's only effect is to raise that fraction, so the five
bands run backwards on their own: the texture sinks, the silhouette shrinks back
to the base, the bare line sweeps back up. COB `BUILD_PERCENT_LEFT` reads the
same field. Nothing was written for either — RWE's `computeBuildPhase` and
`getBuildPercentLeft` are both driven off `buildTimeCompleted`, and both were
verified to be, rather than reimplemented.

### As ported

`GameSimulation::updateNanoframeDecay`, run once a tick after the behaviour
pass, with the timer and the claim flag on `UnitState`
(`nanoframeDecayTime`, `nanoframeWorkedOn`, `nanoframeDecayRemainder`) and the
constants `NanoframeDecayGraceTicks` / `NanoframeDecayCheckTicks` /
`NanoframeDecayTicks` on `GameSimulation`. The timer is wound in `trySpawnUnit`,
where the original installs the mission; the claim is staked in the two places a
builder adds build progress, both before the economy call. `UnitState::
removeBuildProgress` is the mirror of `addBuildProgress` and, like the original,
is the same arithmetic run backwards rather than a second routine. Tests in
`src/rwe/sim/nanoframedecay.test.cpp`, against the shipped FBI numbers above.

Two deliberate departures, both small:

- **A carried remainder.** The original does this division in floats and needs
  no state; RWE's build progress is integer work units, so a truncated
  `buildtime * 11 / buildCostEnergy` would run a percent or two slow (and, for
  an extreme ratio, could stick at zero). `nanoframeDecayRemainder` carries what
  did not divide, which makes the total exactly `p * BuildCostEnergy` again. It
  is reset whenever a builder claims the frame, so it holds no history the
  original does not.
- **No metal refund.** The decode notes a refund on the decay path
  (`0x41BBA1`), scaled by difficulty for AI players, but that half was read from
  `fxch` ordering rather than a direct store and is the weaker claim. It is
  deliberately left out; a frame that decays away in RWE pays nothing back.

The disarm in state 0 needed nothing: RWE's behaviour pass already returns early
for a unit that `isBeingBuilt`, so a nanoframe never runs a weapon.

---

## 94. Air repair pads: who goes, when, and what the pad does about it

Half of this was already recorded — §34's `isairbase` notes and the
`findAirBaseToLandOn` predicate — and the half that was missing is the half
that made it useful: what the pad does once the aircraft arrives.

### Which missions send an aircraft home

Not all of them, and the exceptions are the point. The health test is written
out inside each mission handler rather than sitting in one place, so a mission
whose handler does not carry it never sends its aircraft anywhere:

```
410518  mov  eax,[esi+0x92]          ; the definition
41051e  movsx edx,WORD [esi+0x108]   ; current hit points
410525  mov  eax,[eax+0x1fa]         ; maxdamage
41052b  shr  eax,0x2                 ; >> 2 FIRST
41052e  lea  ecx,[eax+eax*2]         ; ... then x3
410531  cmp  edx,ecx
410533  jae  0x4105fa                ; healthy -> carry on with the mission
```

The truncation is on the quarter and not on the product, which is visible on
real data: ARMHAWK's 510 hit points give `(510 >> 2) * 3 = 381`, where
multiplying first would give 382. The jump out is `jae`, so an aircraft at
exactly three quarters is *not* damaged enough. A Hawk on 381 stays out.

The handler above starts at `0x4103E0`, and the mission record at `0x4FD3DA`
names it: **`VTOL_SeekAttack`**, status text "Seeking to attack". That matters,
because it is easy to mistake this routine for the idle one. It is not.
`VTOL_Standby` is `0x40F7D0` and carries no health test at all, so **a damaged
aircraft with nothing to do lands where it stands rather than crossing the map
to a pad.** Patrol, guard and the attack missions do carry it; `AirToAir` is
the one attack handler that does not, so a fighter already locked onto another
aircraft fights on however badly hurt it is.

When the test does fire, the pad query runs and a `VTOL_LANDING` mission
carrying the chosen pad is pushed in front of whatever the aircraft was doing:

```
41055f  call 0x40b530                ; the pad query, radius 0xf00 = 3840
410595  call 0x4b6c30                ; rand(n) over the candidates
4105b9  push 0x501b94                ; "VTOL_LANDING"
4105be  call 0x438760
```

The mission underneath is left in place, so the route or the guard resumes
once the aircraft is mended.

### What the pad does

A pad is a `Builder`, and mending what sits on it is the work it does when it
has nothing else. RWE runs that from the ordinary builder path, after ordered
work: a pad told to assist a factory does that instead, and only an idle pad
looks for a patient.

A patient is an aircraft of the pad's own owner, finished rather than still
under construction, damaged, **on the ground rather than in the air**, and
within reach — so an aircraft hovering over a pad is not worked on until it
has actually come down. Reach is the larger of the pad's `BuildDistance` and
half its own footprint, which is what keeps a four-by-four pad able to reach
the aircraft parked in its own middle. With two aircraft crowded on, the
nearer one is worked on.

### A pad is taken from the moment someone sets out for it

`VTOL_Landing` re-tests its pad before it commits, and what it tests is
availability rather than existence:

```
411deb  mov  ecx,[esi+0x36]          ; the pad the mission is carrying
411dee  push ecx
411def  push ebx
411df0  call 0x47e570                ; is it free?
411df5  test eax,eax
411df7  jne  0x411e12                ; yes -> land
411df9  push 0x501c30                ; "Landing aborted: no pads available"
```

`0x47E570` returns 0 — taken — when `pad+0x86` is set, or when a walk of the
pad list at `pad+0x8a` (following `+0x8e`, comparing `BYTE [node+0xf9]`) finds
a match. It never looks at the on/off bit.

That split is exactly what the strategy guide reports, and the two confirm each
other:

> When a plane is making its way back to the repair pad, that pad is considered
> to be occupied (even if the unit isn't there yet), so other damaged aircraft
> will not use that pad until the occupying aircraft has been repaired and has
> left.

> After I tested it out, I found that turning an aircraft repair pad 'Off'
> would stop aircraft from returning to it. However, those that were already
> making their way towards it will continue towards it.

**Switching a pad off turns away new arrivals but does not recall the aircraft
already coming.** New arrivals stop because the query walks the owner's air
base list, which holds only switched-on pads (`0x40ABCF`); a trip under way
carries on because the re-test above never asks about the switch. An earlier
reading here had that re-test as an on/off test and turned those aircraft
back, which is wrong on the binary and on the guide alike.

RWE needs no new state for the claim. The claim *is* the `LandOnAirBaseOrder`,
which is already serialized, and an aircraft parked on a pad is already in the
unit list. Physical occupancy is unconditional — an aircraft standing on the
pad holds it however healthy it is and whoever else wants it, which is the
"and has left" half. Two aircraft merely *en route* to the same pad can happen,
since the choice is a random draw, and there the lower `UnitId` keeps it: a
deterministic reading of "no pads available" that needs no tie-break state.

### The practical consequence

The guide is worth quoting on how this plays, because it is the reason the
behaviour is worth having exactly rather than approximately:

> This is both a blessing and a curse when you are making an assault using
> aircraft [...] If you have to kill that buildings *NOW* [...] it can be
> incredibly annoying having your planes continually break off. Even
> retargetting them only causes the planes to fly back, attack for a very short
> time and then go and get repaired.

That loop falls out of the health test living inside each mission handler
rather than being asked once when the order is given: retargeting starts a
fresh attack mission, which tests the health again on its next tick.

And the trick the guide ends on is the practical way a player drives all of
this:

> If you have some damaged aircraft [...] sitting on the ground and you want
> them to repair themselves, set up a 1 point Patrol route (where they are),
> and those planes that are heavily damaged will go off and get repaired.

which works because patrol is one of the missions that carries the test, and
standing still is not.

### A pad is taken from the moment someone sets out for it

VTOL_Landing re-tests its pad before it commits, and what it tests is
availability rather than existence:



 returns 0 -- taken -- when  is set, or when a walk of the
pad list at  (following , comparing ) finds
a match. It never looks at the on/off bit.

That split is exactly what the strategy guide reports, and the two confirm each
other:

> When a plane is making its way back to the repair pad, that pad is considered
> to be occupied (even if the unit isn't there yet), so other damaged aircraft
> will not use that pad until the occupying aircraft has been repaired and has
> left.

> After I tested it out, I found that turning an aircraft repair pad 'Off'
> would stop aircraft from returning to it. However, those that were already
> making their way towards it will continue towards it.

**Switching a pad off turns away new arrivals but does not recall the aircraft
already coming.** New arrivals stop because the query walks the owner's air
base list, which holds only switched-on pads (); a trip under way
carries on because the re-test above never asks about the switch. An earlier
reading here had the re-test as an on/off test and turned those aircraft back,
which is wrong on both the binary and the guide.

RWE needs no new state for the claim. The claim *is* the ,
which is already serialized, and an aircraft parked on a pad is already in the
unit list. Physical occupancy is unconditional -- an aircraft standing on the
pad holds it however healthy it is and whoever else wants it, which is the
"and has left" half. Two aircraft merely *en route* to the same pad can happen,
since the choice is a random draw, and there the lower  keeps it: a
deterministic reading of "no pads available" that needs no tie-break state.

### The practical consequence

The guide is worth quoting on what this feels like to play against, because it
is the reason the behaviour is worth having exactly rather than approximately:

> This is both a blessing and a curse when you are making an assault using
> aircraft [...] If you have to kill that buildings *NOW* [...] it can be
> incredibly annoying having your planes continually break off. Even
> retargetting them only causes the planes to fly back, attack for a very short
> time and then go and get repaired.

That loop falls out of the health test running inside each mission handler
rather than once at the point the order is given: retargeting starts a fresh
attack mission, which tests the health again on its next tick.

And the trick it ends on is the practical way a player drives all of this:

> If you have some damaged aircraft [...] sitting on the ground and you want
> them to repair themselves, set up a 1 point Patrol route (where they are),
> and those planes that are heavily damaged will go off and get repaired.

which works because patrol is one of the missions that carries the test, and
standing still is not.

### What RWE does not do

The pad choice is a `rand(n)` over the candidates in the original and a
determinism-safe modulo draw here, remembered in the navigation state rather
than re-rolled every tick — the original re-rolls because it swaps the mission
once and then never reconsiders, which comes to the same thing.

`VTOL_Standby`'s carrying-something hop and the sight-range search `0x43B700`
remain unported for the reasons §91 gives.

## 95. What blocks a unit: the map square, the passability class, and why a hovercraft cannot cross a wreck

A play-test expected a hovercraft skimming the surface to pass over a wreck
lying on the sea bed, now that wreckage sinks (`TOTALA-EXE-WRECKS.md`). The
original refuses it, and not narrowly. This section is the whole of what a
feature contributes to movement, read out of the binary, because the answer
turns out to be structural rather than a rule that could have gone either way.

### The map square

The map is an array of **13-byte squares** at `globals+0x14287`, `[globals+0x14233]`
wide and `[globals+0x14237]` high — the stride shows up everywhere as
`lea edx,[eax+eax*2] / lea ecx,[eax+edx*4]`. The fields the movement code reads:

| Offset | Meaning |
|---|---|
| `+0x00` / `+0x02` | the two unit slots occupying the square |
| `+0x05` | the cell's **maximum** ground height |
| `+0x06` | the cell's **minimum** ground height |
| `+0x08` | **the feature type index** occupying the square |
| `+0x0A` / `+0x0B` | for a continuation cell, the y and x step back to the origin square |
| `+0x0C` bit 1 | a terrain flag; bit 0 and the owner nibble are the feature-pool bookkeeping of `TOTALA-EXE-WRECKS.md` |

`+0x08` is `0xFFFF` for an empty square, `0xFFFE` for a **continuation** cell
whose origin is `+0x0A`/`+0x0B` squares away, and `0xFFFB`–`0xFFFD` for void
markers that block outright. Anything else is an index into the feature
**definition** table at `[globals+0x1426F]`, stride `0x100`, bounds-checked
against `[globals+0x14253]`.

**The square names a feature *type*, never a feature instance.** That one fact
settles the play-test: the feature's own record — its position, and so its `y` —
is not reachable from any collision test, because no collision test ever looks
it up.

### The blocking bit and its five readers

`blocking` is bit 6 of `featdef+0xFE` (§15's feature flag table). Grepping the
whole `.text` for reads of that byte with `shr 6 / and 1` finds exactly five,
and they are the complete set:

| Address | In | Role |
|---|---|---|
| `0x47E0F7` | `0x47DFC0` | the per-movement-class footprint test |
| `0x47DEDE` | `0x47DE60` | the same for a single square |
| `0x47DCE8` | `0x47DB70` | the footprint test for a unit *definition*, excluding one unit id (nine callers; a building, `def+0x22F`, is handed to §27's yardmap walk instead) |
| `0x47E4AB` | `0x47E2D0` | "may *this unit* stand at this world position" |
| `0x47D5E6` | `0x47D4B6` | the building placement yardmap walk (§27) |

All five run the same seven instructions, and all five reject the square before
a single field of the movement class or the unit definition has been consulted.
`0x47DFC0`'s copy, in full:

```
47e081  cx = WORD[square+8]
47e085  cmp cx,0xffff / jne 47e090
47e08c  xor ecx,ecx / jmp 47e104          ; empty -> not blocked
47e090  cmp cx,0xfffb / jae 47e0ae
47e097  cmp ecx,[gm+0x14253] / jl 47e0ee  ; a real type index
47e0a7  mov ecx,1 / jmp 47e104            ; out of range -> blocked
47e0ae  cmp cx,0xfffe / je 47e0bc
47e0b5  mov ecx,1 / jmp 47e104            ; 0xFFFB..0xFFFD -> blocked
47e0bc  ... step back by square+0xA/+0xB to the origin square ...
47e0ee  edx = [gm+0x1426f]
47e0f4  shl ecx,0x8
47e0f7  cl = BYTE[edx+ecx+0xfe]
47e0fe  shr ecx,0x6 / and ecx,0x1         ; bit 6: blocking
47e104  test ecx,ecx / jne -> refuse the cell
```

**VERIFIED**, and what is *absent* is the finding:

- **No height term.** Neither the feature's `height` key nor its instance `y`
  is read. Blocking is two-dimensional.
- **No altitude term.** The moving unit's own `y` is not read either; the four
  height bytes the test does read are the map's, at `square+5`/`+0x6`.
- **No exemption.** Not for `canhover` (`def+0x241` bit 12), not for `floater`
  (bit 19), not for `amphibious` (bit 21), not for any movement class by name.
  A blocking feature blocks a hovercraft, a tank, a ship and a submarine
  identically.

### The movement class record, and the parser that fills it

Decoded from the MOVEINFO parser at `0x440340` and the cell test at `0x47DFC0`:

| Offset | Key | Default |
|---|---|---|
| `+0x04` | `FootprintX` (`0x505484`) | — |
| `+0x06` | `FootprintZ` (`0x505478`) | — |
| `+0x08` | `MaxWaterDepth` (`0x505468`) | `10000` — §30's seeded record |
| `+0x0A` | `MinWaterDepth` (`0x505458`), **signed** | `-10000` |
| `+0x0C` | `MaxSlope` (`0x50544C`) | `255` |
| `+0x0D` | `BadSlope` (`0x505440`) | **`MaxSlope / 2`** (`0x4403B9 shr eax,1`), so `127` unseeded |
| `+0x0E` | `MaxWaterSlope` (`0x505430`) | `255` |
| `+0x0F` | `BadWaterSlope` (`0x505420`) | **`MaxWaterSlope / 2`** (`0x4403E7`) |
| `+0x1C` | the crush threshold a standing unit's `+0x26` is tested against | — |

then three clamps, `0x4403F4`–`0x440417`: `MaxSlope = min(MaxSlope,
MaxWaterSlope)`, `BadSlope = min(BadSlope, MaxSlope)`, `BadWaterSlope =
min(BadWaterSlope, MaxWaterSlope)`. The seeded defaults are §30's, and they are
why `TANKHOVER3` — which names neither depth key — crosses any water at all;
§30 also records that a hovercraft FBI's own `MaxWaterDepth=0` is ignored once
a movement class is named.

The cell test spends them, `0x47E145`–`0x47E19E`, after the feature test has
passed:

```
square+6 >= seaLevel - MaxWaterDepth      ; deepest point of the cell
square+5 <= seaLevel - MinWaterDepth      ; shallowest point of the cell
spread = square+5 - square+6
if (square+6 >= seaLevel)                 ; dry
    spread <= BadSlope       -> free
    spread <= MaxSlope       -> tight
    else                     -> blocked
else                                      ; wet
    spread <= BadWaterSlope  -> free
    spread <= MaxWaterSlope  -> tight
    else                     -> blocked
```

Both depth tests are worst-case over the cell — the max-depth test reads the
cell's *lowest* corner and the min-depth test its *highest*.

### Free, tight, blocked — and where "tight" comes from

§87 records that the pathfinder's passability table is two bits a cell per
movement class and that a "tight" cell costs an extra 30. This is what fills
it. `0x47E1F0(grid, x, y)` asks `0x47DFC0` for the footprint itself, returns
straight away if the answer is 0 or 1, and otherwise probes the **four
one-cell-wide strips around the footprint** (`0x47E226`, `0x47E257`,
`0x47E28A`, `0x47E2B2`); if any of them is not free it downgrades the answer
from 3 to 1. So **"tight" means clearance, not slope**: a cell is free only
when the ring around the footprint standing on it is free too. `0x440830`
packs that answer into the grid at `[grid+0x18]`, two bits a cell, sixteen
rows of `y` to a dword, indexed `width*(y>>4) + x` with the pair at
`2*(y & 15)`.

RWE has the same idea in a different place: `AbstractUnitPathFinder::computeRoughTerrain`
calls `isAdjacentToObstacle` and adds a cost, which is the clearance half, and
uses `maxSlope / 2` as its rough threshold — which is exactly the original's
default for `BadSlope`, arrived at independently and now confirmed.

### The wreck, specifically

A feature's footprint is stamped into the squares once, at placement
(`0x423C50`, `0x423D5F`–`0x423D80` walking `featdef+0x94`/`+0x96` from the
square the caller gave). The falling-feature physics that sinks a wreck,
`0x424214`, writes only `feature+0x08..0x10` (position) and `feature+0x14..0x1C`
(velocity) and touches no square at all. **A corpse occupies exactly the cells
it was placed on, from the tick it appears until it is reclaimed, at whatever
depth it has reached.**

The shipped data is what makes this visible now that wreckage sinks. All
thirteen hovercraft in `ccdata` leave a corpse written like a land unit's —
`armah_dead`: `footprintx=3 footprintz=3 height=20 blocking=1` — while a ship's
is a flat plate authored to be driven over — `armroy_dead`: `footprintx=5
footprintz=5 height=4 blocking=0`. So in the original a hover battle really does
leave the sea bed obstructed and a naval one does not, and the difference is
the data rather than the engine.

### The verdict, and what RWE does

**The play-test's expectation is wrong: the original blocks hovercraft with
sunken wrecks too, and nothing was changed.** RWE already matches, item by
item:

| | Original | RWE |
|---|---|---|
| What blocks | `blocking` on the feature *type* | `isCollisionAt` tests `FeatureDefinition::blocking` — same |
| Height / depth | not consulted | not consulted — `occupiedGrid` carries no y |
| Hover exemption | none | none |
| A sunken wreck's cells | stamped at placement, never revised | `addFeature` stamps once; `updateFallingFeatures` moves only the position |
| A ship's corpse | `blocking=0`, driven over | parsed, defaulting false — same |

`src/rwe/sim/wreckcollision.test.cpp` pins it with the shipped numbers, so a
later reader tempted to "fix" the play-test's complaint has to argue with the
binary first.

Two smaller things fell out and are recorded in §91 rather than acted on:
`BadSlope`/`BadWaterSlope` are keys RWE does not parse, and the original's
"tight" band is wet-or-dry aware where RWE's rough test is not.

---

## 96. Capture: where the progress is kept, and what sets the clock

The roadmap asked for capture progress to **decay** when the captor stops. The
original has nothing to decay, because the progress was never on the thing
being captured. It is on the captor's own order, and it goes when the order
goes.

Ground mission table row 41 (`0x4FC891`): name `Capture`, display `"Capturing"`
(`0x5013B8`), handler **`0x404270`**. Six states through the jump table at
`0x404714`: `0x4042AB`, `0x404437`, `0x40454E`, `0x404568`, `0x404585`,
`0x4046B7`. Arguments are `(unit, mission, flags)`; `mission+0x16` is the
target.

### The three gates, in the order the handler applies them

State 0, `0x4042AB`:

```
4042c9  mov  edx,[reclaimerDef+0x245] / test dh,0x10   ; the CAPTOR's cancapture (bit 12)
4042e9  mov  ecx,[targetDef+0x245] / shr ecx,0xc / test cl,1
                                                       ; the TARGET's cancapture -> refuse
                                                       ; 0x501618 "That unit cannot be captured"
404313  fld  DWORD PTR [target+0x104] / fcomp 0.0      ; remaining build fraction must be zero
                                                       ; 0x5015E0 "That unit is a cloud of vapor
                                                       ;           and cannot be captured"
```

The first two are §20's rule read from the other side: only the two commanders
set `cancapture`, so a commander is the only thing that can capture and the
only thing that cannot be captured. The third is new here and easy to miss —
**a nanoframe cannot be taken**, and `unit+0x104` is the same remaining build
fraction §23 and §93 use.

### The clock, `0x404313`–`0x404407`

Computed **once**, in state 0, and never revisited:

```
404359  fld  [targetDef+0x186]    ; BuildCostEnergy
40435f  fmul 30.0      (0x4FC930)
404365  fmul 0.0005    (0x4FC934)
40436b  fld  [targetDef+0x18A]    ; BuildCostMetal
404371  fmul 30.0
404377  fmul -0.00714285718 (0x4FC938 = -1/140)
40437d  fsubp                     ; A - B
40437f  fsub -150.0    (0x4FC93C)
404385  call 0x4E43A0             ; truncate
40438a  cmp  eax,0x708 / jl / mov edx,0x708   ; clamp to 1800 ticks
4043a9  movsx eax,WORD [target+0x108]         ; current hit points
4043b0  mov  ebp,[targetDef+0x1FA]            ; MaxDamage
4043c0  imul eax,edx / div (2*MaxDamage)      ; * (hp + max) / (2 * max)
4043d8  mov  cx,WORD [target+0xB8]            ; the target's kill count
4043df  imul 0x66666667 / sar edx,1           ; kills / 5
4043e8  lea  eax,[edx+eax+0xa]                ; + 10
4043ec  imul eax,edi
4043ef  lea  ecx,[eax+eax*4] / shl ecx,1      ; * 10
4043f9  imul 0x51EB851F / sar edx,5           ; / 100
404407  mov  [mission+0x3A],edx
```

So, in one line:

```
t = trunc(BuildCostEnergy*0.015 + BuildCostMetal*3/14 + 150)
t = min(t, 1800)
t = t * (hitPoints + MaxDamage) / (2 * MaxDamage)
t = t * (kills/5 + 10) / 10
```

Three things fall out of it that are worth stating on their own:

- **`workertime` is not in it.** Nothing in the whole handler reads the
  captor's definition after the `cancapture` test. A commander captures a solar
  collector in exactly the time any other captor would.
- **A damaged unit changes hands faster**, down to half the time at zero hit
  points. A veteran one is slower, +10% per five kills — and unlike the damage
  tiers of §6, which stop at five, this one has no ceiling.
- The clamp bites for exactly **eight** of the 189 shipped units: `ARMCOM`,
  `CORCOM`, `ARMGATE`, `CORGATE`, `ARMCKFUS`, `ARMBRTHA`, `CORINT` and
  `CORFMD`. Everything else lands between 5.2 seconds (`ARMDRAG`, 155 ticks)
  and 59.7 (`ARMFUS` and `CORFUS`, 1790).

Replayed over all 189 shipped FBIs, the float chain above and the exact
rational `(21E + 300M + 210000) / 1400` agree to the tick on every one, which
is what lets RWE do it in integers.

### The rate, and where the count lives

State 4, `0x404585`:

```
4045b3  mov  ecx,[mission+0x36] / cmp ecx,[mission+0x3A] / jge  -> return 1 (done)
404698  add  ecx,2 / mov [mission+0x36],ecx     ; +2
4046a3  call 0x439E80(mission, 2)               ; ... every 2 ticks
```

One tick of progress a tick, for everyone. `mission+0x36` and `mission+0x3A`
are the order record's general-purpose scratch dwords (§13, §26, §28 use them
for other things on other missions), allocated by `malloc(0x56)` at `0x43A054`
and initialised by the order constructor `0x43A0C0`, which takes `+0x36` as a
plain argument.

**That is the whole answer to the decay question.** The target unit has no
capture field. Cancel the order and the record is freed with the count in it;
give the order again and state 0 starts from zero. Two captors on one target
each keep their own count, so a second builder does not halve the time — it
just races the first. Progress does survive an interruption that leaves the
order in place: walking out of range and back keeps it, because the record is
still there.

### The two sounds

```
404568  (state 3) 0x47F780(unit, 0x0B, 0)   ; sound slot 11 -- see S:97
4046cc  (state 5) 0x47F780(unit, 0x10, 0)   ; sound slot 16, `capture`
```

So a capture opens with the *reclaim* sound and closes with the capture sound.

### What RWE now does

`CaptureOrder` carries `progress` and `totalWork`, exactly as the mission
record does, and `GameSimulation::computeCaptureTime` is the arithmetic above
in integer form. `captureUnit` no longer takes a work amount; it only performs
the handover. `UnitState::captureProgress` is gone, along with its entries in
the game hash, the save and the state dump — there is no per-target capture
progress in the original and there is none in RWE now.

The nanoframe gate is implemented. The two sounds are not: see §97.

One thing is deliberately left alone. State 4 also refuses to work while
`target+0x110 & 0xC` is set, waiting thirty ticks instead (`0x40458A`). Those
are the two bits §91 lists as unexplained — this is one of the three sites that
read them — so RWE does not guess at what they mean.

---

## 97. Automatic reclaim, `autoreclaimable`, and what `working` in SOUND.TDF is for

Three of the ground missions reclaim, and they are separate handlers:

| # | Row | Name | Handler | Display |
|---|---|---|---|---|
| 43 | `0x4FC8C3` | `Reclaim` | `0x404AD0` | Reclaiming |
| 44 | `0x4FC8DC` | `ReclaimUnit` | `0x404730` | Reclaiming |
| 42 | `0x4FC8AA` | `Resurrect` | `0x404DB0` | Resurrecting |

### How long a feature takes, `0x404BBA`

```
404bba  fld  [featdef+0xEC]        ; energy
404bc0  fadd [featdef+0xF0]        ; + metal
404bc6  fmul -0.5      (0x4FC940)
404bcc  fsubr 15.0     (0x4FC944)
404bd2  call 0x4E43A0              ; truncate
404bdc  mov  [mission+0x36],eax
```

`ticks = 15 + (metal + energy) / 2`, counted **down** two every two ticks
(`0x404C7C`), so again one tick of work a tick and again no `workertime`
anywhere. The feature's own `damage` — `featdef+0xEA` — is not in it.

The key offsets come from the feature parser at `0x422A20`: `metal` →
`featdef+0xF0`, `energy` → `featdef+0xEC`, `damage` → `featdef+0xEA`, and the
flag word is `featdef+0xFE` with `blocking` bit 6, `reclaimable` bit 7,
`autoreclaimable` bit 8 (default **1**, pushed at `0x422BA7`), `indestructible`
bit 9.

**RWE differs here and it is worth writing down rather than quietly
correcting.** RWE's `computeFeatureReclaimWork` is `metal + energy +
hitPoints/4` at `workerTimePerTick` a tick, which came out of a play-test round
(a boulder should take longer than a bush, a shelled wreck should clear
quicker). The original's is flat, half the resource value plus fifteen ticks,
and identical for every builder. Changing it would move every reclaim time in
the game and belongs in its own pass with a play-test, so it has been left as
it is and recorded here.

`ReclaimUnit` is a different mechanism again: state 4 (`0x4048D2`) applies
`0x489BB0(reclaimer, target, mission+0x36, type 5, 0)` — ordinary damage, of a
type all its own — once every fifteen ticks, so a unit being reclaimed is
visibly taken apart rather than dissolved on a timer.

### `autoreclaimable` has exactly one reader

`0x47EA40` is an area scan over map squares. It resolves each square's feature
type against `[globals+0x1426F]` and then:

```
47eb0b  mov  ax,WORD PTR [featdef+0xFE]
47eb13  test al,0x80        ; reclaimable  (bit 7)  -> skip
47eb1b  test ah,0x01        ; autoreclaimable (bit 8) -> skip
47eb24  fld  [featdef+0xEC] / fcomp 0.0    ; non-zero energy -> the energy list
47eb72  fld  [featdef+0xF0] / fcomp 0.0    ; non-zero metal  -> the metal list
```

It fills **two** candidate arrays, one per resource, and a feature carrying
both goes in both. Its only two callers in the whole of `.text` are
`0x405B93`, inside the ground `RepairPatrol` handler `0x405980`, and
`0x41564F`, inside `VTOL_RepairPatrol` `0x4152F0`.

**So automatic reclaim is a repair-patrol behaviour and nothing else.** There
is no area-reclaim command in the binary — that is a Spring idea, not a TA one
— and no idle-builder sweep. The roadmap's "auto-reclaim when a builder is idle
with the area-reclaim command" describes something the original does not have.

### What the patrol does with the two lists

The radius is the unit's own sight: `0x405B74` reads `def+0x202`
(`SightDistance`) and shifts it into 16.16 for the scan. The decision is four
comparisons against the player's stores, `player+0x8C`/`+0xA4` for energy and
`player+0x98`/`+0xA8` for metal (§23), all against the double `0.2` at
`0x4FC950`:

```
4059EA  repair at all only while  energyStored >= 0.2 * energyCap
405B18  scan at all only while    energyStored <  0.2 * energyCap
405B39                       or   metalStored  <  0.2 * metalCap
405BA0  take the metal candidate  if metalStored  < 0.2 * metalCap
405C25  else the energy candidate if energyStored < 0.2 * energyCap
405C79  else the metal candidate  if metalStored  + payout <= metalCap
405CBC  else the energy candidate if energyStored + payout <= energyCap
```

A builder on patrol with both stores full therefore walks past the wreck field,
and one that is short of energy goes for the trees rather than the wrecks. The
same 0.2 that opens the reclaim closes the repair, which is the other half of
what `RepairPatrol` does.

### The sound slots, and `working`

`SOUND.TDF`'s per-category keys are a table of 24-byte records at
`0x5086F0`, six dwords each: the slot id at `+0x00`, the key name at `+0x0C`
and an optional caption at `+0x10` (slot 7's is `"Cannot Comply"`). The ids
that matter here:

| Id | Key | Shipped value for a construction unit |
|---|---|---|
| 9 | `build` | `nanlath1` / `nanlath2` |
| 10 | `repair` | `repair1` / `repair2` |
| 11 | `working` | `reclaim1` |
| 16 | `capture` | — (no shipped category sets it) |

`0x47F780(unit, id, extra)` plays one. Slot **11** is played once when work
actually starts, by all three of `Reclaim` (`0x404C69`), `ReclaimUnit`
(`0x4048B5`) and `Capture` (`0x404568`) — so `working=reclaim1` is the reclaim
sound, and a capture uses it too.

### What RWE now does

The patrol sweep is gated on `autoreclaimable`, uses the unit's own
`SightDistance` as its radius instead of a hardcoded 256, keeps the two
candidates the original keeps, and follows the four store comparisons above.
`src/rwe/sim/autoreclaim.test.cpp` pins it with the shipped numbers — the
`armsolar_dead` wreck, the `Fortification` wall (one of only two shipped
features that say `autoreclaimable=0`), an acid plant for the energy side, and
ARMMSTOR/ARMESTOR for the storage capacity, which the economy rebuilds from
the player's units every second and so is not a fixture's to invent.

The feature's `seqnamereclamate` swirl was already implemented (it plays on
`FeatureReclaimedEvent`); the roadmap entry asking for it was stale.

> **Ported, 2026-09-10.** Both work sounds now play.
> `UnitStartedReclaimingEvent` sits beside `UnitStartedBuildingEvent` in the
> `GameEvent` variant and is raised on the first tick of actual work by all
> three jobs the original plays slot 11 from: feature reclaim and unit
> reclaim, in `deployReclaimArm`, and capture, in `deployCaptureArm`.
> `GameScene::processSimEvents` plays `UnitSoundType::Working` off it. Slot 16
> hangs off the `UnitCapturedEvent` that already marked the end of a capture,
> which now carries the captor as well as the two owners, so the sound is the
> captor's and not the taken unit's; no shipped category sets the slot, so it
> is silent until a mod sets it. Neither needed a new piece of simulation
> state: "work has started on this job" is the reclaiming state's empty
> `nanoParticleOrigin` on one side and a `CaptureOrder::progress` of zero on
> the other, both of which already exist, are already hashed, and are already
> reset when a job ends. `src/rwe/sim/worksounds.test.cpp` pins the *once* and
> the *when* -- in particular that a builder sent to a wreck it must walk to
> announces nothing until it arrives.

### `underattack`, `repair` and `cant`: the three voices that were never played

> **Ported, 2026-09-11.** `UnitDamagedEvent` carries the cause (its
> `paralyzer` flag) and the scene plays slot 2 off it under the gates below;
> `UnitRepairedEvent` is raised where a repair job lands, on both sides of
> it, and plays slot 10; `UnitCannotComplyEvent` carries the caption a
> refusing site passes and plays slot 7. Four of the forty-five `cant`
> sites are wired, the ones whose refusals RWE already makes: the nanoframe
> capture, the Commander reclaim, the pad that is taken, and the corpse that
> has gone. The rest are in the table for when their refusals exist.
> `src/rwe/sim/unitnotifications.test.cpp`.

The rest of the slot table at `0x5086F0`, read out in full this time:

| Id | Key | Caption | | Id | Key | Caption |
|---|---|---|---|---|---|---|
| 1 | `select` | | | 12 | `load` | |
| 2 | `underattack` | Under Attack | | 13 | `unload` | |
| 3 | `activate` | | | 14 | `cloak` | Cloaked |
| 4 | `deactivate` | | | 15 | `uncloak` | Visible |
| 5 | `ok` | | | 16 | `capture` | |
| 6 | `arrived` | Arrived | | 17-22 | `count5`..`count0` | five..zero |
| 7 | `cant` | Cannot Comply | | 23 | `canceldestruct` | Self destruct terminated |
| 8 | `unitcomplete` | Nanolathe Complete | | | | |
| 9 | `build` | | | | | |
| 10 | `repair` | | | | | |
| 11 | `working` | | | | | |

**Three entry points, one player.** `0x47F780(unit, id, caption)` is the one
the eighty-two direct call sites use. It refuses a unit that is not the local
player's, or not alive (`unit+0x110` bit 28 clear or bit 14 set); takes the
caption passed in, or the table's if none; prints it (`0x4C5740`) and hands
`(unit, id, text)` to the player `0x47FAD0`. Two siblings differ only in one
extra gate, `0x48BCB0(unit)`, which walks the frame's draw list (the "HOT
UNITS" list at `world+0x1435F`, §18): `0x47F7E0` plays only for a unit *in*
the list and is never called; `0x47F850` plays only for a unit *not* in it,
and has one caller.

**The player keeps a queue.** `0x47FAD0` holds up to eight pending voices of
17 bytes each. A slot id already waiting is not queued again (`0x47FB0A`), a
full queue drops its head, and the entries are kept in priority order from
the table's third dword (`underattack` 0x14, `cant` 1, `select` 0). So a unit
under sustained fire says "Under Attack" once per playback, not once per hit.
RWE has one reserved voice channel and drops a voice while it is busy, which
comes to nearly the same thing.

**`underattack`, slot 2, one site.** `0x4071D8`, the tail of the return-fire
routine `0x406F80(attacker, victim, damage)` (§"Firing modes"). Every early
exit in that routine jumps to the tail rather than returning, so the voice is
independent of the fire mode and of whether the unit shot back. Its own gates:

- `0x438BE0(victim)` returns the victim's current mission flags word
  (`mission+0x42`); bit 7 set skips the voice. No mission handler sets that
  bit directly in this binary; it is treated as never set.
- `[victim+0xF4]` (the owner of the last unit to damage it) differs from the
  victim's owner: play. Otherwise play only if `[victim+0xF5]`, the cause
  byte, is 1, a weapon hit -- so a paralyser from your own side is silent.
- Through `0x47F850`: only for a unit that is not being drawn this frame. It
  is the warning for what you cannot see.

**`repair`, slot 10, five sites, all with `0x5012CC` "Unit repaired".**
`0x402491` in `SELFREPAIR` (`0x402430`, the aircraft on a pad, once its hit
points meet the maximum), `0x415298` in `VTOL_GetRepaired` (`0x415250`, the
same from the air side), `0x415219` in `VTOL_RepairUnit` (`0x414E70`, the
repairer), and `0x4056FE`/`0x4057A1` in the ground repair body shared below
`0x4056A5` (the repairer again). Both ends of a repair say it.

**`cant`, slot 7, 45 sites.** Not one passes a null caption; every site
overrides "Cannot Comply" with a message of its own, which is why the table's
caption is never seen. The sites, by caption:

| Sites | Caption |
|---|---|
| `4046fe` | Capture failed |
| `402736` | Construction stopped |
| `403a3c, 403fb0, 413dba` | Construction terminated |
| `41473c` | Construction terminated by hostile action |
| `40407a` | I can't get there |
| `403c45` | I can't reach the construction site |
| `41190b` | Landing aborted |
| `411d36` | Landing aborted: all pads are occupied |
| `411e01` | Landing aborted: no pads available |
| `411c3a` | Landing failed |
| `4047a6, 4047e6, 404b00, 41479f, 414d69` | Reclamation failed |
| `40244e, 415268` | Repair aborted. |
| `414f8e` | Repair mission failed |
| `40531e, 4053aa, 40575e, 414e8e, 414f18` | Repairs unsuccessful. |
| `404f7e` | Ressurection failed |
| `403d10, 414055` | Target area was blocked |
| `4042ff` | That unit cannot be captured |
| `404799, 414d91` | That unit cannot be reclaimed |
| `40432e` | That unit is a cloud of vapor and cannot be captured |
| `4068d7, 411207, 411526` | Transport mission failed |
| `402907, 403d78, 405121, 4140b8` | Unable to create any more units |
| `411771` | Unable to unload unit |
| `411275` | Unit is too heavy to transport |
| `4067ea` | Unit is too large to transport |
| `406916` | Unloading process is proceeding non-optimally |
| `403cdf, 414020` | Waiting for target area to clear |

---

## 98. Resurrect: a real mission, a crude corpse mapping, and nothing that can use it

> **Ported, 2026-09-06.** `ResurrectOrder` implements what follows, with one
> departure recorded in §88: the corpse is removed *before* the unit is
> created rather than after. A corpse is `blocking`, and RWE refuses to place
> a unit on an occupied footprint, so the original's order can never succeed
> here. Same outcome, opposite order. The order carries the countdown, as the
> mission record does and as capture does since §96.

The original has a `Resurrect` mission. Nothing in the shipped data can issue
it — §19's shipped-data check found `canresurrect` (`def+0x245` bit 11, key
string `0x503A64`, parsed at `0x42CA2E`) named by not one of the 189 FBIs — but
the mission is complete, and this is what it does.

Row 42 of the ground mission table (`0x4FC8AA`): handler **`0x404DB0`**,
display `"Resurrecting"` (`0x5013A0`), seven states through `0x4052D8`. It
takes a *position*, not a target: `0x421DA0` resolves the map square under
`mission+0x22` to a feature type, and a miss gives `"Ressurection failed"`
(`0x501698`, misspelled in the binary). The feature must be `reclaimable`
(`0x404E0F` tests `featdef+0xFE` bit 7), and the unit must have bit 11
(`0x404E55`).

### The corpse to unit-type mapping

It is a string operation, not a table:

```
404f36  strncpy(buf, featdef->name, 0x40)          ; ds:0x4FC0F0
404f44  scan the first 0x40 bytes for '_' (0x5F)
404f56  overwrite it with NUL
404f5b  call 0x488B10(buf)                         ; unit type id by name
404f6a  mov [mission+0x36],eax & 0xFFFF            ; 0 -> "Ressurection failed"
```

`armsolar_dead` → `armsolar`. `armfus_dead` → `armfus`. Anything whose name
has no underscore, or whose prefix is not a unit type, cannot be resurrected.
The shipped corpses are all named `<unitname>_dead`, so the rule works for
every one of them — and `armsolar_heap`, the second-stage wreck, resolves to
`armsolar` as well, which is presumably not deliberate but is what the code
says.

### The clock, and this one *does* use worker time

```
404fb0  fild DWORD [unitdef + 0x1EA]     ; the resurrected unit's BuildTime
404fbf  mov  cx,WORD [resurrectorDef+0x1FE]   ; WorkerTime
404fc6..404fd0                            ; workerTime / 30, integer
404fc8  fmul 0.3       (0x4FC948)
404fe2  fdivp                             ; (BuildTime * 0.3) / (workerTime/30)
404fe4  call 0x4E43A0
404fee  mov [mission+0x3A],eax
404feb  0x47F780(unit, 0x0B, 0)           ; the `working` sound again
```

Unit definitions are 585 bytes (`0x249`) apart — `id*65*9` at `0x404F92` —
based at `[globals+0x1439B]`, with `BuildTime` at `+0x1EA` and `WorkerTime` at
`+0x1FE`. State 5 (`0x405005`) counts `mission+0x3A` down one a tick.

### What comes back

```
4050f2  al = BYTE [resurrector+0xFF]       ; the owner
4050fe  dx = WORD [mission+0x36]           ; the unit type worked out above
405104  call 0x485F50(...)                 ; create it at the corpse's position
405119  on failure: 0x501310 "Unable to create any more units", retry in 300 ticks
40518d  copy six bytes from featureInstance+0x20 into newUnit+0x64
                                          ; the corpse's roll, heading and pitch
405198  0x4246B0(...)                      ; remove the feature
405219  newUnit+0x104 = 0.0f               ; fully built, not a nanoframe
405226  WORD newUnit+0x108 = 1             ; ... with ONE hit point
405247  0x47F780(unit, 8, 0x50166C)        ; "Resurrection complete"
```

So a resurrected unit arrives complete, facing the way its corpse lay, and on
one hit point — it has to be repaired afterwards or a stiff breeze will finish
it. Nothing is charged for it beyond the time.

### Not implemented in RWE, and why

Two reasons, neither of them the arithmetic:

1. **Nothing in the shipped data can order it.** No FBI sets `CanResurrect`,
   and there is no RESURRECT button among the fifteen the order panel offers
   (§19). Implementing it would add a mod-only capability.
2. **It cannot be done inside `src/rwe/sim` alone.** A `ResurrectOrder` is a
   new alternative in the `UnitOrder` variant, and `GameScene.cpp` matches
   exhaustively over that variant in six places, so the order type cannot be
   added without editing the scene.

The corpse→unit mapping above is the piece the roadmap said was missing, and
it needs no `featureDead`/`corpse` cross-reference at all: chop the feature's
name at the first underscore and look the rest up as a unit type.

---

## 99. The unit info panel, and the three things the gadget renderer does with a colour

Three related reads, all of them interface: the footer that describes the unit
under the cursor (`0x46A860`), the anti-missile ring's second state (an
addendum to §25), and the parts of the gadget renderer that change a colour
rather than draw something — the focus caret, the selected list row, and the
greying §19 recorded as unported.

### Which unit the footer describes

**The one under the cursor, and only that.** `0x46ABA9` reads a word from
`cfg+0x2CBA` and indexes the unit array with it; that word is written in
exactly two places (`0x491D23` and `0x499283`), both immediately after a call
to `0x48CD80`, which walks the unit list and returns what the mouse is over.
There is no fallback to the selection: move the pointer off a unit and the
footer empties, whatever is selected. When the word is zero the routine falls
through to `cfg+0x2CBC`, the hovered *feature*, and draws its description
instead (`0x46B78C`).

RWE already keyed the footer on the hovered unit, which this confirms.

### The redraw key

Before it draws anything, `0x46ABA3`-`0x46ACDD` fills a 60-byte block and
`repz cmpsb`s it against a cached copy at `cfg+0x37E60`; equal means nothing
has changed and the whole routine is skipped. The block is worth listing
because it is the complete set of things the footer can show:

| Offset | Field |
|---|---|
| `+0x00` | `0x439DF0(unit)` — the current mission's display name pointer |
| `+0x04` | the hovered unit's index |
| `+0x06` | `unit+0x108`, hit points |
| `+0x08` | `unit+0xB8`, kills |
| `+0x0A`, `+0x0E`, `+0x12` | per-weapon reload counter, or `-1` |
| `+0x16`, `+0x1A`, `+0x1E`, `+0x22` | `unit+0xD0`, `+0xCC`, `+0xE8`, `+0xE4` — the four resource rates |
| `+0x26`, `+0x28` | the current mission's target unit and its hit points |
| `+0x2A` | the hovered feature |
| `+0x2C` | the build button under the cursor |
| `+0x30`, `+0x34` | `cfg+0x37E90`, `cfg+0x37E94` |

The three weapon reload counters are collected (`0x46AC4B`, skipping any
weapon whose `reloadtime` is under thirty ticks) and then never drawn — the
same dead end as the RELOAD1/2/3 rectangles in §29. They are in the key, so a
reloading weapon forces a redraw of a panel that does not show it.

### What it draws, rectangle by rectangle

`SIDEDATA.TDF`'s footer rectangles, at the offsets the parser at `0x432310`
gives them (the renderer addresses the same struct 0x4A higher, which is where
§29's `DAMAGEBAR` at `side+0x152` comes from):

| Key | Parser offset | What goes there |
|---|---|---|
| `LOGO2` | `+0xE8` | the owner's side logo, `0x467C00` |
| `UNITNAME` | `+0xF8` | the unit's name, **centred on x1** |
| `DAMAGEBAR` | `+0x108` | health, `hp/maxhp`, green over dark red |
| `UNITENERGYMAKE` | `+0x118` | `"+%.0f"` |
| `UNITENERGYUSE` | `+0x128` | `"-%.0f"` |
| `UNITMETALMAKE` | `+0x138` | `"+%.1f"` |
| `UNITMETALUSE` | `+0x148` | `"-%.1f"` |
| `MISSIONTEXT` | `+0x158` | the mission's display name, centred |
| `UNITNAME2` | `+0x168` | what the current mission is pointed at, centred |
| `DAMAGEBAR2` | `+0x178` | that target's health, or the weapon percentage |
| `NAME` | `+0x188` | the hovered feature or build button, `"%s %s%s"` |
| `DESCRIPTION` | `+0x198` | the build button's description |
| `RELOAD1..3` | `+0x1A8`, `+0x1B8`, `+0x1C8` | parsed, never read |

Five details worth having:

- **Metal takes one decimal place and energy none.** Not a rounding
  convention anyone chose later: the four format strings are `"+%.1f"`
  (`0x50788C`) and `"-%.1f"` (`0x50787C`) for metal against `"+%.0f"`
  (`0x507884`) and `"-%.0f"` (`0x507874`) for energy.
- **Every rate is clamped at zero first.** `fcomp` against the zero at
  `0x4FD568` in front of each `sprintf`, so a negative figure prints as `0`
  rather than as a negative.
- **The name is the player's, not the unit's, for a commander in a network
  game.** `0x46AF56` ORs `showplayername` (flags bit 17) with `commander`
  (bit 18) and requires `0x435100` to return 3.
- **The damage bar is skipped for someone else's `hidedamage` unit**
  (`0x46B03F` tests bit 14 of `def+0x241`), and its colours are interface
  slots `0x0A` over `0x04` — bright green over dark red.
- **The four rates, the kills line and the mission line are one block behind
  one ownership test** (`0x46B119`). An enemy unit shows you its logo, its
  name and its health and nothing else. The second name-and-bar slot is
  *outside* that test.

### Kills, and Veteran

`0x46B2B8`, gated on bit 31 of `unit+0x110` and on the kill count at
`unit+0xB8` being non-zero:

```
"%d %s"        kills, "kill" if 1 else "kills"      when kills <= 4
"%d %s - %s"   kills, "kills", "Veteran"            when kills >= 5
```

The comparison is `cmp cx,4 / jbe` at `0x46B30D`. Five kills is the whole of
TA's veterancy display; there is no other reader of the count in the
interface.

It has **no rectangle of its own**. `0x46B2D6` takes `DAMAGEBAR`'s x1 and its
y2 plus two, so the line hangs under the health bar wherever `SIDEDATA.TDF`
put it. The colour is interface slot `0x0F`, which §50's GUIPAL nearest-match
resolves to white.

### The mission line is a table lookup

`0x439DF0` reads the mission id byte at `mission+0x04` and returns
`[table + id*25]`, the `char*` at the front of the mission record. So the
footer never composes a string: every mission in the two tables
(§`TOTALA-EXE-MISSIONS.md`, ground at `0x4FC490`, air at `0x4FCA18`) carries
its own wording. The full vocabulary, both tables:

`Stopping`, `Attacking`, `Activate`, `Deactivate`, `Cloaking`, `Decloaking`,
`Acknowledged`, `Nanolathing`, `SELF DESTRUCT ENGAGED`, `Paralyzed`,
`Under construction`, `Being transported`, `Unit is available`, `Waiting`,
`Waiting for attack`, `Ready`, `Repairing`, `Ready with orders`, `Standby`,
`Moving`, `Guarding`, `Suppressing fire`, `Annihilating`, `Parking`,
`Patrolling`, `Loading`, `Unloading`, `Teleporting`, `Repair patrol`,
`Capturing`, `Resurrecting`, `Reclaiming`, `Landing`, `Airstrike`,
`Engaging target`, `Evading`, `Seeking to attack`, `Seeking to guard`,
`Under repair`, `Seeking to land`.

Note `AttackSpecial` is `Annihilating`, not `Attacking`, and that the three
air-to-X missions share `Engaging target`.

### §29 was wrong about the stockpile bar, and here is where it is drawn

§29 concluded that the progress of the round a silo is building is shown
**nowhere**, having read `0x46AD90`-`0x46B400`. The routine does not end at
`0x46B400`. At `0x46B445`:

```
46b445  push esi
46b446  call 0x439d20              ; ticksPaid * 100 / (reloadtime*30)
46b44b  test eax,eax
46b451  je   0x46b571              ;   nothing on order -> the target-unit slot
46b471  jne  0x46b8ea              ;   not our unit -> nothing
46b477  push 0x5077b0              ;   "Weapon"
...     centred in UNITNAME2
46b4c3  add  ebp,0x1c2             ;   DAMAGEBAR2
46b4f6  cmp  eax,0x64              ;   clamped to 0..100
46b536  call 0x4bf6f0              ;   filled to that percentage
```

`0x439D20` is exactly the arithmetic §29 predicted — it walks the mission list
from `unit+0x60` for the one with the `BUILDWEAPON` flag (`mission+0x42` bit
19), takes the ticks paid at `mission+0x3E` and divides by the weapon's own
`wdef+0xE4` — and the answer is drawn as a percentage bar under the caption
`Weapon`. So the silo readout RWE added as "an addition, not a restoration" is
in fact what the original does, in the same two rectangles, and only the
caption was RWE's invention.

### The second name-and-bar slot is not build-only

When there is no weapon on order, `0x46B571` uses the word the redraw key
collected from `0x439DD0(unit)` — `mission+0x16`, the current mission's target
unit. Whatever the current mission points at gets its name centred in
`UNITNAME2` and its health drawn in `DAMAGEBAR2`, subject to the usual
visibility test (`0x465AC0`) and to `hidedamage`. A builder shows what it is
building and how far along it is; a guard shows what it is guarding; an
attacker shows what it is shooting at.

### Addendum to §25: the anti-missile ring has two states, and the magazine picks

§25 recorded that the coverage ring is dashed when `[weaponSlot+0x0E]` is
non-zero without saying what that byte is. It is the **magazine**: weapon
records are `0x1C` apart from `unit+0x04` with the definition pointer at
`+0x0C` and the round count at `+0x1A`, and `0x46707C` walks the slots by the
definition pointer, so `slot+0x0E` is `record+0x1A` — the same byte
`0x419A33` reads to caption the MAKENUKE button (§29). So:

- **magazine empty → a solid ring** (`0x4C0070`);
- **at least one round stocked → a dashed ring** (`0x4C01A0`), sixteen of the
  thirty-two segments, with the starting parity taken from `cfg+0x142F1` bit
  0.

That blink bit flips every eight passes of the main loop: `0x466580` counts a
word at `cfg+0x142EF` down from 7 and flips the bit when it underflows, and it
is called once a frame from the main loop at `0x4955E5`. So the gaps chase
round the ring about twice a second, and a loaded launcher is distinguishable
from an empty one at a glance.

The rings are also **clipped to the minimap**: `0x4C0070` clips each segment
before handing it to the Bresenham at `0x4CC7AB`, which is what stops a
2000-unit coverage ring painting over the rest of the screen.

### The gadget renderer's three colour tricks

The per-gadget draw dispatches on the type byte through a table at `0x4A962C`
(index `type-1`, so type 1 Button → `0x4A5F40`, type 2 ListBox → `0x4A1B40`,
type 3 TextBox → `0x4A4D70`). Three of the things it does are colour changes
rather than draws, and all three go through the same pair of 32x256
palette-index tables that the model shading uses: `0x4BF4D0(surface, rect,
level)` clamps `level` to `[-32, 31]`, picks row `level+32` of the **SHADE
TABLE** when it is negative and row `level` of the **LIGHT TABLE** when it is
not, and remaps every pixel in the rectangle through it. The two tables are
allocated at `0x4BA610` and `0x4BA660` under the literal tags `"SHADE TABLE"`
and `"LIGHT TABLE"`, and they are the shipped `palettes/PALETTE.SHD` and
`palettes/PALETTE.LHT`.

1. **A greyed control is darkened, not drawn dim.** `0x4A5A9E` tests the
   gadget's greyed flag (bit 0 of the byte at `gadget+0x148`, the flag
   `0x4A1200` sets and §19 found the order panel using) and runs the gadget's
   whole rectangle through level **-20** — SHADE row 12, a measured 0.82x on
   luminance. This is the mechanism §19 recorded as "RWE has no disabled state
   ... recorded here so it is not mistaken for the original's behaviour".
2. **The selected row of a list box is brightened.** `0x4A1FAE`, reached when
   the row index equals the box's selected index at `gadget+0xBA`, runs the
   row's rectangle through level **+30** — LIGHT row 30, a measured median of
   1.84x with the lift largest on dark pixels and none at all on white. It
   runs *after* the row's text, so the text is brightened with the
   background. (A row the list has separately marked — a byte array at
   `gadget+0xD6`, or a caption beginning with the literal `&G` — is instead
   darkened four times over, at levels -19, -20, -21 and -22.)
3. **The focused control shows a caret, and nothing else does.** The only
   comparison against the panel's focused-gadget index (`panel+0x64`) in the
   whole renderer is at `0x4A4F14`, in the text-box draw. When it matches,
   `0x4BE950` draws a one-pixel vertical line at the right-hand end of the
   text, from the text's top to two pixels past the font's height, in
   interface colour **9** — GUIPAL light blue, nearest-matching to palette 9,
   `(84, 84, 252)`. Buttons and list boxes draw no focus indicator at all.

Two smaller findings from the same pass, recorded because they are cheap to
port. A button's caption can carry a drop shadow: `0x4A59A4` tests bit 3 of
the gadget's attribs (`gadget+0x1B`, the dword before `colorf` at `+0x1F`)
and only when it is set draws the caption once at (x+1, y+3) in interface
colour 0 before drawing it in the gadget's own `colorf`
(`0x4A59A9`-`0x4A59E3`). The shipped menu buttons do not set it. And if the
gadget's `quickkey` character appears in its caption, `0x4A5B2C`-`0x4A5CFC`
find it with `strstr`, measure the caption up to it and the character itself
in the font's per-glyph widths, measure the font's `I` (`0x4A5CB7`, its
height plus two), and draw a line under the character with the line routine
`0x4BE950` in interface colour 2 (`[cfg+0x8B4]`), GUIPAL green: from the
character's left edge to its right edge less one, at y + height(`I`) + 1.

### What RWE does with all this

Done: the hovered unit is the subject, metal at one decimal and energy at
none, all four rates clamped at zero, the kills line under the damage bar with
`- Veteran` from the fifth kill, the mission line from a transcription of the
two tables, and the second name-and-bar slot showing the current order's
target unit with its health — with `Weapon` and the percentage taking priority
for a launcher, which is now a restoration rather than an addition. The
minimap coverage ring is drawn, dashed while the launcher has a round, and
both it and the four detection rings are clipped to the minimap. The text box
draws the blue caret only when it has the focus, and a list box's selected row
is brightened rather than washed with 12% white. A button's caption carries
the drop shadow only where its attribs ask for it and its quick key is
underlined in interface green, and a greyed control is drawn
greyed rather than removed — see the note below the list.

Deliberately different, and recorded in §88 rather than left to be found:

- **RWE has no palette, so the brightening is an alpha blend.** The 1.84x
  median of LIGHT row 30 is reproduced on a mid-tone by compositing white at
  45%; the original's per-index behaviour (a dark pixel lifted 3.8x, a white
  one not at all) cannot be reproduced without carrying palette indices
  through the UI renderer, which nothing else would use.
- **`antiweapons` is still not parsed.** The original gates the coverage ring
  on FBI flags bit 29; RWE gates it on the unit having an `interceptor`
  weapon. In the shipped data the two sets are identical — `ARMAMD` and
  `CORFMD`, `AMD_ROCKET` and `FMD_ROCKET` — so nothing shipped can tell the
  difference, but a mod could.
- **A builder walking to its site already says `Nanolathing`.** The original
  would be running a move mission and saying `Moving`; RWE has one
  `BuildOrder` covering the walk and the work.
> **Ported, 2026-09-10.** This list used to end with a bullet saying greying,
> the caption shadow and the quick-key underline were all still unported. All
> three are in.
>
> Greying went in on 2026-09-02 and the bullet was stale when it was written;
> §19 now carries the note, including why RWE's greyed face is the artwork's
> own frame rather than SHADE row 12.
>
> The other two are `UiStagedButton::render`. The shadow is drawn at (+1, +3)
> in black — interface colour 0, as a literal with the slot named, RWE having
> no runtime interface-colour table — for a button whose gadget sets attribs
> bit 3 (`GuiButtonAttrib::CaptionShadow`, passed on by both of `UiFactory`'s
> button builders). The quick-key underline is a one-pixel `fillColor` in
> (0, 128, 0), interface colour 2 on screen, under the first occurrence of the
> gadget's `quickkey` character in the caption, measured with
> `findCharacterInText` in the font's own per-glyph advances so that it lands
> under the character `drawText` actually drew, at textY + bottom(`I`) + 1.
>
> Two things had to be settled that the finding does not record. **The
> alignment**: RWE's button draws its caption through three paths (left,
> centred, bottom-centred), and the shadow and the underline have to follow
> whichever one the gadget uses, so `render` now works out the caption's
> origin once — reproducing what `drawTextCentered` and `drawTextCenteredX`
> compute internally, rounding included — and all three draws go from that.
> The pressed shift survives the move, `round(a + 1)` being `round(a) + 1`.
> **The case**: the original compares the `quickkey` byte against the caption
> as it stands, and the shipped gui files are authored to suit — SKIRMISH.GUI's
> `SelectMap` carries a lowercase `e` for `Select Map`. RWE has folded the
> quickkey to an SDL keycode by the time a button holds it, so the original's
> case is gone and the match is case-insensitive. That is not only the
> available reading but the better one: four shipped gadgets are authored in
> the *other* case and would lose their underline to an exact test —
> MISSION.GUI's `SELECT`, whose key is `L` against `Select Mission`, and the
> `UNDO` buttons on MUSICRT, SOUNDSRT and VISUALRT, whose key is `c` against
> `Undo Changes`.
>
> **Corrected, 2026-09-11** (`2fc621c0`). A play-test against the original
> showed both of these wrong as first ported: no shadow under the menu
> captions, and a green underline. The listing agrees on both. The first
> reading of `0x4A59A4` missed the attribs test in front of the shadow, and
> took the underline for the caption's colour where `0x4A5CD7` loads
> interface colour 2. Measured from the font's `I`, the line also sits one
> row lower than first ported for hattfont12.

---

## 100. Two shadow passes: a unit's shadow is a copy of its own sprite, a building's is a projection

Prompted by Jon Mavor's 2012 account of the engine he wrote
(`mavorsrants.blogspot.com`, "Total Annihilation graphics engine"), which says
unit shadows "simply took the cached texture and rendered it offset from the
unit ... that used a darkening palette lookup", while for buildings, "due to
their tall spires and general complexity I decided to go ahead and properly
project the shadows". Recollection fifteen years after the fact is not evidence,
so this section is what the binary says. **It says he remembered it right.**

### The two passes and how a unit is sorted into one

`0x459288` opens the pass on the global options word, `WORD [0x511DE8]+0x37F06`:

```
459288  mov  ecx,[0x511de8]
45928e  mov  ax,WORD PTR [ecx+0x37f06]
459295  test al,0x4                     ; bit 2, Shadows
459297  je   0x45935c                   ; ... off: nothing at all
4592a0  mov  ecx,[ecx+0x92]             ; the unit definition
4592a6  mov  ecx,[ecx+0x241]            ; flags word A
4592ac  test ecx,0x2000000              ; bit 25, noshadow
4592b6  jne  0x45935c
4592bc  test BYTE PTR [ebp-...+0x113],0x20   ; unit+0x113 bit 5 picks the path
4592c6  je   0x459324                        ; ... clear: the vehicle path
```

The clear branch gates again, on a **second** option bit:

```
459324  shr  al,0x3 / test al,0x1       ; bit 3, VehicleShadows
459329  je   0x45935c
45932b  test DWORD PTR [esp+0x10],0x81000    ; flags A bits 12 and 19
459333  jne  0x45935c
459338  call 0x45a470
```

So the original has **two** shadow switches where RWE has one, and §78's
registry table already listed them: bit 2 `Shadows`, bit 3 `VehicleShadows`,
bit 4 `FeatureShadows`. Turning vehicle shadows off leaves the buildings
casting. All VERIFIED.

### `0x45A470`, the vehicle path: it copies the unit's own bitmap

The routine copies the source drawable's header — width at `+0`, height at
`+2`, then `+4`, `+6` and the byte at `+8` — and then moves the pixel plane
across wholesale:

```
45a4ae  mov  cx,WORD PTR [eax]          ; width
45a4b3  mov  dx,WORD PTR [eax+0x2]      ; height
45a4b7  mov  esi,[eax+0x10]             ; source pixels
45a4ba  imul ecx,edx
45a4c0  mov  edi,[edx+0x10]             ; destination pixels
45a4c8  rep movsd                       ; width*height bytes, verbatim
45a4cf  rep movsb
```

There is no transform anywhere in it. **A unit's shadow is a byte-for-byte copy
of the bitmap the unit was already cached into**, drawn at an offset — which is
exactly the "offset the cached texture" account, and it means a vehicle's shadow
is its own silhouette, undistorted, whatever the unit's height. VERIFIED.

### `0x45A790`, the building path: an extent, filled flat

The other branch computes a size through `0x45A510` — four pointer arguments,
whose results are written straight into the new drawable's width, height, x and
y words — and then fills it:

```
45a7f2  imul ecx,[esp+0x14]             ; width * height
45a7f7  mov  al,[edx+0x8]               ; the drawable's own byte
45a7fd  mov  bl,al / mov bh,bl          ; replicated into all four bytes
45a805  shl  eax,0x10
45a80d  rep stos DWORD                  ; fill the pixel plane
45a815  rep stos BYTE
45a82d  rep stos DWORD (eax = 0)        ; and clear the height plane at +0x14
45a834  rep stos BYTE
```

**A building's shadow is a projected rectangle filled with a single palette
index** — one value, which is why Mavor could RLE it "since it's all the same
intensity", and why cutting the building's own shape out of it was worth doing
separately. Both paths then composite through `0x4B8500`, the recursive drawable
blitter, at a screen x biased by `+0x85`. That is an x and not a y: the unit
itself is drawn by the same blitter at `+0x80` (`0x4597BA`), which is the
128-pixel side panel, and its y carries the `+32` of the top bar. VERIFIED.

### What RWE does instead

`shaders/unitShadow.vert` used to have **one** path for both: every vertex
flattened onto the ground plane at `groundHeight` and sheared by
`(y - groundHeight) * 0.25` in +x and -z. That is the building treatment applied
to everything — RWE gave a tank the shadow the original reserves for a factory,
and a tall unit's shadow stretched where the original's would not.

**It has both passes now.** A model that is not mobile keeps that projection;
a mobile one is displaced by a single vector instead, which carries its
silhouette across unchanged because the world projection is orthographic, so a
constant translation in world space is a constant translation on screen — which
is what blitting the cached bitmap amounts to. The pass already ran with the
depth buffer off and wrote only the stencil, so nothing else had to agree about
where the geometry sits. The gate is the unit's mobility, which is exactly
`unit+0x113` bit 5: that bit is `bmcode == 0`, set at unit creation from
`def+0x22F` (`0x485A8B`), and on the Feature Unit that draws map features
(§3, "The nanoframe's shadow", has the whole pass). A mobile unit that hovers
or floats casts no shadow at all. The second option bit is in too,
as the `vehicle-shadows` key in `rwe.cfg` — not as a button, because VISUALRT
has exactly one shadow gadget and RWE already wires it to the master.

**The offset is decoded, as of 2026-09-11.** The unit's own bitmap goes to
`0x4B8500` at `(sx + 0x80, syUnit)` from `0x4597BA`, and the vehicle shadow's
copy at `(sx + 0x85, syGround)` from `0x45933D`, where

```
sx       = int(unit.x - camX)
syUnit   = int(dz) - int(unit.y) / 2 + 32
syGround = int(dz) - h / 2 + 32          ; h = 0x485070(&unit.pos), the ground
```

So a unit's shadow is its silhouette **five pixels to the right**, at the
height of the ground under it: nothing more for anything that drives, and for
an aircraft the silhouette lands on the ground below it and walks away as it
climbs, which is the behaviour everyone remembers. RWE lowers every vertex by
the unit's height above the ground (the camera's own `y / 2` does the halving)
and adds 5 to x. It used to take the displacement from the top of the model
instead, down-right by a quarter of the model's height, which looked plausible
on paper and in play made a commander look as if it were floating. That
reading had been chosen before this decode, to keep a visible crescent past
the unit; the original shows much less of its unit shadows than that.

One thing is still RWE's own: the **darkening**, a screen fill of black at 70%
alpha through the stencil, where the original's is a palette lookup on a single
fill index.

One thing this does **not** settle, and it is not guessed at here: which table
the darkening lookup uses (`0x4B8500` is a tree walk, and the blit it reaches
was not followed). What `unit+0x113` bit 5 means, left open here until
2026-09-11, is settled: it is "is a building or a feature".

### And a measurement of the third palette table

`PALETTE.LHT`, `[display+0xC8]`, the `LIGHT TABLE` that §09 of
`TOTALA-EXE-SHADING.md` records as not used by the shading path: measured
against `PALETTE.PAL` the same way `PALETTE.SHD` was, it is **identity at row 0
and brightens monotonically to 1.77x at row 31**, and it never darkens at any
row. It is a lighten-only ramp, complementary to SHD's 0 to 1.807, and §99's
interface brightening already uses it. So it is not a candidate for the shadow
darkening.

---

## 101. The purple halo on buildings, and what stood for transparent

A bug of the original's, reproduced on purpose. Jon Mavor names it as his own in
the 2012 post that prompted §100: "Ever notice that a lot of the buildings have
a weird purple halo? Basically the table broke when dealing with the edge and
transparency because I didn't have a correct way to represent that."

The mechanism he describes is the building anti-aliasing: "for the non-animating
part of the building I would allocate a buffer that was double the size in each
dimension. I rendered the building at this larger size and then anti-aliased
that into the final cache", and the filter is a table, applied twice — "a lookup
on the top two pixel and the bottom two pixels. The results from those two ops
were then looked up to give me the final color, so 3 lookups." At the silhouette
the pairs being averaged are a real colour and whatever stood for transparent,
and what came back was wrong.

**And note what it is applied to: a building's cached bitmap, and nothing
else.** Not the map. The original has no supersampled world buffer at all --
the double-size buffer is allocated per building, filtered once, and cached,
which is the whole reason a DONT_CACHE piece never gets a halo (§12a of
`TOTALA-EXE-SHADING.md`, and the arms of a metal extractor in practice). RWE
reaches the same arithmetic from the other end: it renders the *whole world*
at twice the size and filters the lot down, which gives buildings the
original's filter for free and gives the ground a filter the original never
applied to it. That was reported from play as "the terrain just looks blurry",
and it is a fair description of what averaging four samples of a
palette-indexed map does -- the mean of four `PALETTE.SHD` reads names a
colour the palette does not contain, so the crispness the ground is drawn
with goes.

As of 2026-09-09 the filter is **selective, and selects what the original
selected**. A 2x2 block is averaged if a cached building piece covers any of
it -- its silhouette included, which is where the halo comes from, so the two
features necessarily agree about which blocks those are -- and otherwise the
block takes a single sample, which is the pixel a render at native size would
have produced. The ground and the units are therefore left alone, as they
were in 1997. The mask the halo already reads carries the flags: alpha is the
occluder level it always was, and green is the ground.

One switch sits on top of it, `anti-alias-units` (VISUALS page), which puts
units, nanoframes and features back into the filter for anyone who wants
smooth edges on them more than they want the original. It shipped off, the
faithful setting, and was turned on by default on 2026-09-11: in play, sharp
units beside smoothed buildings looked like a fault rather than like 1997. The
ground stays out either way: the blur there was never anti-aliasing, it was a
box filter over a texture that cannot survive one.

**A finished building's dont-cache piece follows the building, not the
switch** (2026-09-11). It used to go into the mask as an ordinary occluder, at
0.5, so a metal extractor's spinning top went sharp or smooth with the units.
It has a level of its own now, 0.7: `building()` in `worldPost.frag` (above
0.6) decides the filter and `cached()` (above 0.85) still decides the halo, so
the top is smoothed with its extractor and still carries no fringe. Strictly
the original drew that piece straight to the screen unfiltered, so this is a
second deliberate divergence beside the switch; the player's reading, that an
extractor's top is part of a building, is the one kept.

So the direction of travel is worth stating plainly, because it is the
opposite of what it looks like: **this removed a divergence rather than
adding one.** RWE was anti-aliasing three things the original anti-aliased
none of, and it now anti-aliases the one thing it did.

**Which table is INFERRED.** `PALETTE.ALP` is the only 256x256 source-by-
destination blend table in the shipped data, it is installed at `[display+0xC0]`
as the anti-alias blend table (`TOTALA-EXE-SHADING.md` §09), and the cloak
composite already goes through it (`0x4B8500` → `0x4CBF2C`). The anti-aliasing
code path itself was **not** followed in the binary, so the identification rests
on there being nothing else it could be.

**Which index stood for transparent is measured.** Blending every palette entry
against each of the 256 possible partners through the shipped `PALETTE.ALP`, and
scoring each partner by how purple it makes ordinary building colours — the mean
of `(R + B) / 2 − G` over every entry of luminance 40 or more — ranks them:

| partner | the index itself | mean result | purpleness |
|---|---|---|---|
| **253** | **(255, 0, 255)** | (158, 108, 169) | **+55.2** |
| 252 | (0, 0, 255) | (55, 70, 186) | +50.7 |
| 5 | (128, 0, 128) | (117, 76, 119) | +41.6 |
| 249 | (255, 0, 0) | (172, 76, 58) | +39.2 |
| 1 | (128, 0, 0) | (123, 65, 58) | +24.9 |
| 0 | (0, 0, 0) | (61, 64, 56) | −5.9 |

**253 wins, but be honest about the margin: pure blue is close behind it.** The
score alone would not settle this. What settles it is that 253 is (255, 0, 255),
plain magenta, which is the conventional colour key and is not a colour any of
the artwork uses, while 252 is pure blue and appears in the artwork all over the
place. A transparency sentinel has to be a colour nothing legitimately is.
Index 0 is black and averages to grey, which is what a sentinel of 0 would have
produced and is not what anybody saw.

An earlier version of this section put 253 at +40.2 against a runner-up of
+25.8, and put index 1 at −8.0 — "comes back reddish rather than purple". Those
figures came from a reconstruction of the table rather than from the shipped
file and are withdrawn; index 1 is quite purple, at +24.9. The identification of
253 survives, on the argument above rather than on the gap.

Worked values, read straight out of `palettes/PALETTE.ALP`:

```
grey  (128,128,128) idx   7 + 253 -> index 148 = (167,123,179)   light purple
white (251,251,251) idx  80 + 253 -> index 146 = (211,171,215)   pale lilac
blue  ( 39, 63, 87) idx 120 + 253 -> index 150 = (119, 75,143)   violet
green (  0,255,  0) idx 250 + 253 -> index 248 = (128,128,128)   grey, not purple
```

That is the whole of the bug, and it is subtler than "the snap has nowhere to
go". The table's answers are perfectly reasonable *blends* — a grey averaged
with magenta really is about (167,123,179). They are simply the wrong thing to
put there, because nothing at that edge was magenta; magenta was the absence of
the building. So the building acquires a rim of plausible-looking purples it has
no business having, and because most buildings are painted in greys and metals,
most of those rims land in the same part of the palette. That is why it reads as
one effect — "a weird purple halo" — rather than as random noise. The green row
is the tell: a green edge comes back plain grey, so this is not a purple filter,
it is an average with a colour that should never have been in the average.

### What RWE does with it

Runs the same arithmetic, rather than painting on an impression of the result.

RWE already renders the world into a buffer of exactly twice the size in each
dimension when anti-aliasing is on (`worldRenderTextureScale`), which is the
same shape as the original's "buffer that was double the size in each
dimension". So every output pixel is one 2x2 block of a supersampled buffer,
and the original's filter applies to it directly.

What was missing was the palette. A second pass draws the finished buildings
into a mask at the supersampled size — but writing each fragment's **palette
index**, not coverage (`unitMask.frag`; the index atlas it reads already exists
for the `PALETTE.SHD` shade lookup, so this costs nothing new). `worldPost.frag`
then filters that 2x2 block down with three chained lookups through
`PALETTE.ALP`, in the original's order: the top pair, the bottom pair, and then
those two results. Any sample the mask says was uncovered stands in as index
253. At the silhouette the pairs being averaged really are a real colour and
whatever stood for transparent, so the wrong colour falls out of the shipped
table instead of being chosen — the same wrong colour, from the same table, by
the same three operations.

Two consequences, and both are how you tell this apart from an approximation:

- **The colour is per pixel.** A dark panel edge comes back near-black purple,
  a lit edge comes back magenta, a pale edge comes back lilac, and a green edge
  comes back grey. A single averaged constant — which is what RWE painted on
  before, (158,109,171), the mean of the row — is right only for the greys.
- **It is a rim, not a glow.** Only a *mixed* block gets it: 1, 2 or 3 of the
  four samples covered. An all-covered block is ordinary anti-aliasing, which
  RWE's supersample already does, and an all-uncovered block is terrain. So the
  artefact sits on the building's outermost pixels the way the original's sat
### Which pieces get it: "the non-animating part of the building"

Mavor's sentence is precise and it names a mechanism RWE had already decoded
under another heading. He anti-aliased **"the non-animating part of the
building"** — and the thing that decides which part that is, is the COB
`CACHE` / `DONT_CACHE` state (`TOTALA-EXE-SHADING.md` §12a, §15 item 8). The
original keeps a finished unit in a cached bitmap rendered by the shaded
rasterizer; a piece the script has marked `DONT_CACHE` is left out of that
bitmap and drawn straight to the screen each frame by the unshaded twin. 137 of
the shipped scripts use it, mostly on the pieces that move — turrets, lab arms,
radar dishes.

So the anti-aliasing and the halo it produces are properties **of the cache**,
not of the building. A piece that is not in the cache never passes through the
table and therefore never acquires a halo, no matter how hard its edge contrasts
with what is behind it.

That is directly observable, and it is what caught the divergence: a metal
extractor in the original has the fringe along its base and pad and **not** on
the arm and counterweights on top, which are exactly the pieces its script keeps
out of the cache. RWE haloed the whole model until 2026-09-09 and now passes
`cachedPiecesOnly` when it builds the halo mask, skipping any piece whose
`UnitMesh::cached` is false. `tools/visual-test.ps1 -phase mex` is the
regression test: base fringed, top clean.

The pleasing part is that no new decoding was needed. `DONT_CACHE` was read out
of the binary for the *shading* — a dont-cache piece is not shaded, because the
unshaded rasterizer draws it — and the same flag turns out to answer "which part
is the non-animating part" exactly. Two of Mavor's sentences, three years of
his engine apart, describing one list of pieces.

### What is RWE's own

Three things, recorded rather than hidden.

The first is a strength setting, `building-halo-strength`. 100 is the original,
where the filtered pixel simply is the pixel; lower values blend back towards
RWE's own rendering. There is deliberately **no width setting**: the artefact is
one output pixel wide because it is a 2x2 downsample, and widening it would mean
inventing pixels the original never drew. What that costs is honest to state —
TA drew its buildings smaller in pixels than RWE does, so one pixel was a larger
share of a building then than it is now, and the effect is correspondingly
subtler here than it was on a 640x480 screen. Matching the *proportion* instead
would mean a wider rim and a less faithful mechanism; the mechanism was chosen.

The second is a colour correction, `building-halo-saturation` (65) and
`building-halo-red-shift` (50), applied to what the table returns. A play-test
of the exact colours asked for something less saturated and redder, and that is
a fair thing to want rather than a fudge: a palette index is not a colour until
something displays it, and TA's were displayed on a 1997 CRT through a hardware
LUT, where phosphor, a warmer white point and the gamma of that path all pull a
saturated blue-purple towards a duller red-magenta. None of that is in the data.
So the arithmetic above stays exact and the correction sits in one place, off at
100 and 0. The red shift pulls blue down towards green rather than reordering
the channels, and that detail matters: the two harshest colours the table
returns are black-with-magenta (128,0,128) and magenta-with-itself (255,0,255),
and both have red and blue *exactly equal*, so any correction phrased as "send
the larger of red and blue into red" leaves precisely the pixels that most need
it untouched.

The third is inherent to reproducing this from a screen-space mask, and is the
one that took a second pass to get right.

The original anti-aliases each building's bitmap **in isolation** — nothing else
is in the buffer when the filter runs — and then paints whatever stands in front
over the top. RWE's mask is drawn depth-tested against the finished world, so
anything in front of a building *removes* the building's samples where it
covers it. The rim of that gap is a boundary, and a boundary is all the post
pass can see, so it drew a fringe there: a line **inside** the model, along the
outline of whatever was in front.

That is not a corner case. A unit's own dont-cache pieces are in front of its
own cached ones, so a metal extractor drew a purple line where its rotating arm
crossed its base, and the line crawled as the arm turned. It was reported from a
play-test within a day of the halo first working.

The fix is to give the mask a third state. Every sample is now one of: nothing
(cleared, alpha 0), an **occluder** (alpha 0.5), or a cached building piece
(alpha 1). A block gets a halo only when it straddles the outer edge of
everything solid — 1, 2 or 3 of its four samples solid — **and** every solid
sample in it is a cached piece. So:

- an arm crossing its own base is four solid samples, no boundary, no halo;
- the arm's own outer edge against the ground is a real boundary, but its
  samples are not cached, so it gets nothing, which is also what the original
  does;
- the building's own outer edge is unchanged.

What goes in as an occluder: every dont-cache piece of a haloed building, every
mobile unit and nanoframe entire, and every modelled map feature. **Two things
still do not**, and they are the remaining way to see a fringe inside a model:
a *billboard* feature is a sprite with no mesh to walk, and the terrain is not
drawn into the mask at all, so a building partly hidden by a cliff would be
fringed along the cliff line. Neither has been seen in play; both are fixable
the same way, by drawing them in at 0.5.

**And the occluders have to actually land, which took a second attempt.** They
were written correctly and then thrown away, because `GameLaunch` enables
blending once for the whole program with
`GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA` and nothing in the render path ever
turns it off. The mask is data, not a picture, and compositing it is nonsense —
but the failure is quiet and, worse, *selective*: a cached piece asks for alpha
1 and blends to 1, so it comes through perfectly, while an occluder asks for 0.5
and lands as `0.5*0.5 + 0.5*0 = 0.25`, which fails the "is this sample solid"
threshold. Every occluder was discarded, every hole stayed open, and the fringe
went on crawling inside the model exactly as before — with the fix apparently
in. The red channel was being halved too, and half of a palette index is a
different colour rather than a darker one. `disableBlending` around the pass is
the whole of it. The general lesson is the one about `GL_LESS` again in a
different suit: this pass writes *values*, not pixels, and every piece of
pipeline state that quietly interpolates or combines them has to be turned off
deliberately rather than left as whatever the last draw wanted.

**How to test it, since no single frame can.** A still cannot show a crawling
line. `tools/visual-test.ps1 -phase mex` takes two frames three seconds apart of
a metal extractor that does not move but whose arm does, and the test is the
diff: a halo pixel present in one frame and absent in the other, inside the
model, is the bug. Beware two false positives that will otherwise convince you
it is still broken. The map's own sand carries pink speckle whose colour is
genuinely indistinguishable from the halo, and the counterweights throw a shadow
that sweeps as the arm turns; both move or match on colour and neither is the
halo. Marking the differing pixels onto a magnified frame and looking at where
they fall separates them in one glance, where counting them does not.

### How the mask is filled, and what it costs

The mask is a **second render target of the passes that already draw the
world**, not a pass of its own. `unitTexture.frag` writes it alongside the
colour it was writing anyway; `mapTerrain.frag` and `unitBuild.frag` write an
occluder value the same way. `glDrawBuffers` turns the second target on for
exactly those passes and off for everything else, because a particle or a flash
must not touch coverage and its shader does not declare that output at all.

That arrangement fell out of noticing that a separate pass was re-deriving two
things the first pass already had in hand: the texel's palette index, which
`unitTexture.frag` samples anyway for the shade lookup, and which surface is in
front, which the depth test had already settled. It also disposes of two whole
classes of bug at once -- there is no second set of matrices to disagree with
the first, so `GL_EQUAL` is not needed and cannot be forgotten, and blending is
disabled once for that attachment with `glDisablei(GL_BLEND, 1)` rather than
around each pass, so a pass that forgets cannot write quarter-alpha samples that
read as empty. Both of those had already cost a day each.

Measured on `battle_test --map "Coast To Coast" --units 200 --unit-type CORAK`,
per unit mesh, because a run drifts and the runs saw 3299, 3625 and 3000 meshes:

| per unit mesh | separate pass | halo off | second render target |
|---|---|---|---|
| draw calls | 3.19 | 2.18 | **2.21** |
| `w.unit.build` | 0.443 us | 0.245 us | **0.232 us** |

The separate pass cost about 740 us a frame and 3300 extra draw calls -- an 81%
rise in the unit build phase and 46% more draws. As a render target it costs no
CPU work and no draw calls at all: both figures are the halo-off baseline.

**What the remaining cost is, and why it is not quoted.** Writing a second
target is real GPU bandwidth, and it is below what this harness can resolve. A
battle_test run drifts by a third in how much is on screen, and two runs
measured `world` per mesh *lower* with the effect on than with it off, which is
impossible and says plainly that the timing noise exceeds the difference. The
draw counts are trustworthy where the microseconds are not: a count does not
drift with load, and 2.21 against 2.18 is the whole story. Anyone wanting the
GPU figure needs a fixed camera on a fixed scene, not battle_test.

It is still gated on a visible finished building -- a scan of positions and
bounding boxes, no mesh work -- so a field battle away from a base pays nothing
at all rather than paying a little.

Dropping the depth test instead would trade all of this for something worse — a
building hidden behind a hill drawing its outline across the hill — so the
depth-tested mask with occluders is the version that keeps.

**One trap, recorded because it cost two days, and now designed out.** When the mask was a separate pass it redrew geometry the world pass had already drawn, so it had to run under `GL_EQUAL` and not `GL_LESS`: the two shaders transformed with the same expression and the same matrix, so every fragment landed at exactly the depth already stored and `GL_LESS` rejected all of them. It did, and the halo was invisible at every setting because it was never drawn at all -- while "the original's is one pixel of a 640x480 screen, so of course it is too small to see" sat there as a ready and completely wrong explanation. An invisible effect is a broken effect until proven otherwise. Filling the mask from the world passes themselves removes the hazard rather than documenting it: there is no second set of matrices to agree with the first.

---

## 102. The ground path follower: an aim point on the segment, and two brakes into the corner

> **Ported, 2026-09-10.** Upstream issue #36, "a unit that overshoots a
> waypoint turns round and goes back for it". Three of the four things below
> are in. The corner the unit has left is kept in the waypoint list, so there
> is a segment to steer along at all — `PathFindingService::pathToWaypoints`
> no longer drops the start cell and `PathFollowingInfo` starts its iterator
> at `begin() + 1`. The aim point is projected onto that segment with the
> original's eighty-unit look-ahead. And the cosine speed factor is gone,
> replaced by the original's two brake tests, written in the original's
> operation order. The straight-line stand-in of §87 is two points now — the
> unit's own position, then the goal, which is what `0x44F3F2` writes — and it
> is walked on the tick the order arrives rather than the one after, because
> `Mover::Update` calls the navigator and then the follower in the same frame
> (`0x43DD28`).
>
> **One departure, and it is measured.** The waypoint advance radius stays at
> RWE's sixteen world units, and eight for the last one, rather than the
> original's five. Five is fine for a unit on its own and costs about a third
> of the arrivals in a crowd: at a hundred units in `path_bench`, five arrived
> 27 against sixteen's 49 and asked for 884 searches against 509, because a
> waypoint is a cell centre and a unit two cells across cannot always reach
> within five of one another unit is standing on — it circles until the
> collision repath rescues it. The original's answer to that is its blocked
> bit, which RWE has; RWE's units get in each other's way differently. With
> the other three in and sixteen kept, the same benchmark arrives 49 where the
> old follower arrived 45, and asks for 509 searches where it asked for 613.
>
> `followPath` in `src/rwe/sim/UnitBehaviorService.cpp` and `followSegment` in
> `src/rwe/sim/UnitBehaviorService_util.cpp`; the old `seek` and `arrive` are
> gone. `src/rwe/sim/pathfollowing.test.cpp` pins it.

§87 decodes the pathfinder and its scheduler and stops at the point where a
route has been handed to a unit; this is the other half, the per-tick step that
walks it.

The short answer is that the original **does** have a look-ahead — a pure
pursuit aim point projected onto the segment the unit is walking, with a
look-ahead of exactly 80 world units — and that it has two braking tests whose
whole job is to stop a unit arriving at a corner too fast to take it. What it
does not have is any test for *having passed* a waypoint. So the original can
turn round for one; it is arranged so that it rarely has to.

### Where it lives

The call graph from the mission to the arithmetic, all of it confirmed by
reading the calls rather than by inference:

| Address | Role |
|---|---|
| `0x4031D0` | ground mission **`Move`** — row 26 of the ground mission table at `0x4FC71A` (base `0x4FC490`, 25-byte records), display string `"Moving"` |
| `0x438930` | `Mission::SetPointGoal(&pos, radius)` — allocates a `PointGoal` and installs it on the navigator. `0x438943` is §87's `canfly` test: an aircraft gets no goal and no path |
| `0x44CF60` | `PointGoal::PointGoal(mission, x, z, radius)` |
| `0x43DD20` | `Mover::Update(unit)` — the per-tick entry. Calls `Navigator::Update` **first**, then the follower, then the collision/integration step |
| `0x44F1A0` | `Navigator::Update()` — the goal test and **the waypoint advance** |
| `0x43CD20` | **the ground path follower**, the subject of this section |
| `0x43D290` | the air follower (§1), chosen instead when `def+0x241` bit 11 (`canfly`) is set |
| `0x43CC20` | `Mover::Move(unit, accel)` — applies the acceleration, caps the speed, writes the velocity vector |
| `0x43D6D0` | integrate: add velocity to position, test the new map square, set the **blocked** bit |
| `0x43DA70` | pick the COB animation band and call `StartMoving`/`StopMoving`/`MoveRate1`..`3` |

`0x43DD20` in full, which is where the ordering comes from:

```
43dd24  ecx = [this]                      ; this+0x00 is the navigator
43dd28  call [[ecx]+0x08]                 ; Navigator::Update  (0x44F1A0)
43dd30  ecx = unit->def (unit+0x92)
43dd36  edx = def->[0x241] >> 11
43dd44  if (edx & 1) call 0x43D290        ; air
43dd4d  else         call 0x43CD20        ; ground
43dd55  call 0x43D6D0 (unit)              ; integrate + collide
43dd5d  call 0x43DA70 (unit)              ; animation band
43dd65  call 0x43DB50 (unit)              ; waterline / height
```

There is exactly **one** ground follower. Ships, hovercraft, tanks and kbots
all reach `0x43CD20`; the only branch anywhere in the movement dispatch is
`canfly`. `0x43CD20` has a single caller, `0x43DD4D`.

Three objects are involved and they point at each other in a cycle worth
writing down, because every offset below depends on it: `unit+0x00` is the
**mover**, `mover+0x00` is the **navigator**, and `navigator+0x08` is the unit.

### Where the path is kept

`Navigator`, vtable `0x4FD458` (the base-class table at `0x4FD428` sits
immediately before it in `.rdata`):

| Offset | Field |
|---|---|
| `+0x00` | vtable |
| `+0x04` | the goal object, or null |
| `+0x08` | the unit |
| `+0x0C` | **the waypoint array**: 20 entries of 4 bytes, `int16 x` then `int16 z` |
| `+0x5C` | waypoint count |
| `+0x60` | tick of the last path request (the 60-tick rate limit of §87) |
| `+0x64` | flags: bit 0 "I have a path", bit 1 "I want a path", bit 3 "dirty, send me" |

Vtable slots used below: `+0x04` `SetGoal` (`0x44F2A0`), `+0x08` `Update`
(`0x44F1A0`), `+0x0C` `GetWaypoints` (`0x44F150`), `+0x14` `HasPath`
(`0x44F290`), `+0x18` `WantsPath` (`0x44F260`), `+0x20` the three-waypoint
bit-serialiser (`0x44F4A0`).

**Waypoints are whole world units in an `int16`, not map squares and not
fixed point.** Two independent confirmations. `Navigator::SetGoal` writes the
*high word* of the unit's 16.16 position straight into a slot
(`0x44F3F9`: `mov dx, WORD PTR [ecx+0x6c]`, and `unit+0x6C` is the high half of
the 16.16 `x` at `unit+0x6A`), and `GetWaypoints` shifts a slot left by 16 to
hand it back (`0x44F178`: `shl ebp,0x10`). And the pathfinder's emitter builds
each corner the same way at `0x40E11F`–`0x40E13A`:

```
world = (2 * cell + footprint) * 8     ; i.e. cell*16 + footprint*8
```

which is `PointGoal::GetPosition`'s formula (`0x44D2E1`: `(footprint + 2*g) << 19`,
`<<19` being ×8 in 16.16) written for integers. A waypoint is therefore the
world position of the **centre of the unit's own footprint** placed on that map
square, and `>>4` on it gives the square index — which is what `SetGoal`'s
ladder does at `0x44F301` before asking the goal whether the path's tail is
already good enough.

The array is walked as `wp[j]` at `nav + 0x0C + 4j`, `j = 0 … count-1`. **`wp[0]`
is the corner the unit has left and `wp[1]` is the corner it is heading for.**
`SetGoal` makes that explicit for the straight-line stand-in of §87: at
`0x44F3F2` it sets `count = 2`, `wp[0] =` the unit's own position, `wp[1] =`
the goal.

The mover, for the fields the follower touches:

| Offset | Field |
|---|---|
| `+0x00` | the navigator |
| `+0x08`…`+0x13` | velocity, three 16.16 world units per tick |
| `+0x20` | **speed**, 16.16 world units per tick |
| `+0x24` | the turn applied this tick, a signed 16-bit angle |
| `+0x2A` | tick the unit last changed square or mode (`0x43D865`) |
| `+0x2E` | flags; **bit 2 is "blocked"**, set at `0x43D92C` from the footprint test `0x47DB70` (§95's third reader) |

And the unit, adding to §82's table: position `unit+0x6A` / `+0x6E` / `+0x72`
(16.16 x, y, z; the high words at `+0x6C`, `+0x70`, `+0x74` are read directly as
`int16` world units in several places), occupied map square `unit+0x76` / `+0x78`
(`int16`), footprint in squares `unit+0x7E` / `+0x80` (`int16`), the unit
definition at `unit+0x92`.

### The waypoint advance — `0x44F1A0`

`Navigator::Update` runs before the follower every tick and does two things.
The second is the advance:

```
44f1d7  edi = this->count
44f1da  if (count < 2) goto repath_check
44f1df  edx = this->unit
44f1e2  eax = (int16) this->wp[1].z          ; nav+0x12
44f1e6  ecx = (int16) unit->z                ; unit+0x74, the high word
44f1ea  edx = (int16) unit->x                ; unit+0x6C
44f1ee  ebp = (int16) this->wp[1].x          ; nav+0x10
44f1f2  ecx -= eax                           ; dz
44f1f7  edx -= ebp                           ; dx
44f1fb  ebp = dx*dx
44f200  edx = dz*dz
44f203  ebp += edx
44f205  cmp ebp, 0x19                        ; 25
44f208  jg repath_check                      ; not there yet
        ; pop wp[0]: memmove wp[1..count-1] down to wp[0..count-2]
44f223  count--
44f22c  if (count < 2) clear bit 0           ; "I have a path" goes away
44f235  set bit 3
```

**The rule.** When the unit is within **5 world units** of the corner it is
heading for — `dx² + dz² ≤ 25`, computed on the truncated integer positions —
the corner behind it is dropped and the list shifts down by one. Exactly one
waypoint is dropped per tick; there is no loop, so a unit cannot skip two
corners in a tick however fast it is going.

Five world units is under a third of a map square. It is a small number and it
is meant to be: nothing else in the original ever advances the list.

There is a `Navigator::PopN(n)` at `0x44F100` that would drop several at once.
Nothing calls it — it is in no vtable, `xref.py` finds no data reference, and
the listing contains no `call 0x44f100`. It is dead code.

**There is no "have I passed it" test anywhere.** No dot product against the
segment, no plane through the waypoint, no skip-ahead. The advance is a radius
and nothing else.

The first thing `Update` does, before the advance, is the goal test:

```
44f1a5  ecx = this->goal
44f1aa  if (goal == 0) skip
44f1b2  if (goal->IsSatisfiedBy(unit))       ; vtable +0x10
        {
44f1bc      Notify(goal, 0x20)               ; mission+0x4E |= 0x20, "arrived"
44f1c8      if (!goal->vf2C())                ; 0x44CEF0, returns 0 for a PointGoal
44f1d4          this->SetGoal(NULL)
        }
```

and the last thing is the repath decision (`0x44F239`), which confirms §87's
three triggers and adds nothing: a goal exists **and** (the mover's blocked bit
is set **or** the waypoint count has fallen below two). Grepping every
instruction in `.text` that ors or ands `[reg+0x64]`, and every `mov` into it,
finds writers at `0x44F0AE`, `0x44F0B1`, `0x44F0E9`, `0x44F0EF`, `0x44F13A`,
`0x44F231`, `0x44F251`, `0x44F2DA`, `0x44F2E1`, `0x44F2E8`, `0x44F313`,
`0x44F3D4`, `0x44F41B`, `0x44F441` and `0x44F55C` — every one of them inside
the navigator's own four methods. **Nothing outside the navigator can ask for a
path**, so there is no "too far from the route" re-request and no stuck timer
feeding one. `mover+0x2A` is stamped with the tick the unit last changed square
but no reader of it turns up in the mover, the navigator or the follower.

### The aim point — `0x43CD20`, first half

The follower opens by asking whether there is a path at all:

```
43cd2d  ecx = this->navigator
43cd31  call HasPath()                        ; vtable +0x14
43cd36  if (!HasPath) {
43cd3c      this->turnDelta = 0
43cd46      eax = -def->brakerate              ; def+0x19A
43cd52      Move(this, unit, eax)              ; 0x43CC20
            return
        }
```

**A unit with no path brakes at `brakerate` and does not turn.** That is the
whole of the stopping behaviour; see "Arrival at the goal" below.

With a path, it asks for three waypoints:

```
43cd61  ecx = this->navigator
43cd63  push 3 ; push 0 ; push &buf
43cd6e  call GetWaypoints(&buf, 0, 3)          ; vtable +0x0C = 0x44F150
```

`GetWaypoints` writes three 12-byte vectors — `(x<<16, 0, z<<16)` — and clamps
the index it reads with `index = min(i, count-1)` (`0x44F16A`–`0x44F172`). So
with a two-point path, `buf[2]` repeats the goal; with a longer one, `buf[2]` is
the corner after next. Name them `prev = buf[0]`, `next = buf[1]`,
`after = buf[2]`.

Then the aim point:

```
43cd75  dx = next.x - unit.x
43cd8c  dz = next.z - unit.z
43cda7  dist = hypot(dx, dz)                   ; 0x4FB440 then ftol at 0x4E43A0
43cdb4  if (dist <= 0x500000) goto aim_at_next  ; 80.0 in 16.16

43cdc3  sx = next.x - prev.x
43cdd5  sz = next.z - prev.z
43cdf4  seglen = hypot(sx, sz)
43ce01  if (seglen < 0x10000) goto aim_at_next  ; 1.0 world unit

43ce1f  ux = (sx << 16) / seglen               ; unit vector along the segment
43ce3b  uz = (sz << 16) / seglen
43ce55  back = min(dist - 0x500000, seglen)
43ce73  aim.x = next.x - ((ux * back) >> 16)
43ce99  aim.z = next.z - ((uz * back) >> 16)

aim_at_next:
43cebc  aim = next
```

**The rule.** The unit does not steer at the corner. It steers at a point on the
segment it is walking, placed so that the aim point is always **80 world units
closer to `next` than the unit itself is**, and never further back than `prev`.

Put on the line, that is plain pure pursuit with a look-ahead of 80 units: a
unit sitting on the segment 300 units short of the corner aims at a point 80
units in front of it. Put off the line, it is a corridor: the aim point is the
point on the segment at distance `dist − 80` from the corner, so a unit that has
drifted sideways is steered back onto the line rather than at the corner, and
the further off it is the further back along the line it aims. Inside 80 units
of the corner the projection stops and the unit homes on the corner itself.

Eighty world units is five map squares. `0x500000 / 65536 = 80`, and
`0x10000 = 1.0`; both are immediates in the instruction stream, not table
lookups.

The `min(…, seglen)` cap means the aim point lies on the closed segment
`prev … next` and never behind `prev`, and the `seglen < 1.0` bail-out is what
stops a degenerate segment dividing by zero. On the tick a unit is given an
order, `prev` is its own position and `seglen` is the whole distance to the
goal, so the stand-in path steers 80 units straight ahead — which is correct
and needs no special case.

### The turn — `0x43CD20`, second half

```
43cf0b  ax = HeadingTo(&unit.pos, &aim)        ; 0x48A980 -> atan2 at 0x4B715A
43cf12  ax -= unit->heading                    ; unit+0x66, 16-bit wrap
43cf1e  err = (int16) ax
43cf8a  if (err == 0) { this->turnDelta = 0; goto speed }

43cf96  rate = def->turnrate                   ; def+0x1BA, a WORD
43cfa5  if (err >=  rate)  this->turnDelta = +rate
43cfb5  else if (err <= -rate) this->turnDelta = -rate
43cfc9  else                   this->turnDelta = err
43cfdb  unit->heading += this->turnDelta
43cfdf  unit->flags(+0x110) |= 0x10000
```

**The rule.** The heading error is clamped to ±`turnrate` and applied. That is
all: no damping, no proportional term, no separate rate for large errors. The
same three lines appear standalone at `0x43CBB0`, which is the helper the rest
of the engine uses to point a unit at something.

`turnrate` is parsed as a plain **integer** (`0x4C46C0`, `atoi`) and stored as a
`WORD` at `def+0x1BA` (`0x42C243`), so it is a raw 16-bit-angle step per tick:
65536 units to the circle, and an FBI `TurnRate=550` is 3.02° a tick, about 90°
a second. `maxvelocity`, `acceleration` and `brakerate` go through `0x4C4800`
instead, which is `atof(value) * 65536.0` truncated (the multiplier is the
double at `0x4FDC20`, and it is exactly 65536.0) — so all three are 16.16 and
**per tick**, with no further division by the frame rate.

### Accelerate or brake — the two tests

This is the part that matters most for #36, and it is the part RWE did not
have.

```
43d000  speed = this->speed
43d00e  edx:eax = |err| * speed
43d022  A = (|err| * speed) / def->turnrate               ; 16.16

43d03e  edx:eax = speed * speed
43d048  t = (speed*speed) >> 16                            ; speed² in 16.16
43d06c  B0 = (t << 16) / (2 * def->brakerate)              ; 16.16
43d080  B  = (B0 * B0) >> 32                               ; squared, integer units²

43d090  C  = 4 * ((A * A) >> 32)                           ; (2A)², integer units²

43d0a2  if (distToAim² > C && distToAfter² > B)
43d0ac      accel = +def->acceleration                     ; def+0x19E
        else
43d0b8      accel = -def->brakerate                        ; def+0x19A
43d0c0  Move(this, unit, accel)
```

`distToAim²` is `ebp`, the squared distance to the aim point above, computed at
`0x43CECC`–`0x43CF09`. `distToAfter²` is `[esp+0x1C]`, the squared distance to
`buf[2]`, computed at `0x43CF37`–`0x43CF7D`. Both are 32.32 products shifted
right 32, so both are in whole world units squared, as are `B` and `C`.

Written out, with the squares removed:

- **The corner test.** Accelerate only while `distance to the aim point > 2 × speed × |heading error| / turnrate`. The right-hand side is twice the distance the unit would cover in the number of ticks it needs to finish turning. It is a turn-radius test in disguise, and it is what makes a unit slow down for a corner in proportion to how sharp the corner is. A unit pointing straight at its aim point (`err = 0`) always passes it.
- **The arrival test.** Accelerate only while `distance to buf[2] > speed² / (2 × brakerate)`, the textbook stopping distance. Because `GetWaypoints` clamps its index, `buf[2]` is the **final** waypoint whenever the path has three points or fewer, so this is the brake into the destination; on a longer path it is a brake into the corner after next, which is a mild look-ahead of its own.

Failing either test brakes at `brakerate`. There is no partial throttle: every
tick the unit either adds `acceleration` or subtracts `brakerate`, and the
speed cap does the rest.

### The speed cap — `0x43CC20`

```
43cc2d  this->speed += accel
43cc37  if (this->speed < 0) this->speed = 0

43cc46  i = unit->pitch >> 11                  ; unit+0x68, 16-bit angle
43cc4d  i = clamp(i, -5, +5)
43cc61  pct = (int8) byte at 0x505205 + i
43cc76  cap = (pct << 16) * def->maxvelocity   ; def+0x192
43cc7c  cap >>= 16
43cc95  cap /= 100.0                           ; 0x640000

43cca8  if (unit->y_high < seaLevel && !(def->[0x241] & 0x81000))
43ccc4      cap = (cap * 0x8000) >> 16          ; halved

43ccd3  if (this->speed > cap) this->speed = cap
43ccde  velocity = (-sin(heading)*speed, 0, -cos(heading)*speed)
```

The slope table at `0x505200`, eleven signed bytes indexed `-5 … +5` from
`0x505205`, is:

| index | −5 | −4 | −3 | −2 | −1 | 0 | +1 | +2 | +3 | +4 | +5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| percent | 25 | 55 | 70 | 85 | 100 | 100 | 75 | 50 | 25 | 20 | 15 |

Each index step is `1 << 11` of a 16-bit angle, 11.25°, so the table covers
±56.25° of pitch and clamps beyond that. It is asymmetric on purpose: one
direction reaches 25% at the third step and the other only at the second-to-last.

`seaLevel` is the byte at `globals+0x1427F` in whole world units, compared
against the high word of the unit's `y`. The exemption is `def+0x241` bits 12
and 19 — `canhover` and `floater` — so a hovercraft or a ship keeps full speed
in the water and a submerged land unit is halved.

`0x4B70EF` and `0x4B7123` are sine and cosine of a 16-bit angle out of the
512-entry table at `0x509F00` (`(table[a] * r + 0x1000) >> 13`), and both
results are negated on the way into the velocity, which is why
`HeadingTo(&unit.pos, &aim)` at `0x48A980` computes `atan2(unit − aim)` rather
than `atan2(aim − unit)`: the two negations cancel.

`0x43DA70` then bands the speed for the animation — 0, or 1/2/3 by comparison
against `def->moverate1` (`+0x1AE`) and `def->moverate2` (`+0x1B2`) — and calls
the COB functions `StopMoving`, `StartMoving`, `MoveRate1`, `MoveRate2`,
`MoveRate3` (strings at `0x50523C`, `0x505230`, `0x50520C`, `0x505218`,
`0x505224`). A blocked unit reports band 0 whatever its speed.

### Arrival at the goal

`PointGoal`, built at `0x44CF60`, vtable `0x4FD328`:

| Offset | Field |
|---|---|
| `+0x04` | the mission |
| `+0x08` / `+0x0A` | the goal **map square**, `int16` |
| `+0x0C` | the radius in world units |
| `+0x10` | `(radius / 16)²`, in map squares squared |

The constructor converts the requested world position to a square with the same
footprint correction the collision code uses,
`(x - (footprint << 19) + 0x80000) >> 0x14`, and divides the radius by 16
rounding toward zero.

`IsSatisfiedBy(unit)` (`0x44D310`, vtable `+0x10`) is

```
(unit->squareX - gx)² + (unit->squareZ - gz)² <= radiusSq
```

on `unit+0x76` / `unit+0x78`, and `IsSatisfiedAt(x, z)` (`0x44D290`, vtable
`+0x14`) is the same test on supplied square coordinates — that is the one
`SetGoal` uses at `0x44F309` when it decides whether an existing path's tail
already satisfies a new goal.

The `Move` mission passes `mission+0x36 + 4` as the radius (`0x403232`), where
`mission+0x36` is the order's own tolerance. For a plain move order that is a
radius of 4 world units, which divides down to **0 map squares**: the unit must
end up standing on the goal square itself.

So arrival is a three-step sequence rather than one test:

1. `Navigator::Update` finds `IsSatisfiedBy` true, sets `mission+0x4E |= 0x20`, and calls `SetGoal(NULL)`.
2. `SetGoal(NULL)` clears both "I have a path" and "I want a path" (`0x44F2E1`).
3. The follower sees `HasPath` false and brakes at `brakerate` with no turn until the speed reaches zero.

The braking is not abrupt, because the arrival test has already been pulling
the speed down over the last `speed²/(2·brakerate)` world units. The two halves
are designed together: the follower gets the unit to the goal square slowly, and
the goal test then takes the path away.

### So what happens when a unit misses a waypoint?

Putting the advance, the aim point and the two brakes together, and this is the
answer #36 needs.

The original has **no** skip-ahead and **no** passed-the-plane test. If a unit
gets past the corner it was heading for without having come within 5 world
units of it, the corner is still `wp[1]`, and:

- while the unit is within 80 units of it — which it is immediately after passing — the aim point *is* that corner, so the unit turns back toward it;
- once it is more than 80 units past, the aim point becomes a point on the segment `prev → next` pulled back by `dist − 80`, which is behind the corner from the unit's point of view, so it turns back harder.

**So yes, the original will turn round and go back for a waypoint.** #36
describes real TA behaviour, not a divergence.

What the original has that keeps it rare is the corner brake. As a unit
approaches a corner the heading error to the aim point grows, and the corner
test `distToAim > 2·speed·|err|/turnrate` fails: the unit brakes at `brakerate`
every tick until it is going slowly enough that it can turn inside the distance
remaining. A unit therefore normally arrives at a corner at a speed its
`turnrate` can cope with, passes within 5 units, and advances. The
overshoot-and-return is the failure mode of the corner brake, not the normal
path of the code.

Two secondary effects make it rarer still. Sliding sideways off the line is
corrected before the corner arrives, because the aim point is on the segment
rather than at its end. And a unit that ends up genuinely lost will hit one of
the three repath triggers above — most often the blocked bit, since a unit
circling a corner in a crowd collides — and get a fresh route from where it
actually is.

### The FBI fields the follower reads

Confirmed by the string comparisons at `0x42C129`–`0x42C25F`, keeping §82's
warning in mind that the parser stores a key's value one push late.

| FBI key | Offset | Parsed by | Units | Read at |
|---|---|---|---|---|
| `maxvelocity` | `def+0x192` | `0x4C4800`, ×65536 | 16.16 world units per tick | `0x43CC76` (the cap), `0x43D9A2` (halved when blocked) |
| `brakerate` | `def+0x19A` | `0x4C4800`, ×65536 | 16.16 per tick² | `0x43CD46`, `0x43D02F`, `0x43D0B4` |
| `acceleration` | `def+0x19E` | `0x4C4800`, ×65536 | 16.16 per tick² | `0x43D0AC` |
| `moverate1` / `moverate2` | `def+0x1AE` / `+0x1B2` | `0x4C4800`, ×65536, default `maxvelocity × 2` | 16.16 | `0x43DA9A`, `0x43DAA9` — animation bands only |
| `turnrate` | `def+0x1BA` | `0x4C46C0`, `atoi`, `WORD` | 16-bit angle per tick | `0x43CBCA`, `0x43CF96`, `0x43D014` |
| `waterline` | `def+0x22C` | `0x4C46C0`, `BYTE` | world units | `0x43D72E` (§12) |
| `canfly` | `def+0x241` bit 11 | | | `0x43DD3E` — picks air over ground |
| `canhover` / `floater` | `def+0x241` bits 12 / 19 | | | `0x43CCAE` — exempt from the underwater halving |

Notably **not** read by the follower: `footprintx`/`footprintz` (they are baked
into the waypoints by the emitter and into the goal square by the constructor,
so the follower never sees them), and `movementclass`, which belongs to the
search and to the footprint test, not to the walking.

`brakerate` is read three times and the three uses are different: as the
deceleration when there is no path, as the deceleration when either test
fails, and inside the stopping-distance formula. It is *not* the "nose
re-aim only" field §82 describes — that note is about the air path at
`0x43D3BC`.

### What RWE does with this

RWE's follower is `followPath` in `src/rwe/sim/UnitBehaviorService.cpp`, with
the steering in `followSegment` (`UnitBehaviorService_util.cpp`), the turn in
`UnitBehaviorService::updateUnitRotation` and the speed in
`computeNewGroundUnitSpeed`. The path itself is `PathFollowingInfo` in
`src/rwe/sim/UnitState.h` — a `UnitPath` plus an iterator into its waypoints.
As with the original, only ground units come here: `moveTo` branches on
`canFly` and nothing else, exactly as `0x43DD3E` does.

| The original | RWE before this port | RWE now |
|---|---|---|
| Keeps the corner behind (`wp[0]`) as well as the one ahead | Kept only an iterator to the one ahead; `pathToWaypoints` started its loop at `++simplifiedPath.cbegin()`, so the cell the unit started in was dropped | Keeps it. `waypoints[0]` is the corner behind, the iterator starts at `begin() + 1`, and a path is never shorter than two points |
| Aim point projected onto the segment, look-ahead 80 world units | `seek` aimed straight at the current waypoint, always | The projection, the 80-unit look-ahead, the `min(dist − 80, seglen)` cap and the degenerate-segment bail-out |
| Advance when within **5** world units of the waypoint ahead | Advance when within **16**; **8** for the last one | Unchanged, and deliberately — see the note at the head of this section |
| Turn: clamp the heading error to ±`turnrate` | `turnTowards(rotation, targetAngle, turnRate)` — the same rule | Unchanged |
| Accelerate iff `distToAim > 2·speed·abs(err)/turnrate` **and** `distToAfterNext > speed²/(2·brakerate)`; otherwise brake at `brakerate` | Target speed was `maxVelocity × max(0, cos θ)`, squared again inside one turn radius of the goal | Both tests, in the original's operation order. Target speed is `maxVelocity` when both pass and zero when either fails, and `computeNewGroundUnitSpeed`'s accelerate/brake step — which already matched `0x43CC20` — does the rest |
| Arrival brake keyed to `buf[2]`, the final waypoint on a short path | `arrive` did the same `speed²/(2·brakeRate)` test, but only on the last waypoint; intermediate corners got no arrival brake at all | Keyed to the waypoint after next, clamped to the last the way `GetWaypoints` clamps its index, so every corner gets it |
| Speed cap: `maxvelocity × slope%[pitch]`, halved underwater unless `canhover`/`floater` | `computeNewGroundUnitSpeed` caps at `maxVelocity`, halves it below sea level, and scales by `computeSlopeSpeedFactor` — a rise-over-one-tile ratio with a floor of ¼, not the original's eleven-entry pitch table | Unchanged; the pitch table is still not ported |
| No path → brake at `brakerate`, no turn | Reaching the end of the path returned `true` and asked for another; the steering info was left as it was | The end of the path sets the steering to "hold this heading, stop", which is `0x43CD36` |
| Straight-line stand-in is two points: unit position, then goal (`0x44F3F2`) | `groundUnitMoveTo` pushed **one** waypoint, the destination | Two points, and the unit walks it on the tick the order arrives rather than the one after |
| Repath triggers: goal changed, count < 2, blocked | Goal moved, `inCollision` (with a 30-tick cooldown), path finished | Unchanged — the same three in substance |

**The two tests are written the original's way round.** `A = |err| × speed /
turnrate` first and `distToAim² > (2A)²` after, not cross-multiplied into a
division-free form; and `B0 = speed²/(2 × brakerate)` first and
`distToAfterNext² > B0²` after. The original divides first with 64-bit
intermediates, and where that division truncates is part of the arithmetic:
rearranging it moves the tick a unit starts braking on. The determinism worry
about a division is a real one in general and does not bite here — a
`SimScalar` divide is a single IEEE operation with one correctly rounded
result, so every peer gets the same number out of it. Both divisions are
guarded against a zero `turnrate` or `brakerate`, which the shipped data never
has and a test fixture can.

**No new state.** The only thing the follower needed that it did not have is
`prev`, and putting it in `path.waypoints` rather than beside them is what
keeps the bookkeeping free: `save_util.cpp` already serialises the vector and
the iterator's index, and `dump_util.cpp` already dumps the moving state.
`computeHashOf(NavigationStateMoving)` hashes `pathDestination`,
`pathRequested` and `reachableDestination` but not the path — the path is saved
and not hashed, which is defensible under CLAUDE.md's rule only because every
peer computes the same path from the same search, and a follower carrying extra
state must keep it inside `UnitPath` for the same reason rather than inventing a
second unhashed field beside it.

**And one thing found on the way.** A unit picked up by a transport kept the
speed it was walking at, and nothing runs a carried unit's physics, so the
number was still sitting there when it was set down again and the unit coasted
a world unit or two out of the spot the transport had chosen for it, in
whatever direction the transport happened to be facing. `loadUnitIntoTransport`
stops it now.

### What is not settled

- **`mover+0x2A`.** Stamped at `0x43D865` with the tick the unit last changed map square or movement mode. No reader turned up in the mover, the navigator or the follower. It may be a stuck timer read from somewhere further away, or dead. Stated as found.
- **`mover+0x04`.** The scheduler checks it non-null before touching the navigator (`0x40ED4A`) and nothing else read here uses it.
- **The `min(i, count-1)` clamp's intent.** It makes `buf[2]` the last waypoint on a short path, which is what turns the second test into an arrival brake. Whether that was the intent or a convenience that happens to work is not decidable from the code.
- **Bit 2 of `mover+0x2E`.** Read as "blocked" here and in §87, and the only writer found is `0x43D92C`, which sets it when `0x47DB70` — §95's footprint test for a unit definition — refuses the square. But that writer sits inside a branch gated on `unit+0x96` (`0x43D8E3`), so there may be a second writer on a path not walked here.
- **The exact sign convention of the pitch index** into the slope table. The table is asymmetric, so which end is uphill matters; the code is `clamp(pitch >> 11, -5, +5)` with no negation, and the table's shape (100% at −1 and 0, falling faster on the positive side) reads as positive = uphill, but that is inference from the numbers rather than a decode.
- **Whether ships differ.** They do not take a different follower — `0x43DD3E` branches on `canfly` alone — but `0x43DB50`, the waterline step, was not read past its first few instructions and may adjust the speed for a floater in a way the follower does not see.

---

## 103. Loading is issued to the transport, and what a click on a unit does

> **Ported, 2026-09-10.** The roadmap's "units ordering themselves aboard
> (select units, click transport)" is closed as **not-TA**: there is no
> passenger-side boarding order in the v3.1 binary, and the count below is
> exhaustive rather than a search that came up empty. What is ported instead
> is the thing next door that *is* the original — the default-action ladder,
> which RWE had written out twice, in the cursor chooser and in the click
> handler, already disagreeing. It is one free function now,
> `computeDefaultAction` in `src/rwe/game/DefaultAction.{h,cpp}`, taking the
> simulation, one selected unit, what is under the cursor and which scheme is
> in force, and returning the order *and* the cursor together, so the two
> cannot come apart. `GameScene.cpp`'s cursor arms and `GameScene_input.cpp`'s
> left, right, minimap and MOVE-armed click handlers all call it.
> `CursorType::Pickup` is new and loads `cursorpickup`, which RWE had never
> used, so the air/crane split can be drawn. The visible change is that a
> click on a friendly unit now does something: in "Right Click" mode it
> guards, repairs, completes, lands on a pad, picks up or moves, where before
> it silently dropped every click that was not an enemy or a nanoframe.
>
> **Three things are deliberately not ported.** A passenger-side board order,
> because there is none — implementing one would be a §88 divergence and
> belongs under features rather than fidelity. Picking up an enemy unit:
> §5 below shows the original has no ownership test at any of the five
> decision points, and RWE keeps its own-units rule, now stated once in
> `DefaultAction.cpp` instead of scattered (§88). And `cursorrevive`, which
> the base game's `CURSORS.GAF` does not ship; a resurrect keeps the reclaim
> cursor, as it already did for an order in flight. The air pickup's order of
> operations — `QueryTransport` and `BeginTransport` before the descent rather
> than after the attach — is the other thing this section turned up, and it
> was fixed in its own commit the same day rather than here. What is left
> unported and recorded is the crane path's ten-second self-attach fallback
> and its `CraneReach` gate, neither of which the original has an equivalent
> of.
>
> `src/rwe/game/DefaultAction.test.cpp` pins the arms, including the pair the
> roadmap item was about: a Peewee over a friendly Hulk guards it, and an
> Atlas over a friendly Peewee loads it.

§31–§41 decode the transport *missions*: what `CanLoadUnit` allows, what the
crane and the Atlas do once a pickup mission is running, and what the carried
state is. This is the layer above them — the click, the command and the
cursor.

**The headline answer is no.** Loading is a *transport-side* order in every
path: the transport is always the unit being commanded and the cargo is always
the click target, never the other way about. What a selection of ordinary
mobile units gets for clicking a friendly transport is, depending on the mouse
scheme, either nothing but a change of selection or a **Guard** order:

| `Interface Type` | Button | Cursor shown | What the click does |
|---|---|---|---|
| **0 — "Left Click"** (the shipped default) | left | 15 `cursorselect` | **selects the transport**, replacing the selection. No order at all |
| 0 | right | (unchanged) | cancels an armed command; otherwise starts the screen drag-scroll |
| **1 — "Right Click"** | left | 15 `cursorselect` | selects the transport |
| 1 | right | 15 `cursorselect` (feedback only) | **`FOLLOW_GROUND` / `VTOL_FOLLOW`** — a Guard order on the transport |
| either, with the MOVE button armed (command 2) | left | 5 `cursordefend` | **`FOLLOW_GROUND` / `VTOL_FOLLOW`** — Guard |

The confidence is **high**, and it rests on a count rather than on a failed
search. There are exactly five calls to `CanLoadUnit` (`0x489A90`) in the whole
image, all five pass the *ordering* unit as the transport, and the ground
mission table has 46 rows of which exactly three touch transports at all
(`BeCarried`, `Ground_Pickup`, `Ground_Unload`) — and `BeCarried` is entered
only from the attach routine, never from an order.

### The command table: fourteen commands, and where they come from

A click becomes an order in two steps. The order panel arms a **command id**
in `BYTE [game+0x2CC3]` (`game` = `ds:0x511DE8`); a click in the world then
runs that id, the clicked unit and the clicked point through a builder that
produces a **mission name** per selected unit. The ids are not the mission rows
and not the cursor ids — three separate numberings, which is the trap in
reading any of this.

The arming sites are one per button, all in `0x419BA0`–`0x41A0E0`, each
`strstr`-matched by gadget name (§19's `0x49FE60` caveat applies) and each
toggling back to 1 when pressed a second time:

| Button | Arms `[game+0x2CC3]` | Order-builder arm | Cursor-chooser arm |
|---|---|---|---|
| — (nothing armed) | **1** | `0x43F9E9` | `0x43E505` |
| `MOVE` | **2** | `0x43F845` | `0x43E8BB` |
| `ATTACK` | 3 | `0x43F154` | `0x43E545` |
| `BLAST` | 4 | `0x43F7E8` | `0x43E850` |
| `UNLOAD` | **5** | `0x43F735` | `0x43E80C` |
| `LOAD` | **6** | `0x43F701` | `0x43E7D3` |
| `DEFEND` | 7 | `0x43F4C7` | `0x43E615` |
| `REPAIR` | 8 | `0x43F46C` | `0x43E5FA` |
| `PATROL` | 9 | `0x43F3B9` | `0x43E5DE` |
| `STOP` | 10 (issued at once, then back to 1) | `0x43F82C` | — (`0x43F098`) |
| (no button) | 11 — `TELEPORT` | `0x43F813` | `0x43E8AE` |
| `RECLAIM` | 12 | `0x43F4F7` | `0x43E65C` |
| `CAPTURE` | 13 | `0x43F6D1` | `0x43E797` |
| building placement | 14 | `0x43F7A0` | `0x43E828` |

Both dispatchers are the same shape — `eax = cmd & 0xFF; dec; cmp 0xD; ja
default; jmp [table + eax*4]` — with tables at `0x4401EC` (order builder, entry
`0x43F0E0`) and `0x43F0A8` (cursor chooser, entry `0x43E490`).

`0x43E470` is the small predicate that says whether a command consults the unit
under the cursor at all: commands **5 (UNLOAD), 10 (STOP) and 14 (placement)**
return 0 and are point-or-nothing orders; everything else returns 1 and takes
the hovered unit as its target. That is why an unload is aimed at a spot and a
load is aimed at a unit.

**`LOAD` is command 6 and only the LOAD button arms it**, which §19 has already
shown is offered only when some selected unit has `canload` — the four base-game
transports (`armatlas`, `armtship`, `cortship`, `corvalk`) plus the two Core
Contingency hover transports. A Peewee can never have the LOAD button, so a
passenger can never arm command 6.

### The order builder, `0x43F0E0`, called once per selected unit

```
0x43F0E0(char* outMissionName, BYTE cmd, Unit* orderer, Unit* target, Point* clickXZ)
```

The issue routine `0x48CF30` walks the local player's unit array (stride
`0x118`), keeps those with `unit+0x110` bit 4 (selected), **skips the click
target itself** (`0x48D07B` — a selected unit that is also the thing clicked
does not order itself), and calls the builder once per survivor at `0x48D0A0`.
So a mixed selection produces a different mission per unit from the same click,
and `orderer` is always the *selected* unit.

Its preamble sets the two relationship flags the rest reads
(`0x43F0EC`–`0x43F12D`):

```
if (target) {
    require target+0x110 & 0x10000000            ; else no order at all
    ebx = 1 if ordererPlayer->allyTable[targetPlayerIndex] != 0   ; ALLIED
    eax = 1 if that byte == 0                                     ; ENEMY
}
```

The polarity is pinned twice over: the guard arm is friendly-only and requires
the `!= 0` flag, and the cursor chooser's red/green pair puts `cursorred` under
the `== 0` flag and `cursorgrn` under the other.

**Command 6, LOAD** (`0x43F701`) refuses without a target, calls
`CanLoadUnit(transport = orderer, candidate = target)` with `ecx = ebp =
orderer` — the transport is the unit being commanded — and produces
`VTOL_PICKUP` or `GROUND_PICKUP` on `canfly`. There is no call with the
operands the other way round anywhere in the image.

**Command 5, UNLOAD** (`0x43F735`) produces `VTOL_LANDING` when a `canload`
aircraft is aimed at an `isairbase` target, and otherwise requires `canload`
and gives `VTOL_UNLOAD` or `GROUND_UNLOAD`.

**Command 2, MOVE** (`0x43F845`) is not a plain move: it is the full context
ladder, and the only armed command that can produce a pickup without the LOAD
button. Read top to bottom; the first arm that fires wins:

```
43f845  require ordererDef+0x245 bit 7 (canmove)          ; else no order
43f854  if (orderer has no mover)          -> "QMOVE"     ; a factory: rally point
43f873  if (no target unit)                -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
43f893  if (cancapture && ENEMY)            -> "CAPTURE"
43f8b5  if (canreclamate && ENEMY)          -> canfly ? "VTOL_RECLAIMUNIT" : "RECLAIMUNIT"
43f8d9  if (ALLIED && CanRepair && target buildFraction != 1.0)
                                            -> canfly ? "VTOL_HELPBUILD" : "HELPBUILD"
43f918  if (ALLIED && CanRepair && target hp < maxhp)
                                            -> canfly ? "VTOL_REPAIRUNIT" : "REPAIRUNIT"
43f959  if (canfly && ALLIED && targetDef isairbase)
                                            -> "VTOL_LANDING"
43f97d  if (CanLoadUnit(orderer, target))   -> canfly ? "VTOL_PICKUP" : "GROUND_PICKUP"
43f9a4  if (canguard && ALLIED)             -> canfly ? "VTOL_FOLLOW" : "FOLLOW_GROUND"
43f9cd  otherwise                           -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
```

`CanRepair` is `0x4899B0`; `CanLoadUnit` is `0x489A90` and is again called with
`ecx = orderer` (`0x43F97E`: `mov ecx,ebp`).

**Trace a Peewee down that list against a friendly Hulk.** Not an enemy, so the
capture and reclaim arms are skipped. `CanRepair` fails — a Peewee is not a
builder. Not `canfly`. `CanLoadUnit(peewee, hulk)` fails at its second test,
`0x489AB8`, because the *transport* argument is the Peewee and a Peewee has
`canload = 0`. `canguard` is set on essentially every mobile unit and the Hulk
is allied, so the Peewee gets **`FOLLOW_GROUND`** — ground mission row 27,
handler `0x406300`, display "Guarding". It walks over and escorts the
transport. It does not board it.

**Command 1, the default action**, reads `DWORD [game+0x37EFA]` at `0x43F9E9`
and forks on it. **Interface Type 1's right-click chain** (`0x43FA00`) is
command 2's ladder with `CAPTURE` at the front, and it keeps the transport
arms: capture (enemy) → `RECLAIMUNIT` (enemy) → `HELPBUILD` → `REPAIRUNIT` →
`VTOL_LANDING` (`0x43FB04`) → **`CanLoadUnit` at `0x43FB3D`** → `FOLLOW_GROUND`
at `0x43FB7B` → feature reclaim → `MOVE_GROUND` at `0x4401C2`.

**Interface Type 0's left-click chain** (`0x43FE35`) is shorter, and the
difference is the interesting part:

```
43fe35  if (canattack && ENEMY)     -> recurse into 0x43F0E0 with cmd 3   ; ATTACK
43fe68  if (canreclamate && ENEMY)  -> recurse into 0x43F0E0 with cmd 12  ; RECLAIMUNIT
43fe91  if (CanRepair && under construction / damaged) -> HELPBUILD / REPAIRUNIT
43ff38  if (canresurrect && the feature under the cursor is resurrectable) -> RESURRECT
440065  if (canreclamate && the feature under the cursor is reclaimable)   -> RECLAIM
44019d  if (canmove && has a mover) -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
        else no order
```

**There is no guard arm and no load arm in the left-click chain.** In the
shipped default scheme, clicking a friendly unit can never produce a Guard
order and can never produce a pickup; the DEFEND and LOAD buttons are the only
routes to either.

### The mission tables

Ground table: base `0x4FC490`, **25-byte records**, 46 live rows, laid out as
`TOTALA-EXE-MISSIONS.md` S:1 describes. The rows this section needs are **14
`BeCarried`** (`0x4FC5EE`, `0x402FC0`, "Being transported"), **27
`Follow_Ground`** (`0x4FC733`, `0x406300`, "Guarding"), **34 `Ground_Pickup`**
(`0x4FC7E2`, `0x406780`, "Loading") and **35 `Ground_Unload`** (`0x4FC7FB`,
`0x406900`, "Unloading"). Row 23 is not a mission at all: the twenty-five bytes
there are float constants that happen to sit inside the array's stride.

**The air table's prose in `TOTALA-EXE-MISSIONS.md` was wrong and is corrected
by this pass.** It said the VTOL table "begins at `0x4FCA7C`, 18 records" where
its own table (correctly) starts at `0x4FCA18` and runs 22 rows; `0x4FCA7C` is
row 4, `VTOL_Unload`. The rows wanted here are **3 `VTOL_Pickup`**
(`0x4FCA63`, `0x4111B0`, "Loading"), **4 `VTOL_Unload`** (`0x4FCA7C`,
`0x411560`, "Unloading"), 2 `VTOL_Landing` (`0x4118E0`) and 5 `VTOL_Follow`
(`0x40FBE0`).

**Every transport-related row is transport-side except `BeCarried`, and
`BeCarried` is not orderable.** It is installed only by `0x4384A0`, called from
the attach routine `0x48AAC0` at `0x48ACF3` when a unit is put aboard something
that is not a repair pad (§37). Nothing names it in the order builder, no
button arms it, and it carries no target of its own.

There is no `Load`, `LoadUnits`, `Board`, `BoardTransport`, `GetLoaded`,
`EnterTransport` or `GotoTransport` string anywhere in the binary. The only
`Transport` literals are `TransportPickup` (`0x5016F4`), `TransportDrop`
(`0x501734`), `EndTransport` (`0x501B0C`), `BeginTransport` (`0x501BA4`),
`QueryTransport` (`0x501BB4`), the three announcements ("Transport mission
failed", "Unit is too large to transport", "Unit is too heavy to transport"),
"Being transported" (`0x501160`), and the FBI key names.

> A lead, recorded but not decoded: the **low byte of the word at `+0x10`**
> agrees with the cursor id the next part derives for the same action in most
> rows — `Ground_Pickup` 12, `VTOL_Pickup` 8, `Ground_Unload` 13,
> `Follow_Ground` 5, `Move_Ground` 14, `Capture` 4, the reclaim rows 11, the
> attack rows 1. It is **not** a reliable source for the cursor:
> `VTOL_Unload` carries 9 where the chooser returns 13, `MobileBuild` carries
> 0, and `Standby` carries 15. Treat the chooser as the authority and this as
> a coincidence worth someone's afternoon.

### The cursor chooser, `0x43E490`, and the cursor table

```
0x43E490(BYTE cmd, Unit* orderer, Unit* target, Point* clickXZ) -> cursor id
```

It is called from exactly one place, `0x48D3E4`, in a loop over the selection
that starts at `0x13` (`cursornormal`) and keeps the **minimum**
(`48d3e9  if (eax < edi) edi = eax`). So a mixed selection shows the cursor of
whichever selected unit has the lowest-numbered applicable action — the
cursor's counterpart to §19's "any, not all" button rule. `0x43F098`, the
switch default, returns 19, which is the sentinel a unit contributes when it
cannot do the armed command at all.

The cursor ids are **1-based indices into an array of GAF handles at
`game+0x14883`**, loaded in one straight-line run at `0x429C9A`–`0x429E94` from
`CURSORS.GAF`. The consumer at `0x4992B9` reads `[game + id*4 + 0x1487F]`,
which is the same array biased by one; `BYTE [game+0x2CBE]` caches the id
currently displayed.

| id | Field | Sequence | Frames |
|---:|---|---|---:|
| 1 | `+0x14883` | `cursorattack` | 10 |
| 2 | `+0x14887` | `cursorairstrike` | 16 |
| 3 | `+0x1488B` | `cursortoofar` | 2 |
| 4 | `+0x1488F` | `cursorcapture` | 13 |
| 5 | `+0x14893` | `cursordefend` | 16 |
| 6 | `+0x14897` | `cursorrepair` | 12 |
| 7 | `+0x1489B` | `cursorpatrol` | 14 |
| **8** | `+0x1489F` | **`cursorpickup`** | 24 |
| 9 | `+0x148A3` | `cursorteleport` | 46 |
| 10 | `+0x148A7` | `cursorrevive` | 18 |
| 11 | `+0x148AB` | `cursorreclamate` | 11 |
| **12** | `+0x148AF` | **`cursorload`** | 16 |
| **13** | `+0x148B3` | **`cursorunload`** | 16 |
| 14 | `+0x148B7` | `cursormove` | 8 |
| 15 | `+0x148BB` | `cursorselect` | 2 |
| 16 | `+0x148BF` | `cursorfindsite` | 2 |
| 17 | `+0x148C3` | `cursorred` | 1 |
| 18 | `+0x148C7` | `cursorgrn` | 1 |
| 19 | `+0x148CB` | `cursornormal` | 1 |
| 20 | `+0x148CF` | `cursorhourglass` | 8 |
| 21 | `+0x148D3` | `pathicon` | 1 |

`cursorrevive` is out of order in the array because the load run assigns it
last (`0x429E94`) into the slot skipped at `0x429D9D`; it is also the only one
of these absent from the base game's `CURSORS.GAF` and present in `rev31.gp3`'s.
`pathicon` is the marching-waypoint sprite of §26, riding in the same array.
`CURSORS.GAF` also ships `cursorprotect` and `MISCART.GAF` a whole parallel set
(`cursor load`, `cursor moveto`, …) — **the exe loads none of them**; no such
strings exist in the binary.

**Command 6, LOAD** (`0x43E7D3`) returns 19 without a target or when
`CanLoadUnit(orderer, target)` fails, and otherwise computes
`((~(def+0x241 >> 11) & 1) | 2) << 2` — that is, **8 (`cursorpickup`) when the
transport is `canfly` and 12 (`cursorload`) when it is not**. That split is the
only air-versus-crane difference anywhere in the cursor path. **Command 5,
UNLOAD** (`0x43E80C`) is `canload ? 13 : 19` and does *not* branch on `canfly`
— the Atlas and the Hulk both show `cursorunload`.

**Command 2, MOVE** (`0x43E8BB`) mirrors its ladder arm for arm: 14 with no
target or no mover, 4 for capture, 11 for reclaim, 6 for both repair arms, 13
when a flyer is over an air base, `canfly ? 8 : 12` for a pickup, 5 for a guard,
14 otherwise.

**Command 1** forks on `Interface Type` at `0x43E505` exactly as the order
builder does:

```
43e505  if (Interface Type == 1) goto 0x43eb02        ; the RIGHT-click scheme
        ; Interface Type 0 -- the LEFT-click scheme:
43e512  if (canattack && ENEMY)     { cmd := 3;  re-enter the switch }
43e52a  if (canreclamate && ENEMY)  { cmd := 12; re-enter the switch }
43edb6  if (CanRepair && under construction) -> 6  (cursorrepair)
43edec  if (target is the LOCAL player's own, selectable, fully built,
            [target+0xFB]==0, and either unattached or on a repair pad)
                                             -> 15 (cursorselect)
43ee4f  if (canresurrect && resurrectable feature) -> 10 (cursorrevive)
43ef6c  if (canreclamate && reclaimable feature)   -> 11 (cursorreclamate)
43f07c  canmove ? 14 (cursormove) : 19

43eb02  ; Interface Type 1 -- selection feedback only:
        if (target is the local player's own and selectable, as above) -> 15
43eb63  if (ENEMY)  -> 17 (cursorred)
43eb74  if (ALLIED) -> 18 (cursorgrn)
43eb85  ... otherwise probe the terrain and features, ending at 14/19
```

Note the fourth arm of the left-click chain: **your own units answer with
`cursorselect` before anything else can fire.** That is why, in the shipped
default scheme, no amount of hovering your own transport produces a load
cursor — and why the LOAD button exists.

### The cursor is the decision

**Left button** (`0x4993B6` for the down-event with a command armed;
`0x4995AE` for a click that ended a drag of under 32 pixels in under 0x19
ticks) → **`0x498F70`**, which dispatches on the **currently displayed cursor
id**, not on the command:

```
498f77  cl = [game+0x2CC3]                     ; armed command
498f7d  if (cl == 14) { building placement, separate path }
499027  dl = [game+0x2CBE]                     ; the cursor on screen right now
49902d  if (dl == 15 /*cursorselect*/) { 0x48C7F0(click); return }   ; select that unit
499041  if (dl >= 17) {                        ; red, green, normal, hourglass, pathicon
            if (Interface Type == 1 && cl == 1) { clear the selection }
            return                             ; otherwise the click does nothing
        }
49906d  0x48CF30(click, cl, 0, &game+0x2CAA, 0, 0)     ; issue the order
49908c  if (!shift) unarm the command and un-press its button
```

That is the tidiest thing in this whole path: **the cursor is the decision.** A
left click issues an order exactly when the chooser returned an id below 17,
selects when it returned 15, and does nothing otherwise. A real drag goes to
`0x48C390` instead, which is the rubber-band box selection and issues nothing.

**Right button down** → `0x499100`: an armed command is cancelled first
(`0x499107`), Interface Type 0 then starts the screen drag-scroll
(`0x499162`), and Interface Type 1 calls `0x48CF30` with command 1 directly
(`0x4991D5`). So cancelling with the right button works in **both** schemes,
only "Right Click" mode issues on it, and — because that path does *not*
consult the cursor — in "Right Click" mode the cursor reads `cursorselect` over
your own transport while the right button still issues a Guard order on it.

`DWORD [game+0x37EFA]` is the registry value **`Interface Type`** under
`HKCU\Software\Cavedog Entertainment\Total Annihilation` (read at `0x42F9A0`,
written back at `0x430F1C`), clamped to 0..1 at `0x42F9CF` and defaulting to
**0** when absent. Its UI is the `LEFTCLICK` gadget on `SPEEDSRT.GUI` /
`SPEEDS.GUI`, labelled `"Left Click|Right Click"` (§65).

The unit under the cursor is resolved once per frame by `0x48CD80` into
`WORD [game+0x2CBA]`, from the drawn-unit list by a ray test with **no owner
filter**; `0x48CF30` converts it to a pointer only when `0x43E470` says the
command in hand takes a unit target.

### An aside the code is unambiguous about: enemies are loadable

Neither `CanLoadUnit` (§31 already noted this), nor the LOAD cursor arm
(`0x43E7D3`), nor any of the three order-builder call sites applies an
ownership or alliance test. In the command-2 ladder the `CanLoadUnit` arm at
`0x43F97D` sits *after* capture and reclaim but is itself unguarded, and a
transport has neither `cancapture` nor `canreclamate`, so it is reached with an
enemy target; and with the LOAD button armed, `0x43E7D3` will happily hand back
cursor 8 or 12 over an enemy. §31's parenthetical "the UI only offers the
cursor on the player's own units" and §39's "RWE should keep its UI-level
own-units-only rule" both read as though the UI supplies the missing test —
**it does not**. That rule is a deliberate RWE house rule, recorded in §88, not
a description of the original. Whether the resulting pickup then completes was
not traced and was not play-tested.

### What the transport's script sees, and when

The roadmap's second item asks whether the `TransportPickup` boom animation is
timed to the actual attach. **On the crane path it is the other way round: the
attach is timed to the animation, and the engine never attaches at all.** It
starts the script and polls a field.

`Ground_Pickup` (`0x406780`) state 2 is the whole of the engine's involvement:
it starts `TransportPickup(cargoId)` through `0x4B0A70`, plays announcement
slot 0xC, bumps an attempt counter and sets a 15-tick timer. State 3 sleeps
while the COB `BUSY` value at `unit+0x10F` bit 1 is set, and state 4 tests
`target+0x86 != 0` — *is the cargo attached to anything at all*. The attach
itself happens inside the script when it executes `ATTACH_UNIT`. Three
consequences worth naming:

- **There is no engine-side range gate for the crane.** The script's own
  `BoomCalc` reach test is the only one. The engine's check in state 0
  (`0x4067B1`) is a *footprint* test, not a distance test, and its failure
  message is "Unit is too large to transport".
- **The announcement is not timed to the attach on the ground path** — slot 0xC
  plays when the *script starts*, before any hook has touched anything. On the
  air path (`VTOL_Pickup` state 4, `0x411479`) the same slot plays *at* the
  attach. The two genuinely differ.
- **A script that never sets `BUSY` costs 15 ticks, not a failure.** State 4
  finds nothing attached and, while the attempt counter is under 3, installs a
  ground move goal at the cargo's position. Three attempts exhausted, it returns
  9 and parks for `rand(30)+30` ticks before restarting from state 0. There is
  no timeout after which the engine takes the unit aboard itself.

`VTOL_Pickup` (`0x4111B0`) is the reverse. State 2 calls `QueryTransport` after
arriving within 48 wu of the cargo at cruise altitude and **before** any
descent, and the script returns the piece to hang the cargo from. State 3 calls
`BeginTransport(targetDef+0x16E)` — the cargo's model height — still before the
descent, and the Atlas's `BeginTransport` is a single `MOVE_NOW link y -> -h`;
the engine then resolves that piece's offset and descends until the hook sits
on the cargo's roof. State 4 does the attach itself, `0x48AAC0`, with no script
call at all. So on the air path **the animation is timed to the engine's
attach**, where on the crane path the attach is timed to the animation.

`EndTransport` has five call sites and two are not where you would look:
`0x411E27` runs it inside `VTOL_Landing` whenever `transport+0x8A` is
non-empty — the Atlas folds its arms whenever it *lands* loaded, not only when
it lets go — and `0x411D9C` on that mission's abort path. The others are
`VTOL_Pickup`'s abort (`0x411489`), `VTOL_Unload` state 2 (`0x411790`), and
`VTOL_LandIfCan` (`0x40F42E`).

`Ground_Unload` state 0 starts `TransportDrop(passengerId, (intX<<16)|intZ)`
and the same `BUSY` protocol runs. The release is the script's `DROP_UNIT`, and
the engine's contribution is the legality veto inside that opcode's handler
(`0x4813B0`, §37): a `DROP_UNIT` onto an illegal cell does nothing at all and
the unit stays hooked, which is what makes the mission retry.

### Sea transports and the AI

§39 answers "how do the crane transports load" completely. What it does not
carry, and what an AI would need, is the surrounding geography. Recorded as
gaps rather than findings, because none of it was decoded in this pass:

- **Whether the original's computer player uses transports at all** was not
  traced. Nothing transport-shaped turned up in the AI while walking these
  tables, but that is an absence of evidence.
- **Nothing in the original ever moves the passenger.** `Ground_Pickup` state 4
  installs the move goal on the *transport* with tolerance 0. The cargo is
  never ordered anywhere. Whatever RWE does about meeting points is RWE's own
  invention with no original behind it.
- **`Ground_Unload`'s arrival tolerance** is the one hover-specific number:
  `int(footprintZ * 16 * 1.5)` world units when `canhover` is set, 0 otherwise
  — 96 wu for the 4-footprint Bear and Turtle (§35). That is the beach-reach
  allowance, and the closest thing the original has to a "dock here" rule.
- **The load predicate already forbids the interesting case**: a sea or hover
  transport refuses any candidate whose `minwaterdepth >= 0` (`0x489B44`), so a
  Hulk can never carry a ship, and every surface ship's footprint exceeds
  `transportsize = 3` anyway. An AI planning sea transport is only ever
  planning to move *land* units *across* water.
- **One unload order sets down one unit** (§35); a full Hulk needs the order
  re-issued twenty times, which the `Standby` re-execution loop does for the
  human player. An AI queueing unloads must queue one per passenger.

### What RWE does with this

The ladder is `computeDefaultAction` in `src/rwe/game/DefaultAction.{h,cpp}`,
over `(simulation, scheme, orderer, hovered unit, hovered feature)`, returning
the order to issue — or "select it", or "move to the point under the cursor",
or nothing — together with the cursor. Three schemes, named after the arms they
run: `LeftClickDefault`, `RightClickDefault` and `MoveButton`.
`GameScene::selectionDefaultCursor` folds it over the selection with
`preferredCursor`, which is the original's minimum-id rule; `GameScene::
issueDefaultAction` runs it for one unit and issues or queues on the shift key.
The old arrangement — an `any_of` ladder in `GameScene.cpp` and an if-chain in
`GameScene_input.cpp` — is gone.

| Question | Original | RWE before | RWE now |
|---|---|---|---|
| Passenger-side board order | none | none | none — and now known to match rather than merely to coincide |
| Selected transport, click cargo with LOAD armed | `GROUND_PICKUP` / `VTOL_PICKUP` | `LoadOrder` per selected unit | unchanged |
| Cursor with LOAD armed | 8 `cursorpickup` if `canfly`, else 12 `cursorload`; 19 over anything unliftable | always `CursorType::Load` | the split, and the plain arrow when the hover cannot be lifted |
| Ownership test at order time | **none**, anywhere | `isFriendly` in two places | one place, `canLoad` in `DefaultAction.cpp`, still own-units-only (§88) |
| Default action on a friendly unit, "Left Click" | selects it | selected it | unchanged |
| Default action on a friendly unit, "Right Click" | guard, repair, complete, land, pick up, or move | **nothing at all** unless it was a nanoframe | the ladder |
| Default action on an enemy, "Right Click" | capture if `cancapture`, else reclaim if `canreclamate` | always attack | capture, then reclaim, then attack |
| MOVE button armed | the whole ladder, with its own cursors | land on a pad, else move | the whole ladder |
| Whether the cursor gates the click | yes below 17, selects at 15 | two independent ladders that disagreed | one ladder; in "Left Click" the cursor is the decision, in "Right Click" it is feedback, as in the original |
| `Interface Type` | registry DWORD 0/1, `LEFTCLICK` gadget, default 0 | `globalConfig->leftClickInterfaceMode`, same polarity | unchanged |
| Crane pickup | script does everything; engine polls `target+0x86`; no range gate; three attempts then park | `TransportPickup` then a 10-second self-attach fallback, and a `CraneReach` gate | unchanged, and both differences recorded here |
| Air pickup | `QueryTransport` and `BeginTransport` before the descent | both after `loadUnitIntoTransport` had already succeeded | the two calls sit either side of the descent, fixed in its own commit rather than this one |
| Pickup announcement | ground at script start, air at the attach | neither played | unchanged |

Two arms are RWE's own and are marked as such in the source. The **attack arm
in the right-click ladder** is kept although the decode of `0x43FA00` lists
none: right-clicking an enemy has always attacked it here, and removing it on
the strength of an elided list would be the worse mistake. And the **resurrect
arm shows the reclaim cursor**, because `cursorrevive` is not in the base
game's `CURSORS.GAF`.

### What is unsettled

- **Whether an enemy unit can actually be picked up.** The order layer permits
  it at all five decision points and `CanLoadUnit` has no team test, so the
  order will be issued and the mission will start. Whether `ATTACH_UNIT` or
  `0x48AAC0` refuses later was not traced, and this was not play-tested. Do not
  port "transports can steal enemy units" on the strength of this section.
- **Whether `0x43FA00` really has no attack arm.** The chain was read from its
  capture arm onward; an attack arm ahead of it, or a recursion into command 3
  like the left chain's, would not have shown up in that reading. RWE keeps its
  own attack arm until this is settled.
- **`BYTE [game+0x2CC6]`**, the mouse state byte, is used as opaque bits above.
  Only bit 3's role (a selection box is being dragged) is firmly established;
  bit 2's is guessed from context.
- **Command 10's issue path.** The STOP button writes 1 to `[game+0x2CC3]` and
  acts immediately rather than arming 10, yet command 10 has a live builder arm
  (`0x43F82C` → `"STOP"`) and appears in `0x43E470`'s no-target list. Something
  issues it; that something was not found.
- **`0x489960` and `0x4899B0`**, the reclaim and repair predicates, are used as
  named black boxes here; only their positions in the ladders were established.
- **`0x43E828`** (command 14, the placement cursor) and the feature probes in
  the command-1 and command-2 arms were read only far enough to identify their
  return values. RWE's own feature arms therefore fire only when nothing is
  under the cursor but the feature, where the original probes the map cell
  after its unit arms have failed.
- The **frame counts** in the cursor table come from a prior `gaf.py` sweep of
  every shipped GAF, not from a fresh read of `CURSORS.GAF`. The sequence
  *names* are from the binary and are certain.
- The mission tables' **`+0x10` word** is tabulated above only as a lead; its
  meaning is not decoded, and its correlation with the cursor id has three
  counter-examples.
