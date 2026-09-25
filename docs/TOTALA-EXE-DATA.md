# What the original executable does: file formats, field offsets and small systems

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §8, §15, §24, §30, §82, §89, §105, §106, §109.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

8. [Which way a finished building faces](#8-which-way-a-finished-building-faces)
15. [Thermal vents](#15-thermal-vents)
24. [Small systems: hit density, regrowth, kamikaze, paralysis, move rate](#24-small-systems-hit-density-regrowth-kamikaze-paralysis-move-rate)
30. [The FBI keys, and which of them the exe actually reads](#30-the-fbi-keys-and-which-of-them-the-exe-actually-reads)
82. [Field offsets](#82-field-offsets)
89. [Keys the original parses and never uses](#89-keys-the-original-parses-and-never-uses)
105. [The campaign: campaign files, mission files, and a unit's first orders](#105-the-campaign-campaign-files-mission-files-and-a-units-first-orders)
106. [Blast impulse: `ImpulseFactor` and `ImpulseBoost` are not in this game](#106-blast-impulse-impulsefactor-and-impulseboost-are-not-in-this-game)
109. [`0x46d630`, the unit-table packet builder, and the checksum behind it that got away](#109-0x46d630-the-unit-table-packet-builder-and-the-checksum-behind-it-that-got-away)

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
- ~~**Downwind drift**~~ Ported 2026-09-24 (#111): a vent's steam drifts with
  the map's wind like every other puff, the wind vector times eight a tick.

---

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

## 82. Field offsets

FBI key names are compared at `0x42C129`–`0x42C1C5`, which gives the unit
definition layout:

| FBI key | Offset | Notes |
|---|---|---|
| `maxvelocity` | `def+0x192` | an asymptote set by drag, never clamped |
| `brakerate` | `def+0x19A` | used by the nose re-aim only |
| `acceleration` | `def+0x19E` | |
| `bankscale` | `def+0x1A2` | default `0x10000` = 1.0 |
| `pitchscale` | `def+0x1A6` | `UnitDefinition::pitchScale`, since 2026-09-24 (#24) |
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

## 105. The campaign: campaign files, mission files, and a unit's first orders

Decoded 2026-09-15 for B4 #55, the first piece of #38. Everything here is
read out of the same binary as the rest of this file; where a reading is
inferred rather than followed to its consumer it says so.

### The campaign file

`0x476AE0` loads a campaign by name: `camps\<name>.tdf` (`0x476B07`,
`0x476B11`), then `[HEADER]` (`0x476B32`) and its one key, `campaignside`
(`0x476B57`), a string defaulting to `ALL` (`0x476BB1`). The two names the
front end passes are `"Arm Campaign"` and `"Core Campaign"` (`0x477C0A`,
`0x477C17`, under the `Campaign` and `Missions` menu strings); the shipped
files are `ccdata.ccx/CAMPS/Arm Campaign.tdf` and `Core Campaign.tdf`, 25
missions each. They are the original game's campaigns: the `.ccx` archives
carry the campaign disc, and the mission maps are in `ccmiss.ccx/Maps/`.

The mission list is read by `0x4356C0`-`0x4359C0`: blocks named by the format
`MISSION%d` (`0x504A78`) from 0 up until one is missing, each giving
`missionname` and `missionfile`. `missionname` goes through `0x4C58A0`, the
localised-key reader: it copies the language string at `0x51FDC0` in front of
the key (so `Germanmissionname`, `Frenchmissionname`, ...) and reads that;
a mission with no name shows `"Error -- Unnamed Mission"` (`0x504A84`).
`missionfile` names the map: `0x435F5E` reads it and `0x4290F0` resolves it
to `Maps\<missionfile>` with the `OTA` extension; a missing one is
`"The requested mission file, %s, does not exist."` (`0x504DE8`), and a
mission entry with no `missionfile` at all is the affectionate
`"Hey, joker!  There is no mission defintion for this mission: %s"`
(`0x504D9C`).

### The mission file

A mission is an ordinary OTA read by `0x435F00`-`0x437300`, the same reader
skirmish maps go through, with the campaign-only keys below on top of the
ones RWE's `parseOta` already knows. The `.tnt` beside it is
`Maps\<name>.TNT` (`0x43604E`). No `[GlobalHeader]` is fatal
(`"No GlobalHeader block in mission file!"`, `0x4361B5`), and a header that
parses but has no usable schema is `"No suitable schema type in mission
file!"` (`0x43662C`).

**`[GlobalHeader]` keys and defaults**, in the order they are read. Integer
keys come through `0x4C46C0` with the default shown, strings through
`0x4C48C0` into a buffer of the size shown, floats through `0x4C4760`.

| Key | Type | Default | What the reader does with it |
|---|---|---|---|
| `maxunits` | int | 200 (`0xC8`) | the unit cap for the mission |
| `brief` | string, 256 | | `camps\briefs\<brief>.TXT` (`0x436204`), read into a `"Briefing"`-tagged buffer (`0x436258`): the briefing text |
| `narration` | string, 256 | | `camps\briefs\<narration>.WAV` (`0x4362A6`): the briefing voice-over |
| `missionhint` | string, 256 | | `camps\hints\<hint>.TXT` (`0x4362DC`) |
| `glamour` | string, 256 | | `<glamour>.PCX` (`0x43630B`): the briefing picture |
| `glamoursound` | string, 256 | | `camps\briefs\<glamoursound>.WAV` (`0x436346`) |
| `UseOnlyUnits` | string, 256 | | `camps\useonly\<name>.TDF` (`0x43637B`): the unit list the mission restricts building to |
| `mapping` / `lineofsight` | int | 0 | as skirmish |
| `memory` / `numplayers` / `Planet` | string, 128 | | display only |
| `nomovie` | int | 0 | skips the mission's movie |
| `missiondescription` | string | `"No description available"` (`0x504C58`) | |
| `minwindspeed` / `maxwindspeed` / `gravity` | int | 0 | as skirmish |
| `tidalstrength` / `killmul` / `timemul` | float | 0.0 | as skirmish |
| `lavaworld` / `nosealeveltrigger` / `waterdoesdamage` / `waterdamage` | int | 0 | `waterdamage` is the per-tick damage when `waterdoesdamage` is set; the shipped missions all say `waterdoesdamage=0`, `waterdamage=100` |

**Win and lose conditions** are not read by the mission reader at all. The
rule evaluator at `0x48E040`-`0x48E940` reads them straight off the header
block when the mission starts and, for each that is set, allocates a rule
object (`0x4B4F10`). This section first named four; there are twenty
(corrected 2026-09-25, B4 #55). In the evaluator's order, with the reader
each goes through and, where it is a string, the `sscanf` format it is then
scanned with:

| Key | Read as | Side | Shipped missions using it (of 76, base and Core Contingency) |
|---|---|---|---|
| `KillEnemyCommander` (`0x48E029`) | int | victory | 4 |
| `DestroyAllUnits` (`0x48E06B`) | int | victory | 52 |
| `KillAllMobileUnits` (`0x48E0A9`) | int | victory | 0 |
| `BuildUnitType` (`0x48E105`) | unit name | victory | 5 |
| `CaptureUnitType` (`0x48E196`) | unit name | victory | 23 |
| `KillAllOfType` (`0x48E215`) | unit name | victory | 4 |
| `KillUnitType` (`0x48E2A2`) | `%[a-zA-Z],%i` | victory | 4 |
| `MoveUnitToRadius` (`0x48E353`) | `%[a-zA-Z],%i,%i,%i`, the type may be `ANYTYPE` | victory | 6 |
| `UnitTypePassesX` / `UnitTypePassesZ` (`0x48E46C`, `0x48E555`) | `%[a-zA-Z],%i`, or `ANYTYPE` | victory | 2 |
| `VictoryTimerRunsOut` (`0x48E631`) | int | victory | 1 |
| `CommanderKilled` (`0x48E67C`) | int | defeat | 75 |
| `AllUnitsKilled` (`0x48E6C3`) | int | defeat | 76 |
| `AllUnitsKilledOfType` (`0x48E725`) | unit name | defeat | 27 |
| `UnitTypeKilled` (`0x48E7BB`) | `%[a-zA-Z],%i` | defeat | 3 |
| `DeathTimerRunsOut` (`0x48E86B`) | int | defeat | 2 |
| `AnyUnitPassesX` / `AnyUnitPassesZ` (`0x48E8C0`, `0x48E91E`) | int | defeat | 0 |

Each rule is then registered under a `VictoryCondition_<key>` or
`DefeatCondition_<key>` name with `Satisfied` and `Celebrated` flags
(`0x48EAC8` onwards). What each tests, tick by tick, is not followed here;
it is the next piece.

**Which schema is played.** A skirmish map's schemas are `Network n`; a
mission's are `Easy`, `Medium` and `Hard` (25, 25 and 26 of the shipped 26).
`0x43689A`-`0x4368CA` lays the seven type names out in a table, and the
switch at `0x4368DC` picks an order of preference from the difficulty
setting at `[globals+0x37EEE]` (§24): difficulty 0 tries `Easy` then `Medium`
then `Hard`, 1 tries `Medium` first, 2 tries `Hard` first, each falling back
through the others (`0x4368F9`-`0x43693C`). The first schema whose `type`
matches is the one played; none matching is the "No suitable schema type"
error above. Inside the schema, `HumanMetal`, `HumanEnergy`,
`ComputerMetal`, `ComputerEnergy`, `SurfaceMetal` (int, default 0),
`aiprofile` (string; `ai\<profile>.txt`, or `ai\default.txt` when absent,
`0x4366EA`-`0x43672D`), and the five `Meteor*` keys are read as for
skirmish.

### `[units]` — the mission's starting units

Each schema's `[units]` block (`0x436C7E`) holds `[unit0]`, `[unit1]`, ...
(9,576 of them across the shipped 26). Each is read at `0x436DFE`-`0x437002`
into a record tagged `"MISSIONUNIT DATA"` (`0x436DA4`):

| Key | Read as | Default | Stored at | Meaning |
|---|---|---|---|---|
| `Unitname` | string, 1024 | | the record's name | the FBI name |
| `Ident` | string, 1024 | | | a label other orders can name (see `g` and `i` below) |
| `InitialMission` | string, 1024 | | | the unit's first orders, the mini-language below |
| `XPos` / `YPos` / `ZPos` | int | 0 | `+0x0C` / `+0x10` / `+0x14` | position |
| `Angle` | int, degrees | 0 | `+0x18` | heading; `0x436EF9`-`0x436F0A` converts degrees to the 16-bit angle |
| `Player` | int | 0 | byte `+0x22` | owning player index |
| `HealthPercentage` | int | 100 (`0x64`) | word `+0x1A` | starting health |
| `BuildPriority` | int | 0 | word `+0x20` | |
| `CreationCountdown` | int | 0 | `+0x1C` | ticks before the unit appears; the shipped data only ever says 0 |
| `MissionCriticalUnit` | int | 0 | `+0x23` bit 4 | |
| `AiIgnore` | int | 0 | `+0x23` bit 5 | |
| `AiPriorityTarget` | int | 0 | `+0x23` bit 6 | |
| `InitialGroup` | int | 0 | `+0x23` bits 0-3 | a squad number |
| `Immunity` | int | 0 | `+0x23` bit 7 | |

`Kills`, which every shipped `[unit]` block carries, is not read by the
executable at all.

### `InitialMission`, the order mini-language

The string is a comma-separated list of orders, each a letter and its
arguments, interpreted by `0x487BF0`. The scanner stops at each `,`
(`0x487C3D`); the letter, upper or lower case, indexes the byte table at
`0x4882CC` and that the jump table at `0x488270` (`0x487C7D`-`0x487C96`).
Arguments are scanned with the `sscanf` formats shown. Point missions go
through `0x43F0E0(id, x, z, ...)`; named ones through `0x438760(name)`.

| Letter | Scans | Issues | Read as |
|---|---|---|---|
| `m` | ` %f %f` | mission 2 at the point | move to (x, z) |
| `p` | ` %f %f %f` | mission 9 at the point | patrol to (x, z) |
| `a` | ` %f %f`, else ` %[a-zA-Z0-9_.]` | mission 3 at the point, else `ATTACKUTYPE` with the name (`0x487FEC`) | attack the point, or attack every unit of that type: `a CORCOM,` |
| `g` | ` %[a-zA-Z0-9_.]` | `0x487AF0` looks the name up, then mission 7 | guard the unit whose `Ident` this is (inferred from mission 7's use) |
| `i` | ` %[a-zA-Z0-9_.]` | `0x487AF0` on the name (`0x48822C`) | a link to the unit whose `Ident` this is: `i CHRIS,` in the shipped data; what the link does is not followed |
| `o` | ` %d %d` | (`0x487C9D`) | the standing orders, read as (fire, move) from the shipped `o 0 1,` (inferred; the consumer is not followed) |
| `w` | ` %f %d`, else `a` | `WAIT` with the number (`0x48816B`); `wa` is `WAITFORATTACK` (`0x4881C7`) | wait that long, or wait to be attacked |
| `u` | ` %f %f` | mission 5 at the point | no shipped mission uses it; by its shape, unload at the point |
| `b` | ` %[a-zA-Z0-9_.] %d %f %f` | `MOBILEBUILD` / `BUILDINGBUILD` / `BUILDWEAPON` (`0x4880A1`, `0x4880C8`, `0x488106`) | build the named unit at the point |
| `d` | | `SELFDESTRUCTFG` (`0x4881E0`) | self-destruct |
| `s` | | `MAKESELECTABLE` (`0x488206`) | make the unit selectable |
| any other letter, and the end of the string | | `MAKESELECTABLE` (`0x487E50`) | the unit is handed to the player once its orders are done |

The shipped missions use twelve shapes, the commonest being `w N,p X Z,`
(958 units), `p X Z,` (206), `o N N,w N,` (175) and `w N,a CORCOM,` /
`w N,a ARMCOM,` (142): wait, then patrol; or wait, then hunt the enemy
commander.

Across all 76 shipped missions, the Core Contingency ones included (22,103
starting units, 12,181 of them with orders), three more things show up:

- **`b` is also a factory queue.** Core Contingency writes `b ARMAMPH 4` and
  the like, a name and a count with no point, to have a plant build; the
  format's `%f %f` then scan nothing.
- **`u` is used**, in Core Contingency (`EXP1AC10`, `EXP1AC12`); "no shipped
  mission" above was true of the original 26 only.
- **The data has typos the original reads literally**: `P P 502 1224` (AC01),
  `m 1557,` with one coordinate (AC06), `w 444,m` (AC05), and
  `w 600u 4682 2762` with a comma missing (EXP1AC10), where the unload is
  swallowed into the wait's text and never happens. The scanner stops at
  commas and `sscanf` stops at the first thing it cannot read, so each of
  these orders simply has fewer arguments than its format asks for.

### `[specials]` and `[features]`

The schema's `[specials]` (`0x437010`, tagged `"MISSIONRULE DATA"`) are the
`StartPos` entries RWE already reads, and nothing else in the shipped
missions. `[features]` (`0x437183`, `"MISSIONFEATURE DATA"`) are
`Featurename` / `XPos` / `ZPos` with `-1` as the coordinate default, as RWE
reads them for skirmish; the shipped missions place 91.

> **Ported 2026-09-25 (#55):** the readers. `parseOta` now reads the campaign
> header keys (`maxunits`, `glamoursound`, `nomovie`, `nosealeveltrigger`,
> `waterdoesdamage`, `waterdamage`), all twenty conditions as data
> (`OtaMissionRules`), and each schema's `[units]` (`OtaMissionUnit`), with
> `parseInitialMission` reading the order list as `sscanf` would, typos
> included. `io/campaign` reads a campaign file: `campaignside` and the
> `MISSION%d` list, with `campaignMissionName` for the localised name. All
> 76 shipped missions and the six campaign files parse. Not yet: acting on
> any of it (the interpreter's missions, the conditions, the schema choice
> by difficulty, the briefing).

### What RWE has, and what the port is

`parseOta` reads the header keys shared with skirmish, the schemas' specials
and features, and nothing above the line: none of the campaign header keys,
no `[units]`, no rule keys. Nothing reads a campaign file. The port, per #55,
is a campaign TDF reader with the `MISSION%d` enumeration and the localised
name, the header keys above added to `OtaRecord`, a `[units]` reader with the
record's fields, and the `InitialMission` grammar as data; the interpreter's
missions, the rules and the schema choice are the pieces after it.

## 106. Blast impulse: `ImpulseFactor` and `ImpulseBoost` are not in this game

Asked for from play: "powerful explosions create shockwaves that physically
toss surrounding units." **The original does not do this.** There is no blast
impulse in `TotalA.exe`, not even an unused one: the key names are not in the
binary, nothing could read them if a TDF supplied them, and the blast code
writes hit points and nothing else. This is a "not implemented" finding, not a
"decoded but subtle" one, and it is recorded so that nobody ports it as a
fidelity fix.

**The strings are absent.** A case-insensitive search of the whole
1,178,624-byte file, as ASCII and as UTF-16LE, for `impuls` -- shorter than
either key, to catch any case or truncation -- finds **nothing**; nor do
`knockback`, `kickback`, `blastforce`, `pushback`, `recoil` or `shove`. The
controls each appear exactly once (`areaofeffect`, `edgeeffectiveness`,
`weaponvelocity`), which is what a sound search should give: the parser holds
each key it recognises as one literal.

**That is the whole key space.** The weapon TDF parser's keys sit as one
contiguous run in `.data`, roughly `0x503FE8`–`0x50410E`, directly after the
FBI keys of §17:

```
firestarter, minbarrelangle, holdtime, flighttime, smokedelay, randomdecay,
duration, sprayangle, burstrate, burst, noautorange, weapontimer,
edgeeffectiveness, areaofeffect, metalpershot, energypershot, reloadtime,
coverage, range, weaponacceleration, startvelocity, weaponvelocity
```

Each is fetched at its own call site through `0x4C4760` (`GetFloat(key,
default)`: a binary search over the section's parsed key/value pairs,
returning the caller's default when the key is absent) or its integer sibling
`0x4C46C0`, with the key string pushed as a literal beside the call -- for
example `push 0x504278` (`"edgeeffectiveness"`) at `0x42E59B`, stored to weapon
`+0xD8`. A section is tokenised into that key/value array generically, so an
`ImpulseFactor=1.5` written by a modder is parsed like any other line and then
never retrieved, because no call site pushes its name. It is not clamped or
defaulted; it simply has no reader and no struct offset.

**The blast code moves nothing.** Both routines §6 decodes end every per-unit
effect in the same three-argument call to `0x499CD0` (attacker, victim,
scale): `0x499FA0`, the single-target path, pushes `1.0f`; `0x49A120`, the area
path, pushes the §6 falloff scale at `0x49A3EE`–`0x49A3F5` after its box-clamp
distance, and afterwards writes only its two per-owner damage accumulators.
Neither reads or writes anything shaped like a velocity, and `0x499CD0` and the
damage choke point `0x489BB0` behind it take no argument an impulse could ride
in.

**Nor does the shipped content try.** Every weapon TDF in the GOG install --
the base game's 8 in `totala1.hpi`, 66 in Core Contingency's `ccdata.ccx` and 38
in Battle Tactics' `btdata.ccx`, 112 in all -- was extracted with a
reimplementation of RWE's HPI reader and checked against the archive
directories: none contains either key. `COMMANDER_BLAST` (`weapons/UNITS.TDF`),
the most violent explosion in the game, is `AreaOfEffect=950`,
`EdgeEffectiveness=0.75`, `Damage=9999`, and nothing else of the kind.

Verified by reading instructions or by exhaustive search: the absent strings;
the key table and that it has no impulse entry; `0x4C4760` and `0x4C46C0` in
full; that every recognised weapon key has its own literal call site; that
`0x499FA0` and `0x49A120` end in `0x499CD0` with no velocity access; the 112
data files. **Inferred, not checked:** that FBI parsing uses the identical
accessor rather than an equivalent one; and that every damage entry point
(contact fuzes, the D-gun's own dispatch, §92) passes through one of those two
routines before `0x499CD0`, which was traced for the two paths §6 documents
rather than rebuilt from every dispatcher. Where the belief comes from is
also inference: `COMMANDER_BLAST` still does about 7500 at the rim of its
475-unit radius, which reads as a shockwave without anything being thrown,
and the Spring engine -- an independent reimplementation -- gives these same
key names real physics, so TA-derived communities meet them there.

For RWE: nothing to port. `WeaponTdf` does not parse either key and should
not. A knockback added for feel would be a deliberate departure and belongs in
§88 with the others, not in the simulation as though it matched the original.

## 109. `0x46d630`, the unit-table packet builder, and the checksum behind it that got away

This one is included because it **failed**, and the shape of the failure is
worth having written down before someone spends the same two days on it again.

A `.tad` demo carries a `UnitData` record listing every unit type the game
knew, as 14-byte `0x1a` subpackets (see `docs/TA-DEMOS.md`). The stream then
refers to unit types by an *index into that table*, so a demo's build events
cannot be attributed to a named unit without knowing how the table's ids are
computed. That is the whole reason for looking.

### The packet builder, decoded

`0x46d630`, one of four near-identical siblings at `0x46d500`, `0x46d530`,
`0x46d5b0` and `0x46d630` laid out contiguously with `nop` padding — apparently
send-path variants over one payload shape:

```
46d63a  mov  al,[esp+0x18]      ; the caller's 'sub' argument, 2 or 3
46d63f  mov  [buf+1],al
46d643  mov  eax,[esp+0x20]     ; a pointer to a per-unit-type record
46d64b  movb [buf+0],0x1a
46d650  mov  edx,[eax+0x0]      ; record+0x0 ...
46d652  mov  [buf+6],edx        ; ... is the id, buf[6:10]
46d656  mov  dl,[eax+0x8]
46d659  mov  [buf+10],dl        ; record+0x8 -> buf[10]
46d65d  mov  dl,[eax+0xa]
46d660  mov  ax,[eax+0xc]
46d664  mov  [buf+11],dl        ; record+0xa -> buf[11]
46d668  mov  [buf+12],ax        ; record+0xc -> buf[12:14]
```

Two things fall out, both **VERIFIED** against thirteen real demos:

- **`buf[2:6]` is never written.** The "zero" field of the record layout is
  whatever the caller left in its scratch buffer, not a field.
- **`buf[10:14]` is three fields, not one dword.** That is exactly why the
  corpus shows `0xffff0101` on every restricted-block entry but one: a constant
  low byte, a flag byte, and a `0xffff` sentinel word. The exception has the
  flag byte cleared, and is the same id in demos of two entirely unrelated data
  sets — so it is a fixed pseudo-entry rather than a unit type.

### Where it stopped, and why

The id is a dword at offset 0 of that record, and the record is **not** the
585-byte FBI-parse struct. That struct was mapped along the way and is worth
recording: the loader from `0x42aa66` enumerates `units\*.FBI`, writes the file
count to `globals+0x1438f` and allocates `count * 585` at `globals+0x1439b` —
the `shl eax,6; add ebx` then `lea ebp,[eax+eax*8]` at `0x42aa72`/`0x42aa7d` is
`*65` then `*9`, and the restriction dialog at `0x44ca4e` strides by the same
`0x249`. Confirmed fields: `+0x186` and `+0x18a` are the build costs as floats
(already at §on the economy), `+0x21e` is the unit's own table index written at
`0x42ab51`, `+0x241`/`+0x245` are the packed flag words, `+0x15a` is
initialised to `-1`.

**Nothing in the per-unit FBI read block writes a checksum-shaped value to
offset 0**, and the first write to a freshly indexed record is `+0x21e`. So
`0x46d630`'s record is a separate transient structure built for the lobby
exchange, and its construction site was not found. Two obstacles, both worth
knowing about generally:

- `0x46d630`, `0x46d530` and `0x46d5b0` have **zero call sites** findable either
  by `xref.py` (absolute references only, per `tools/exe/README.md`) or by
  grepping the full objdump listing for their addresses as resolved call
  targets. Only `0x46d500` has one, from `0x4559b7` inside a large incoming
  DirectPlay message dispatcher.
- **objdump's linear sweep desynchronises here.** The switch table at `0x46d84c`
  is data and disassembles as nonsense (`inc edx`, `fadds`, ...); its five
  entries had to be read by hand as `0x46d842, 0x46d738, 0x46d748, 0x46d842,
  0x46d7a5`. A second table nearby whose bytes decode as *plausible*
  instructions would swallow a real call and give no sign of it. That is the
  likely reason the caller could not be found — offered as the best explanation,
  not as a certainty.

### What was ruled out, so nobody repeats it

Against both mods' real shipped data and their real demo tables: 88 name-hash
and case/suffix/path combinations; crc32 and adler32 of the raw FBI bytes, of
`\r`-stripped and case-folded forms, and of a canonical sorted `key=value;`
serialisation with comments stripped; the same over the referenced `.3do` and
`.cob` files; and every 1-, 2- and 3-field permutation of the obvious FBI fields
under four separators. Zero matches throughout. `tools/exe/unitsync.py` runs
these against a ground-truth CSV so the negative is reproducible and a new
candidate can be checked before it is believed.

Next time: a recursive-descent disassembly of `0x455000`-`0x46e000`, or a live
breakpoint on the restrictions-dialog arrays at `0x44ca4e`.

### What it was wanted for, and why that no longer needs it

The checksum was chased in order to name the unit type a demo's `0x09` refers
to. It turns out not to be on that path at all: the `0x09` index is not an index
into the `0x1a` table, but the **load-order index this section already
documents** — the one written to `+0x21e` at `0x42ab51`. The order the
enumeration at `0x42aa66` produces was then recovered from the demo corpus
rather than from the binary: sort every `units\*.FBI` name the merged VFS
presents and number from one. `docs/TA-DEMOS.md`, `0x09`, carries the evidence
and the two data sets it replicates across.

So the useful part of this section was the struct map, not the packet builder,
and the part that got away is wanted only for reproducing the table itself. The
lesson for the next dead end is the one this section was written to record: the
thing the work was blocked on was not the thing it was chasing, and an hour
spent testing whether the blocker was real would have been worth more than the
day spent on the checksum.
