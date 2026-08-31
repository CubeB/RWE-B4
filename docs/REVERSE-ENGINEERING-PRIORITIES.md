# What is most worth reverse-engineering next

Research note, 2026-08-31. No source was changed.

Ranked by (player-visible impact) × (confidence it can be recovered) / (effort).
Everything below is backed either by a count over the real game data, a
`file:line` in RWE, or an address in `TotalA.exe` (GOG build, MD5
`8e74a1dffa1f5988624c52048f5b20cd`, the one `docs/TOTALA-EXE.md` is written
against).

Two things are excluded because work is already under way on them: tank recoil
direction, and bomber attack runs / gunship hover-attack. §A at the end records
the few adjacent findings that work might otherwise miss.

---

## How the evidence was gathered

**Data side.** `D:\RWE-extract\totala1\units` and `rev31\UNITS` were merged with
rev31 winning, giving 195 distinct FBI files; likewise `totala1\weapons` +
`rev31\Weapons` + `rev31\gamedata\WEAPONS.TDF`, giving 170 weapon definitions
across 31 files. Every `key=` was counted and cross-checked against
`src/rwe/io/fbi/io.cpp`, `src/rwe/io/weapontdf/WeaponTdf.cpp` and
`src/rwe/io/featuretdf/io.cpp`, and then against whether the parsed field is
read anywhere outside the parser.

**Binary side.** New this round: the FBI and weapon-TDF parsers were walked
instruction by instruction to recover the *whole* struct layout, not just the
eight fields already in the findings doc. The parser emits a fixed pipeline —
`push <keystring>` / `call <reader>` / store — where a key's store lands *after
the next key's push*. Pairing keys to stores under that rule reproduces all five
offsets already validated in `docs/TOTALA-EXE.md` (`maxvelocity` +0x192,
`brakerate` +0x19A, `acceleration` +0x19E, `bankscale` +0x1A2, `pitchscale`
+0x1A6, `turnrate` +0x1BA, `sightdistance` +0x202, `sortbias` +0x21A), and every
newly-derived offset that has an independent reader lands in a routine that
makes sense for it (`damagemodifier` +0x1AA in the damage routine, `buildangle`
+0x210 in unit placement, `mincloakdistance` +0x208 in the cloak code). Treat
the table in §B as high-confidence but re-verify any single offset before
building on it.

Helper scripts written for this pass, in the scratchpad next to this file:
`fbiseq.py` (dump the key→store sequence for an address range), `fbimap.py`
(context around every reference to a key string), `bits.py` (decode the packed
flag bitfields). They read `totala.asm` and `ta.strings` in the same directory.

---

## Tier 1 — do these next

### 1. Target selection: how a unit picks what to shoot at

**Behaviour.** RWE walks `sim->units` in id order and takes the *first* live
enemy inside `maxRange` that it can detect
(`src/rwe/sim/UnitBehaviorService.cpp:481–511`). There is no priority, no
category filter, no anti-air preference and no spread of fire.

**How the player notices.** A Samson sits under a bomber and shoots a rock. A
flak gun engages ground units. Every unit in a group targets the same enemy —
whichever happens to have the lowest unit id — so a twelve-unit volley kills one
Peewee and wastes the rest. Nothing ever declines to chase a target it should
ignore.

**Evidence it is a real gap.** `wpri_badTargetCategory` on 79/195 units,
`wsec_badTargetCategory` 15, `noChaseCategory` 48. On the weapon side
`toAirWeapon` (4), `unitsOnly` (5) and `turret` (129) are all parsed into
`WeaponTdf` and read **zero** times outside `io/weapontdf/`.

**Where to start.** The keys are literals at `0x503E88` (`wpri_`), `0x503E70`
(`wsec_`), `0x503E58` (`wspe_`), `0x503E48` (`noChaseCategory`), parsed at
`0x42C00E`–`0x42C0AB` into `def+0x231 / +0x235 / +0x239 / +0x23D`. Each is a
**pointer to a bit array**, not a single dword: category names are interned by
`0x488C50` (a binary search over a sorted table at `0x51E6B4`, allocating on a
miss). **Corrected since this was written** — see `TOTALA-EXE.md` §9, which
answers this entry: what `0x488C50` allocates is a 512-bit *set of unit type
indices*, and `WORD unit+0xA6` is the unit's **type index**, not a category id.
The filter itself is `0x407084`–`0x4070C6`:

