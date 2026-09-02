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

Ported; the findings are written up as `TOTALA-EXE.md` §8. Two things below
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
+0xFA` (`0x42E6CF`). *(Done 2026-08-31; `flighttime` is at `+0xFC`, not `+0xFA`
— `+0xFA` is `smokedelay`. See `TOTALA-EXE.md` §7 and the corrected weapon
offsets in §B below.)* The booleans are a bitfield at `wdef+0x111`: `selfprop`
`0x42E927`, `twophase` `0x42E9C1`, `vlaunch` `0x42EB20`, `guidance` `0x42E7A6`,
`tracks` `0x42E7CB`, `cruise` `0x42E9E9`, `propeller` `0x42E949`. From there,
`grep` the listing for reads of `+0x68`/`+0x6C`/`+0x70` inside the projectile
update to find the integration step.

**Effort / risk.** 3–5 days. **High risk** — this is precisely the class where a
plausible reading silently produces wrong behaviour (the `min`/`max` trap from
the aircraft profile). Use the documented method: transcribe the decoded routine
into a standalone program, feed it real weapon values, and compare tick by tick
against RWE before touching the engine.

### 5. Blast damage: edge falloff, armour, and the veterancy that TA actually has — DONE

Implemented; the whole pipeline is written up in `TOTALA-EXE.md` §6. What was
decoded corrected this entry in four places, recorded here so the corrections
are not lost:

- The falloff is **quadratic**, `(1 − d/R)² · (1 − E) + E` at `0x49A3B6`, not
  linear. Even for the default `E = 0` — which this entry called "already
  right" — RWE was doing double the damage at the half-way point.
- `R` is `areaofeffect / 2` (`0x49A149`), and a weapon with `areaofeffect <= 16`
  skips the blast loop entirely and hits one target at full strength
  (`0x49A049`). RWE already halved the radius, so that part was right.
- Veterancy has a **second, opposite** step this entry missed: the attacker's
  kill count *raises* damage dealt by 6% a tier at `0x499DAE`, before armour,
  where the victim's discount of 4% a tier comes after it. Reading only the
  defender's side would have made every veteran strictly weaker.
- `GET VETERAN_LEVEL` returns **nothing** in the original: the COB `GET`
  dispatcher at `0x480770` covers ids 1–20 and answers zero for anything else,
  so value 32 never had an implementation to recover. RWE keeps a getter, but
  answering with the engine's own tier (five kills apiece, capped at five)
  rather than the invented four-per-tier-capped-at-three scheme.

Two things found next to it and deliberately left alone: the original **declines
to credit a kill when killer and victim share a player** (`0x4869BA`–`0x4869C8`)
where RWE credits friendly fire, and a unit with more than five kills earns
**target leading** at `0x48A324`.

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

**Done — see `TOTALA-EXE.md` §9.** Every offset above held up, and the
`*standorders` guess was right: `0x41B3F6`–`0x41B44E` skips a unit that does
not name the flag when it gathers the order panel for a selection. Three things
in the paragraphs above are wrong, though. The counts of units "setting" a key
read as though the rest were left at some neutral value; the parser defaults
both standing orders to **2**, and since **every mobile unit names a
`StandingMoveOrder` of its own**, the 67 silent ones are all buildings and the
default never reaches anything that moves. The 123/117 counts are also not the
interesting number: 117 of the move orders say 1, which is what RWE already
hard-coded, so the whole of the move-order fix is four units. And the real
payoff is on the fire side, where **thirteen** units say 0 — the artillery, the
silos, the mines and the bombers.

`defaultmissiontype` turned out not to be a build-time field at all. It is
applied at `0x43B9AD` on the branch of the mission service loop taken when a
unit's mission list becomes **empty**, so it covers build time and going idle
through one path. It is left decoded but unported: `Standby` versus
`Guard_NoMove` is only the question of whether an idle unit walks off to find a
fight, and RWE has no equivalent of the sight-range search `0x43B700` that
would make the two differ.

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

**Done, and three things above are wrong** — written up as `TOTALA-EXE.md` §10.

- The `turret=0` weapons are **not hull-mounted tank guns**; there is no such
  thing in the shipped data. They are aircraft weapons, torpedoes, vertical-launch
  missiles and bombs. What the fix actually buys is a gunship that has to point
  at its target, not a tank that turns its body.
- `minbarrelangle` is a **float at `wdef+0xC8`**, not a word at `+0xFE` (that is
  `holdtime`), and it is **not a depression limit**: its only reader can never
  return a negative elevation, so every negative value in the data — which is all
  of them — is dead. Confirmed by replaying the decoded routine over 72,900
  geometries.
- `aimrate` **is not a key the original knows**. The string is absent from the
  binary; the two weapons that set it are ignored.

Implemented: the hull-heading gate for a plain `turret=0` weapon, skipping its
aim script the way the original does, and `pitchtolerance` falling back to
`tolerance`. Not implemented: the pitch half of the gate (RWE has no hull pitch),
`holdtime` (no reader found), `minbarrelangle` and `aimrate` (nothing to do).

---

## Tier 2 — high value but harder

### 8. Smoke from damaged units

Done, and this entry was wrong about almost all of it. Written up as
`TOTALA-EXE.md` §4.

Two of its premises did not survive contact with the binary. **There is no
health threshold or emission rate in the per-tick unit update**, because there
is no engine code for damage smoke at all: the trigger is the `SmokeUnit` thread
in each unit's own COB, which smokes below 66% health and sleeps
`max(health% × 50, 200)` milliseconds between puffs. And **RWE was not missing
the effect** — `emit-sfx` 257/258 already reached `emitLightSmokeFromPiece` /
`emitBlackSmokeFromPiece`, so damaged units did smoke; the `grep` above only
missed it because those functions are named after the sfx type rather than after
smoke.

What was actually wrong was the puff itself. RWE played every puff's whole
sequence at an even two ticks a frame, so each one ballooned to full size and
they all looked alike; the original holds the first frame for seven ticks, three
to five for each one after, and stops on a frame drawn at random when the puff
is born, so most die small. That is now `makeSmokePuffFrameSchedule` in
`game/Particle.h`, with `game/damagesmoke.test.cpp` covering the threshold, the
rate and the frame walk.

Still open from the same reading: smoke does not drift downwind (the wind vector
is decoded, RWE has no map wind), and neither explosions nor wreckage smoke
afterwards. Both are noted in `TOTALA-EXE.md` §20.

### 9. Radar jammers, stealth, sonar jamming — DONE

Every offset above is confirmed, and the jammer is real gameplay rather than
only a drawing: the readers at `0x466FFC` and `0x467610` are the minimap and
main-view range rings, but the same word is also read at `0x4675EC`, inside the
per-tick visibility pass at `0x467440`, where it drives a spatial visit that
**clears the radar contact flag on every unit inside the radius**. A jammer
hides units, it does not produce false contacts; it must be switched on; and it
does not jam its owner's own picture, only everybody else's. `stealth` is read
once, at `0x467881`, and keeps a unit off radar and sonar entirely.

All of it is implemented, with `src/rwe/sim/concealment.test.cpp` covering it.
The full write-up, with the loop-by-loop decode of `0x467440`, is in
`TOTALA-EXE.md` under "Jamming, stealth and cloaking".

### 10. Cloaking — DONE

The offsets are confirmed, and three things the note above had wrong or
missing:

- **`cloakable` is not an FBI key.** No such string exists in the binary.
  `0x42CA5A` derives the flag from `cloakcost > 0`, which is why RWE's parsed
  `Cloakable` never fired: nothing in the shipped data writes it.
- **`cloakcostmoving` defaults to `cloakcost`**, pushed as the default argument
  at `0x42C525`.
- **`mincloakdistance` is a proximity fuse, not a refusal to cloak.**
  `0x467690` checks every cloakable unit against `0x40B0D0` — is a live enemy
  within that distance — and on a hit stamps `unit+0xB0 = tick + 90` and
  decloaks it. The unit stays decloaked for three seconds after the enemy
  leaves. `0x4390D2` only draws the ring.

The drain is at `0x4017CB`, once per economy tick, truncated to a whole number,
all-or-nothing: if the player's energy will not cover it the unit simply does
not cloak. Implemented, with tests; see `TOTALA-EXE.md` as above.

### 11. Nukes, stockpiles and anti-nukes — *decoded in full; half ported*

All of it is now read out of the binary and written up in the findings doc under
"Stockpiled weapons and interception". What has landed in RWE:

- **`metalPerShot`**, and with it the check the original does *before* the shot
  rather than after (`0x49E3ED`): a weapon that cannot find both its
  `energypershot` and its `metalpershot` in the player's stores does not fire.
  This changed `energyPerShot` too, which RWE used to spend after the fact.
- **Stockpile accumulation.** `UnitWeapon` carries the magazine, the outstanding
  count and the progress; a round takes the weapon's `reloadtime` to build and
  costs `metalpershot` and `energypershot` on the original's truncated ramp,
  charged one tick in five, stalling without progress when the economy cannot
  pay, capped at 200. Firing spends a round and sets no reload timer.
  `src/rwe/sim/stockpile.test.cpp`.
- **`commandfire`** turned out to be implemented already — the auto-target and
  return-fire paths both skip it. Only the BLAST button was wrong (see 12).

What is left, and it is the larger half:

- **Interception.** The whole chain is decoded — `coverage` as a square around
  the launcher tested against the incoming missile's *aim point* (`0x49D120`),
  the launch that aborts without a target (`0x49DC17`), the projectile-targets-
  projectile slot at `proj+0x56`, the proximity detonation (`0x49B106`) and,
  the piece §7 was missing, the interceptor blast that detonates every
  projectile inside its `areaofeffect` (`0x49A664`). None of it is ported.
  Perhaps a day and a half now the reading is done.
- **The queue button.** `PlayerUnitCommand::ModifyStockpile` and
  `GameSimulation::modifyStockpileQueue` exist and are serialised, but no GUI
  control raises the command, so a magazine can only be filled from a test. The
  original's button shows `N +M` from `unit+0x1E` and the outstanding order
  count (`0x419A2B`). Half a day.
- **`antiweapons`** (bit 29 of `def+0x241`) has no reader anywhere. Two units
  set it and nothing appears to read it; interception keys entirely off the
  weapon flags. Recorded as not understood.
- **The reload-time formula** at `0x49E468`, which scales `reloadtime` by both
  the firer's veterancy and its damage. Decoded, not ported, and it belongs with
  whatever picks up the rest of veterancy.

### 12. D-gun — *done, and smaller than it looked*

The D-gun is an ordinary weapon. `ARM_DISINTEGRATOR` is `commandfire=1`,
`energypershot=400`, `[DAMAGE] default=30000` — and 30000 is exactly the armour
cut-out at `0x489BD1`, which RWE already had. So the parts that were actually
missing were both small and both now in:

- **`candgun`** (bit 14 of `def+0x245`, `0x42CAA3` — verified) is parsed and
  gates the BLAST button. RWE was offering BLAST to any unit with a command-fire
  weapon, which would have put it on a nuclear silo.
- **The energy price is now enforced** by the pre-fire check described in 11, so
  a commander with a flat battery cannot D-gun. `src/rwe/sim/dgun.test.cpp`.

Two things the old entry got wrong and that need no further work:

- The `ProjectileRenderTypeMindgun` TODO is **not** the D-gun. That is
  `rendertype=2`, which belongs to `MINDGUN`, a 100-damage `unitsonly` beam
  nothing in the shipped data uses. The D-gun is `rendertype=3`, the ordinary
  model type, which RWE has handled all along.
- **Nothing suppresses the wreck.** `0x4864B0` takes the corpse level from the
  victim's own COB `Killed` script, given a severity derived from the overkill;
  a 30000-point hit pins that at 100 and the script leaves nothing of its own
  accord. What is still unported is the severity formula itself — see the
  findings doc.

### 13. Paralyzer / EMP — **done**

3 weapons (`ArmEMP` and friends). `paralyzer` is bit 7 of `wdef+0x111`
(`0x42EB95`) and turns every hit into damage **type 2** (`0x499E20`), which the
damage handler branches away at `0x489DEB` and which therefore **never reaches
the hit-point subtraction at `0x489EB1`**: a paralyzer does no damage at all.
The `[DAMAGE]` number is the stun length in ticks, exactly as the game data's
own comment in `WEAPONS.TDF` says, which is what makes the EMP missile's 1800
against every CORE unit a minute of paralysis rather than an instant kill.
Repeat hits **add** to the remaining time (`0x489E69`) and the total is clamped
to 1800 ticks (`0x402D33`); `immunetoparalyzer` is bit 26 of `def+0x241`
(`0x42C7FA`) and both commanders have it. Implemented: `WeaponDefinition::paralyzer`,
`UnitDefinition::immuneToParalyzer`, `UnitState::paralyzedUntil` (hashed and
dumped), `src/rwe/sim/paralyzer.test.cpp`. See `TOTALA-EXE.md` §NN.

### 14. Kamikaze — **done**

The Roach and the Invader. `kamikaze` is bit 28 of `def+0x241` (`0x42CB18`),
`kamikazedistance` → `WORD def+0x218` (`0x42CB29`). Both offsets confirmed. The
two readers at `0x4391B6` and `0x4393E6` turned out to be **cosmetic** — the
selected-unit range ring and a debug circle labelled with the key's own name.
The mechanic is an **order**, not a proximity fuse: a kamikaze unit's attack
command becomes the `ATTACK_KAMIKAZE` mission (`0x43F38A`), whose handler
(`0x403336`) closes to `max(kamikazedistance, 16)` and then issues the ordinary
`SELFDESTRUCT` order (`0x4032E4`), so the blast is `SelfDestructAs`. The player
has to order it. Implemented in `UnitBehaviorService::kamikazeRun`, with
`src/rwe/sim/kamikaze.test.cpp`. Still not ported: the original also lets a
kamikaze chase off its own bat (`0x407025`, `0x40B901`), which RWE cannot do
because its auto-targeting needs a weapon to pick a target with.

### 15. `MoveRate1` / `MoveRate2` and their COB callbacks — **done**

`moverate1` on 7 units, `moverate2` on 1; `def+0x1AE` and `+0x1B2`, read by the
band machine at `0x43DA70`. The thing that had been missed is the **default**:
both keys default to `MaxVelocity * 2` (`0x42C1E6`, `0x42C206`), a speed nothing
can reach, so *every* moving unit is in band 1 and calls `MoveRate1` — which is
why the Atlas, which names no threshold at all, starts its exhaust there. Only
five of the two hundred shipped scripts define any `MoveRate` function: the
Atlas and the Valkyrie define all three and use them to drive their thruster
flames, and the Fighter, the Hawk and the Vamp define only `MoveRate2`, whose
body in all three is a one-in-ten **barrel roll**. So the play-visible effect is
thruster flames and the occasional fighter roll — no wheel or track animation
hangs off these anywhere in the shipped data. Implemented as
`UnitBehaviorService::updateMoveRateBand` with `UnitState::moveRateBand`
(hashed and dumped); `src/rwe/sim/moverate.test.cpp`.

### 16. Trees and grass regrow — `reproduce` / `reproduceArea` — **done, but inert on stock data**

Both are bytes at `feat+0xFC` / `feat+0xFD` (`0x422A13`, `0x422A27`) and both
are read by the sweep at the tail of `0x424050`. The rule: one map square is
examined per tick, walking the grid backwards and wrapping at the bottom, so a
square gets one chance per full sweep; `reproduce` is a **percentage** rolled
against `rand(100)`, not a flag; the seed lands at a uniform offset of
`rand(area) - area/2` on each axis; the destination square's feature slot must
be exactly empty and the source square must carry no unit.

**The premise of this entry was wrong.** Every feature in every extract sets
`reproduce=0` — all 83 that name the key — so in stock Total Annihilation a
forest never grows back and there are no reclaim economics to change. The
mechanism is implemented anyway
(`GameSimulation::updateFeatureRegrowth`, cursor hashed and dumped;
`src/rwe/sim/regrowth.test.cpp`) so a mod can use it, and
`FeatureDefinition::reproduce` is now an `unsigned int`.

### 17. `hitDensity` — **refuted; nothing to implement**

The guess was wrong, and not merely unconfirmed. **The string `hitdensity` does
not occur anywhere in `TotalA.exe`** — nor in `TAE.EXE` or the shipped DLLs —
and the feature TDF parser pushes every key it reads as a literal, so the key
has no reader in this build at all. The projectile-versus-feature collision at
`0x49B2B3` is purely geometric: same map square, and the shot below
`squareGroundHeight + featureDefinition.height`. There is no roll and no
density. RWE already does exactly this, so **no code was changed**; only
`src/rwe/sim/hitdensity.test.cpp`, which fires the same shot at the same rock at
each of the four densities the shipped data uses and requires it to stop every
time, so nobody implements the guess later. `TOTALA-EXE.md` §21's note should be
read together with §NN.

---

## Tier 3 — nice to have

18. **Screen shake — done.** `shakemagnitude` is `DWORD wdef+0xCC` and
    `shakeduration` `DWORD wdef+0xD0` (seconds × 30 on the way in), both stored
    from `0x42EC5A`/`0x42EC70`. The `WORD wdef+0x108` in the note above was
    wrong; that slot is `pitchtolerance`. Nothing reads the weapon struct's
    copy directly — the way in is the `NoShake` console command at `0x502444`,
    whose handler `0x416E60` toggles a bit tested only at `0x41C5E6` and
    `0x41C646`, which are the shake. Accumulated from the one call site
    `0x499FBA` in the detonation routine; consumed per frame at `0x41C6F0`
    with a linear ramp-down and **no falloff with distance at all**. Every
    shipped weapon that sets the keys is an `explodeas`/`selfdestructas`, not
    a gun, so the note above was also wrong to call it "noticeable on
    artillery" — it fires on big deaths and nukes. Implemented render-side;
    see `TOTALA-EXE.md`'s "Where a shell actually lands" section.
19. **`accuracy` — done, and it was the answer to a player's question.**
    `WORD wdef+0x104`, parsed `0x42EBFE`, stored `0x42EC19`, and read in
    exactly one place, `0x49D6D7`, between solving the aim and spawning the
    projectile — so it reaches every kind of weapon, not just ballistic ones.
    The cone is the weapon's `accuracy` widened by up to an eighth of a turn as
    the shooter loses health and narrowed by its kill count over three, and
    heading and pitch are drawn independently. Deciding this rather than the
    ballistic solver was the cause of long-range misses meant decoding the
    solver too (`0x49A890`): it is the same quadratic RWE already had, in
    double, and the two agree to one part in 65536. Implemented.
20. **`waterline` — done, but not where it looked.** `BYTE def+0x22C`, read
    with the integer reader at `0x42C24A` and stored `0x42C259`. Of its two
    readers only `0x43DBA9` matters: `0x43D72E` is gated on the FBI's
    `Floater` key, and the twenty-one units that set `waterline` and the
    eighteen that set `Floater` **do not overlap at all**, so that clamp always
    reduces to what RWE already did. The live reader is the routine that calls
    the COB entry point `setSFXoccupy` with a 0–4 water state, which is what a
    ship's script waits on before laying a wake. RWE's version of that now
    follows the original's cascade. State 3 needs a model height RWE does not
    carry and is left out.
21. **The remaining COB SFX types — done.** All five are routed now. The four
    wakes turned out to be one routine (`0x472430`) with two knobs: which of
    the emitting piece's two vertices comes first, which is the whole of the
    difference between `Wake` and `ReverseWake`, and a ramp period of 16 or 8,
    which is the whole of the difference between 1 and 2. `Thrust` is the
    `Vtol` emitter with 6 changed to 7. The premise in the note above was
    half wrong: the ramp of indices 97–103 is right, but the emitter does
    **not** draw `smoke 1` — it fills a one-pixel rectangle with a palette
    index, and the sequence handle it stores is never read. §4 is corrected in
    the findings doc.
22. **Small FBI flags — decoded; the ones with an effect are done.** Counts
    below are base OTA only (163 units), no Core Contingency or Battle
    Tactics.

    - **`canreclamate`** (bit 10, 16 units) — **done, and it was a real gap.**
      `CanReclaimTarget` `0x489960` tests it and never reads `workertime`; the
      parser mirrors it into bit 9, which `CanRepair` `0x4899CC` tests, so one
      key gates reclaiming *and* repairing. 21 units had a worker time without
      the bit — every factory, both air repair pads, both carriers, CORSOLAR —
      and all of them could reclaim and repair in RWE. Gated now, along with
      the target-side rule that a unit with `cancapture` (the Commanders)
      cannot be reclaimed.
    - **`upright`** (bit 20, 21 units) — decoded, not implemented. One read
      site, `0x48A8BF`, in the per-tick ground placement `0x48A870`. Set means
      "stay vertical, take height from one sample under the centre"; clear
      means `0x48A490`, which samples four rotated footprint corners and sets
      `WORD unit+0x68` pitch and `WORD unit+0x64` roll from the slope. Kbots
      and submarines set it; ARMFIDO explicitly clears it. Worth doing when
      somebody takes on terrain conforming, which RWE does not do at all yet.
    - **`healtime`** (`WORD def+0x200`, 2 units) — **done.** Single reader
      `0x48AF3D`: every 8th tick, if not at full health, heal
      `(healtime * 8) / 30` HP, integer-truncated. The Commanders' 27 works
      out at 26.25 HP/s. Implemented free, where the original charges it to
      the owner's stores, because nothing else RWE repairs costs anything.
    - **`isairbase`** (bit 9, 4 units) — decoded, not implemented. Cached onto
      the instance as `unit+0x110` bit 30 at `0x485AE7`. Three effects: a unit
      held by an air base stays selectable and orderable where one held by
      anything else is unlinked from the world (`0x48AD19` → `0x4384A0`, and
      the predicate repeats at some twenty sites); `builder && isairbase`
      makes the unit an aircraft-repair host and queues a `"SELFREPAIR"` order
      at `0x411ECE`; and it selects the `VTOL_LANDING` cursor at `0x43EA9A`.
    - **`noshadow`** (bit 25, 10 units) and **`digger`** (bit 30) — both are
      shadow-pass flags. `noshadow` skips the shadow at `0x4592A6`/`0x4594BA`;
      `digger` is **not** a terrain flag at all, it selects a second shadow
      path at `0x4594D0` and a projection constant of 125 instead of 50. No
      base-OTA unit sets `digger`.
    - **`norestrict`** (bit 15, 6 units) — **UI only.** All four read sites
      (`0x44C15F`, `0x44C4EA`, `0x44C73A`, `0x44CA59`) are the Unit
      Restrictions screen, which skips defs with the bit so a host cannot
      switch them off. The 6 are the two Commanders and the map props.
    - **`cantbetransported`** (bit 19) — decoded. Read at `0x489AA3` in
      `CanTransport`, which also wants the transport's `canload` (bit 8),
      counts cargo through `transport+0x8A` against `BYTE def+0x22B`, and
      requires `WORD candidatedef+0x14A <= BYTE transportdef+0x22A`. No
      base-OTA unit sets it.

    Also resolved along the way: the `mobilestandorders`/`firestandorders`/
    `onoffable` ambiguity §B flags is `mobilestandorders` bit 0 (`0x42C8DB`),
    `firestandorders` bit 1 (`0x42C8FF`), `onoffable` bit 2 (`0x42C8BE`). And
    the unit definition stride is `0x249`, the instance stride `0x118`.
23. **TA's Permanent and Circular LOS modes.** Already in `TOTALA-EXE.md` §21.
    Circular is fully understood (a `vismasks.gaf` stamp, radius
    `clamp(SightDistance/32, 5, 14)`); Permanent has not been looked at. Only
    reachable once there is a skirmish option to select them, so low urgency.
24. **`selfdestructcountdown` — checked; nothing to change.** The parser's
    *absent* case at `0x42CC07` sets the field to **5**, so five seconds is
    the original's own default and RWE's hardcoded five is already right.
    An explicit `0` is not "use the default" — `0x402053` detonates at once
    with no announcement. The reader `0x402010` counts one step a second
    (`0x4020F6`), announces five down to zero from the table at `0x5086D8`,
    then waits `rand(0..14)` ticks before dealing 30000 damage — the same
    armour-bypass threshold as the D-gun. What RWE lacks is the
    announcements, the random slop and the explicit-zero case, not the
    timing. Recorded in the findings doc rather than implemented.

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
27. **The deliberate departures in `TOTALA-EXE.md` §20** — the nanolathe
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

Also adjacent: the `>= 30000` damage cut-out found in §5 sits in the same
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

**Correction (2026-08-31, from the §4 work).** The last five of those are each
one key early: the pipeline rule was applied a slot short from `smokedelay`
onwards. `flighttime` is `+0xFC` and `+0xFA` is `smokedelay`; `minbarrelangle` is
a **float** at `+0xC8` and `+0xFE` is `holdtime`; `+0x104` is `accuracy`,
`+0x106` is `tolerance`, `+0x108` is `pitchtolerance`; `shakemagnitude` is a
dword at `+0xCC` and `shakeduration` at `+0xD0`. Everything before them in the
line is right. The full corrected weapon layout, with the flag bit numbers, is
in `TOTALA-EXE.md` §19.

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
