# What the original executable does: the renderer, effects, shadows and placement

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §4, §5, §26, §27, §28, §50, §51, §52, §53, §54, §55, §56, §57, §58, §59, §100, §101.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

4. [Effects a script asks for: `emit-sfx`](#4-effects-a-script-asks-for-emit-sfx)
5. [Render order](#5-render-order)
26. [The marching waypoint trail](#26-the-marching-waypoint-trail)
27. [The building placement box](#27-the-building-placement-box)
28. [The placement animation](#28-the-placement-animation)
50. [The interface colour table: found, and it was never a table of constants](#50-the-interface-colour-table-found-and-it-was-never-a-table-of-constants)
51. [Which line is darker: the inner - but only where there are two colours](#51-which-line-is-darker-the-inner---but-only-where-there-are-two-colours)
52. [The unit model draw path](#52-the-unit-model-draw-path)
53. [There is no lighting. None.](#53-there-is-no-lighting-none)
54. [The palette tables — real, loaded, and not for models](#54-the-palette-tables--real-loaded-and-not-for-models)
55. [Texture lookup and frame selection](#55-texture-lookup-and-frame-selection)
56. [The quad mapping — decoded from `0x4C7580`](#56-the-quad-mapping--decoded-from-0x4c7580)
57. [Why ARMSOLAR looks wrong in RWE](#57-why-armsolar-looks-wrong-in-rwe)
58. [Where RWE diverges, item by item](#58-where-rwe-diverges-item-by-item)
59. [Implementation spec for RWE](#59-implementation-spec-for-rwe)
100. [Two shadow passes: a unit's shadow is a copy of its own sprite, a building's is a projection](#100-two-shadow-passes-a-units-shadow-is-a-copy-of-its-own-sprite-a-buildings-is-a-projection)
101. [The purple halo on buildings, and what stood for transparent](#101-the-purple-halo-on-buildings-and-what-stood-for-transparent)

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
| 259 | `SFXTYPE_SUBBUBBLES` | `0x472530` | 7 | the wake's dot, palette 103–97 (below) |

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

### `SFXTYPE_SUBBUBBLES` — there is no bubble emitter

Read 2026-09-18, because an underwater metal extractor sheds bubbles as it
turns in the original and shed nothing in RWE.

**The scripts do not say 259.** `ARMUWMEX.COB`, `CORUWMEX.COB`, `ARMSUB.COB`
and `CORSUB.COB` all push `3`, OR it with a `256` already on the stack
(`0x10036000`), and emit that -- the compiler's spelling of
`SFXTYPE_SUBBUBBLES | 3`... which is simply 259. A scan for the constant 259
in every shipped script finds nothing, which is why this looked unused. The
extractor emits from three pieces (its arms), the submarines from one.

**The engine builds the wake emitter, pointed up.** The dispatcher's branch for
259 (`0x4810D4`) makes the emitter's two points itself: the piece's first
vertex, and a copy of it whose height is overwritten with the sea level (the
byte at globals `+0x1427F`, shifted into the high word). It passes those with a
period of **8** and layer **7** to `0x472530`, which is `0x472430` -- the wake
handler -- instruction for instruction, the same 0x48-byte object with the same
vtable `0x4FD5F8`, but for one argument to `Init` (`0x474760`): the wakes push
`1` where the bubbles push `0`. `Init` stores it at `+0x44`.

**That flag's one reader is the colour ramp**, at `0x474A85`. Both cases load
the `smoke 1` slot and the palette bounds `0x61` and `0x67`. Set, the particle
starts at 97 and steps `+1`: foam, darkening into the sea. Clear, it starts at
103 and steps `-1`: a bubble, paling as it rises.

So a bubble is a dot of foam that leaves the piece, climbs straight toward the
surface at the wake's half a world unit a tick (the difference of the two
points is normalised, so depth does not change the speed), lives the wake's six
steps of eight ticks, and runs the seven blues backwards. Forty-eight ticks is
twenty-four units of climb, so from any depth it fades on the way up and does
not reach the surface. Two dots a call, the second a tick later, each scattered
by -3..3 on all three axes, as the wake.

For RWE: `GameScene::emitBubblesFromPiece` calls the wake's own
`computeWakeEmission` and `spawnWake`, with `reverseRamp` on
`ParticleRenderTypeWake`. **Not carried over:** the layer. The original files
bubbles in particle layer 7 where wakes go in 2; RWE draws both with the wakes.

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

**RWE matches this, and the sentence that used to stand here was wrong.** It
claimed RWE "reaches the same result by a different route, depth-testing against
world Y", and that it would "also let a tall ground unit in front occlude the
plume, which TA would not". It does neither. `Particle.h` carries an `inWorld`
flag that would put a sprite particle in the depth-tested pass, but **nothing
anywhere sets it**, so every sprite particle -- explosions, smoke, exhaust alike
-- is drawn over the finished frame with the depth test off. Features are drawn
with depth *writes* disabled as well, so a feature could not occlude a particle
even if the flag were set.

Confirmed against the original 2026-09-21, after scenery on Crystal Maze was
reported as being drawn under the smoke: effects are not obscured at all. So the
blanket "over everything" that RWE actually does is the faithful behaviour here,
and the depth-testing the old text described would have been the divergence. The
`inWorld` branch is unused and should stay that way unless something is found
that the original really does draw an effect behind.

The Atlas exhaust above remains the one case of an effect going under something,
and it is explicit layering rather than depth: the airborne pass runs after layer
7, so the aircraft is blitted over its own plume.

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

### The margin round the map

`0x47D2E0` refuses a site on its position alone before it reads a cell
(`0x47D302`-`0x47D352`). The arguments are the footprint's top-left corner in
build cells, and the footprint comes from the definition:

```
47d302  cmp  cx,0x1               ; x
47d30b  jl   0x47d80f             ;   x < 1 -> refuse
47d316  cmp  di,0x1               ; z
47d31a  jl   0x47d80f             ;   z < 1 -> refuse
47d32c  mov  eax,[ebp+0x14233]    ; map width in cells
47d336  lea  ecx,[esi+edx]        ; x + footprintX
47d339  cmp  ecx,eax
47d33b  jge  0x47d80f             ;   x + footprintX >= width -> refuse
47d349  lea  edi,[ecx+eax]        ; z + footprintZ
47d34c  cmp  edi,[ebp+0x14237]    ; map height in cells
47d352  jge  0x47d80f             ;   z + footprintZ >= height -> refuse
```

So a building never touches the edge of the map: one clear cell on every side.
`gs+0x14233` is the same width the routine multiplies by a little further down
to index the 13-byte cell array (`0x47D47E`), which is what identifies it.
RWE had no such rule until 2026-09-18 (`GameSimulation::isInsideBuildableArea`).

### Correction: the gate is fog-aware, and this reading was wrong

The passage above says bit 6 decides, and that `0x47D2E0` settles it from the
map's own occupancy. Tested against the running game, that is **not** the whole
gate: the original **does** let a building be placed on top of an enemy
structure that is not in view, and refuses once the structure has been seen.

About two thirds of `0x47D2E0` was traced when the paragraphs above were
written, and no reference to the explored array of section 2 was found in it,
which is why the reading came out as "just as fog-blind as RWE". Either the
untraced span (`0x47D479`-`0x47D800`) consults it, or something upstream of the
call filters the occupancy the check sees. Whichever it is, the behaviour is
settled by observation and the mechanism is not.

RWE follows the observed behaviour: the box and the click gate ignore an
occupant the local player has not discovered, and the build then fails at the
site with the constructor message, which is the sequence the original shows.
That test lives on the presentation side -- `canBeBuiltAtAsSeenBy` -- because
the simulation's own occupancy test has to stay identical on every peer, and
one player's fog is not.

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

> **Superseded by [TOTALA-EXE-SHADING.md](TOTALA-EXE-SHADING.md).** A second,
> independent read of `0x459C70` found that this pass had followed the
> `SHADING=off` path. The original *does* light its models: it computes a
> shade level **once per vertex** from a smoothed, deliberately un-normalised
> normal against an un-normalised light vector, then Gouraud-interpolates the
> integer row across the polygon and indexes `PALETTE.SHD[row * 256 + texel]`.
> What is below is kept because it records what was checked and how the
> `SHADING=off` path reads, which is a real path. **Do not implement from it.**

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

> **Partly superseded by [TOTALA-EXE-SHADING.md](TOTALA-EXE-SHADING.md).**
> The tables are real and loaded, as below. "Not for models" is the part
> that is wrong: the span filler indexes `PALETTE.SHD[row * 256 + texel]`
> for every shaded unit polygon. `shdgen.py` in `tools/exe/shading/`
> regenerates that file byte for byte, which is what pins its layout.

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
| 2 | shades buildings with `0.72 + 0.36*(0.5+0.5*dot(N,L))`, `L = norm(-1.3, 1.0, 0.3)` | ~~no lighting whatsoever~~ **wrong, see [TOTALA-EXE-SHADING.md](TOTALA-EXE-SHADING.md)**: a per-vertex level off an un-normalised smoothed normal, Gouraud-interpolated, through `PALETTE.SHD` | fix, to the shading document's model |
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

### Shadows on water: considered, and declined

Upstream #25 asked for shadows to be cast on the water surface rather than the
sea floor. That is not what the original does: a building's projection is
drawn flat at the ground under it wherever that ground is, a finished unit's
copy is not cut at the water line (above), and the only water test in either
pass is the one on map features in §3, which casts nothing for a feature whose
ground is below the sea level. RWE follows all three, as of B4 #9, and the
request is declined rather than recorded in §88 or `compatibility.md`, since
nothing here departs from the original.

One thing did depart, in RWE's code rather than in this reading: a **floating
building's** shadow was lifted to the water surface with
`rweMax(groundHeight, seaLevel)`. The projected pass has no water test in it,
so the shadow belongs on the sea bed under the building, and that is where RWE
draws it now. A floating *unit* is a separate matter and unchanged: it casts no
shadow at all, the copied pass skipping `canhover` and `floater` alike
(`0x45957F`).

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
smooth edges on them more than they want the original. It is off by default,
the faithful setting. It was turned on by default for part of 2026-09-11,
after a play-test found sharp units beside smoothed buildings looked like a
fault, and went back off the same day, when the player asked for the original
exactly. The ground stays out either way: the blur there was never
anti-aliasing, it was a box filter over a texture that cannot survive one.

**A finished building's dont-cache piece is drawn sharp, and the switch
does not reach it** (2026-09-11). It used to go into the mask as an ordinary
occluder, at 0.5, so a metal extractor's spinning top went sharp or smooth
with the units, which a play-test rightly found wrong. It has a level of its
own, 0.7, and `dontCache()` in `worldPost.frag` keeps a block whose native
sample is such a piece out of every filter, the units switch included. The
original left that piece out of the cached bitmap and drew it straight to
the screen afterwards, unfiltered and unfringed, and that is what this
reproduces. For a few hours the same day it was smoothed with its building
instead; that went back when the player asked for the original exactly.
`cached()` (above 0.85) alone now decides both the filter and the halo.

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