```
40707d  mov ax, WORD PTR [edi+0xa6]     ; candidate's category id
407084  mov ebx, DWORD PTR [edx+0x23d]  ; attacker's noChaseCategory mask
407099  and ecx,0x1f ; 40709c shr edi,5 ; 40709f shl ebp,cl
4070a1  test DWORD PTR [ebx+edi*4],ebp  ; mask[id>>5] & (1<<(id&31))
4070a4  jne skip
4070a6  mov edx, DWORD PTR [edx+0x231]  ; wpri_badTargetCategory, same test
4070d0  call 0x49abb0                   ; can this weapon engage that unit?
4070dd  call 0x43b1f0                   ; issue the attack
```

Other readers of `+0x23D`: `0x4063D8`, `0x40B93F`, `0x40FD7E`. The ordering
rule they lead to is in `0x40B7B0`: each candidate is scored `rand(dx² + dz²)`
and the lowest wins, with the bad-target ones kept as a fallback rather than
rejected. Written up in `TOTALA-EXE.md` §9 and implemented.

**Effort / risk.** 2–3 days. Medium risk: the interned id numbering only has to
be internally consistent (RWE would rebuild masks from names on both sides), so
the classic silent-misreading trap does not apply here — a mistake shows up
immediately as units shooting visibly wrong things.

### 2. Return Fire does nothing at all

**Behaviour.** `UnitFireOrders::ReturnFire` exists in the enum, the UI, the
network protocol and the COB getter, and has **no handler in the simulation**.
Target acquisition is gated on `unit.fireOrders == UnitFireOrders::FireAtWill`
(`UnitBehaviorService.cpp:479`), so a unit set to Return Fire simply never
shoots.

**How the player notices.** Switching a unit to Return Fire disarms it.

**Evidence.** `grep -rn ReturnFire src/rwe` returns only
`game/GameScene.cpp:4807/4835/4836`, `proto/serialization.cpp:234/352`,
`sim/cob.cpp:483`, `sim/UnitFireOrders.h:8`. No sim behaviour.

**Where to start.** The damage routine `0x489BB0` is passed the attacker; find
where it records it on the victim, then the path to `0x43B1F0` (issue attack).
The FBI side is `standingfireorder` → bits 2–3 of `def+0x241` (parsed at
`0x42C437`), values 0/1/2 = Hold Fire / Return Fire / Fire At Will, matching
RWE's enum order. Answered and implemented: the trail runs `0x489CE0` →
`0x406F80`, and the *current* firing mode is bits 20–21 of `unit+0x110`, not
the FBI default. `TOTALA-EXE.md` §9.

**Effort / risk.** Half a day for a usable version. Low risk.

### 3. `buildangle` — which way a finished building faces — **done**

Ported; the findings are written up as `TOTALA-EXE.md` §9. Two things below
turned out to be wrong and are corrected in place: the ten capital ships mean
that not every unit setting `buildangle` is a building, and `0x485E77` is the
**mobile** branch of the ordinary spawn, gated on `bmcode`, not a nanoframe or a
factory path. There is also a sixth reader, `0x4862FA`, and a `def`-to-`def`
copy at `0x42B6F1`.

**Behaviour.** RWE invents a flat ±10° random twist for every non-factory
building (`src/rwe/sim/GameSimulation.cpp:2633–2644`, test at
`src/rwe/sim/aircraft.test.cpp:254`). The original reads a per-unit
`buildangle` and spreads over exactly that arc, centred on heading `0x8000`:

```
485bd6  mov ax, WORD PTR [edx+0x210]    ; buildangle
485bde  call 0x4b6c30                   ; random in [0, buildangle)
485bf0  mov ecx,0x8000
485bf5  shr dx,1 ; 485bf8 sub ecx,edx   ; 0x8000 - buildangle/2
485c00  add eax,ecx
485c06  mov WORD PTR [esi+0x66],ax      ; the unit's heading
```

