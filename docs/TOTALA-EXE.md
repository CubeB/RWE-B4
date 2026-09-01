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

## 4. `SFXTYPE_VTOL` — the Atlas exhaust

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

One trap worth recording: **FX.GAF's loader stores each handle into the *next*
iteration's slot.** The off-by-one is confirmed independently by the wake
emitter, which uses `smoke 1` with a hardcoded palette ramp of indices 97–103 —
pale blue to deep blue, i.e. water foam, which is obviously right for a wake and
would be obviously wrong one slot over.

In the game data, the Atlas and Valkyrie are the only OTA units that emit
`SFXTYPE_VTOL`, from three (four) thruster pieces at `sleep 67`.

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
| `minbarrelangle` | `wdef+0xC8` | float, **not** a word at `+0xFE` |
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
| `holdtime` | `wdef+0xFE` | word, `×30` |
| `accuracy` | `wdef+0x104` | word |
| `tolerance` / `pitchtolerance` | `wdef+0x106` / `+0x108` | word |
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

## 11. Where RWE deliberately differs

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
  The original does the same, and holds its aim regardless of where the nose
  points; RWE relies on the same thing, so a gunship fires across the swing.

---

## 12. Still unknown or unported

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

---

