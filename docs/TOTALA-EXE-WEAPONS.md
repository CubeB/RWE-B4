# What the original executable does: weapons, damage, targeting and flight

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §6, §7, §9, §10, §11, §12, §14, §21, §22, §85, §90, §92, §116.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

6. [The damage pipeline](#6-the-damage-pipeline)
7. [Missile and projectile flight](#7-missile-and-projectile-flight)
9. [Target selection, and what the firing modes really do](#9-target-selection-and-what-the-firing-modes-really-do)
10. [Weapon target eligibility, `0x49ABB0` in full](#10-weapon-target-eligibility-0x49abb0-in-full)
11. [`turret`, and what a hull-mounted gun waits for](#11-turret-and-what-a-hull-mounted-gun-waits-for)
12. [Where a shell actually lands, screen shake, and waterline](#12-where-a-shell-actually-lands-screen-shake-and-waterline)
14. [Recoil](#14-recoil)
21. [Stockpiled weapons and interception](#21-stockpiled-weapons-and-interception)
22. [The D-gun](#22-the-d-gun)
85. [The D-gun: `ATTACKSPECIAL`, and what `commandfire` really costs you](#85-the-d-gun-attackspecial-and-what-commandfire-really-costs-you)
90. [`AirToAir`: a pursuit, and the twenty units that are not a hop](#90-airtoair-a-pursuit-and-the-twenty-units-that-are-not-a-hop)
92. [The D-gun's projectile: `noexplode`, the full-range flight, and who gets hurt](#92-the-d-guns-projectile-noexplode-the-full-range-flight-and-who-gets-hurt)
116. [What a round aims at on a unit, and why the water comes after it](#116-what-a-round-aims-at-on-a-unit-and-why-the-water-comes-after-it)

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
last-damager pointer at `unit+0xF0` which `0x489DBA` records.

**Two tests stand in front of that increment, and RWE used to fail both.**
Read out on 2026-09-15 and now ported:

```
4869a3  test ecx,ecx ; je                ; no known last damager -> nothing
4869a7  fld [esi+0x104] ; fcomp 0.0      ; the victim must be FINISHED
4869ba  dl = [esi+0xff]                  ; the victim's own owner
4869c0  al = [esi+0xf4]                  ; the player recorded with the damager
4869c6  cmp dl,al ; je                   ; the same player -> nothing
4869ca  inc WORD [ecx+0xb8]              ; otherwise, one kill
```

So **friendly fire earns no veterancy at all** -- shooting your own units is not
a way to farm the damage bonus above or the reload bonus of §21 -- and neither
does flattening a half-built nanoframe, whose build progress is not yet zero.
`unit+0xFF` is the owner byte, the same one §8 checks a projectile's owner
against. Tests in `sim/damage.test.cpp`.

### The player-level counters, and the death-cause dispatch that gates them

Read out on 2026-09-15 (issue #51), and the answer changes what the end-of-game
chart shows. `0x4647FB` zeroes four words together at `player+0xFC`, `+0xFE`,
`+0x104` and `+0x106`; `0x466215` reloads the first two from the lobby under the
literal names `"Kills"` and `"Losses"` (`0x502BB8`, `0x502BB0`), which is what
fixes their meaning, and §104's chart reads those two. The other two go with the
"Commanders Killed" / "Commanders Lost" strings, and nothing in the shipped
interface displays them.

Their increments hang off a jump table. `0x48687C` takes the cause nibble as
`[pkt+0xA] >> 4`, **rejects anything outside 1 to 6 outright**, and dispatches
through `0x486E64`:

| Cause | Target | What it does |
|---|---|---|
| 1 weapon | `0x4868B3` | the counters, with no question about who fired |
| 2 paralyser | `0x486A98` | nothing at all |
| 3 self-destruct | `0x4869F5` | its own copy, behind an ally-matrix test |
| 4 owner change | `0x486A98` | nothing at all |
| 5 reclaim | `0x486899` | the same as 1, but only from another player |
| 6 carrier died | `0x4868B3` | the same as 1 |

The player record is `[globals+0x1B63]` with a stride of **331 bytes**, which is
what lets the indexed increments and the pointer ones be read as the same
fields: `0x486906` writes `globals + 331·killer + 0x1C5F`, and `0x1C5F − 0x1B63`
is `0xFC`, Kills.

So, in order:

```
4868c1  inc WORD [player+0xfe]          ; the VICTIM's owner: Losses, always
4868ce  cmp cl,0xa                      ; the recorded killer, vs the neutral slot
4868d3  fld [esi+0x104] ; fcomp 0.0     ; the victim must be FINISHED
4868e6  cmp [esi+0xff],cl               ; and must not be the killer's own player
486906  inc WORD [globals+331*cl+0x1c5f]; the KILLER's Kills
```

**Losses counts every death, friendly fire and self-inflicted alike** -- the
increment has nothing standing in front of it -- while **Kills is gated by
exactly the two tests that gate veterancy above**. The only handler that asks
who did it is reclaim's: `0x486899` runs the killer-vs-owner test *before*
falling into the Losses increment, so recycling your own base costs you nothing
on the chart while having it eaten by an enemy builder does. And because the
dispatch takes only 1 to 6, a nanoframe that rotted away or was taken back by
its builder (cause 9) moves no counter at all, though one *shot* to pieces is
cause 1 and does cost its owner a loss.

The two commander columns come off a string compare at `0x48694B`: the dying
unit's name (`def+0x20`) against the entry for its owner in a table of 562-byte
records at `globals+0x37F5F`. On a match, the killer's `+0x104` and the victim's
`+0x106` both go up.

All of this is ported. `GameSimulation::killUnit` gates `unitsKilled` with the
veterancy pair, `reclaimUnit` charges a loss only to a victim of somebody else,
and `removeUnfinishedUnit` is the cause-9 removal that charges nobody anything.
`sim/damage.test.cpp` and `sim/reclaim.test.cpp`. The neutral slot `0xA` has no
RWE equivalent and is the one test not reproduced.

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

**Both comparisons are unsigned**, and that is load-bearing rather than
incidental: `jae` and `jbe`, not `jge` and `jle`. A weapon whose
`weaponvelocity` is **negative** — fourteen blocks in the Escalation data write
one — therefore has a cap near 2^32 after the conversion, so the clamp can never
fire. Its entire function is to *disable the ceiling*, which is what lets the
negative `weaponacceleration` those same blocks carry decelerate the round from
a large positive `startvelocity` for as long as the motor burns. `VSPAM_ALL` is
the pattern: 480 off the rail, losing 0.083 a tick. A decelerating missile has
no other spelling in this format. (`BOMB_MS` and `BOMB_SHOCK` are the degenerate
case — `startvelocity` negative and equal to the cap, so the first `jae` is
taken and the speed never changes, leaving a round that flies backward along its
nose, which for a vertical launch is straight down.) None of the fourteen is
ported or scored; `docs/TA-DEMOS.md`, "Fourteen weapon blocks declare a negative
`weaponvelocity`", has what it cost the miner before the guard.

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

**This section is now checked against real games.** Replaying exactly the above
-- the launch speed picked the way `0x49C980` picks it, the cap and the
acceleration as converted here, the burn as `0x49C920` times it, and coasting
after -- reproduces the observed flight time in **13 of 13** (shooter, weapon)
cells mined from a demo corpus once the round is stopped on the victim's
footprint (below), against **0 of 27** for a model that flies a missile at its
`weaponvelocity` from the muzzle to its aim point. All thirteen are checked in
as conformance episodes in `src/rwe/sim/tad_weapon_episodes.h`. Two details of
this reading are what the corpus is agreeing with rather than incidental: a
`startvelocity` of zero meaning full speed with no motor and a standstill with
one, and the burn being range-derived unless `noautorange`. The burn is thinly
covered: one checked-in episode outlives its own motor and coasts the last two
ticks in, and the rest arrive before theirs stops, so that half is confirmed as
arithmetic more than as an outcome. See `docs/TA-DEMOS.md`,
"Pairing a `0x0d` to the `0x0b` it caused".

### Where a round stops, `0x49B090`

Called at `0x49BD88`, after every kind's move. It takes the map square the round
now stands in (`0x4815A0`) and tests, in order: the square's first unit slot
(`WORD sq+0x0`) -- a unit not owned by the round's owner, with the round below
`unit+0x6E + def+0x16E`, the top of its model; the second slot (`WORD sq+0x2`),
the same with a floor at `def+0x162`; the feature (§24); then the ground; then the sea (§116, which has the
rest of the routine). Any hit
goes to `0x499EB0`. A unit fills the slots of the squares its footprint covers,
so a round detonates **on the victim's footprint, about half a footprint short
of the point it was aimed at**, and not at that point. Against the demo corpus
that stop takes 792 of 810 constant-speed pairings on still victims where the
aim point took 65%, flat across footprints 2 to 8. RWE's
`checkProjectileCollision` is already this shape -- move, then the occupied
grid, then the model-height test -- and `weaponflight.test.cpp` now drives it
against a victim with the episode's own footprint.

### Which tick a new round first moves on

**The tick it is fired.** The order in one sim step (§111, `0x4954BD`) is: game
tick incremented, unit pass `0x48AD30`, projectile pass `0x49B720`
(`0x495513`), feature pass, per-player settle. The fire routine runs inside the
unit pass, reached from the per-tick weapon update `0x49E1A0`, and at
`0x49D77E` it calls `0x49C9C0`, which appends the round to the flat projectile
array -- count at `globals+0x141F3`, base at `+0x141F7`, stride 107 -- and
increments the count before returning. It then builds the 36-byte `0x0d` and
queues it (`0x49D859`).

The projectile pass reads that count **once** into its trip counter
(`0x49B728`, stored at `0x49B740`) and counts it down (`0x49BE41`) without
re-reading it. The round created a moment earlier is inside the count, so it is
walked: it moves (`0x49BD41`) and `0x49B090` tests the square it now stands in
straight afterwards (`0x49BD88`). A hit goes `0x499EB0` → `0x499CD0` →
`0x489BB0`, synchronously, which is where the damage and its `0x0b` happen.

So a round fired on tick T detonates on **`T + k - 1`** for `k` steps to the
footprint, and there is no creation-tick guard anywhere in that chain. The only
per-round time test on the way in is the **burst** gate at `0x49B790`, and a
burst is not a deferral for an ordinary weapon: `0x49CB79` copies the new
round's `proj+0x60` from `wdef+0xEA`, which the parser at `0x42E619` defaults
to zero, and a zero sends the record down the ordinary flying path. A non-zero
one makes the record a template that emits one copy per `burstrate` and never
flies itself, and those copies *are* a tick late -- `0x49B810` appends them past
the trip count the pass had already latched, which is the one place the
snapshot is observable.

The demo corpus records the interval between a shot and its damage as `k`
rather than `k - 1`, and that extra tick is the demo's clock and not the
engine's: an event is stamped with the last `0x2c` before it in its sender's
stream, the `0x2c` is queued at the end of that player's unit sub-pass
(`0x48B003`), and so a `0x0d` is stamped a tick early while its `0x0b` is
stamped true. `docs/TA-DEMOS.md`, "Which tick a round first moves on", has the
stream measurement that confirms it and the Escalation patch check for every
routine named here. RWE's own order is the same -- `spawnProjectile` emplaces
during the behaviour pass and `updateProjectiles` walks the new round in the
same tick, applying damage inline on the collision -- and
`src/rwe/sim/weaponfiretick.test.cpp` pins it end to end through `tick()`.

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

**This routine is reached from one place only: the guidance step of the motor
update**, the `twoPhase ? secondPhase : guidance` test at `0x49BA44`. So every
behaviour in it — the cruise clause included — is unreachable for a round that
cannot steer, meaning one with neither `guidance` nor `twophase`, or with a
`turnrate` that truncates to nothing per tick. Such a round flies wherever it
was pointed at launch whatever flags it carries. That is not a curiosity:
`ROCKET_HRK` is exactly it, and reading `cruise=1` as a shape of flight rather
than as a clause of the aim point is what had its demo cells excluded from the
weapon oracle for nothing. `docs/TA-DEMOS.md`, "And `cruise` has left the table
altogether".

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
of gravity. **Ported** — `computeWindVector`, and the `currentWindVector` the
simulation now retains.

The vector is built once at `0x490CA4`–`0x490D35`: the speed is
`minwindspeed + rand(maxwindspeed − minwindspeed)` straight out of the OTA, the
direction is `rand(0x10000)`, and the two components stored are
`−2·sin(dir)·speed` in X and `−2·cos(dir)·speed` in Z, in the same 16.16 units
as a position.

That is the opposite way round from what this section said until 2026-09-17,
and the table settles it. X comes from the routine at `0x4b70ef` and Z from
`0x4b7123`; the two are identical but for the index, which the second advances
by `0x4000` — a quarter turn. Both read the table at `0x509f00`, whose first
entry is `0` and which climbs to a peak of `0x2000`, so it is a **sine** table
of amplitude 8192. The first routine is therefore sin and the second cos, not
the reverse. (Each multiplies by the speed and then does `shrd …, 0xd`, a `>>13`
that exactly cancels the 8192, so a routine returns `trig(dir)·speed`.) The
error was invisible in play — against a uniformly random direction the two
conventions produce the same distribution — which is why it survived the first
reading, and why the test that pins it asserts fixed cardinal directions rather
than sampling.

There is no Y term: the word at `globals+0x37ED0` is never written anywhere in
the binary, so the wind is strictly horizontal.

At Brain Coral's `maxwindspeed` of 3000 this is 0.092 world units a tick, which
carries a Crusader shell about six units over its flight against a damage radius
of 24 — a real nudge rather than a dominant force. Note that nothing compensates
for it: the original's bombsight does not model the wind either, so a bomber's
aim drifts very slightly downwind, and RWE's release, taken from the mission's
own fall-time arithmetic (#110), ignores it too.

Smoke uses the same two words, scaled by 8 per tick (§4), and that is still not
ported — every puff in the original leans downwind together. Nothing about it is
undecoded now; it wants only the particle code wired to the vector the
simulation already keeps.

---

### Decoded but not ported

- **Smoke drifting with the wind** (§4 — the same two words the projectiles
  use, scaled by 8 a tick) is decoded but not ported. The wind on the
  projectiles themselves, which used to head this list, is ported now.
- **Escalation changes what an expired ballistic round does.** The GOG binary
  tests `burnblow` (bit 23) at `0x49BC67` -- `shr eax,0x17` then `je` past the
  detonation -- and Escalation patches the two bytes to `shr eax,0x1b` and
  `jne`, so it tests `noautorange` (bit 27) and in the opposite sense. The
  selfprop equivalent at `0x49BAC3` is unpatched and so is everything else in
  `0x49B720`. Nothing scored today goes near it, but the ballistic oracle is
  the next one to be written and it would be scored against Escalation demos,
  so it has to use Escalation's rule and not this one.
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

Every reader of those two bits in the binary, for the record (issue #109):

| Site | What it is | What the mode does there |
|---|---|---|
| `0x43B211`, `0x43B25D` | `0x43B1F0(unit, target, forced)`, the sighting-to-attack routine; nine callers (`0x4033EB`, `0x40600F`, `0x4063F5`, `0x4070DD`, `0x40F999`, `0x40FD9B`, `0x410608`, `0x41072A`, `0x411079`), Standby, the patrols, guard and the return-fire path among them, and every one passes `forced = 0` | 0: no mission at all; 1: a move back to `unit+0x6A` planted before the attack; 2: the attack alone. The same test right after it (`0x43B224`) refuses when the fire mode is 0, Hold Fire |
| `0x43B431`, `0x43B563` | `0x43B400`, the repair patrols' job issuer (called from `RepairPatrol` at `0x405ACA` and `VTOL_RepairPatrol` at `0x41554E`) | 0 **and** 1 plant the move back to where the unit stood before the job; 2 does not |
| `0x46A9CD` | the unit-info string builder, format `0x507914` | displayed |
| `0x4807AA` | the COB `get` handler `0x480770`, one of ids 1-20 | returned to the script (`0x4807BD` beside it returns the fire mode) |
| `0x487CA9` | the `InitialMission` interpreter's `o` letter (§105) | read back as the two standing orders a mission file wrote |
| `0x41B435` | the order panel's accumulator (§19) | shown on MOVEORD |

> **RWE, 2026-09-24 (#109):** the patrol break-off and the idle aircraft's
> sighting both read the unit's own `moveOrders` now, refuse on Hold
> Position and plant the return move on Maneuver; the patrol used to read
> the definition's standing order, so the MOVEORD button changed nothing,
> and the idle aircraft ignored the mode altogether. Still RWE's own: an idle
> **ground** unit never goes after a sighting at all, where the original's
> Standby hands it to `0x43B1F0` like the patrols do, and the repair patrols'
> return move (`0x43B400`) is not planted.

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

The four names in the data resolve to:

| Name | Handler | What it is |
|---|---|---|
| `Standby` | `0x405FE0` | Call `0x43B700` (the sight-range search) and hand any sighting to `0x43B1F0`; on a miss, clear the weapons' targets and sleep `rand(30)+30` ticks |
| `Guard_NoMove` | `0x4021F0` | Free all three weapons (`0x489800(unit, 3)`), sleep 30 ticks, and never search |
| `VTOL_Standby` | `0x40F7D0` | The aircraft equivalent, record 0 of the VTOL table |
| `Standby_Mine` | `0x406090` | `Standby` for a mine: on a sighting standing on the ground, push `SELFDESTRUCT` instead of attacking |

Across the effective unit set (`rev31.gp3` over `btdata.ccx` and `ccdata.ccx`
over `totala1.hpi`), 117 units name `Standby` (every mobile ground and sea
unit), 30 `VTOL_Standby` (every aircraft), 25 `Guard_NoMove` (the armed
towers), 12 `Standby_Mine` (Core Contingency's `ARMMINE1`-`6` and
`CORMINE1`-`6`) and 88 nothing (factories, economy, silos). The counts above
this table were taken over the base game before the Core Contingency files
were read, which is why they said three names.

**`Standby_Mine`, `0x406090`,** is the record at `0x4FC701`, row 1 of the
table at `0x4FC6E8` (see `TOTALA-EXE-MISSIONS.md` §1). It differs from
`Standby` in two places and is otherwise the same code:

```
state 0 (installed)
406155  test [unit+0x110], 0x20000000   ; bit 29: set at 0x485A81 when bmcode is 0
406161  jne ... else return 7           ; a mobile unit ends the mission at once
40616B  0x489800(unit, 3)               ; free the weapons, as Standby does
40617D  sleep 1 tick, go to state 1
state 1
4060B6  t = 0x43B700(unit)              ; the sight-range search: nothing unless Fire At Will
4060BF  [t+0x110] & 3 == 1 ?            ; the pick is standing on the ground (2 is airborne)
4060CC  [unit+0x110] & 0x300000 != 0 ?  ; not on Hold Fire (already true after 0x43B700)
4060D8  new mission FromName("SELFDESTRUCT" 0x501520), push it, return 5
40612A  else sleep rand(30)+30, stay in state 1
```

Where `Standby` hands a sighting to `0x43B1F0` for an attack, the mine blows
itself up. The chooser behind `0x43B700` is the ordinary one over the unit's
`SightDistance` (55 to 100 for the mines), and a kamikaze unit skips
`0x49ABB0`, so a mine, which has no weapon, still has something to choose
with. `SELFDESTRUCT` then counts `selfdestructcountdown` seconds -- 1 for
mines one to five, 2 for the sixth -- and a mine on Hold Fire never gets that
far: `ARMMINE6` and `CORMINE6` start there (`StandingFireOrder=0`, with the
button) and sit quiet until the player arms them.

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

### Correction: an attack order does not follow a target through the fog

Sections 9 and 10 are about *choosing* a target, and that half is fog-correct:
the acquisition scan asks `0x465AC0` -- RWE's `canSeeUnit` -- so a unit will not
pick something it cannot see. What neither section says, because it was never
asked, is what becomes of a target **already** being attacked when it goes out
of sight.

Tested against the running game: the order **stays on the last position the
attacker's owner actually saw**. The attacker walks to that spot rather than to
wherever the target has gone, and when the ground is visible again the order
picks the target up where it now is -- moved, if it moved while it was dark.

RWE followed the live position the whole time, which is omniscient: an ordered
attacker walked to where its quarry really was rather than to where it was last
seen. The last seen position now rides on the order itself, beside the leash
anchor the original already keeps there (`0x43B330`), and is refreshed only
while `canSeeUnit` is true.

Deliberately `canSeeUnit` and not `canDetectUnit`: a radar contact is a blip and
not a target, and the radar picture is recomputed for one player a tick
(section 18), so it could not feed a deterministic decision even if the original
wanted it to.

The routine in the exe that does this has not been found, so this is behaviour
rather than transcription -- the same standing as the placement gate corrected
in section 27, and the second time a static reading here said "no fog
involvement" where the running game disagrees. A target that was never seen at
all keeps the live position rather than being lost, which is the conservative
half: in play an attack order is issued by clicking something visible, so the
remembered position is set on the first tick.

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

The list builder also asks who the candidate belongs to before it asks
whether it can be seen: `0x40AB05` reads the searcher's ally byte for the
candidate's player (`player+0x108+idx`, the same table §103's click ladder
consults) and skips the unit when it is set, so an ally's units are never
candidates at all. RWE ported that on 2026-09-24 (#237): `chooseTarget`
rejects a candidate whose owner `arePlayersAllied` with the searcher's.

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

None of the three conditions clears the slot by itself. Each falls through to
the same re-pick as an empty slot does, and what the pick returns decides:

```
408b76  call 0x40b7b0(unit, slot, 1)
408b7e  je   408b89
408b82  call 0x48a060(unit, target, slot)   ; write the pick, which may be the same unit
408b8a  call 0x48a0f0(unit, slot)           ; nothing came back: clear the slot
```

`0x48A0F0` writes the empty marker `{0, 0x8000}` and, unless the slot was
already empty, runs the unit's `TargetCleared` script (string `0x508D58`).
The pick's candidates are can-see filtered (§17a), so **a held target in the
slot's bad-target set is lost as soon as it is in fog with nothing else
visible**: it cannot come back out of the bad bucket, the pick is empty, and
the slot is cleared. A held target that trips none of the three conditions
never reaches the pick, which is why fog alone does not cost a target. The ARM
Skeeter is where this shows in play: its secondary missile names
`wsec_badTargetCategory=NOTAIR` and reaches 604 on 280 of sight, so a tower it
is shooting can go into fog it cannot see into, and the missile falls silent.

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
does nothing at all in a local game. **Issue #42 is not explained by this** —
what does explain it is the next subsection.

### One aim per shot

Issue #42 was real, and it was RWE's. A commander attacking the ground ran
`AimPrimary` 151 times for 12 shots; the original runs it once a shot. What
holds it to that is the slot's flag byte at `slot+0x1B` and the script's answer
beside it at `slot+0x08`:

- **Bit 0 up means aimed, or aiming.** A turret whose bit is up goes straight
  past the aim call to the fire check (`0x49E211`–`0x49E215`). Otherwise the
  update works out a heading and pitch, stores them at `slot+0x16` and
  `slot+0x18`, zeroes the answer (`0x49E2FF`), starts the script with
  `slot+0x04` as the place to put its answer (`0x49E31C`), and raises the bit
  (`0x49E3AB`).
- **Nothing is looked at while the reload runs.** The fire check opens by
  testing the reload counter at `slot+0x14` and leaves if it is not zero
  (`0x49E3AE`), so an aimed gun is not rechecked or re-aimed until the reload is
  done.
- **The turret handler waits on the answer.** With the bit down or the answer
  still zero, `0x49D580` returns 0 at `0x49D86D` and changes nothing: the script
  is still turning, or it said no.
- **Three things in the handler lower the bit.** The eligibility test failing
  (`0x49D65B`, which also raises bit 12 of the unit's event word); the fresh
  angles missing the stored ones by more than tolerance (`0x49D68A` — the drift
  check, which is what stops a ballistic shot going off on a stale aim); and the
  round being spawned (`0x49D78B`, which zeroes the answer as well). Losing the
  target lowers it too (`0x49E1EA`).

So a gun aims, fires, aims once more on the next tick at wherever the target
stands then, and sits on that aim for the rest of the reload. If the target has
moved past tolerance by the time the reload runs out, the drift check sends it
round for a second aim, and that is the only way a shot gets two.

RWE went back to idle whenever a successful aim found the reload unfinished, and
idle starts a new aim: with a script that answers straight away, one every other
tick of every reload. It holds the aim now, as `AimedInfo`, which keeps the
angles and not the thread, because a finished thread is deleted on the next COB
pass. The drift check is unchanged and runs when the reload does.
`sim/aimpershot.test.cpp` counts aims and shots with a tally-keeping script on
ARMCOM and the shipped J7 laser: one aim a shot at the ground, one a shot at a
unit that stands still, and two for the shot after it moves a hundred units.

An answer of no is where RWE still differs: see §91.

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

taking `√` of that and dividing by the speed, then `acos` (`0x4E67F0`, at
`0x49A9C8` and `0x49A9FE`).

> **Corrected 2026-09-24.** This paragraph said "to get a sine, then `asin`",
> which contradicted §11's reading of the same call — and §11 was right.
> `0x4E67F0` computes `fld1; fadd` → `(1+x)`, `fld1; fsub` → `(1−x)`, `fmulp`,
> `fsqrt` → `√(1−x²)`, then `fxch; fpatan` → `atan2(√(1−x²), x)`, which is
> **`acos`**; the |x|=1 arms return `fldz` (0) and `fldpi` (π), and the
> rejected-argument fallback at `0x49A9D9` writes `0x3FF921FB54442D11` = π/2,
> all three of which are `acos` values and none of them `asin` values. The
> quantity being solved for is therefore the **horizontal** launch speed
> squared, not the vertical — which also falls out of the algebra: solving
> `dy·p = D·√(p·(V²−p)) − ½·g·D²` for `p` gives exactly the form above, and
> that `p` is `V²cos²θ`. Nanolathe's independent reading agrees. The
> discriminant, the root choice and the 45° ceiling below are unaffected. Written out with `dy` flipped to the
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
vet  = (uint16)killcount / 12                  ; 0x49D6E0-0x49D700
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
  actually has: twenty-three kills do nothing, twenty-four halve the cone,
  thirty-six divide it by three. It is an integer divide, so it never reaches
  zero.

  > **Corrected 2026-09-24: the divisor is 12, not 3.** This section read the
  > divide as `/3` and put the first effect at six kills. The site is
  > `mov eax,0x2AAAAAAB; imul edx; sar edx,1`, and `0x2AAAAAAB` is
  > `2^33/12` rounded up: the `imul` leaves the high half in `edx`
  > (product ≫ 32) and the `sar edx,1` takes it to product ≫ 33, so the
  > result is `n · 715827883 / 2^33` = `n/12`. A `/3` would have been magic
  > `0x55555556` with no shift, and a `/6` the same magic with no shift.
  > The kill count is zero-extended from a word (`xor edx,edx;
  > mov dx,WORD PTR [edi+0xb8]`), so it is `(uint16)kills / 12`. With the
  > `if (vet > 1)` gate at `0x49D702` the first effect is therefore at
  > **24 kills**, not 6 — four times further away than this document said, and
  > far enough that on stock content it almost never fires. Found by
  > Nanolathe's independent reading and confirmed here.
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

## 116. What a round aims at on a unit, and why the water comes after it

Reported as a Skeeter (`ARMPT`) firing its `ARMKBOT_MISSILE` at a Storm
(`CORSTORM`) wading in the sea: the missiles went into the water short of it.
The question was whether the original aims at the foot of the target. It does,
and it splashes them too. RWE had only the order of the collision tests wrong,
and it matters in the last half footprint before the target.

### The aim point is the unit's origin

`0x49B3E0` (§7, "The aim point") returns, for a round with a target unit and no
target projectile:

```
49b48e  mov  eax,[esi+0x4e]            ; target unit
49b495  test [eax+0x110],0x10000000    ; alive
49b4a1  add  eax,0x6a                  ; &unit.position
```

`unit+0x6A`/`+0x6E`/`+0x72` is the position, 16.16. The original does not add
half the model height, the model top or a waterline offset. A ground unit's `y`
is the ground under it, so a kbot standing in water is aimed at on the
**seabed**. A floater's `y` is `seaLevel - waterline` (§12), just under the
surface.

The one floor in the routine is not on this path. A **`cruise`** round with no
live target unit, once inside its 1024-unit handover, re-aims at its stored
point with `Y` set to `max(seaLevel, 0x485070(point))`, the ground height or
the sea, whichever is higher (`0x49B4AE`-`0x49B501`; the test is of `bl`, the
`cruise` bit read at the top). Any other round with no target unit steers at
the stored point unchanged, and a live unit is always aimed at wherever it
stands.

### The order the stop tests run in, `0x49B090`

§7, "Where a round stops", has the unit slots. The rest of the routine, in the
order it runs, for the square the round now stands in:

1. **The two unit slots** (`WORD sq+0x0`, `WORD sq+0x2`) and then **the
   feature**, as §7 describes.
2. **`unitsonly`** (bit 14 of `wdef+0x111`, `0x49B294`): a round that has it
   returns here and never tests the ground or the sea.
3. **The ground** (`0x49B36D`): `BYTE sq+0x6` above the round's integer `y`.
   With `groundbounce` (bit 15, `0x49B37F`) the round is not detonated. Its `vy`
   becomes `-(vy >> 2)` and it flies on.
4. **The sea** (`0x49B3A1`): the round's integer `y` below `BYTE
   [globals+0x1427F]`, skipped for a `waterweapon` (bit 16) and when
   `[[globals+0x391E9]+0xD48]` is set. That word is the map's
   `nosealeveltrigger`, stored from the OTA at `0x4365A1` (the key string is
   `0x504BD0`).

Every hit goes to `0x499EB0`. Whether a detonation plays the water art does not
depend on which of these tests fired. `0x499ECF` compares the square's
`BYTE sq+0x5`, its high corner, with sea level. So a round that hits a wading
unit in a wet square still throws up spray.

### What that makes of a wading target

The round steers at the origin under the water, so its path crosses the surface
before it reaches the origin. The unit is tested first, but only in squares its
footprint covers. So the round hits the unit only if it is still above sea
level when it enters the footprint, about half a footprint from the origin. The
same geometry decides the per-tick sample, and a round moving 15 to 22 units a
tick often jumps that last half footprint in one step.

Take a muzzle `h` over the water, a target whose base is `d` under it, and a
footprint half-width `f`. A near-straight guided shot at range `D` enters the
footprint still above the water only when `D < f·(h + d) / d`. For the Storm
(`f` = 16) with its base 10 under and a muzzle 10 over, that is about 32 units,
which is point-blank. So against a kbot in water deeper than a few units,
**the original's missiles land in the sea**, and so do RWE's. The shallower the
water, the longer the range at which they get through.

### Ported

- `checkProjectileCollision` tests the square's units and features, then the
  flying units, **before** the sea and the ground. It used to test the sea
  first, so a round that went below the surface inside a wading unit's
  footprint splashed where the original hits the unit. RWE keeps its sea test
  ahead of its ground test; §88 does not need an entry, because the art comes
  from the square either way (above) and the two differ only for
  `groundbounce`, below.
- `nosealeveltrigger` reaches `GameSimulation::noSeaLevelTrigger` from the OTA
  and switches the sea test off. It is saved with the other map constants and
  is not hashed, as they are not.
- `src/rwe/sim/seacollision.test.cpp` fires the real `ARMKBOT_MISSILE` at a
  wading `CORSTORM` (2x2, `MaxWaterDepth=21`, model top 25.94) under twenty
  units of water: it hits from just outside the footprint, splashes from 80
  units out, and reaches the Storm from 80 units out on a
  `nosealeveltrigger` map.

### Not ported

- **`unitsonly`** is not parsed. No round in RWE skips the ground and the sea.
- **`groundbounce`** zeroes `vy` and puts the round back at its previous height.
  The original sets `vy` to minus a quarter of itself and leaves the position
  alone.

Both are in §91.