**How the player notices.** The spread is per-unit and ranges over a factor of
32: a vehicle plant ships `buildangle=1024` (±2.8°, essentially square), a solar
collector 4096 (±11°), a metal extractor 8192 (±22.5°), a light laser tower
32768 (**±90°**). RWE gives all of them the same ±10°, so towers are suspiciously
aligned and plants are visibly crooked. It is not only buildings: of the 80
units that set it, ten have a real `MaxVelocity`, and they are exactly the
capital ships — Millenium, Colossus, Conqueror, Crusader, Hulk, Warlord, Hive,
Executioner, Enforcer, Envoy — every one at `16384`, so a ship leaves its yard
on a ±45° spread. (Factories read as mobile too, since TA gives them
`canmove=1` with no `MaxVelocity`.) Land and air units set it nowhere.

**Evidence.** 80/189 of the rev31 units set `buildangle`; seventy are buildings
and ten are the capital ships. `grep -i buildangle` returns nothing in `src/rwe`
outside the invented-twist test.

**Where to start.** String at `0x503C00`, parsed at `0x42C54E` into `WORD
def+0x210`. Readers: `0x485BD6`, `0x485BE9`, `0x485E77`, `0x485F3A`, `0x4860E6`.
Note `0x485E77` is a *different* path that sets the heading to `buildangle`
verbatim rather than randomising — work out which case that is (likely the
nanoframe's initial heading, or a unit emerging from a factory) before
implementing.

**Effort / risk.** 2–4 hours. Low risk, trivially checkable by eye.

### 4. Missile flight: acceleration, launch velocity, vertical launch

**Behaviour.** RWE gives every projectile a single constant speed,
`weaponVelocity/30` (`src/rwe/LoadingScene_util.cpp:156`), and picks one of four
physics types from `tracks` / `lineofsight` / `dropped` / `ballistic`
(`:166–182`). `weaponAcceleration`, `startVelocity`, `selfProp`, `twoPhase`,
`vLaunch`, `flightTime`, `guidance` and `propeller` are all parsed and read
nowhere.

**How the player notices.** Missiles leave the rail at terminal speed instead of
accelerating, so every missile duel resolves at the wrong range and lead. A nuke,
an anti-nuke, a Diplomat/Merl and every missile ship should climb vertically
before turning over; in RWE they head straight for the target from the tube.
Missiles never expire on `flighttime`.

**Evidence.** Of 170 weapon definitions: `weaponacceleration` 54, `selfprop` 54,
`guidance` 50, `startvelocity` 41, `propeller` 23, `vlaunch` 15, `twophase` 15,
`flighttime` 15. `vlaunch` is set on NUCLEAR_MISSILE, CRBLMSSL, AMD_ROCKET,
FMD_ROCKET, SBMISSILE, ARMTRUCK/CORTRUCK_ROCKET and ARM/CORMSHIP_ROCKET.

**Where to start.** The weapon-TDF parser runs `0x42E484`–`0x42EFC3`.
`weaponvelocity` → `wdef+0x68` (`0x42E4BA`), `startvelocity` → `+0x6C`
(`0x42E4D5`), `weaponacceleration` → `+0x70` (`0x42E4F3`), `flighttime` → `WORD
+0xFA` (`0x42E6CF`). The booleans are a bitfield at `wdef+0x111`: `selfprop`
`0x42E927`, `twophase` `0x42E9C1`, `vlaunch` `0x42EB20`, `guidance` `0x42E7A6`,
`tracks` `0x42E7CB`, `cruise` `0x42E9E9`, `propeller` `0x42E949`. From there,
`grep` the listing for reads of `+0x68`/`+0x6C`/`+0x70` inside the projectile
update to find the integration step.

**Effort / risk.** 3–5 days. **High risk** — this is precisely the class where a
plausible reading silently produces wrong behaviour (the `min`/`max` trap from
the aircraft profile). Use the documented method: transcribe the decoded routine
into a standalone program, feed it real weapon values, and compare tick by tick
against RWE before touching the engine.

### 5. Blast damage: edge falloff, armour, and the veterancy that TA actually has

Three findings in one routine, all cheap once you are in it.

**5a. `edgeeffectiveness`.** RWE hardcodes linear-to-zero falloff
(`GameSimulation.cpp:2212` and `:2236`: `1 - d/r`). TA's default really *is*
zero at the edge — the parse at `0x42E59B` pushes a `0.0` default into float
`wdef+0xD8` — so the default case is already right. But 17 weapons override it
(0.25, 0.5, 0.75, 0.8, 0.9), and they are the big-area ones: those weapons
currently do far too little damage away from the impact point. The data files'
own comment confirms the semantics: *"the percentage (1.0 = 100%) of the damage
that is inflicted at the edge of the area of effect"*.

**5b. Armour is parsed and ignored.** `unit.armored` exists in RWE as a
COB-settable flag (`cob.cpp` `SetQueryStatus::Armored`) and has no effect on
damage; `damagemodifier` is not parsed at all. In the original, the damage
routine at `0x489BB0` does:

```
489bc3  mov al, BYTE PTR [esi+0x10e]    ; unit flags
489bcd  test al,0x2                     ; ARMORED?
489bd1  cmp edi,0x7530                  ; damage >= 30000 -> ignore armour
489be4  mov eax, DWORD PTR [eax+0x1aa]  ; DamageModifier (16.16 fixed point)
489bea  imul edi ; 489bec call 0x4e43d0 ; damage * modifier >> 16
```

Nine units ship `DamageModifier`: 0.15, 0.28, 0.33333 (×2) and 0.5 (×5) — the
Annihilator, Doomsday Machine, both anti-nukes, both solar collectors, the metal
maker and the Toaster. The `>= 30000` cut-out is why the D-gun ignores armour.

**5c. TA has real, hidden veterancy, and RWE invented a different one.**
Immediately after the armour step:

```
489bfa  mov cx, WORD PTR [esi+0xb8]     ; the unit's kill count
489c01..489c0a                          ; kills / 5
489c0c  cmp edx,5 ; 489c11 mov edx,5    ; clamp to 5
489c20  sub ecx,edx                     ; 25 - tier
489c22  imul ecx,edi ; 489c25 shl ecx,2 ; * damage * 4
489c28..489c2d                          ; / 100
```

i.e. **damage taken × (1 − 0.04 × min(5, kills/5))** — up to a 20% reduction at
25 kills. `unit+0xB8` is zeroed at unit creation (`0x485C76`) and read by the
unit info panel (`0x46B2C8`, `0x46B306`, `0x46B338`) and the renderer
(`0x467CCF`, `0x467CF1`). RWE's `src/rwe/sim/cob.cpp:365–383` openly invents a
different scheme ("`VeteranKillsPerTier = 4`… the conventional cap used by
TA-derived RTS engines such as Spring's experience tiering") and applies it to
nothing but the COB getter. While you are in there, settle what `GET
VETERAN_LEVEL` actually returns — the raw count at `+0xB8` or the clamped tier.

**Effort / risk.** 5a ~2 h, 5b ~3 h, 5c ~half a day. Low-to-medium risk; each
is a handful of instructions, short enough to transcribe and check against a
hand-worked example.

### 6. Standing orders and mission type from the FBI

**Behaviour.** RWE hard-codes every unit to Fire At Will and Maneuver
(`src/rwe/sim/UnitState.h:335–336`). The FBI says otherwise per unit.

**How the player notices.** Units that should sit still and hold fire —
minelayers' mines, missile silos, anti-nukes — go hunting and shooting. Twenty
units that should default to guard-without-moving do not.

**Evidence.** `standingmoveorder` on 123/195 units (4 of them `=0`, hold
position), `standingfireorder` 117 (13 of them `=0`, hold fire),
`defaultmissiontype` 126 (85 `Standby`, 20 `GUARD_NOMOVE`, 21 `VTOL_standby`),
`mobilestandorders` 123 and `firestandorders` 121 (4 each `=0`). None of the
five is parsed by `src/rwe/io/fbi/io.cpp`.

**Where to start.** `standingmoveorder` → bits 0–1 of `def+0x241` (`0x42C419`),
`standingfireorder` → bits 2–3 of the same word (`0x42C437`). The two
`*standorders` keys are single bits in `def+0x245` (`0x42C8C4`, `0x42C8DD`) and
almost certainly gate whether the corresponding button is offered at all.
`defaultmissiontype` is a `BYTE` at `def+0x230` (parsed `0x42BFE2`, the string
table of mission names is next to `0x503EA8`), read at `0x43B9D0`.

**Effort / risk.** Half a day. Low risk.

### 7. `turret` — weapons that need the hull turned

**Behaviour.** RWE parses `turret` and never reads it. Aiming is delegated
entirely to the COB aim script's return value plus `tolerance`/`pitchTolerance`
(`UnitBehaviorService.cpp:604–655`); nothing makes a unit rotate its body to
bring a fixed gun to bear.

**How the player notices.** Units with hull-mounted guns fire sideways and
backwards without turning.

**Evidence.** 129 of 170 weapon definitions set `turret=1`; the other 41 do not,
and those are the ones that need the body turned. `WeaponTdf::turret` has zero
readers outside `io/weapontdf/`.

**Where to start.** Flag bit in `wdef+0x111`, parsed at `0x42E8F9`; also
`minbarrelangle` → `WORD wdef+0xFE` (`0x42E716`, 19 weapons) for the depression
limit, and `aimrate`/`holdtime` (2 each) for how the turret tracks.

**Effort / risk.** 1–2 days. Medium risk — interacts with the existing aim-script
flow, and issue #42 (aiming scripts running twice) is in the same code.

---

## Tier 2 — high value but harder

### 8. Smoke from damaged units

Every damaged unit in TA trails smoke; RWE has none (`grep -i smoke` in
`src/rwe` finds only weapon smoke trails and debris trails at
`game/GameScene.cpp:4095–4104`). The findings doc already establishes that
damage smoke lives on particle layer 9, drawn over everything including health
bars — so the layer table found for §5 of `TOTALA-EXE.md` is the way in; what is
missing is the health threshold and emission rate in the per-tick unit update.
Cheap to spot on screen, so low risk. ~1 day.

### 9. Radar jammers, stealth, sonar jamming

`radardistancejam` on 6 units, `sonardistancejam`, `stealth` on 2. None parsed.
Given that radar is already a per-tick unit-vs-unit range query in RWE, a jammer
is a small addition. `radardistancejam` → `WORD def+0x20A` (`0x42C3D6`), read at
`0x4392F8` and — worth a look — at `0x466FFC` and `0x467610`, in the
render/overlay path, which suggests the jammed area is drawn. `stealth` is bit 8
of `def+0x241` (`0x42C4D8`). ~1–2 days.

### 10. Cloaking

RWE shows the CLOAK button (`cloakable` is parsed) and does nothing else.
`cloakcost` → float `def+0x1DA` (`0x42C4FE`), `cloakcostmoving` → `+0x1DE`,
`mincloakdistance` → `WORD def+0x208` (`0x42C531`; note `0x42D135` supplies a
default of `0x50` = 80 when the FBI is silent, and `0x4390D2` reads it),
`init_cloaked` bit 4 of `def+0x241`. Four units set `cloakcost`, three
`cloakcostmoving`/`mincloakdistance` — but cloaking is player-facing on the
commander, so impact is out of proportion to the counts. ~2 days.

### 11. Nukes, stockpiles and anti-nukes

`stockpile` 8 weapons, `interceptor` / `coverage` / `targetable` 4 each,
`commandfire` 16, `metalpershot` 8 non-zero (1000–2000 metal a shot),
`antiweapons` on 2 units. All parsed into `WeaponTdf`, none used; `metalPerShot`
in particular is a straight omission next to `energyPerShot`, which *is*
implemented. This is a whole game-layer, so effort is real (~3–4 days), but it
is the difference between a skirmish that can end and one that cannot.

### 12. D-gun

`candgun` on both commanders (bit 14 of `def+0x245`, `0x42CAA3`); the `MINDGUN`
weapon exists in the shipped data and RWE has a render type for it with
`// TODO: implement mindgun if anyone actually uses it`
(`src/rwe/game/GameScene_util.cpp:1205–1206`). Note the `>= 30000` armour bypass
found in §5b is exactly the D-gun's signature. ~1–2 days.

### 13. Paralyzer / EMP

3 weapons (`ArmEMP` and friends). `paralyzer` parsed at `0x42EB95` into the
`wdef+0x111` bitfield, unused in RWE. Needs a "stunned" unit state, so it
touches the sim state and the hash. ~1 day.

### 14. Kamikaze

The Roach and the Invader. `kamikaze` is bit 28 of `def+0x241` (`0x42CB18`),
`kamikazedistance` → `WORD def+0x218` (`0x42CB29`), read at `0x40335D`,
`0x4391B6`, `0x4393E6`. Small, self-contained, and two units in the base data
are useless without it. ~4 hours.

### 15. `MoveRate1` / `MoveRate2` and their COB callbacks

`moverate1` on 7 units, `moverate2` on 1; `def+0x1AE` and `+0x1B2`, read at
`0x43DA9A`/`0x43DAA9` in the movement code and again around `0x4C2394`. Play-test
round 9 already found that never calling `MoveRate1` silently killed the Atlas
exhaust, because the script starts its flame loop there. The same omission is
very likely suppressing wheel/track animations and dust on the units that set it.
Worth a couple of hours just to see what those seven units do when the callback
fires.

### 16. Trees and grass regrow — `reproduce` / `reproduceArea`

83 features set `reproduce`; both fields are parsed
(`src/rwe/io/featuretdf/io.cpp`) and copied into `FeatureDefinition`
(`src/rwe/LoadingScene.cpp:587–588`) and then read by nothing. In TA a forest
slowly grows back into cleared ground, which changes reclaim economics on long
games. Feature parsing is otherwise complete, so this is the only real gap on
that side besides `hitDensity`. ~half a day, low risk.

### 17. `hitDensity` — projectiles pass through scenery

Already on the roadmap. 559 features set it, it is parsed and copied
(`LoadingScene.cpp:585`) and never read. A shot should stop on a rock in the way.
The findings doc's §8 flags the pass-through-chance reading as unconfirmed;
confirming it in the binary is the actual task here, and the pivot is the
projectile-vs-feature collision rather than the feature TDF.

---

## Tier 3 — nice to have

18. **Screen shake.** `shakemagnitude` / `shakeduration` on 18 weapons
    (non-zero), parsed at `0x42EC3C`/`0x42EC53` into `WORD wdef+0x108` and the
    pair at `+0xCC`/`+0xD0`. Purely cosmetic but very noticeable on artillery.
    ~3 hours.
19. **`accuracy`.** 9 weapons, and the data file's own comment defines it:
    *"amount of accuracy in 64K deg that weapon is good for, 0 = 100%"* — i.e.
    a 16-bit angle error, distinct from `sprayangle` (which RWE does implement).
    Parsed at `0x42EBFE`. ~2 hours.
20. **`waterline`.** 21 units; `BYTE def+0x22C` (`0x42C23A`), read at
    `0x43D72E` and `0x43DBA9` — inside the same movement region as the aircraft
    code already decoded, so the surrounding function is half-familiar. Controls
    how deep a ship floats. ~3 hours.
21. **The remaining COB SFX types.** `Thrust`, `Wake2`, `ReverseWake1`,
    `ReverseWake2` are a bare `// TODO: support these SFX types` at
    `src/rwe/sim/cob.cpp:238–243`. Wakes matter for every ship, and the findings
    doc already establishes that the wake emitter uses `smoke 1` with a palette
    ramp of indices 97–103 — so most of the research is done. ~half a day.
22. **Small FBI flags nobody has looked at.** `canstop` (148 units), `shootme`
    (135, bit 15 of `def+0x241`), `canreclamate` (16 — RWE currently lets
    anything with `workerTime` reclaim), `upright` (31), `noshadow` (15),
    `isairbase` (4), `canload` (4), `healtime` (2, `WORD def+0x200`),
    `norestrict` (6), `digger`, `teleporter`, `immunetoparalyzer`,
    `cantbetransported` (1). Each is an hour or two. `canstop` and `shootme` are
    the two with enough coverage to matter.
23. **TA's Permanent and Circular LOS modes.** Already in `TOTALA-EXE.md` §8.
    Circular is fully understood (a `vismasks.gaf` stamp, radius
    `clamp(SightDistance/32, 5, 14)`); Permanent has not been looked at. Only
    reachable once there is a skirmish option to select them, so low urgency.
24. **`selfdestructcountdown`.** Parsed into a 3-bit field at `def+0x245` bits
    20–22 (`0x42CBC8`), so the original supports a per-unit countdown of 0–7
    seconds. No shipped unit sets it, so RWE's hardcoded 5 s is fine unless mod
    support is wanted. Recorded so nobody re-derives it.

---

## Tier 4 — probably not worth it, or deliberately different

25. **Keys the original itself ignores.** These appear all over the shipped unit
    files but **do not exist as strings anywhere in `TotalA.exe`** (checked by
    searching the binary directly, not just the extracted string table):

    | Key | Units that set it | Status |
    |---|---|---|
    | `noautofire` | 185 | not in the binary |
    | `ovradjust` | 162 | not in the binary |
    | `steeringmode` | 106 | not in the binary |
    | `badtargetcategory` (unprefixed) | 75 | only the `wpri_`/`wsec_`/`wspe_` forms exist |
    | `tedclass` | 189 | not in the binary |
    | `designation` | 185 | not in the binary |
    | `scale` | 20 | the literal exists but not in the FBI parser |
    | `altfromsealevel` | 5 | not in the binary |
    | `threed`, `unitnumber`, the German/French/Spanish/Italian name and description keys | 188–189 | not in the binary (localisation is done through `gamedata/Translate.tdf`) |

    Implementing any of these would make RWE *less* faithful, not more. Note
    RWE does parse `tedClass` and uses it for its own AI classifier — that is
    fine, it is RWE's own machinery, but it should not be mistaken for TA
    behaviour.

26. **`sortbias`** — parsed by the original into `def+0x21A` and read nowhere.
    Already recorded in the findings doc; leave dead.
27. **The five deliberate departures in `TOTALA-EXE.md` §7** — the nanolathe
    spray landing on the roof, depth-tested exhaust occlusion, the
    camera-windowed fog raster, off-map fog cells reading as the nearest on-map
    cell, and the absent `BrakeRate` nose re-aim. These are decisions, not gaps.
28. **`zbuffer`** (189 units; parsed at `0x42C5A6` into bit 7 of `def+0x241`).
    Given `sortbias` is dead and the original has no depth buffer at the world
    level at all, this is very likely dead too — worth one `grep` of the listing
    for that bit test to confirm and then record, but not worth implementing.

---

## §A. Adjacent to the work already under way

Not part of the excluded work, but the exact read sites the bomber/gunship pass
would otherwise have to rediscover:

- **`maneuverleashlength`** → `WORD def+0x214` (parsed `0x42CABF`), read at
  `0x4393A9`, `0x43B311`, `0x43B60F`. 106 units set it (640 or 1280).
- **`attackrunlength`** → `WORD def+0x216` (parsed `0x42CAD4`), read at
  `0x412403`, `0x4124AA`, `0x43991E`. Only the four bombers set it.
- **`hoverattack`** → bit 27 of `def+0x241` (parsed `0x42C823`). Only the
  Brawler and the Rapier.

Those three keys have just landed in `src/rwe/io/fbi/io.cpp` and
`src/rwe/sim/UnitDefinition.h`, so the parse side is done; the read sites above
are what says how the original *uses* them.

Also adjacent: the `>= 30000` damage cut-out found in §5b sits in the same
routine any weapon-damage change goes through, and `turret` (§7) shares code
with issue #42's double-aim problem.

---

## §B. Newly recovered field offsets

Derived by pairing keys to stores through the FBI parser (`0x42BFA1`–`0x42D13F`)
under the pipeline rule described at the top; validated against every offset
already in `docs/TOTALA-EXE.md`, and corroborated where an independent reader
exists. Not yet promoted into the findings document — do that once one or two
have been used in anger.

| FBI key | Offset | Type | Corroborating reader |
|---|---|---|---|
| `defaultmissiontype` | `def+0x230` | byte | `0x43B9D0` |
| `wpri_badTargetCategory` | `def+0x231` | ptr to bit array | `0x4070A6` |
| `wsec_badTargetCategory` | `def+0x235` | ptr to bit array | |
| `wspe_badTargetCategory` | `def+0x239` | ptr to bit array | |
| `noChaseCategory` | `def+0x23D` | ptr to bit array | `0x407084`, `0x4063D8`, `0x40B93F`, `0x40FD7E` |
| `buildcostenergy` | `def+0x186` | float | |
| `buildcostmetal` | `def+0x18A` | float | |
| `damagemodifier` | `def+0x1AA` | 16.16 fixed | `0x489BE4` |
| `moverate1` | `def+0x1AE` | dword | `0x43DA9A` |
| `moverate2` | `def+0x1B2` | dword | `0x43DAA9` |
| `energymake` / `energyuse` | `def+0x1C2` / `+0x1C6` | float | |
| `metalmake` / `extractsmetal` | `def+0x1CA` / `+0x1CE` | float | |
| `cloakcost` / `cloakcostmoving` | `def+0x1DA` / `+0x1DE` | float | `0x40181E`, `0x401810` |
| `metalstorage` | `def+0x1E6` | float | |
| `buildtime` | `def+0x1EA` | dword | |
| `weapon1` / `weapon2` / `weapon3` | `def+0x1EE` / `+0x1F2` / `+0x1F6` | ptr | |
| `maxdamage` | `def+0x1FA` | dword | |
| `workertime` | `def+0x1FE` | word | many |
| `healtime` | `def+0x200` | word | |
| `radardistance` / `sonardistance` | `def+0x204` / `+0x206` | word | |
| `mincloakdistance` | `def+0x208` | word | `0x4390D2` (default 80 set at `0x42D13F`) |
| `radardistancejam` | `def+0x20A` | word | `0x4392F8`, `0x466FFC`, `0x467610` |
| `sonardistancejam` | `def+0x20C` | word | |
| `buildangle` | `def+0x210` | word | `0x485BD6`, `0x485BE9`, `0x485E77`, `0x485F3A`, `0x4860E6` |
| `builddistance` | `def+0x212` | word | |
| `maneuverleashlength` | `def+0x214` | word | `0x4393A9`, `0x43B311`, `0x43B60F` |
| `attackrunlength` | `def+0x216` | word | `0x412403`, `0x4124AA`, `0x43991E` |
| `kamikazedistance` | `def+0x218` | word | `0x40335D`, `0x4391B6`, `0x4393E6` |
| `cruisealt` | `def+0x21C` | word | `0x40F270`, `0x40F707`, `0x40F8BD`, … |
| `explodeas` / `selfdestructas` | `def+0x220` / `+0x224` | ptr | |
| `transportsize` / `transportcapacity` | `def+0x22A` / `+0x22B` | byte | |
| `waterline` | `def+0x22C` | byte | `0x43D72E`, `0x43DBA9` |
| `makesmetal` | `def+0x22D` | byte | |
| `bmcode` | `def+0x22F` | byte | |
| **flags word A** | `def+0x241` | dword | `standingmoveorder` bits 0–1, `standingfireorder` bits 2–3, `init_cloaked` 4, `downloadable` 5, `builder` 6, `zbuffer` 7, `stealth` 8, `isairbase` 9, `istargetingupgrade` 10, `canfly` 11, `canhover` 12, `teleporter` 13, `hidedamage` 14, `shootme` 15, `armoredstate` 17, `activatewhenbuilt` 18, `floater` 19, `upright` 20, `amphibious` 21, `isfeature` 24, `noshadow` 25, `immunetoparalyzer` 26, `hoverattack` 27, `kamikaze` 28, `antiweapons` 29, `digger` 30 |
| **flags word B** | `def+0x245` | dword | `mobilestandorders` / `firestandorders` / `onoffable` occupy bits 0–2 (the pipeline makes the exact assignment among those three ambiguous — re-check before relying on it), `canstop` 3, `canattack` 4, `canguard` 5, `canpatrol` 6, `canmove` 7, `canload` 8, `canreclamate` 10, `canresurrect` 11, `cancapture` 12, `candgun` 14, `norestrict` 15, `showplayername` 17, `commander` 18, `cantbetransported` 19, `selfdestructcountdown` bits 20–22 |

Unit instance fields found along the way: `unit+0xA6` = category id (word),
`unit+0xB8` = kill count (word, zeroed at `0x485C76`), `unit+0x10E` = flags with
bit 1 = ARMORED. Weapon definition: flags bitfield at `wdef+0x111`,
`weaponvelocity` `+0x68`, `startvelocity` `+0x6C`, `weaponacceleration` `+0x70`,
`areaofeffect` `+0xD6` (word), `edgeeffectiveness` `+0xD8` (float, default 0.0),
`weapontimer` `+0xE6`, `flighttime` `+0xFA`, `minbarrelangle` `+0xFE`,
`tolerance` `+0x104`, `pitchtolerance` `+0x106`, `shakemagnitude` `+0x108`.

Useful new routine addresses:

| Address | What |
|---|---|
| `0x42BFA1`–`0x42D13F` | the FBI parser, in full |
| `0x42E484`–`0x42EFC3` | the weapon TDF parser, in full |
| `0x488C50` | category name → interned bit id |
| `0x407040`–`0x4071B0` | auto-target scan and category filter |
| `0x49ABB0` | can this weapon engage that unit |
| `0x43B1F0` | issue an attack |
| `0x489BB0` | apply damage (armour, veterancy) |
| `0x485BD6` | new unit placement, uses `buildangle` |
| `0x4B6C30` | random number in `[0, n)` |
