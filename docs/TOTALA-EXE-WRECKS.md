# Wreckage over water

Section numbers below are placeholders (`## NN.`); they are written to be
lifted into `docs/TOTALA-EXE.md` and renumbered.

The short answer, before the working. **Wreckage sinks.** A unit that dies over
water leaves its corpse feature at the exact point it died — the same position,
the same rotation, the same y as the living unit — and the feature is then
handed a fixed downward velocity of `-11468` in 16.16, about **0.175 world
units a tick**, which carries it straight down until it grounds on the terrain
beneath and stops. Nothing floats. There is no `floating` key on a feature, in
the exe or in any of the 2 976 feature sections the game ships. The one
exception is a unit with `IsFeature=1` — in practice the Core Contingency's
dragon's teeth and forts, and above all `ARMFDRAG`/`CORFDRAG`, the **floating**
dragon's teeth — whose wreck is deliberately not given the sink velocity and so
stays where it died. That exception is the best evidence the rule is real: the
exe carves out exactly the one thing that is supposed to stay on the surface.

The mechanism is *not* "take the terrain height and ignore sea level". The
resting height is the terrain height, but the wreck reaches it by falling over
several seconds, and the code that decides to make it fall is a sea-level test
on the terrain under the dying unit. Both halves matter, and the second half is
also what stops the wreck catching fire.

---

## NN. Where a wreck comes from at all

The chain, from the damage that killed the unit down to the feature:

| Address | Role |
|---|---|
| `0x489BB0` | the single damage choke point (§6) |
| `0x4864B0` | kill: build the death packet, run the COB `Killed` |
| `0x4866D0` | the death handler that consumes that packet |
| `0x486360` | **the corpse spawner** — everything here starts from this |
| `0x423C50` | generic feature placement |
| `0x424214` | the falling-feature physics, in the per-tick feature sweep |

`0x4864B0` packs the outcome into one byte of the packet, `[pkt+0xA]`: the
**death cause** in the high nibble and the **corpse level** the script asked
for in the low nibble. Causes 4, 5 and 9 skip the `Killed` script entirely and
leave level 0; cause 7 forces level 1; everything else calls the script — §22
already had this much:

```
486525  cmp ebx,0x7                 ; cause 7 -> forced corpse, level 1
486542  cmp ebx,0x4 / 486548 cmp ebx,0x5 / 48654d cmp ebx,0x9  ; -> level 0
4865b8  mov ecx,[esi+0x9a] / push 0x508be8 / call 0x4b0bc0     ; "Killed"
48661c  shl bl,0x4 / 48661f or al,bl ; cause<<4 | level  ->  pkt+0xA
```

and `0x4866D0` unpacks it and calls the spawner:

```
486d55  mov  al,BYTE PTR [edi+0xa]
486d58  test al,0xf
486d5a  jbe  0x486d74               ; level 0 -> no wreck, at all, anywhere
486d60  and  dl,0xf0
486d63  cmp  dl,0x70
486d66  setne cl                    ; cl = (cause != 7)  -> "may it burn?"
486d69  and  eax,0xf                ; level 1..15
486d6c  push ecx / push eax / push esi
486d6f  call 0x486360
```

So `0x486360(unit, corpseLevel, mayBurn)`. **VERIFIED.**

