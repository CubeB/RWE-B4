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

Six steps, in this order. The order matters: bank is fed the *total* change in
velocity for the tick, after everything else has had its say.

| Step | Address range | What it does |
|---|---|---|
| Drag | `0x43D2FB`–`0x43D38B` | `vel *= 1 − Acceleration/MaxVelocity` |
| Brake | `0x43D38E`–`0x43D47D` | if horizontal speed > `BrakeRate`, scale it back to `BrakeRate` and re-inject the excess along the nose |
| Turn | `0x43D520`–`0x43D585` | heading steps toward the desired heading, limited by `TurnRate` |
| Profile | `0x43D58B`–`0x43D5EA` | `desiredVel = toTarget × sqrt(2 × Acceleration / max(distance, 8))` |
| Clamp | `0x43D60A` | the change in velocity is limited to one `Acceleration` |
| Bank | `0x43D68B`–`0x43D6B3` | roll from the tick's total velocity change |

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

- **The `BrakeRate` nose re-aim** (`0x43D38E`–`0x43D47D`) is real behaviour and is
  why the original's aircraft barely crab — speed above `BrakeRate` is stripped
  and re-injected along the nose, so they fly roughly where they point. It moved
  the construction-aircraft bank peak by 0.00° in testing, because a
  construction aircraft never reaches its `BrakeRate` of 1.5, so it was left out
  rather than risk changing fast aircraft. `brakeRate` is still unused for air
  units in RWE.
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

**Radar has no grid at all.** It is a per-tick unit-versus-unit range query with
`range = RadarDistance + 2 × altitude`. Terrain never blocks it, and it flags
units rather than revealing ground.

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
offset §10 already records.

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
  The original's cursor exists to spread the cost over a frame budget; RWE's
  check already costs nothing by comparison, and the delay would only make
  units slow on the draw.
- **The fifty-candidate cap and the sampling without replacement.** Below fifty
  candidates the two are identical; above it the original is simply sampling,
  where RWE scores them all.
- **The retaliation attack order.** RWE points the weapons and issues no order,
  because the original only issues one when the attacker is already inside
  weapon range, where the two come to the same thing.
- **`unitsOnly`, `turret`, `lineOfSight`, `minbarrelangle`.** Decoded far enough
  to say they are no part of this decision: `0x49ABB0` never looks at them.

## 10. Field offsets

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
| `firestarter` / `rendertype` / `color` / `color2` | `wdef+0x10B`..`+0x10E` | byte |
| flags | `wdef+0x111` | dword, see below |

The flag bits at `wdef+0x111`, read off the parser's shifts: `lineofsight` 0,
`ballistic` 1, `shellweapon` 2, `beamweapon` 3, `vlaunch` 4, `meteor` 5,
`dropped` 8, `soundtrigger` 11, `guidance` 12, `tracks` 13, `unitsonly` 14,
`groundbounce` 15, `waterweapon` 16, `toairweapon` 17, `smoketrail` 18,
`turret` 19, `selfprop` 20, `propeller` 21, `noexplode` 22, `burnblow` 23,
`twophase` 24, `cruise` 25, `commandfire` 26, `noautorange` 27, `stockpile` 28,
`targetable` 29, `interceptor` 30.

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

## 11. `turret`, and what a hull-mounted gun waits for

`turret` is **bit 19 of `wdef+0x111`**, parsed at `0x42E8F9` — the key string is
pushed there, the read follows at `0x42E906`, and `and eax,1 / shl eax,0x13` at
`0x42E911`–`0x42E91A` puts it in place. The bits either side are `smoketrail`
(18, key at `0x504150`) and `selfprop` (20, `0x50413C`), which agrees with the
table in §10.

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
`0xC026800000000000` pushed at `0x42E70F`. §10 has this right and the priorities
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

## 12. Where RWE deliberately differs

Recorded so these do not get "fixed" back later by someone comparing against the
original:

- **Nanolathe spray lands on the roof**, not inside the model. The original
  samples the landing height inside the model too, which it can afford because
  its spray is composited in a late layer. RWE's is depth-tested so a
  construction aircraft can cover its own beam, so a landing point inside the
  geometry would be swallowed.
