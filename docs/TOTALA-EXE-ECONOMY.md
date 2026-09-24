# What the original executable does: the economy, building, reclaim and repair

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §3, §20, §23, §29, §93, §94, §96, §97, §98, §110, §111.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

3. [Nanolathe spray and the construction display](#3-nanolathe-spray-and-the-construction-display)
20. [Who may reclaim, and who may not be reclaimed](#20-who-may-reclaim-and-who-may-not-be-reclaimed)
23. [The streaming economy](#23-the-streaming-economy)
29. [The nuclear silo stockpile and build progress](#29-the-nuclear-silo-stockpile-and-build-progress)
93. [Abandoned nanoframes decay](#93-abandoned-nanoframes-decay)
94. [Air repair pads: who goes, when, and what the pad does about it](#94-air-repair-pads-who-goes-when-and-what-the-pad-does-about-it)
96. [Capture: where the progress is kept, and what sets the clock](#96-capture-where-the-progress-is-kept-and-what-sets-the-clock)
97. [Automatic reclaim, `autoreclaimable`, and what `working` in SOUND.TDF is for](#97-automatic-reclaim-autoreclaimable-and-what-working-in-soundtdf-is-for)
98. [Resurrect: a real mission, a crude corpse mapping, and nothing that can use it](#98-resurrect-a-real-mission-a-crude-corpse-mapping-and-nothing-that-can-use-it)
110. [Why a construction aircraft finishes a build one tick early: a second lathe on the creation tick](#110-why-a-construction-aircraft-finishes-a-build-one-tick-early-a-second-lathe-on-the-creation-tick)
111. [The settle, re-read: one phase for every player, and what a refusal costs](#111-the-settle-re-read-one-phase-for-every-player-and-what-a-refusal-costs)

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
  `-z - y/4`, and cached on the unit until its bitmap is re-rendered. The
  water test here (`0x4592D5`-`0x4592F1`) is skipped when `[unit+0xA6]`, the
  unit's type index (§18), is non-zero -- so it is a test on the **Feature
  Unit** only: a map feature whose ground (`0x485070`) lies below the sea
  level byte casts none, and a real building is never tested;
- anything else — every mobile unit — takes the **copied** shadow if the
  second option bit (bit 3) is set and it neither hovers nor floats
  (`def+0x241 & 0x81000`, canhover and floater: so hovercraft and ships have
  no shadow). The cached bitmap is copied (`0x45A470`), every opaque pixel
  becomes index 0 (`0x4B96A0`), and it goes down five pixels right at ground
  level (§100). The cut below the water line (`0x4BA1B0`, at
  `sea - unitY + 0x32` against the height plane) is in the `0x45949D` block
  only, the one taken when the bitmap carries a height plane; a finished
  unit's bitmap has none (`0x437B50`, B4 #40), so a finished unit driving
  through the shallows keeps its whole silhouette.

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

**The blit is not it either (B4 #40, 2026-09-24).** `0x4B8500` was
followed to its span blitters. Both, `0x4CBF2C` and `0x4CC057`, remap every
pixel through `PALETTE.ALP` at `[display+0xC0]` by `(source, destination)`
and skip only the drawable's own key (§100 has the loop). The copied shadow
is a silhouette of index 0 (`0x4B96A0` writes 0 over every non-key pixel of
the copy) and the projected one is filled flat, so a shadow pixel is
`ALP[0][dest]`, the darkening, wherever the silhouette is; nothing in the
blitter looks at what is drawn later, and nothing masks one drawable by
another. The frame's own image is then blitted with its erased pixels
written as the key (`0x458DA8`), which the blitter skips. **So the code says
the frame's own shadow is visible through its erased interior**, exactly as
another unit's is. What was seen in play on 2026-09-11 says it is not. One of
the two is wrong, and the listing has now been read end to end on this
path: the draw order (`0x459200`, shadow then image), the two shadow
builders, the blitter and its table. The look that would settle it is the
earliest phase, "a bare line sweeping down" (remaining 236-255), when the
whole interior is erased: is the ground inside the outline darkened where the
frame's own shadow falls, or not? If it is, the observation was of a later
phase and the code is right; if it is not, the mask is somewhere outside the
unit draw, in how the frame's pixels reach the screen.

RWE does not wait on that: it cuts the frame's own shadow by the model's
outline, which is the look. The mobile case the issue asked about goes the
same way. A unit under construction on a factory pad has a bitmap with a
plane, takes the `0x45949D` block, and gets the copied shadow like any
other mobile unit. The copy is of the cached bitmap, not of the display's
scratch copy, so its shadow is the whole silhouette from the first frame,
and RWE cuts it by the outline in the same way.

---

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
second per player**, on whatever tick each player's counter started at -- which
§111 finds is the same tick for every player of a game, so in practice they all
settle together. The
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

Called as `(builder, target, amount)`. Every mission that lathes passes the same
amount (`0x402A09`, `0x403E43`, `0x404139`, `0x414235`, `0x414656`); the sixth
caller, `0x41BCFB`, passes a negative one for nanoframe decay (§110 attributes
all six):

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
resource, the whole job takes about `buildTime * 30 / workerTime` ticks -- see
below, "about" is doing real work in that sentence -- and the answer to "what
happens to progress when the spend is throttled" is: **nothing happens to it**. Progress is applied in full on the ticks the request is
accepted and not at all on the ticks it is refused. The throttle acts on the
resources; the debt it leaves behind is what stops the builder next tick.

A negative amount runs the same routine backwards: `0x41BBA1` refunds
`buildcostmetal * delta` into the builder's metal production. Energy is not
refunded.

Replaying this against real FBI data, an ARMCOM (`WorkerTime=300`) building an
ARMSOLAR (`BuildTime=2495`, `BuildCostMetal=145`, `BuildCostEnergy=760`) takes
250 ticks — 8.33 seconds — and drains 17.43 metal and 91.38 energy per second,
which are the numbers the original shows.

#### The tick count is not a division, and the corpus can tell

`buildTime * 30 / workerTime` is the right answer to the wrong question. The
routine above does not divide to find a duration: it steps `unit+0x104` by
`amount / buildTime` once a tick and stops when it reaches the end, and
`unit+0x104` is a **4-byte float** — `0x485B27` stores the constant
`0x3f800000` into it and `0x489B7D` compares it with `fcomp 1.0f`. So the count
is however many single-precision steps it takes, which is `ceil` of the division
except when the division comes out exact, where it depends on whether the
repeated addition lands on the endpoint or steps past it.

> **Unresolved, and it is about this field.** This document reads
> `unit+0x104`'s polarity both ways and they cannot both be right. §23's listing
> has `0x41BAD7` doing `progress - amount/buildTime`, and both §9 (target
> eligibility) and §23 itself test `unit+0x104 == 0` for *fully built*; but the
> layout table in §30 has `0x485B27` writing `1.0f` into it at spawn, and §31's
> transport load check at `0x489B7D` rejects any candidate whose value is not
> exactly `1.0f`, which only makes sense if `1.0f` is the complete end.
> Whoever settles it should fix the losing side rather than add a third reading.
> Nothing below depends on the answer — the corpus replay gives the same tick
> count counting up to 1.0 or down to 0.0, so what follows is about the *width*,
> which both readings agree on.

That is not a distinction worth asserting from a listing, so it was checked
against real games. Over the demo corpus there are 15 builder/product pairs
whose `BuildTime` is an exact multiple of the builder's `workerTime / 30`; ten
of them take an extra tick and five do not, the float32 replay predicts which
ten, and the same replay in `double` gets 6 of 15. Across all 41 scored
pairs the replay is exact where `ceil` manages 10 and `floor` 36. The evidence,
the table and the re-runnable check are in
[TA-DEMOS.md](TA-DEMOS.md), under `0x09`.

Two things fall out of that for anyone reading this section:

- **The first increment lands on the tick the nanoframe is created**, not the
  tick after. The corpus measures nanoframe-to-finish as one less than the
  number of increments, without exception on a factory build.
- **RWE's integer `addBuildProgress` is already right** everywhere `BuildTime`
  is not a multiple of the rate, and one tick fast where it is. Putting a
  `float` in the simulation to close that is very likely a bad trade; see the
  determinism rules in `CLAUDE.md`.

One thing did not fall out, and is now explained in §110: **a construction
aircraft finishes one tick sooner than the replay allows**, 60 of its 68 builds
in the Escalation corpus with none faster. It gets two increments on the
creation tick where a factory gets one: `VTOL_MobileBuild` discards the answer
of its stance wait, and the wait's wake mask lets a COB event left pending since
the last `set` run the lathe a second time before the tick ends. The call-site
list above is also short by one; §110 has all six, attributed to their missions.

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
  stockpile as storage instead. **The demo corpus says that route reaches the
  right number**, which is as close to an answer as the binary has given: every
  one of the 86 players in the thirteen demos opens on a capacity of exactly
  1000 metal and 1000 energy, with a stockpile that started at 1000 as well, and
  neither data set's commander declares any `MetalStorage` or `EnergyStorage` of
  its own. So in a skirmish the base is the starting stockpile, whichever field
  the original reads it out of. See `docs/TA-DEMOS.md`.
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

### `SELFREPAIR`, and the repair tick every repairer in the game shares

Read out 2026-09-15 for issue #52, which asked whether RWE's pads may be said
to *match*. They may now, and the read turned up two divergences, one of them
worth a fifth of all repairer/target pairs in the shipped data.

**Where the mission comes from.** `VTOL_Landing` (`0x4118E0`) runs a seven-state
machine, and in the state that finishes a landing it checks the pad it landed
on — `mission+0x16` — against three things before doing anything else:

```
411e7c  ecx = [mission+0x16]         ; the pad
411e7f  eax = [ecx+0x92]             ; its definition
411e85  eax = [eax+0x241]
411e8b  test ah,0x2                  ; bit 9, isairbase
411e90  test al,0x40                 ; bit 6, builder
411e94  fld [ecx+0x104] ; fcomp 0.0  ; and finished, not a nanoframe
411eb1  call 0x4b4f10                ; new Mission(0x56 bytes)
411ece  push 0x501c24                ; "SELFREPAIR"
411ee1  call 0x43acb0                ; appended to the AIRCRAFT's order list
```

Bit 6 is `builder`, parsed at `0x42C4CB` from the key at `0x503C44` — the same
run of boolean keys §34's table comes from. So a pad that is not a builder, or
that is still a frame, parks the aircraft and mends nothing.

`SELFREPAIR` is not a VTOL mission at all: it is row 20 of the **ground** table
at `0x4FC490`, handler `0x402430`, and the two tables are merged into one sorted
vector at startup, so either side can name either. Its state machine has three
states: 0 re-tests the pad's `builder` bit and build progress and starts the
mission, 1 does the work, 2 prints "Unit repaired" (`0x5012CC`). State 1, per
tick:

```
4024af  if (unit.hitPoints >= def.maxdamage) return done
4024e1  unit+0xB0 = clock + 150           ; the nanolathe stance deadline
4024f0  cx = [padDef+0x1FE]               ; the PAD's WorkerTime
4024f7  work = WorkerTime / 30            ; integer
402518  call 0x41BD10(pad, aircraft, (float)work)
40252e  0x43E400 / 0x4720D0 type 6        ; the nanolathe spray, pad to aircraft
4025bb  sleep 1 tick
```

Two things to notice before the arithmetic. The mission belongs to the
**aircraft** and the rate comes from the **pad**, and the sleep is one tick, so
this runs every tick like any other builder's work. RWE arrives at the same
pair of facts from the other side — its pads run the repair from the ordinary
builder path, with the pad as the repairer — and that is not a divergence, only
a different place to put the loop.

**The repair tick, `0x41BD10`.** This is not the pad's own routine. It has five
callers — the two ground `Repair` missions, `VTOL_RepairUnit`, `SELFREPAIR`, and
`0x48AF92` — so it is *the* repair tick, and what it says about pads it says
about every builder in the game:

```
41bd33  if (hitPoints >= maxdamage) return 0
41bd48  hp     = ftol((maxdamage       * work - 1.0) / buildtime + 1.0)
41bd68  energy = ftol((buildcostenergy * work - 1.0) / buildtime + 1.0)
41bd87  if (hp     >= 1) hp     = 1
41bd97  if (energy >= 1) energy = 1
41bdb7  if (!0x401180(&pad->economy, (float)energy)) return 0
41bdc7  0x489BB0(pad, aircraft, hp, cause 10, 0)
```

Both clamps are **upper** bounds, and that is the whole of the behaviour. The
expression reaches 1 whenever the product does — `maxdamage` and `work` are both
integers, so `(n-1)/bt + 1 >= 1` exactly when `n >= 1` — so the elaborate
formula collapses to:

> **A repairer with any worker time at all mends exactly one hit point a tick
> and pays exactly one energy for it, whatever it is mending and however fast a
> worker it is. One with none mends nothing.**

Repair scales with the *number* of repairers, not with their WorkerTime. Thirty
hit points a second each, thirty energy a second each. Nothing in the shipped
data has a WorkerTime between 1 and 29 — the values present are 0, 30, 50, 80,
100, 125, 160, 200, 225 and 300 over 258 units — so the "mends nothing" case is
unreachable there and is recorded rather than relied on.

The energy request is `0x401180`, the **single-resource** one, not the
two-resource `0x4011C0` the build path uses. It books the demand on the
repairer's own block either way and answers no only when that block is still
paying off an *energy* shortfall, so a player who owes metal can still repair.
And `0x41BDBC` tests the answer: refused means no hit points at all, not fewer.
The heal itself goes through the damage choke point with **cause 10**, which
`0x489BB0` short-circuits at `0x489BC1` — no armour, no veterancy, no
direction — straight into adding hit points.

**What RWE had wrong.** Two things, both now fixed and both pinned in
`sim/repair.test.cpp`:

- `deployRepairArm` computed `max(1, maxHitPoints * work / buildTime)` — a
  *lower* clamp where the original has an upper one. For most aircraft the two
  agree at 1, which is why the pads looked right; they diverge wherever a unit
  is cheap in build time and rich in hit points. A construction vehicle mending
  a dragon's tooth — 3500 points on a buildtime of 520 — ran it up forty points
  a tick instead of one. Counted over the shipped data, 403 of 2080
  repairer/target pairs differed.
- The comment beside it said "Repairing costs nothing, as in TA". It does cost:
  one energy a tick, and a builder whose owner is in energy debt repairs
  nothing. `GameSimulation::addEnergyRequest` is `0x401180`, added for this.

**And writing an end-to-end test for the pads found that they had never mended
anything at all.** Two faults, both in RWE alone and neither visible from the
findings, because every test on this until now called the patient search
directly rather than ticking the simulation:

- the search uses `airBaseRepairReach` — the larger of `BuildDistance` and half
  the pad's own footprint, which is the whole reason that helper exists — and
  then handed the patient to `repairExistingUnit`, which re-tested the distance
  with `BuildDistance` **alone**. ARMASP's `BuildDistance` is 6 against a
  four-by-four footprint 64 world units across, so the re-test refused every
  patient the search had just found, and sent an immobile pad off to "navigate"
  towards an aircraft parked in its own middle. The pad path goes straight to
  `deployRepairArm` now.
- and the builder update ends with `else { changeState(Idle) }` for anything
  with no orders, which runs *after* the repair block — so it wiped the building
  state the pad had entered a few lines earlier and told the script to stow the
  arm, every tick. The reset skips a pad that is mending.

`sim/airbase.test.cpp` ticks the simulation and watches the hit points now, so
neither can come back quietly.

### Where an aircraft actually comes to rest, and the third pad fault

A play-test the same day: "airplanes sink through the pads when trying to land
on them and never regain health". Both halves were real, and the second had a
cause neither of the two above accounts for.

**The deck is a piece of the pad's model.** `VTOL_Landing` asks the pad's own
script where to put the aircraft. It calls `QueryLandingPad` — the string at
`0x501C14` — at `0x411A35` and again at `0x411CEC`, gets back up to four piece
ids in the query's locals, tests each with `0x47E570` until it finds one that is
free, keeps it on the mission at `mission+0x36`, and hands it to the navigator
as a **piece** goal rather than a point (`0x44E250` at `0x411D60`). The circling
approach before that is a point on a ring about the pad at the pad's own `y`,
stepped a quarter turn each time round (`0x411AF1`–`0x411B3F`).

ARMASP's `QueryLandingPad` answers with piece 1, `landpad`, and in `ARMASP.3DO`
that piece sits at **(0, 20, 0)** — twenty world units above the pad's base.
CORASP, ARMCARRY and CORCARRY all carry the same function. The one other thing
the landing goal is given is a height offset, and it is not the pad's:
`0x411D6F`–`0x411D8D` reads `def+0x170`, the model height, of whatever the
aircraft is **carrying**, so a transport sets down high enough not to bury its
cargo, and passes zero when it is empty.

RWE's `descendToGroundLevel` used the terrain height and nothing else, so an
aircraft landing on a pad sank twenty units through the platform and came to
rest inside the ground. It runs the pad's `QueryLandingPad` and stops at that
piece's world height now — only the height, since the piece's x and z are the
pad's own on every shipped pad and the navigation has already brought the
aircraft over it, and only while the aircraft holds a `LandOnAirBaseOrder` for a
pad it is actually above.

**And the third reason nothing was ever mended: none of the four `isairbase`
units has a `StartBuilding` thread.** ARMASP and CORASP carry SmokeUnit, Create,
SweetSpot, QueryLandingPad, QueryNanoPiece and Killed; the two carriers not even
QueryNanoPiece. Their `Create` does not touch `INBUILDSTANCE` either — ARMASP's
zeroes a static and starts SmokeUnit, and that is all of it. RWE's builder path
waits for the build stance before it works, so it created a `StartBuilding`
thread that found no such function and then waited for a flag nothing would ever
set. The original has no stance test anywhere in its repair tick; RWE's is a
sequencing device for the nanolathe animation, and now applies only to a unit
whose script actually raises an arm.

`sim/airbase.test.cpp` flies a damaged fighter to a pad, lands it, and checks
both the deck height and that the hit points climb — the whole journey in one
test, because each piece of it was already covered in isolation and every fault
was in a join.

### The pad's own footprint, and why none of the above was enough

The play-test after that one: "they land on the pads but do not repair, and
while a few brawlers were sat on the pads I set some other random planes to
patrol and it kicked the first lot off with the message 'none available'". One
cause underneath both halves, and it is a piece of RWE's own data handling
rather than anything in the binary.

**ARMASP's yardmap is `oooo oooo oooo oooo`, and `o` is not an open cell.**
`parseYardMapCell` maps it to `YardMapCell::Ground`, and `isPassable` says
Ground is impassable — correctly, because that is what stops a tank walking
through a building. So the pad's whole four-by-four footprint blocks movement,
and `tryTransitionFromAirToGround` tested the aircraft's footprint against it
like any other touchdown and refused. The aircraft set `landingFailed`, climbed
away, came back round, and did it again for ever.

Everything else follows from that. It never became a ground unit, so
`findAircraftToRepairOnPad` — which skips anything still flying — never saw it,
and nothing mended it. And `airBaseIsClaimedByAnother`'s physical-occupancy
branch also asks for a ground unit, so an aircraft cycling over a pad never
held it: a later arrival was offered the same pad, and then turned the first
one away with "Landing aborted: no pads available". The whole report is that one
fault seen from three sides, and it was invisible to every test here because the
fixture had written `GroundPassable` into the pad's yardmap and called it
"sixteen open cells".

**The original never asks the question.** A landed aircraft is *attached* to the
pad: `0x48AAC0` links it into the pad's list and `0x47E570` walks the links to
see which landing slots are free. It is carried, not standing on cells, which is
why `QueryLandingPad` returns *pieces* and why a pad's yardmap has nothing to do
with it. RWE has no attachment, so the equivalent is to let the aircraft through
the footprint of the one pad it is landing on:
`GameSimulation::isCollisionAtIgnoringBuilding`.

Two smaller things went with it, both consequences of the aircraft now sitting
twenty units up rather than on the ground:

- **Every "is it standing on that pad?" test measures flat now**
  (`distanceSquaredXZ`). Against a reach sized from the pad's footprint — 32
  world units for a four-by-four — a deck height of 20 ate two fifths of the
  radius, and the thing it stands in for, the original's attachment, has no
  notion of height at all.
- **Standing on a pad beats every claim on it.** The claim test in
  `handleLandOnAirBaseOrder` ran *before* the standing-on-it test, so a later
  arrival with a lower unit id could turn a parked aircraft off its own pad.
  The guide's rule is "until the occupying aircraft has been repaired *and has
  left*", and the lower-id tie-break is only for two aircraft both still in the
  air.

Not ported, and neither has anywhere to attach: `unit+0xB0`, the
clock-plus-150 stance deadline, which RWE keeps as build-stance state instead;
and the spray's own particle type, RWE running the nanolathe effect it already
has.

### What RWE does not do

The pad choice is a `rand(n)` over the candidates in the original and a
determinism-safe modulo draw here, remembered in the navigation state rather
than re-rolled every tick — the original re-rolls because it swaps the mission
once and then never reconsiders, which comes to the same thing.

`VTOL_Standby`'s carrying-something hop and the sight-range search `0x43B700`
remain unported for the reasons §91 gives.

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
`0x489BB0(reclaimer, target, mission+0x36, type 5, 0)`, ordinary damage of a
type all its own, so a unit being reclaimed is visibly taken apart rather than
dissolved on a timer, and its health bar in the info panel's second slot is
the progress (§99). The bite is sized once, as the work starts, by
`0x438650(reclaimer, target, 15)`:

```
trunc( workertime(reclaimer, def+0x1FE)
       * ((kills(reclaimer, unit+0xB8) + 5) / 5)         ; integer veterancy
       * maxdamage(target, def+0x1FA) * 15
       / (300 * max(buildcostmetal(target, def+0x18A), 10.0)) ), at least 1
```

and it lands every sixteen ticks, not fifteen: each pass sleeps two ticks and
adds two to `mission+0x3A`, and the bite is taken once that has reached
fifteen (`0x40496E`), so the count runs 0, 2, ... 14, 16. `MaxDamage` cancels
out of the total, so a unit is reclaimed in `300 * buildcostmetal /
(workertime * veterancy)` ticks of bites whatever its hit points: ten seconds
per metal-per-workertime, plus the sixteenth.

**Ported 2026-09-24 (#19):** `computeUnitReclaimStep` is the formula and
`GameSimulation::reclaimUnitStep` the bite, on `UnitBehaviorStateReclaiming`'s
sixteen-tick `stepCounter`; `UnitState::reclaimProgress`, the drained
work-count it replaced, is gone. Two things stay RWE's own and are §88's: the
bite is taken bare off the hit points, without asking whether cause-5 damage
skips armour the way the cause-10 repair does (not read), and the payback is
still credited a bite at a time in both resources, where the original pays
`trunc((1 - progress) * buildcostmetal)` in one lump as the unit dies, metal
only (`0x402666`).

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

## 110. Why a construction aircraft finishes a build one tick early: a second lathe on the creation tick

Over the demo corpus a construction aircraft finishes one tick sooner than §23's
float32 replay allows, where a factory lands on it exactly. The cause is not a
different build call, a different rate or a different place in the tick. It is
the mission service loop running the aircraft's build state **twice on the tick
the nanoframe is created**, so the job gets two increments on that tick where a
factory gets one. What sets it off is a COB event nothing had consumed, and
what lets it through is one discarded return value in `VTOL_MobileBuild`.

### The two packets bound the increments exactly

- The `0x09` is sent from inside the unit constructor `0x485F50`, at `0x486115`
  (`call 0x456050`, which writes the 23-byte record and hands it to
  `0x451DF0`). Every build mission creates its nanoframe through that
  constructor, so the `0x09` goes out on the tick of creation, from inside the
  mission handler.
- The `0x12` is sent from inside `0x41BA60` itself: when a step leaves
  `unit+0x104` at `0.0f` (`0x41BCAA`), it calls `0x41B8D0`, which sends it at
  `0x41BA26` (`call 0x4560C0`, 5 bytes). `0x48612A` is the only other sender,
  for a unit created already complete.

So an episode's `finishTick - startTick` counts the increments between the two
and nothing else; neither packet batching nor end-of-build bookkeeping can move
it.

### Six call sites, three missions

`0x41BA60` has **six** callers, not the five §23 listed. Attributed through the
mission tables (ground at `0x4FC490`, VTOL at `0x4FCA18`; 25-byte records,
handler at `+4`, layout in `TOTALA-EXE-MISSIONS.md` §1):

| Call | Mission | Handler |
|---|---|---|
| `0x402A09` | `BuildingBuild` (a factory) | `0x402640` |
| `0x403E43` | `MobileBuild` (a ground constructor) | `0x403A20` |
| `0x404139` | `HelpBuild` | `0x403F70` |
| `0x414235` | `VTOL_MobileBuild` | `0x413D80` |
| `0x414656` | `VTOL_HelpBuild` | `0x414380` |
| `0x41BCFB` | `0x41BCD0`, a negative amount: the nanoframe decay `GetBuilt` runs at `0x402F6B` (§93) | — |

The three that start a job order their states differently, and the difference
is the finding:

| Mission | Jump table | Order of events |
|---|---|---|
| `BuildingBuild` | `0x402B5C` | wait for `INBUILDSTANCE` (`0x4027CA`), **then** create (`0x4028EA`), return 1; lathe every tick (`0x4029CA`) |
| `MobileBuild` | `0x403F5C` | create (`0x403D5B`) and start `StartBuilding`, return 1; wait for `INBUILDSTANCE` and **return its answer** (`0x403DF5`); lathe (`0x403E0C`) |
| `VTOL_MobileBuild` | `0x414330` | create (`0x41409B`) and start `StartBuilding`, return 1; call the wait and **discard its answer**, falling straight into the lathe (`0x414136` → `0x414149` → `0x414235`) |

The lathe tail is the same in all three: call `0x41BA60`, and if the job is not
done set a one-tick timer (`0x439E80`, which also sets wake bit 0), OR `0xA`
into the wake mask and return 2.

### The pieces

**The stance wait, `0x438700(unit, mission, mask)`.** If `unit+0x10F` bit 0
(`INBUILDSTANCE`) is set it returns 1 and touches nothing. Otherwise it
**writes the mission's wake mask** to `mask | 4` and returns 2. Bit 2 of a wake
mask answers bit 2 of the unit's event word `unit+0xBA`.

**Every COB `set` raises that bit.** `SET_VALUE` (`0x10082000`, dispatched at
`0x4B1B48`, called at `0x4B1BD2`) goes through the unit callback's vtable slot
at `0x4FD6D8`, which is `0x480B20`. That routine switches on the port, and every
arm, the default included, ends in `or byte ptr [unit+0xBA], 4` (`0x480B57`,
`0x480B73`, `0x480B97`, `0x480BB3`, `0x480BD2`, `0x480BF1`). Port 5,
`INBUILDSTANCE`, is the arm at `0x480B62` that writes `unit+0x10F` bit 0; port
20, `ARMORED`, calls `0x48B090` first. Nothing clears the bit except the service
loop consuming it for a mission that waits on it, and `0x438700` and its sibling
`0x438730` have eleven callers of which **`VTOL_MobileBuild` is the only VTOL
mission**. So on an aircraft the bit is sticky: the first `set` after the last
build leaves it pending until the next one.

**`StartBuilding` is queued, not run.** `0x438590` starts it through `0x4B0B00`
with the run-now argument zero (tested at `0x4B0B82`), so the script's own
`set INBUILDSTANCE to 1` happens in a later COB pass, never inside the mission
that asked for it. On the creation tick an aircraft's stance is still clear.

**The service loop re-runs the head mission in the same tick** (`0x43B7C0`).
After a handler returns it jumps back to the top (`0x43B99F` → `0x43B7DD`) and
leaves only when the wake mask is non-zero **and** no pending bit matches it
(`0x43B817`–`0x43B81D`). Pending means `mission+0x4E | unit+0xBA`; the matched
bits are cleared and the mask zeroed before the handler runs (`0x43B831`,
`0x43B83D`, `0x43B846`). A handler that returns 1 has left the mask at zero, so
the next state runs at once, which is why every mission creates and lathes on
the same tick.

### The creation tick, for each

A construction aircraft with a `set` pending from earlier:

1. State 2 creates the nanoframe (`0x09` sent), queues `StartBuilding`, returns
   1. The mask is zero, so the loop runs state 3 immediately.
2. State 3: `0x438700` finds the stance clear and writes the mask to `0xE`; the
   answer is discarded; `0x41BA60` lathes (**increment 1**); the timer and `0xA`
   make the mask `0xF`; return 2.
3. The loop checks: the timer is a tick away, but `unit+0xBA` bit 2 is pending
   and the mask now has bit 2. It consumes the bit and runs state 3 again.
4. State 3 again: stance still clear, mask `0xE`, **increment 2**, mask `0xF`,
   return 2. Nothing is pending now; the loop leaves.

From the next tick on there is one increment a tick. The queued `StartBuilding`
has set the stance by then, raising bit 2 again, but that bit is consumed in the
same pass as the timer's; and with the stance set `0x438700` stops putting bit 2
in the mask at all. So the job gets exactly one extra increment over its life,
on the creation tick.

A **factory** waits for the stance *before* creating, so its lathe state never
calls the wait and its mask stays `0xB`: one increment on the creation tick. A
**ground constructor** does wait after creating, but its wait state returns the
answer, so a stale bit 2 re-runs the *wait*, which lathes nothing, and the lathe
starts only once the stance is set. That delay is the constructor's own script,
which §23 and `TA-DEMOS.md` already treat as mod data.

`tools/exe/buildloop.py` transcribes exactly these pieces and prints the three
durations. For `CORCA` on `CORDRAG` (`BuildTime` 1130, p=2, 566 increments):
factory 565, ground constructor 566 (with a `StartBuilding` that does not
sleep), aircraft **564** with a stale event and 565 without. The answers are the
same whichever order the COB and mission passes run in, which is not settled
here.

### Why the event is always there in Escalation

The corpus's three airborne builders ship scripts (read with
`tools/exe/coblist.py`) in which `StartBuilding` and `StopBuilding` are each one
unconditional `set INBUILDSTANCE`, and `Activate` and `Deactivate` `set ARMORED`.
So the previous job's `StopBuilding` leaves bit 2 pending and the stance clear.
An aircraft's first build is covered too: `VTOL_MobileBuild`'s state 0 turns
activation on (`0x413E40`, `0x48B090(1,1)`), which queues `Activate` through
`0x4B0940` if the aircraft was not active, and `Activate` `set`s `ARMORED`. The
prediction that follows, that an aircraft whose scripts had `set` nothing before
its build would land on the factory model and not a tick under it, has no build
in the corpus to test it.

### Against the corpus

Scoring airborne builders against `ticks_to_build - 2` instead of `- 1`, over
the Escalation demos as `tad_episodes` emits them now (68 airborne builds under
the -20..+120 cap; the "58 of 66" this was first reported as predates the id
scoping):

- **60 of 68 builds land on the model exactly, and none is early**, across all
  three builders (`CORCA` 47 of 55, `CORACA` 9 of 9, `ARMCA` 4 of 4) and both
  rates.
- **All 15 (builder, product) modes agree**, including the 10 pairs whose
  `BuildTime` divides exactly by p, where the float32 accumulator decides whether
  one more increment is needed: `CORDRAG`, `CORMEX`, `CORMOHO`, `CORHP` and
  `CORMAKR` take it, `ARMDRAG`, `ARMRL`, `CORARAD`, `CORSES` and `CORSMS` do
  not, and the corpus follows each. So the aircraft's shortfall is a shift in the
  count of the same accumulator, not a rate or a rounding of its own. Most of
  those cells hold one to three builds, which makes this corroboration rather
  than proof; the three `CORCA` cells with seven or more builds are what
  `tools/tad-buildtime.py` scores by default.
- **The other eight are late by whole seconds**: +30 twice, +60 three times,
  +120 three times. That is not an aircraft effect. It reads as a resource stall
  under §23's throttle, where a refused builder's debt is looked at only by the
  once-a-second settle, and factories show the same shape: of the 384 factory
  builds late against the model, **347 are late by an exact multiple of 30**.

### Escalation's binary

Escalation's patched `TotalA.exe` changes none of it. `tools/exe/patchdiff.py
--range` finds no patched byte in `VTOL_MobileBuild` (`0x413D80`–`0x414350`),
`VTOL_HelpBuild`, `BuildingBuild`, the service loop (`0x43B7C0`–`0x43BAD0`),
`0x438590`–`0x438760`, `0x439E80`, the set callback (`0x480B20`–`0x480C30`) or
its vtable, the VM's `SET_VALUE` dispatch, `0x4B08C0`–`0x4B0BB5`, `0x48B090`,
`0x41B8D0`, the unit constructor `0x485F50` or the `0x09` sender. The nearest
change on a build path is two bytes in the ground `MobileBuild` at `0x403D29`,
retargeting a call made before the nanoframe exists (`0x4898B0` to `0x489800`).
It is not on the lathe path and was not followed. The unit-initialisation patch
at `0x485C69` moves the code around the write of `unit+0xBA` but still zeroes
it.

### What it means for RWE, and what was done about it

**Ported**, in `UnitBehaviorService.cpp`. Both halves:

- `deployBuildArm` no longer gates a `canfly` builder on `inBuildStance`. The
  gate stays for a factory and a ground constructor, which is where the
  original has it, and it covers assisting as well as building because both go
  through that one handler -- `VTOL_HelpBuild` does not consult the stance
  either.
- `buildUnit` passes `frameJustCreated` on the tick it picks its own finished
  creation up, and that tick runs the lathe **twice**. The lathe moved into
  `latheNanoframe` so it can be called twice in a tick the way the service loop
  calls it, and the sequence in the handler is the original's: deploy (which is
  `StartBuilding`, queued), lathe, lathe. No new simulation state -- the second
  increment is sequenced inside the one call rather than remembered across
  ticks -- so there is nothing for the four-places rule to catch.

**Where the creation tick is, in RWE.** The original creates the nanoframe and
lathes it inside one mission pass on one tick. RWE cannot: a behaviour handler
may not add a unit while the behaviour pass is walking the unit map, so a build
order pushes onto `unitCreationRequests` and `spawnNewUnits` lays the frame down
at the end of that tick. The builder meets its own frame on the **next** tick,
as `UnitCreationStatusDone`, and that is the tick RWE treats as the creation
tick: the first tick on which there is a frame to lathe at all. The two
increments land there. So the relation the corpus actually pins -- two
increments on the job's first tick and one a tick after, ending a tick before a
factory at the same rate -- holds exactly; what differs is a fixed tick of
scheduling latency between the order and the frame, which no episode measures.
A factory pays one tick more of that latency still (§88).

**The fixture.** Airborne cells are now in it, on the same delta convention as
the factory ones, because the emitter subtracts two increments rather than one
for that class: `CORCA` to `CORDRAG` -1, to `CORMEX` -1, to `CORRAD` 0. The -1s
are §88's integer accumulator on a divisible pair and nothing else, which is the
point of porting rather than licensing -- there is no airborne delta left to
write down. `buildtime.test.cpp` drives the accumulator with the double credit
and carries a case showing what it buys: credited once, all three cells come out
a tick late against the games they were measured in. The behaviour half is
`aircraftbuild.test.cpp`, in the real simulation on `CORCA`'s own FBI figures.

It took one session. The discarded return value at `0x41413E` was visible as
soon as the two mobile build handlers were read side by side; what took the time
was finding what could make the service loop run a waiting mission again in the
same tick, and that was the sticky event bit.

## 111. The settle, re-read: one phase for every player, and what a refusal costs

§23 decoded the once-a-second settle and RWE ported it. This section is the
second reading, done to check §23 against the demo corpus's stall evidence
before anything was asserted from it. It confirms the arithmetic, corrects the
claim that players settle on staggered ticks, and lists everywhere RWE's port
still differs. The corpus side is in [TA-DEMOS.md](TA-DEMOS.md), under `0x28`,
"What a stalled settle costs a factory".

**Escalation did not touch any of it.** `tools/exe/patchdiff.py --range` finds
no patched byte in `0x4011C0`–`0x401360` (the request and the three direct
charges), `0x401360`–`0x401C20` (the settle), `0x464F80`–`0x4655B0` (the
per-player pass), `0x402640`–`0x402B80` (`BuildingBuild`) or `0x41BA60`–`0x41BCD0`
(the lathe). So the Escalation demos are valid evidence for everything below.

### One settle, in order

`0x401360(player)`:

1. **Unit sweep**, every unit with `unit+0x110` bit 28, stride `0x118`:
   - **`EnergyUse` is asked for here, once, for the whole second**, not a tick
     at a time. On the switch path (bit 29, unit on) and on the no-switch path
     (on or moving, bits 2-3) alike, `EnergyUse > 0` adds to *asked*
     (`+0xC0`) and, only if the unit's **energy** owed (`+0xC8`) is not
     positive, to *granted* (`+0xC4`) and sets the powered flag
     (`0x4013F9`, `0x40164F`). That gate reads energy owed only; the request
     routine `0x4011C0` a builder uses tests both.
   - `EnergyUse < 0` is production, handicapped for a computer player, and
     **leaves the powered flag clear** (`0x40147E`), so such a unit never takes
     the extraction or metal-maker branch that follows.
   - The extraction / `makesmetal` / wind / tidal chain is on the switch path
     only; the no-switch path jumps straight to `0x4016C5`. §23 reads bit 29 as
     "has a switch". That reading is not confirmed here -- the constructor does
     not copy `onoffable` (`def+0x245` bit 2, stored at `0x42C8C9`) into it
     directly -- and it matters for four Escalation metal makers that declare
     no `OnOffable` (ARMFORGE, CORVAULT, ARMUWMFUS, CORUWMFUS).
   - `EnergyMake`, `MetalMake` and both storage figures, for `unit+0x104 == 0`.
   - **Cloak** (`0x4017D9`): the cost is truncated (`0x4E43A0`), compared with
     the player's energy **stockpile** and, if covered, *subtracted from the
     stockpile on the spot* (`0x40184A`), with the amount added to *asked* for
     the display only. It never enters *granted*, so it is never throttled and
     never becomes debt.
   - The unit's eight live figures are added to the running totals.
2. **Player block** `player+0xEC`, the same layout, added to the totals; then
   the per-player base storage when `player+0x149` bit 0.
3. Display copies and the lifetime totals (the qwords at `player+0xAC`-`+0xC4`).
4. `S = stockpile + produced` per resource (`0x401A1D`, `0x401A25`).
5. **The fractions**, energy then metal, `0x401A4D`, exactly as §23 has them.
   The arithmetic runs on the x87 stack at 80 bits, but the two fractions are
   stored as 4-byte floats (`fstp dword` at `0x401A72` and `0x401A9F`) before
   the per-unit pass reads them back.
6. Stockpile clamped to the storage **this same settle** rebuilt, the excess
   added to the waste total.
7. **Per-unit debt** `0x401B37`, then the player block's at `0x401BAE`:
   `owed' = (granted - granted * newFraction) + (owed - owed * debtFraction)`,
   the display copies taken, and produced, asked and granted zeroed.

### What a refusal costs

A factory's lathe charges its own block through `0x4011C0` (`0x41BC60`), which
refuses when **either** resource is owed, records the ask for the display, and
applies no progress. Nothing but step 7 writes *owed*, so:

- A builder granted anything in a second whose settle falls short carries debt
  out of it and is refused **every tick** until a settle pays it.
- A settle that can pay it all has `debtFraction = 1.0`, and `owed - owed * 1.0`
  is exactly zero, so the refusal ends **on** that settle and on no other tick.
- A settle that cannot pay it leaves `debtFraction < 1` and the builder still
  owes, so a long stall costs a whole second per settle.

So a stall delays a job by whole seconds, and a job started while its factory
still owes for the one before is delayed to the next settle that pays. The
corpus shows both; see TA-DEMOS.md.

### Every player settles on the same tick

§23 and §88 said the settle is staggered per player "by whatever each player's
counter started at". The counter is `player+0xF0`, and it has exactly two
writers besides the `+= 30` at `0x465092`:

- `0x464715`, in `0x464700(player)`, which stores the current game tick
  (`global+0x38A47`). Its one caller, `0x464990`, walks **all ten player slots
  in one loop** (`global+0x1B63`, stride `0x14B`), so every player of a game
  gets the same tick.
- `0x46623F`, which reads `UpdateTime` back out of a saved game.

So in a game started normally every player settles on the same tick, and the
stagger exists only if a saved game recorded different `UpdateTime`s. The corpus
agrees without exception worth the name: 60,588 of the 60,760 non-watcher `0x28`
samples, from all 86 senders in the twelve Escalation demos, are stamped within
6 ticks of a multiple of 30 of the demo clock, and the least aligned sender still
has 208 of 211 there.

**Inside a tick**, the step at `0x4954BD` increments the game tick, then runs
`0x48AD30` -- the unit pass, which reaches the mission service loop `0x43B7C0`
at `0x48AF98` -- and only after the projectile (`0x49B720`) and feature
(`0x420F30`) passes calls the per-player pass `0x464F80`. So a tick's lathe is
counted into that same tick's settle. The corpus puts a stalled factory's first
accepted increment on demo ticks that are multiples of 30, which with that order
means either the settle runs at internal ticks one short of a multiple of 30 or
the demo clock is stamped a tick behind the internal one. **It is the second.**
The weapon work settled it: everything queued during the unit pass -- a `0x09`
among it -- goes into the sender's buffer *before* that tick's `0x2c`
(`0x48B003`), so it is stamped with the previous tick's serial. See §7, "Which
tick a new round first moves on", and `docs/TA-DEMOS.md`, "Which tick a round
first moves on", for the decode and the stream measurement behind that. Nothing
depended on it either way: the seconds partition the lathes the same way.

### Where RWE's economy differs

Checked against `GameSimulation::updateResources`, `settleResourcePool`,
`UnitState::addResourceDelta` / `settleResources` and the builder paths in
`UnitBehaviorService`.

| # | Difference | Documented before? | Effect |
|---|---|---|---|
| 1 | RWE settles every player when `gameTime % 30 == 0`. | §88, as a deliberate divergence from a stagger. | **None in a normal game** -- the original does the same, above. §88 is updated. |
| 2 | RWE settles before the behaviour pass in a tick; the original after the unit pass. | No. | None: both count a tick's lathe into the same second (RWE's settle at 30 closes ticks 0-29). The stall episodes replay RWE's order and land on the corpus. |
| 3 | RWE's `EnergyUse` request went through `UnitState::addResourceDelta`, which refuses a unit owing **metal** as well; the original's settle sweep checks energy owed only. | No. | **Fixed 2026-09-20.** `ResourceDebtGate` names the two gates apart: the settle sweep's `EnergyUse` request passes `EnergyOnly` and reads the unit's energy owed alone (`0x4013F9`, `0x40164F`), while every other caller keeps `EnergyOrMetal`, which is what the request routine a builder goes through does (`0x4011C0`). A unit that owes metal but has the energy now stays powered. Pinned by "a unit that owes metal but has the energy stays powered" in `economy.test.cpp`. |
| 4 | RWE treats a negative `EnergyUse` as production **and** reports the unit powered, so it still extracts or makes metal; the original leaves it unpowered. | No. | None on Escalation's data: no unit has both a negative `EnergyUse` and an extractor or metal maker. |
| 5 | RWE runs the make-and-use pass for any `activated` unit; the original runs the extraction/maker/wind/tidal chain only on the bit-29 path and charges a no-switch unit's `EnergyUse` while it is on **or moving**. | No. | Open while bit 29 is unconfirmed; see the four Escalation metal makers above. **Decided 2026-09-17: not chased.** It moves four units in one mod and nothing depends on it; the bit stays an open question. |
| 6 | ~~**Cloak**: RWE compares the untruncated cost with the stockpile and then *requests* it through `addResourceDelta`~~ **Fixed 2026-09-24** (#143): `GameSimulation::chargeStockpile` truncates the cost, compares it with the stockpile and subtracts it there and then, booking it as demand for the display. Pinned in `concealment.test.cpp`. | Yes. | Hashed state moves: a cloak used to reach `player.energy` at the settle, now on the tick. |
| 7 | ~~**Weapons** (`energypershot`, `metalpershot`): RWE checks the stockpile, then *requests* the cost through `addResourceDelta`~~ **Fixed 2026-09-24** (#143): the fire path calls `chargeStockpile`, which takes the price from the stockpile on the spot or refuses the shot (`0x401220`, `0x401260`, `0x4012A0`). Several shots in one second each see the stock the last one left, and a shooter that owes for earlier work pays rather than firing for free. Pinned in `economy.test.cpp`. | Yes. | Hashed state moves: every energy weapon's cost lands on the fire tick instead of the settle, and never throttles the builders. |
| 8 | Debt carried as `owed * (1 - f)` in float against the original's `owed - owed * f` at 80 bits. | No. | Sub-ulp. A fully paid debt is exactly zero in both. |
| 9 | RWE clamps a negative supply to zero before settling. | In the code. | None: the original's stockpile cannot go negative. |
| 10 | The computer player's production handicap. | §23, not ported. | Tuning, not compatibility. |

Nothing in the table was changed. Row 7 is the one worth a decision.