The severity the script is handed, and so the level it picks, is
`clamp(1, 100, (100*overkill/maxdamage + unit[0xF7]) / 2)` — §22's formula,
unchanged. A shipped `Killed` is a three-band ladder; `ARMAH.COB` is typical
(disassembled with the scratchpad's `cobdis.py`):

```
719: PUSH_LOCAL_VAR 0 / PUSH_CONSTANT 25 / SET_LESS_OR_EQUAL / JUMP_IF_ZERO 768
726: PUSH_CONSTANT 1  / POP_LOCAL_VAR 1        ; severity <= 25 -> level 1
775: PUSH_CONSTANT 2  / POP_LOCAL_VAR 1        ; severity <= 50 -> level 2
824: PUSH_CONSTANT 3  / POP_LOCAL_VAR 1        ; severity <= 99 -> level 3
```

## NN. `0x486360`, the corpse spawner, in full

This is the whole routine, and the whole of the water handling:

```
486368  mov  eax,[edi+0x92]              ; the unit definition
48636e  mov  si,WORD PTR [eax+0x1bc]     ; Corpse, a feature type index
486375  mov  eax,[esp+0x18]              ; corpse level
486379  cmp  eax,1 / jle 0x4863ac
486384  cmp  si,0xfffb / jae 0x486450    ; sentinel -> nothing is left
48639f  mov  si,WORD PTR [edx+esi*1+0xf4]; featuredead: walk one level down
4863a7  cmp  eax,1 / jg 0x486384         ; ... level-1 times
4863ac  cmp  si,0xfffb / jae 0x486450    ; still a sentinel -> nothing
4863b7  movsx eax,WORD PTR [edi+0x78]    ; the unit footprint cell z
4863bb  movsx ecx,WORD PTR [edi+0x76]    ; ... and x
4863c1  call 0x481550                    ; -> the map square
4863c8  test ebx,ebx / je 0x486450       ; off map -> nothing

4863d0  lea  ebp,[edi+0x6a]              ; &unit.position
4863d3  push ebp
4863d4  call 0x485070                    ; interpolated TERRAIN height there
4863e1  mov  dl,BYTE PTR [ecx+0x1427f]   ; sea level
4863e7  cmp  eax,edx
4863e9  jg   0x486426                    ; terrain ABOVE sea level -> dry

;--- wet: terrain at or below sea level --------------------------------
4863f4  push edx                         ; unit+0xFF, the owner
4863f5  lea  eax,[edi+0x64] / push eax   ; &unit.rotation
4863f6  push ebp                         ; &unit.position
4863f7  push esi                         ; the feature type
4863f8  push ebx                         ; the map square
4863f9  call 0x423c50
486402  je   0x486439                    ; placement refused -> nothing to sink
486404  mov  edx,[edi+0x92]
48640a  test DWORD PTR [edx+0x241],0x1000000    ; isfeature?
486414  jne  0x486420                            ; ... then leave it alone
486416  mov  DWORD PTR [eax+0x18],0xffffd334     ; feature velocity.y
48641d  mov  DWORD PTR [eax+0x1c],ecx            ; feature velocity.z = 0
486420  mov  DWORD PTR [esp+0x1c],ecx            ; mayBurn = 0
486424  jmp  0x486439

;--- dry ---------------------------------------------------------------
486426  mov  al,BYTE PTR [edi+0xff]
48642c  add  edi,0x64
48642f  push eax / push edi / push ebp / push esi / push ebx
486434  call 0x423c50                    ; identical arguments

486439  mov  eax,[esp+0x1c]
48643f  je   0x486450
486441  push 0x9 / push 0x384 / push 0xf / push ebp
48644b  call 0x472630                    ; the 30-second burning-wreck plume
```

Five things fall out of that, all **VERIFIED**:

1. **The two branches create the feature with identical arguments.** Same
   square, same feature type, same position pointer, same rotation pointer,
   same owner. The water branch does not adjust the height, does not consult
   `waterline`, does not look at the unit's own y. Whatever the unit's y was at
   the instant it died is the wreck's starting y, on land and at sea alike.
2. **The test is on the terrain, not on the unit.** `0x485070` is the bilinear
   heightmap sample at the unit's (x, z) — it reads `WORD[pos+2]` and
   `WORD[pos+0xA]`, the integer halves of the 16.16 x and z, and interpolates
   the four corner bytes. So a unit dying twenty units *above* deep water takes
   the wet branch, and a hovercraft sitting on a beach one unit above the
   waterline takes the dry one. The comparison is `jg` on the terrain, so
   terrain **exactly at** sea level counts as wet.
3. **The wet branch sets a downward velocity**, `0xFFFFD334` = `-11468`, into
   `feature+0x18`, and zeroes `feature+0x1C`. Those are the y and z components
   of a three-component velocity at `feature+0x14`; the next section is the
   physics that spends them.
4. **`isfeature` (flags word A bit 24) exempts a wreck from sinking.** The bit
   is parsed at `0x42C7B3`–`0x42C7CB` from key string `0x503B30` `"isfeature"`,
   `and eax,1 / shl eax,0x18` immediately after the call that read it, so the
   §82 pipeline trap does not apply and the bit is unambiguous. It extends
   §16's small-flag table.
5. **A wreck that lands in water does not burn.** The wet branch zeroes the
   flag that would otherwise fire `0x472630` — §4's burning-wreck emitter,
   layer 9, one puff every 15 ticks for 900 ticks. A land wreck smoulders for
   thirty seconds; a sea wreck never does.

Two smaller quirks, both **VERIFIED**, both harmless:

- The `mayBurn` flag is cleared **inside** the `feature != null` test, so if the
  wet placement is *refused* (occupied cells, an indestructible feature in the
  way) the plume still fires — smoke over open water with no wreck under it.
- `feature+0x14` (velocity.x) is never written by the spawner. It is zero in
  practice: the pool is `rep stos`-cleared at `0x421F45`, the physics zeroes all
  three on landing (`0x424265`) and zeroes x again on every underwater tick
  (`0x42428C`), and nothing in the binary ever writes a non-zero x. The only
  way a stale value could survive is a wreck destroyed mid-sink, and the next
  tenant of that pool slot is clamped to the ground on its first tick anyway.

### A correction to §4

§4 records `0x4863E7` as one of two sites where "the explosion and wreck
emitters check the world `y` of the point against the sea level byte at
`globals+0x1427F` first and emit nothing underwater". Half right. The site is
the wreck spawner, the emitter really is suppressed, but the value compared is
the **terrain height under the dying unit**, not the point's y — and the same
test does the far more consequential job of deciding whether the wreck sinks.

## NN. `0x423C50`, and where a feature's y actually comes from

The feature record is **48 bytes** (`lea eax,[edi+edi*2] / shl eax,4`), in a
pool of `0x800` allocated and zeroed at `0x421F29`–`0x421F4A` (`0x18000` bytes).
What the placement routine writes:

| Offset | Meaning | Written at |
|---|---|---|
| `+0x00` / `+0x02` | next / previous index in the intrusive list | `0x4232F0` |
| `+0x04` | the animation/model object, from `featdef+0x98` | `0x423ECB` |
| `+0x08` `+0x0C` `+0x10` | **position x, y, z**, 16.16 | `0x423E28`–`0x423E38` |
| `+0x14` `+0x18` `+0x1C` | **velocity x, y, z**, 16.16 | only `0x486416`, `0x424292` |
| `+0x20` `+0x24` | rotation, three words | `0x423E9B`–`0x423EA6` |
| `+0x28` | the packed cell coordinates | `0x423E23` |
| `+0x2C` | the feature type index | `0x423E19` |
| `+0x2F` bit 0 | the sprite-animation flag, cleared on allocation | `0x423DDA` |

The position is a straight copy when the caller supplies one:

```
423e13  test ecx,ecx                    ; arg3, the position pointer
423e26  je   0x423e3d                   ; null -> derive from the cell instead
423e28  mov  edx,[ecx]   / mov [eax],edx        ; eax = feature+8
423e2f  mov  edx,[ecx+4] / mov [eax+4],edx
423e35  mov  ecx,[ecx+8] / mov [eax+8],ecx
```

and only the *other* path — the map loader, regrowth, reclamate smudges --
derives a height from the terrain:

```
423e70  call 0x485070
423e75  shl  eax,0x10                   ; terrain height << 16 -> y
```

**VERIFIED: a corpse is placed at the dead unit's own y, land or sea.** There is
no sea-level clamp, no `waterline`, no footprint-centre re-derivation. Note
also that `0x486360` passes the *cell* from `unit+0x76`/`+0x78` — the top-left
of the unit's footprint, written by `0x48A9F0` at `0x48AA81` — while the
*position* is the unit's centre, so a wreck whose footprint differs from its
unit's is laid out from the unit's corner and drawn from the unit's middle.

The square records the owner too: `0x423EF4`–`0x423F07` puts arg5 (`unit+0xFF`,
the dying unit's player) into bits 3-6 of `square+0xC`, and bit 0 of the same
byte marks that this square's feature has a pool record at all. The owner
nibble matters exactly once, in the `nodrawundergray` test below.

## NN. `0x424214`: the sink, and it is the only thing features ever do

Inside the per-tick feature sweep, after the regrowth roll, the engine walks the
active list at `globals+0x14213` (head; `+0x14217` is the retired list,
`+0x1421B` the free list) and, for every entry whose definition is a **3-D**
feature — `featdef+0xFE` bit 0 clear, i.e. it named an `object=`, which every
corpse in the game does — runs this:

```
424214  mov  edx,[esi+0x14]             ; velocity.x
42421a  test edx,edx / jne 0x424230
42421e  mov  edx,[edi+0x8]              ; velocity.z
424223  jne  0x424230
424225  mov  edx,[edi+0x4]              ; velocity.y
42422a  je   0x4242b8                   ; all zero -> retire to the idle list

424230  position += velocity            ; 0x424230-0x424250, three adds
424253  call 0x485140                   ; the square mid height under it
424258  mov  ecx,[esi+0xc]              ; position.y
42425b  shl  eax,0x10
42425e  cmp  ecx,eax
424260  jg   0x424278
424262  mov  [esi+0xc],eax              ; LANDED: snap to the ground
424265  velocity = (0,0,0)              ; 0x424265-0x424270

424278  mov  dl,BYTE PTR [eax+0x1427f]  ; sea level
424285  shl  edx,0x10
424288  cmp  ecx,edx
42428a  jge  0x4242a5
42428c  mov  DWORD PTR [edi],0x0        ; UNDERWATER: velocity.x = 0
424292  mov  DWORD PTR [esi+0x18],0xffffd334   ;   velocity.y = -11468, held
424299  mov  DWORD PTR [esi+0x1c],0x0   ;   velocity.z = 0
4242a0  jmp  done

4242a5  mov  eax,[eax+0x14263]          ; ABOVE WATER: the map gravity
4242ab  mov  ecx,[esi+0x18]
4242ae  sub  ecx,eax
4242b0  mov  [esi+0x18],ecx             ;   velocity.y -= g
```

**VERIFIED**, and the shape of it is worth stating plainly:

- Above the waterline a feature is in **free fall** under the same per-tick
  gravity the ballistic solver reads at `0x49A89F` (§12), `[globals+0x14263]`,
  loaded from the OTA's `gravity` key (parsed `0x436542` into `mapinfo+0xD38`).
- Below the waterline it moves at a **fixed terminal velocity** straight down,
  with any horizontal drift killed. The velocity is re-asserted every tick, so
  it neither accelerates nor decays.
- At the terrain it **stops dead**, and the zero velocity retires it from the
  active list on the following tick (`0x4242B8` moves the node to the list at
  `globals+0x14217`). It stays there for the rest of the game.

Two details of the landing. The height it lands on comes from `0x485140`, which
is *not* the bilinear `0x485070` the spawner used to decide wet-or-dry: it
returns `(square[6] + square[5]) / 2`, the average of the cell's stored **min
and max** heights — the same pair §2's line-of-sight code reads — i.e. the
cell's mid height, and `-1` off the map. So the resting height is a whole
number, per cell, and can differ by a unit or two from the interpolated
surface. And the grounding test is `<=`, so a wreck that starts on the ground --
every land wreck — would ground on its first tick if it had any velocity at all.

**The velocity block exists for nothing else.** Grepping the whole `.text` for
stores to `feature+0x14/0x18/0x1C`, the only ones are the spawner's `0x486416`
and `0x48641D`, the physics' own `0x424292`, `0x424299` and `0x4242B0`, and the
zeroing at `0x424265` and `0x42428C`. `0xFFFFD334` appears exactly twice in the
binary, at `0x424292` and `0x486416`. Nothing else in Total Annihilation ever
gives a feature a velocity. **The falling-feature physics is the wreck sink, and
only that.**

The corollary is a real quirk, **VERIFIED** by the same absence: a wreck created
in mid-air **over land** — a Peewee killed in an Atlas's claw over a plateau,
say — is created at the transport's y with zero velocity, is retired from the
active list on its first tick, and hangs in the air for good. The original only
ever drops wreckage into water.

## NN. What the sink rate is worth on the shipped maps

`0xFFFFD334` is `-11468` in 16.16, so **0.174988 world units a tick**, and at
thirty ticks a second **5.25 units a second**. For comparison, one tick of the
`gravity=112` nearly every map ships is `112/900 = 0.1244` units, so terminal
velocity is reached about a tick and a half into a fall: the transition through
the surface is effectively instant.

Read out of the 37 shipped `.tnt` headers (sea level is the tenth dword of the
header; the tile attribute table is one height byte per 16-unit cell):

| Map | Sea level | Water cells | Deepest | Time to sink from the surface |
|---|---|---|---|---|
| Anteer Strait | 75 | 95.0 % | 75 | 14.3 s |
| Seven Islands | 75 | 93.1 % | 75 | 14.3 s |
| Shore to Shore | 85 | 71.5 % | 85 | 16.2 s |
| Hundred Isles | 80 | 92.9 % | 80 | 15.2 s |
| Gods of War | 55 | 78.7 % | 55 | 10.5 s |
| Kill The Middle | 24 | 42.0 % | 24 | 4.6 s |
| Lava Run | 21 | 9.2 % | 21 | 4.0 s |
| Lava Mania | 23 | 2.7 % | 2 | 0.4 s |

Deep ocean is 55-85 units below the surface on every naval map in the box, so a
wreck dropped in open water takes **ten to sixteen seconds** to reach the bottom
and is visibly on its way down for all of it. In the shallows near a shore, or
on a map like Lava Mania whose "water" is two units deep, it is on the bottom
inside half a second and the sink is invisible. That is the likeliest root of
any "it just floated" impression: a hovercraft fights near a shoreline, and near
a shoreline the sink is a fraction of a second and a couple of pixels.

## NN. The wreck still draws, but its shadow does not

The wreck does not vanish when it goes under. **VERIFIED**, and worth stating
because the opposite would be the natural guess.

The world painter (§5) has no depth buffer; features are blitted after the
terrain, so a wreck on the sea bed is drawn *over* the water tiles, displaced
down-screen by `y/2` under the `screenY = z - y/2` projection — about 37 pixels
at 1x on a 75-deep sea. The feature draw dispatch at `0x46A610` takes the 3-D
branch and hands the whole thing to the ordinary unit draw:

```
46a721  mov  eax,[eax+0x1420f]          ; a scratch unit object
46a734  lea  ecx,[ebx+0x20]             ; the feature rotation
46a73a  add  ebx,0x8                    ; ... and its position
46a74e  mov  [eax+0x6a],ecx             ; -> scratchUnit.position
46a762  call 0x45ac20                   ; the section-52 unit draw path
```

so the y that gets projected is the feature's own. And there is no waterline
clip anywhere in the model path: every read of the sea level byte in the whole
`0x458000`–`0x45B000` model-drawing module is at `0x4592E4`, `0x45953D`,
`0x459599` and `0x4596F7`, and all four are inside the **shadow** block
`0x45949D`–`0x4595EE` and its twin. That block does clip:

```
459591  eax = seaLevel
4595a3  sub  eax,edx                    ; depth of water under the caster
4595a5  test eax,eax / jle skip
4595c5  and  ecx,0x4b / add ecx,0x32    ; digger ? 0x7D : 0x32
4595cb  add  eax,ecx
4595cf  call 0x4ba1b0                   ; erase every pixel below that level
```

with `0x4BA1B0` a per-pixel erase of the shadow bitmap against a height buffer:

```
4ba1e0  cmp BYTE PTR [ebx],cl / ja skip ; ebx = the per-pixel height buffer
4ba1e4  mov dl,BYTE PTR [ebp+0x8]
4ba1e7  mov BYTE PTR [edi],dl           ; -> the transparent index
```

Shadows are also skipped outright for `canhover|floater`
(`test [esp+0x10],0x81000` at `0x45957F`) and for a caster below the waterline
(`0x45953D`). So: **the wreck is drawn in full at whatever depth it reaches;
only its shadow is cut off at the waterline.**

One extra flag surfaced while reading the draw dispatch, and it belongs in
§15's feature-flag table: **bit 11 of `featdef+0xFE` is `nodrawundergray`** (key
string `0x502CF0`, read at `0x422C3B`, `shl eax,0xb` at `0x422C55`). Its only
reader is the feature draw at `0x4698D8`: a feature carrying it is drawn
unconditionally for the player who owns it (the owner nibble in `square+0xC`),
and for anyone else only if the cell passes `0x4658E0` — currently visible,
not merely explored — and is otherwise **not drawn at all**. Exactly two
features in the shipped data set it, and they are `FloatingTeeth` and
`FloatingTeeth_core`.

## NN. The shipped data: what each kind of unit actually names

Joined over the 272 FBIs and 1 075 feature sections RWE loads (base game plus
`ccdata`), classified by the unit's `Category`:

| Class | Units | With no `Corpse` key | Corpse has no `featuredead` |
|---|---|---|---|
| VTOL | 30 | **30** | — |
| SHIP | 23 | 0 | **23** |
| UNDERWATER | 8 | 0 | **8** |
| HOVER | 11 | 0 | **11** |
| land + buildings | 200 | 22 | 25 |

Corpse shape, by class (`height` / `blocking`):

| Class | 4 / 0 | 20 / 1 | 40 / 1 | other |
|---|---|---|---|---|
| HOVER | — | 11 | — | — |
| SHIP | 15 | 6 | 1 | 1 (`corcarry_dead`, 4 / 1) |
| UNDERWATER | 5 | 3 | — | — |
| land + buildings | 13 | 135 | 25 | 5 |

Samples, verbatim from `rev31\features\Corpses\arm_corpses.tdf` and
`ccdata\features\Corpses\Armah_dead.tdf`:

```
[armah_dead]        [armroy_dead]        [armsub_dead]        [armpw_dead]
 object=armah_dead   object=armroy_dead   object=armsub_dead   object=armpw_dead
 footprintx=3        footprintx=5         footprintx=3         featuredead=armpw_heap
 footprintz=3        footprintz=5         footprintz=3         footprintx=2
 height=20           height=4             height=4             height=40
 blocking=1          blocking=0           blocking=0           blocking=1
 metal=96            metal=718            metal=921            metal=42
 damage=300          damage=24000         damage=24000         damage=200
 reclaimable=1       reclaimable=1        reclaimable=1        reclaimable=1
```

The readings, all **VERIFIED from the shipped data**:

- **A hovercraft's corpse is written exactly like a land unit's** — `height=20`,
  `blocking=1`, a few hundred `damage`, a 3x3 footprint. There is nothing naval
  about it and nothing that could make it float. All eleven are alike, and all
  eleven hovercraft FBIs are alike too: `canhover=1`, `MaxWaterDepth=0`,
  `MovementClass=TANKHOVER3`, no `Floater`, no `WaterLine`.
- **Ship and submarine corpses are flat non-blocking plates**: `height=4`,
  `blocking=0`, `damage=24000`. They are authored to lie on the sea bed and be
  driven over. The six SHIP entries at `20 / 1` are the *floating buildings* the
  category sweeps up — the AA platform, sonar, jammers — not vessels.
  `corcarry_dead` at `4 / 1` is an inconsistency in the data, not a rule.
- **`damage=24000` is not a marker the engine reads.** §24 established that a
  feature's `damage` is only its hit points; the naval corpses simply declare a
  large number so nothing clears them.
- **No naval or hover corpse names `featuredead`.** Every land corpse
  (`armpw_dead` -> `armpw_heap`) does, and no heap names one in turn
  (`armpw_heap` has none). So the three-band `Killed` ladder resolves as: level
  1 -> the wreck; level 2 -> the heap for a land unit, **nothing** for a ship, a
  submarine or a hovercraft; level 3 -> nothing for anybody. The chain walk is
  `0x48639F` (`mov si,WORD[featdef+0xF4]`, the key parsed at `0x422F2F` and
  defaulted to `0xFFFF` at `0x422FBF`) and the sentinel test is
  `cmp si,0xfffb / jae return`.
- **No aircraft in the game names a `Corpse` at all** — all thirty, both sides,
  base game and Core Contingency. `unitdef+0x1BC` is preset to `0xFFFF` at
  `0x42CCD4` and only overwritten if the key is present (`0x42CCF0`), so a
  shot-down aircraft leaves nothing, over water or over land, at any severity.

And the key census, which is the negative result the brief asked for. Over
`totala1/features`, `rev31/features` and `ccdata/features` together — 2 976
sections — the complete set of keys that ever appears is:

```
animating animtrans autoreclaimable blocking burnmax burnmin burnweapon
category damage description energy featureburnt featuredead featurereclamamate
featurereclamate filename flamable footprintx footprintz geothermal height
hitdensity indestructible metal nodisplayinfo nodrawundergray object permanent
reclaimable reproduce reproducearea seqname seqnameburn seqnameburnshad
seqnamedie seqnamereclamate seqnameshad shadtrans sparktime spreadchance world
```

**`floating`, `waterline`, `sinks`, `sealevel` and anything else water-related:
not found.** A feature has no idea it is in water. Everything water-related
about a wreck is decided by the engine, in `0x486360` and `0x424214`.

(`permanent` appears 157 times in the data and its string is **not in the exe**
at all — a dead key, like §24's `hitdensity`. Noted in passing.)

## NN. The one thing that does not sink: `IsFeature=1`

Six units set `IsFeature=1` (flags word A bit 24), and they are the walls:

| Unit | Corpse | `Waterline` | Notes |
|---|---|---|---|
| ARMDRAG / CORDRAG | DragonsTeeth / ..._core | — | land dragon's teeth |
| ARMFORT / CORFORT | fortification / ..._core | — | land forts |
| **ARMFDRAG / CORFDRAG** | **FloatingTeeth / ..._core** | **6** | `MinWaterDepth=1`, `MaxWaterDepth=0` |

`ARMFDRAG.FBI`:

```
MaxWaterDepth=0;   MinWaterDepth=1;   Waterline=6;   IsFeature=1;
Corpse=FloatingTeeth;
```

and `FloatingTeeth`:

```
object=armfdrag;  footprintx=2;  footprintz=2;  height=100;  blocking=1;
metal=16;  damage=3800;  reclaimable=1;  autoreclaimable=0;  nodrawundergray=1;
```

The floating dragon's teeth exist only in water — `MaxWaterDepth=0` means they
cannot be built on land at all — and their wreck has to stay on the surface
where it can still block. `0x48640A` is the line that makes that work, and it is
the only conditional in the whole wreck path that is not about geometry.
`nodrawundergray` on the corpse is the companion: a floating barricade must not
leave a ghost outline in remembered fog. **VERIFIED.**

Two further notes, since this is the only place `Waterline` and `IsFeature` are
exercised together. §12 established that the `waterline` clamp at `0x43D72E`
is gated on `Floater` and that the two key sets do not overlap in
`rev31\UNITS`; `ARMFDRAG` does not set `Floater` either, so its `Waterline=6`
still only reaches `setSFXoccupy`. And its wreck, unsunk, keeps the y the unit
had, which for a `Waterline=6` unit is six units below the surface — the same
place the living barricade sat.

## NN. When there is no wreck at all

Five ways, only one of which is even indirectly about water:

1. **The unit names no `Corpse`.** `unitdef+0x1BC` stays `0xFFFF`
   (`0x42CCD4`), `cmp si,0xfffb / jae` at `0x4863AC` returns. Every aircraft,
   plus `armvader` and `corroach`.
2. **The corpse level is 0.** `test al,0xf / jbe` at `0x486D58`. Death causes 4,
   5 and 9 force it (`0x486542`–`0x486550`), and a script can ask for it
   directly; §22 is the D-gun case, where the *script* declines at severity 100.
3. **The level walks off the end of the `featuredead` chain.** Level 2 on a
   ship, a submarine or a hovercraft; level 3 on anything at all.
4. **The cell is off the map** — `0x481550` returns null, `0x4863C8` returns.
5. **The footprint is not clear.** `0x423D5F`–`0x423D71` walks the footprint
   and, for every cell that already holds a feature, calls `0x4246B0(square, 0)`;
   if that refuses, the whole placement fails and returns null. `0x4246B0`
   refuses when the cell's feature word is `>= 0xFFFB` (a void or blocked
   marker) or when the standing feature is **indestructible** (`featdef+0xFF`
   bit 1, tested at `0x424715`) — rocks, metal patches, geothermal vents. So a
   unit that dies standing on a metal patch leaves nothing, and this applies at
   sea as much as on land: a wreck cannot be laid over an underwater metal
   deposit.

**There is no water-only "no wreck" rule.** The wet branch of `0x486360` creates
the feature unconditionally. **VERIFIED by reading the whole routine.**

## NN. The five questions, answered

1. **Hovercraft.** The "floats" account is *refuted*; the sinking account is
   *confirmed*. A hovercraft's corpse feature is data-identical to a land
   unit's, there is no float key in the exe or the data, and `0x486416` gives
   its wreck the sink velocity like any other. It is created at the unit's y --
   which for a hovercraft over water is sea level exactly (§10; RWE agrees,
   `GameSimulation.cpp:848`, `UnitBehaviorService.cpp:1329`) — and then falls
   0.175 units a tick to the sea bed and stops. It stays drawn there, and stays
   blocking and reclaimable. In shallow water the drop is a fraction of a second
   and is easily mistaken for floating; in deep water it takes ten to sixteen
   seconds and is unmistakable.
2. **Ships and submarines.** Same code path, same sink, and their corpses are
   authored for it: `height=4`, `blocking=0`, so they lie flat on the bottom and
   obstruct nothing. A submarine's wreck starts at the submarine's cruising
   depth rather than at the surface and so has less far to fall. Neither names a
   `featuredead`, so a hard enough kill leaves nothing at all.
3. **Ground units killed over water** — dropped by a transport, wading a shallow
   tile, or dying inside a transport that dies (§38: passengers are killed by
   30 000 points and detached at the transport's position). The wreck is created
   at that position with the unit's y, whatever it is. If that y is above the
   surface it free-falls under gravity until it crosses the waterline and then
   sinks at the fixed rate; either way it ends on the sea bed. Its corpse is
   `blocking=1` with `height=20` or `40`, so unlike a ship's it *does* obstruct
   the cells it lands on.
4. **Aircraft.** No wreck, anywhere, ever. Not a water rule — thirty out of
   thirty aircraft omit the `Corpse` key.
5. **No wreck because of water?** No. The list in the previous section is the
   complete set, and none of it is water-conditional. What water *does* remove
   is the fire: the thirty-second burning plume is suppressed for any wreck that
   lands in it.

---

## NN. What RWE does today

RWE spawns the corpse and then never touches it again.

`src/rwe/sim/GameSimulation.cpp:3705`, in `deleteDeadUnits`:

```cpp
if (deadState->leaveCorpse && !unitDefinition.corpse.empty())
{
    corpsesToSpawn.push_back(CorpseSpawnInfo{
        unitDefinition.corpse,
        unit.position,
        unit.rotation});
}
```

and `trySpawnFeature` (`GameSimulation.cpp:3674`) hands that straight to
`addFeature(MapFeature{*featureId, position, rotation})`. `MapFeature`
(`src/rwe/sim/MapFeature.h`) has a position, a rotation, reclaim progress, hit
points and the two burning fields — **no velocity**. The per-tick sweep
(`GameSimulation::tick`, around line 3900) runs `updateBurningFeatures` and
`updateFeatureRegrowth` and nothing else that touches a feature's position.

Item by item against the original:

| | Original | RWE today |
|---|---|---|
| Wreck position at spawn | the dead unit's exact position | the same — **already correct** |
| Wreck rotation | the dead unit's rotation | the same — **already correct** |
| Over water | sinks 0.175 u/tick to the sea bed | **stays at the death y forever** |
| Above the waterline | free-falls at map gravity | **hangs in the air** |
| Resting height | the cell's `(min+max)/2` | never rests; never moves |
| `isfeature` exemption | wreck does not sink | n/a — no sink, and `isfeature` is not parsed at all |
| Burning wreck | 30 s plume, suppressed in water | no burning wreck at all (§84 already lists this) |
| Corpse level from `Killed` | 1/2/3, walking `featuredead` | **matched** since September 2026: the severity is computed from the overkill and the level the script writes back is read and walked |
| No `Corpse` key | no wreck | the same — **already correct** |
| Placement blocked by an indestructible feature | no wreck | `addFeature` refuses on `anyFeatureOccupies` (`GameSimulation.cpp:259`) — close, but it refuses on *any* standing feature rather than only on indestructible ones, and never clears a destructible one |
| Blocking on the sea bed | the cell is occupied whatever the y | the same — **already correct**: `addFeature` stamps `occupiedGrid` regardless of height, so a wreck already blocks submarines and amphibians |
| Drawing a submerged wreck | drawn in full over the water | drawn, tinted by `waterTint` in `shaders/unitTexture.frag`; the terrain is drawn with `GL_DEPTH_TEST` off (`GameScene.cpp:1531`) so it writes no depth and the later feature pass wins. §59 already resolved to keep the tint |
| Shadow of a wreck over water | clipped at the waterline | `GameScene.cpp:1646` already lifts the shadow plane to sea level for a feature at or above it; below the surface, nothing |

The practical consequence: **in RWE today a hovercraft wreck really does float**,
because RWE leaves it at sea level. So does a ship's, and a submarine's wreck
hangs at whatever depth the submarine was cruising at. That is the behaviour to
change.

## NN. Implementation spec for RWE

Scope: the sink, and the two things that hang off the same test. Not the corpse
level ladder — worth doing, but it is a separate change to the `Killed`
plumbing, and it is listed at the end as a follow-up.

### 1. Give a feature a velocity

`src/rwe/sim/MapFeature.h`, next to `position`:

```cpp
/**
 * Per-tick fall, in world units. Non-zero only while a wreck that died over
 * water is on its way to the sea bed: TotalA.exe 0x486416 sets it and
 * 0x424214 spends it. Every other feature in the game has zero here and is
 * never moved again -- the original's whole falling-feature physics exists
 * to sink wreckage and does nothing else.
 */
SimVector velocity{0_ss, 0_ss, 0_ss};
```

Then, because it is simulation state: add it to `computeHashOf(const MapFeature&)`
(`src/rwe/sim/GameHash_util.cpp:319`), to `saveMapFeature`
(`src/rwe/game/save_util.cpp:321`) and to the feature loader
(`save_util.cpp:1886`, after `addFeature`, alongside `reclaimProgress`). The
saveload round-trip test will fail if the hash is missed, which is the point.

### 2. Set it when the unit died over water

`src/rwe/sim/GameSimulation.cpp`, where `trySpawnFeature` is called from
`deleteDeadUnits`. The test is on the **terrain** under the dying unit, not on
the unit's own y, and it is `<=`, so a cell exactly at sea level counts as water:

```cpp
// TotalA.exe 0x4863D4-0x4863E9. Wet or dry is decided by the ground under
// the corpse, not by where the unit was: a unit dying twenty units above
// deep water leaves a sinking wreck, and a hovercraft on a beach one unit
// above the waterline does not.
// 0x486416: the sink is a fixed -11468 in 16.16 a tick, not an acceleration.
static const SimScalar WreckSinkSpeed(0.174988f);

auto ground = terrain.getHeightAt(position.x, position.z);
if (ground <= terrain.getSeaLevel() && !unitDefinition.isFeature)
{
    feature.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
}
```

`trySpawnFeature` currently discards the `FeatureId` that `addFeature` returns;
it needs to keep it so the velocity can be written onto the placed feature, and
to do nothing when placement was refused (matching `0x486402`).

`isFeature` needs parsing: `src/rwe/io/fbi/io.cpp` alongside the other boolean
keys, a `bool isFeature` on `src/rwe/sim/UnitDefinition.h`, and the copy in
`src/rwe/LoadingScene_util.cpp`. Six units set it; the two that matter are
`ARMFDRAG`/`CORFDRAG`, whose wrecks must stay on the surface.

### 3. Move it, once a tick

A new `GameSimulation::updateFallingFeatures()`, called from `tick()` next to
`updateBurningFeatures()` and **before** `deleteDeadUnits()`, so a wreck spawned
this tick first moves next tick as in the original. Transcribed from `0x424214`:

```cpp
for (auto& [id, f] : features)
{
    if (f.velocity == SimVector(0_ss, 0_ss, 0_ss))
    {
        continue;   // 0x42421A-0x42422A: retired, and never looked at again
    }

    f.position += f.velocity;

    auto ground = terrain.getHeightAt(f.position.x, f.position.z);
    if (f.position.y <= ground)
    {
        f.position.y = ground;                       // 0x424262
        f.velocity = SimVector(0_ss, 0_ss, 0_ss);    // 0x424265
    }
    else if (f.position.y < terrain.getSeaLevel())
    {
        // Underwater: a fixed terminal fall, re-asserted every tick, with
        // any sideways drift killed. 0x42428C-0x424299.
        f.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
    }
    else
    {
        f.velocity.y -= 112_ss / (30_ss * 30_ss);     // 0x4242A5, map gravity
    }
}
```

The original grounds against the cell's `(min+max)/2` (`0x485140`) rather than
the interpolated surface (`0x485070`); RWE's `terrain.getHeightAt` is the
interpolated one and the difference is a unit or two. Use `getHeightAt` for
both — a second height function for that is not worth it — and add the line to
§83 as a deliberate difference.

Nothing else needs touching. The wreck's `occupiedGrid` cells are stamped at
placement and are independent of its y, so it blocks submarines and amphibians
from the tick it appears, which is what the original does too. Reclaim is
likewise y-independent.

### 4. Do not set the wreck on fire in water

Whenever §4's burning-wreck plume is implemented, gate it on the same test:
`0x486420` clears the flag in the wet branch, so a wreck that lands in water
never smoulders. Today RWE has no burning wreck, so there is nothing to change
yet — but the note belongs beside the sink so the two do not get implemented
separately and inconsistently.

### 5. Tests

`src/rwe/sim/wreckage.test.cpp` already builds wreck-shaped features on flat
terrain (`makeWreckDef`, `makeFlatTerrain`), so extend it there:

- a wreck spawned at sea level over a 20-deep sea reaches `y == groundHeight`
  after `ceil(20 / 0.174988) = 115` ticks and not before, and stops there;
- once landed its velocity is zero and it never moves again, however long the
  sim runs;
- a wreck spawned on land above sea level never moves at all;
- a wreck spawned 40 units *above* the water accelerates under gravity, crosses
  the surface, and from then on descends at exactly `WreckSinkSpeed` a tick;
- a unit with `isFeature` leaves a wreck over water that does not move;
- the wreck's cells are in `occupiedGrid` from the tick it appears, not the tick
  it lands;
- a save/load round trip taken mid-sink preserves the velocity and the game hash
  (`sim/saveload.test.cpp`).

### Observable result

Kill a hovercraft over open ocean: the wreck appears at the surface, sinks
visibly over ten to sixteen seconds, settles on the sea bed and stays there,
blue through the water by the existing `waterTint`, still reclaimable by a
construction submarine, an amphibious constructor or an air constructor from
above. Kill one in the shallows near a shore and it drops half a second's worth
and stops — which is what a play-tester remembers as floating. Kill a ship: its
flat `blocking=0` plate lands on the bottom and nothing has to path around it.
Kill a Peewee dropped from an Atlas over water: its `blocking=1` wreck falls the
whole way and blocks the sea bed where it lands. Shoot down an aircraft over
anything: nothing at all.

### The corpse level, done separately

Since September 2026 this is ported too, and it was the bigger visible
difference of the two. The severity is §22's formula — with `unit+0xF7` taken
as zero, since that term has no known writer anywhere in the binary — and the
level the script writes into its second parameter is read back and walked
along the `featuredead` chain (`0x486379`–`0x4863AC`), so a unit blown apart
hard leaves rubble or nothing where a gently killed one leaves the intact
wreck.

The part that wanted care was reading the answer out of a COB thread that may
have stopped at a sleep. It turns out not to need a mechanism: a finished
thread goes to the environment's `finishedQueue` and is not deleted until the
*next* pass over the scripts, and a sleeping one sits in the sleeping queue,
so the thread is alive either way at the moment the pass returns. A finished
thread has its locals in `returnLocals` and a suspended one has them on its
call stack; both are read, in that order.

---

## NN. Loose ends

- **`unit+0xF7`**, the second term in the `Killed` severity, is still
  unidentified (§22 already flagged it). It matters here only because it feeds
  the corpse level, which decides whether a naval unit leaves anything at all.
- **Death causes 4, 5, 7 and 9** are still decoded as a set and not named
  individually. Cause 7 is the one that both forces a wreck and suppresses the
  burning plume, so naming it would settle what "a wreck that does not burn on
  land" actually is.
- **`0x4658E0`**, the visibility test `nodrawundergray` gates on, is taken to be
  "is this cell currently seen rather than merely explored" from its use in the
  feature draw. *INFERRED*, not followed into.
- **The exact scaling of `[globals+0x14263]`.** The feature physics subtracts it
  from a 16.16 velocity, which forces it to be 16.16 — `112/900` is `8155`
  there, and the sink rate of `11468` is 1.41 times it. §12's ballistic solver
  reads the same dword and squares it as a plain integer, which does not
  obviously reconcile; §12's arithmetic was validated against real shot data,
  so the discrepancy is recorded rather than resolved. Nothing in this document
  depends on it: the sink rate is a literal.
- **`0x485140` versus `0x485070`.** Two different terrain-height functions used
  a hundred instructions apart in the same feature — the cell mid-height for
  the landing clamp, the bilinear surface for the wet/dry decision. No
  consequence found, but worth knowing when replaying the sink tick by tick
  against RWE.
- **A wreck created in mid-air over land hangs there forever.** Stated above as
  a consequence of the zero-velocity retire at `0x42422A`; it was read, not
  observed in the running game. Worth a live check before RWE copies it.