- **Exhaust occlusion is depth-tested**, not hand-layered — see §5.
- **The fog raster is windowed on the camera.** At one texel per world unit a
  whole 640×640-cell map would be 400 MB, past most drivers' limits, so RWE holds
  a 2.6 MB window a few tiles larger than the view. This is what the original
  effectively does anyway, composing its overlay per visible screen tile.
- **Off-map fog cells read as the nearest on-map cell.** Reading them as "clear"
  leaves the frame's ragged edge with no neighbouring tile to cover it, and a
  strip of map shows through at the border.
- **No `BrakeRate` nose re-aim and no pitch** — see §1.
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
- **Only the heading half of the `turret=0` check is enforced.** The original
  compares the required elevation against the hull's own pitch at `unit+0x68`
  (§11). RWE's simulation has no hull pitch — `UnitState` carries a rotation and
  nothing else — so comparing against a notional zero would be a different rule
  wearing the same name rather than the original's. The heading half is the one
  that stops a unit shooting sideways and backwards, and it is the half that is
  implemented.

---

## 13. Still unknown or unported

- TA's **Permanent** LOS mode has not been looked at.
- **Circular** LOS mode (the `vismasks.gaf` stamp) is understood but not
  implemented; RWE always uses True.
- RWE's explored grid is **per-player** rather than the original's one shared
  bitmask with a bit per LOS group. Equivalent until allied vision groups exist.
- `hitDensity` is parsed (100 for solid things, 5–10 for foliage, 0 for smudges)
  and is very likely the pass-through chance for projectiles hitting features,
  but this has not been confirmed in the binary.
- The **strafing pass** (`AirToGround`, `0x412710`) is decoded but not ported:
  RWE's fighters still fly the generic attack run. See the missions document §5.
- **`maneuverleashlength`** is now parsed but not enforced. In the original it
  aborts an attack when the aircraft strays that far from where it was standing
  when the order was given — missions document §8.
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
  targeting stand in for `Standby`; what it has no equivalent of is the
  sight-range search `0x43B700`, which is the only thing that makes `Standby`
  and `Guard_NoMove` behave differently. Until a unit can decide to walk off
  and find a fight, the key has nothing to change.
- **The movement mode is stored but not acted on.** RWE now builds a unit on
  the Hold Position, Maneuver or Roam its FBI names and reports it to the COB
  scripts, but nothing reads it back: there is no leash on a unit that breaks
  off to attack, so Maneuver and Roam come to the same thing and Hold Position
  is honoured only in that such a unit is never given an attack order to begin
  with. The original's version is the `0x43B1F0` gate and the anchor order it
  plants at `unit+0x6a`.
- **Smoke does not drift downwind.** The vector and the ×8 scaling are decoded
  (§4, §7) but RWE has no map wind, so every puff goes straight up. The lift
  itself is right: RWE's half a unit a tick is the original's gravity × 4 on the
  112 that nearly every map uses, though it will not track a map that sets
  gravity to something else.
- The **explosion smoke** (`0x472630` from `0x420AE1`, three puffs seven ticks
  apart) and the **30-second burning wreck plume** (`0x48644B`) are decoded but
  not ported; RWE's explosions and wreckage do not smoke afterwards.

---

## NN. What an aircraft does with nothing to do

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
uses for its short hops.

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
  equivalent — see §13 — and its own idle weapon acquisition stands in.
- **The go-home-when-hurt branch.** Below 75 % health with an active repair pad
  within 3840, both the search circuit and the strafing pass abandon what they
  are doing and push a `VTOL_LANDING` on a pad chosen at random. RWE has no
  air repair pads, so there is nothing to fly to.
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

## NN. Recoil

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

## NN. Thermal vents

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
- **Downwind drift**, as §13 already records for the rest of the smoke: RWE has
  no map wind, so a vent's plume goes straight up.

---

## NN. Jamming, stealth and cloaking

Three keys that change who can see or shoot whom. They are one section because
the original answers all three in one routine — the per-tick visibility pass at
`0x467440` — and because all three land in the same place in RWE: the radar
query that `GameSimulation::updateVisibility` builds and `canDetectUnit` reads.

### The field offsets

Read off the FBI parser under the pipeline rule §10 describes — a key's value is
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
  and `d² <= (radardistance + 2 × unit+0x70)²`.

That is where §2's `RadarDistance + 2 × altitude` lives, and the split explains
what sonar is for: a submerged unit is only ever a sonar contact, a unit standing
clear of the water only ever a radar one, and a half-submerged one can be both.

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
unit) looks at it. In the original a cloaked unit standing inside an enemy gun's
range is still shot at; cloak hides it from your eyes and from the AI's search,
not from a turret already pointed at it.

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

Still unported from this reading:

- **The COB side.** Events `0xE` and `0xF` at `0x48B14E`/`0x48B1A7` are not
  raised, so a script cannot react to its unit cloaking, and the `0x10000` render
  flag is not set on the pieces — a cloaked unit is simply not drawn to an enemy
  rather than being drawn shaded.
- **Bit 10, "jammed".** The original marks a unit a jammer has erased separately
  from one that was never seen. Nothing in RWE reads it, and nothing in the
  original appears to either beyond the flag itself.
- **The jammer's effect on an ally's radar.** RWE follows the original in
  skipping only the viewer's own jammers, so allies do jam each other; it is
  recorded here in case it ever looks like a bug.

## NN. Stockpiled weapons and interception

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
reloads in 70% of the time; a unit at the point of death takes 120%. **Decoded,
not ported.**

### `commandfire`

Bit 26 of `wdef+0x111`, and it means one thing: the weapon is never given a
target by the machine. The auto-target scan skips it (`0x40643F`, `0x407131`,
`0x40FDE5`) and so does the return-fire path, which takes an "is this an
explicit order" argument and only consults the bit when that is zero
(`0x408A88`–`0x408A96`). Sixteen weapons carry it — the D-gun, the nukes, the
anti-nukes, the Krogoth's tremor.

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

### `antiweapons`

Bit 29 of `def+0x241`, pushed at `0x42C84C`. Two units set it, and they are the
two anti-nuke launchers. No reader was found for it; on the evidence it is a
label for the target-category machinery rather than anything that gates the
interception above, which keys entirely off the weapon flags. **Not ported, and
not understood.**

### A correction to §10: `holdtime` does have a reader

`WORD wdef+0xFE` is read at `0x499E81` and `0x49C8D6`, both on the path that
retires the projectile the camera is following (`globals+0x142F7`), and stored
into `globals+0x1434B`. It is how long the view holds on the impact — the nuke
cam. Nothing in the simulation reads it.

### Decoded but not ported

- **Interception in full.** Everything in the five steps above is decoded and
  none of it is in RWE: there is no interceptor weapon behaviour, no
  projectile-targets-projectile slot, and no blast that kills projectiles.
  `coverage` and `targetable` are parsed and unused.
- **`antiweapons`**, as above.
- **The veterancy and damage terms on reload time** (`0x49E468`).
- **The queue button.** RWE has the order
  (`PlayerUnitCommand::ModifyStockpile`) and the simulation behind it, but
  nothing in the GUI raises it yet, so a launcher's magazine can only be filled
  from code or a test.
- **The 300-tick wait on a full magazine** is implemented, but nothing in RWE
  can reach 200 rounds in practice.
- **Per burst, not per shot.** The cost check, the spend and the round out of
  the magazine all sit in the per-weapon routine that runs once a tick, and a
  burst is expanded afterwards from the projectile's own `+0x60` counter, so the
  original pays for a burst once. RWE charges each shot of a burst separately,
  as it always has for `energypershot`. Nothing in the shipped data can tell the
  difference: the four weapons with a burst — the flamethrower and the three
  EMGs — name no `energypershot`, no `metalpershot` and no `stockpile`.

## NN. The D-gun

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
  outright, cause 7 forces a wreck, and everything else — the D-gun included —
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
- **The damage-type death causes** 4, 5, 7 and 9 are decoded as a set but not
  individually named; nothing was traced far enough to say which weapon or
  event produces each.
