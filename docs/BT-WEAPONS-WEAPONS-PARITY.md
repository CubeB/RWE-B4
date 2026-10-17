# Live weapons, projectiles, impacts and area damage parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for what a
weapon slot does once a unit has a target: the firing decision, the four fire families,
the projectile pool, burst emitters, flight, collision, detonation, area damage and
interception, per `AGENTS.md` and ADR-0004. How a weapon *definition* is read from its
TDF is `BT-WEAPONS-WEAPON-TYPES-PARITY.md`; explosions, debris and shards are
`BT-WEAPONS-EXPLOSIONS-PARITY.md`; reach near water, coordinate targets, the sea-level
collision rules, water art and area damage's lack of a water filter are
`NAVAL-SYSTEMS-PARITY.md` rules 5 to 9 and are not restated here.

This is the most thoroughly decoded subject in the register. `TOTALA-EXE-WEAPONS.md`
(sections 6, 7, 11, 12, 14, 21, 22, 85, 92, 116, 126 and 127) was read out of the retail
executable and already holds most of what the chapter says. **Most of this audit is
therefore corroboration**, and the rules below say so in one line and name the section.
Where the chapter adds something the register lacks, or disagrees, the rule says which,
and the weight of the register (which has a demo oracle behind several of its sections)
is on the register's side until a spec-room read says otherwise.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\weapons-weapons.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its
  source links were followed. Addresses are the ones the chapter names; rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that
  rows already resolved there are not raised again; the rows this audit touches are BT-9,
  BT-14, BT-34 and BT-47.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable.
  Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was
  run. The Gaps table says which items an original check settles.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/io/weapontdf` and
  `src/rwe/LoadingScene_util.cpp` at the working tree of 2026-10-16. Nothing was built,
  run or changed, and tests were not run; a MATCHES below means the code reads as the rule
  says. Another agent had uncommitted edits in the tree while this was written, so every
  line number below is "about".
- **A census of the shipped weapon TDFs** (retail install through `tools/hpi.py`: 184
  distinct weapon names across `totala1.hpi`, `ccdata.ccx` and the patch `.ufo` files,
  scratch script, last definition wins) and of the **272 shipped maps' `.ota` files**
  supplies the "what would this change on shipped data" figures. It is a count of keys
  in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Fire-handler and flight-family precedence
3. The firing decision, reload and accuracy
4. Aim tolerance and its zero default
5. The launch-angle solver, its ceiling and the muzzle correction
6. Map gravity
7. Burst emitters
8. The 300-record projectile pool
9. Flight, guidance and the cruise aim point
10. Collision, detonation and direct damage
11. Area damage: the dedupe lists and the small-blast path
12. Interception
13. Replication, compaction and drawing (not applicable or presentation)
14. Gaps to fix

---

## 1. Shape and what the register already holds

The chapter's sections against the register, with the verdict of this audit:

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 2, 5 reload formula, accuracy cone, independent heading and pitch draws, kills/12 divisor | `TOTALA-EXE-WEAPONS.md` section 11 (accuracy) and section 21 (reload) | **MATCHES** (`computeAccuracyCone`, `computeReloadTicks` in `UnitBehaviorService_util.cpp`) |
| 2 charge compared before, taken as the round leaves | section 21 "Per burst" and BT-34 (resolved) | **MATCHES** (`chargeStockpile`, `UnitBehaviorService.cpp` about 1503) |
| 3 reach versus position reach | `NAVAL-SYSTEMS-PARITY.md` rules 5, 6 | covered there |
| 9 selfprop motor, twophase, burnblow, guidance, wind | section 7 | **MATCHES** (`updateSelfPropelledProjectile`, `BurnBlowAbortAngle`, `currentWindVector`) |
| 11 collision order, `unitsonly`, `groundbounce`, sea trigger, feature | section 7 "Where a round stops", section 116 | **MATCHES** (`checkProjectileCollision`, about 3157) |
| 11 water art, entry splash | `NAVAL-SYSTEMS-PARITY.md` rule 7, BT-9, BT-14 | covered there |
| 12 attacker veterancy plus 6 percent a tier, the global speed flags | section 6 | **MATCHES** (corroboration only) |
| 12, 13 firer exempt, no ally filter, strict radius, whole-unit distance, quadratic falloff | section 6, section 22, section 85 | **MATCHES** (`applyDamageInRadius`, `blastDamageScaleForUnit`) |
| 13 feature continuation resolves to the origin | BT-47 (resolved) | already adjudicated |
| 14 interceptor acquisition (stored aim, square, one claimant) | section 21 | **MATCHES** (`findInterceptTarget`, `isWithinCoverage`) |
| 4 `minbarrelangle` and the 45 degree ceiling | section 12 "`minbarrelangle` is inert" | rule 5 |
| 8 burst template | section 7, "sprayangle", section 21 | rule 7 |

Everything below is the part that is not already a MATCHES line in that table.

## 2. Fire-handler and flight-family precedence

**Reference (sections 1, 5, 9, 15).** Three different precedence orders exist and the
chapter says so explicitly. The fire-handler selector, run once when a unit's slots are
initialised, tests **turret, then vlaunch, then lineofsight or selfprop, then dropped**,
and leaves the existing callback untouched when nothing matches: **a turretless ballistic
weapon has no handler at all** and never fires. The flight pass tests **selfprop,
lineofsight, ballistic, dropped, meteor**. The network receiver tests **ballistic first**,
then vlaunch, then lineofsight or selfprop, then dropped. Contradictory flag sets are not
normalised.

**RWE.** `parseWeaponDefinition` (`LoadingScene_util.cpp`, about 219-262) collapses the
flags into one `physicsType` variant with its own precedence: **selfprop, then tracks,
then lineofsight, then dropped, then ballistic**. `turret` and `vLaunch` are kept as
independent booleans on the definition (about 162-175). The register's section 7 flight
order and its note that `vlaunch` is tested before `selfprop` on the launch side are
reproduced.

- **Flight order: MATCHES the register** for every combination the shipped data contains.
  The one reordering in RWE is that `tracks` precedes `lineofsight` where the original
  has no `tracks` branch in the dispatch at all (it is a guidance modifier). Census:
  no shipped weapon sets `tracks` without `selfprop`, so the branch is unreachable on
  shipped data.
- **Dropped versus ballistic: DIFFERS in the code comment, not in the data.** The comment
  at about 246 says bombs in TA set both `ballistic=1` and `dropped=1`. The census does not
  agree: the four shipped bombs (`armbomb`, `armadvbomb`, `corbomb`, `coradvbomb`) set
  `dropped` and not `ballistic`; no shipped weapon sets both, and none sets
  `lineofsight` with `ballistic`. RWE's order (dropped before ballistic) is the flight
  order's inverse for a weapon that does set both (flight: ballistic first), so it
  matters only to a mod that follows the comment.
- **Turretless ballistic: UNCLEAR.** Twelve shipped weapons are ballistic with no turret
  and no `dropped`: `armmine1` to `armmine6`, `cormine1` to `cormine6`. In the original
  these have no fire handler and never fire. In RWE a ballistic weapon without a turret
  goes through `weaponNeedsTheHullTurned` (`UnitBehaviorService_util.cpp`, about 651) and
  is allowed to fire once the hull faces the target. Whether a mine unit ever reaches
  that path was not traced; mines run `StandbyMine` and detonate on contact, and that
  mission may never ask the weapon pass to fire.
- **Receiver order: not applicable.** RWE is lockstep and sends commands, not
  per-shot packets. The own-clock and TA-host paths (`net/ta/`) reconstruct a shot from
  the received record and were not audited for this order.

## 3. The firing decision, reload and accuracy

**Reference (section 2, 5).** `UpdateUnitWeapons` decrements a positive reload counter,
resolves the target position, and starts the aim script even while the counter is still
running; a nonzero counter only prevents the shot. Once ready it checks position reach,
then for stockpile weapons nonzero ammunition and for the rest a comparison of the owner's
energy and metal against the per-shot costs; the callback fires, and the spend follows
without a re-check. The reload is `((120 - 20*hp/max) * ((100 - 6*tier) * reload / 100)) / 100`
with `tier = min(5, kills/5)` and every division truncating; the result is stored as a
16-bit value. The unit gets event `0x800` for a command-fire shot and `0x400` otherwise;
a reach failure raises `0x1000`. Spread is `accuracy - hp*2048/max + 2048` as a signed
short, divided by `kills/12` when that exceeds one, and drawn independently for heading and
pitch.

**RWE.**

- **Reload: MATCHES** (`computeReloadTicks`, `UnitBehaviorService_util.cpp` about 197;
  register section 21). Two details differ and neither is reachable on shipped data:
  RWE does not narrow the result to 16 bits, and a zero maximum health is guarded where
  the original divides.
- **Accuracy: MATCHES** (`computeAccuracyCone`, about 73; register section 11), including
  independent heading and pitch draws and the turret-only gate (`weaponAimScatters`,
  about 231).
- **Aim before the reload ends: MATCHES in effect.** `updateWeapon` runs the aim state
  machine regardless of `readyTime` and gates only the shot (about 1294-1332).
- **Charge: MATCHES the register** (BT-34 resolved); the chapter's compare-then-spend is
  one step in RWE, and the observable difference (a shot that cannot be paid for does not
  go) is the same.
- **`SetMaxReloadTime`: MATCHES.** `UnitBehaviorService.cpp` about 160-175 passes
  `maximum reload ticks * 1000 / 30` to the script, which is the chapter's integer
  milliseconds.
- **The slot-enable test on definition index zero: not applicable.** The chapter says a
  weapon whose index is zero is "absent" for the enable test. RWE binds a slot by name.
  The census shows no shipped weapon with `id=0`, and two sections without an id (`damage`
  and `vtol_foomissile`, neither named by any unit), so the quirk is unreachable on shipped
  data and moot for mods that give every weapon a nonzero id.
- **Events `0x800`, `0x400`, `0x1000`: UNCLEAR.** RWE has `commandFireShotFired`
  (about 1677) for the command-fire case; the other two wake bits are the register's
  section 14 material and were not re-traced.

## 4. Aim tolerance and its zero default

**Reference (section 4).** `AimWithinTolerance` compares wrapped signed-16-bit heading
and pitch errors, inclusive. An explicit `tolerance` is used for heading and, when
`pitchtolerance` is zero, for pitch too. **A zero tolerance means 2000 when the unit's
runtime moving mask `0x0c` is present and 150 otherwise**, "a default zero is therefore
not exact-only alignment".

**Register.** Section 11 records the same rule (`0x49D880`, `0x49D895`-`0x49D8CF`).

**RWE: DIFFERS.** `parseWeaponBlock` (`io/weapontdf/WeaponTdf.cpp` about 74-79) supplies
**256** when `tolerance` is absent and passes the same figure to `pitchtolerance`. There
is no 2000-versus-150 choice anywhere. The value is used in two places:
`UnitBehaviorService.cpp` about 1308 (the hull-turn check for a turretless weapon) and
about 1394 (the drift check between aiming and firing for a turret).

**What it changes on shipped data.** Of the 184 weapons, **95 name no `tolerance`**; 74
of those are turret weapons, 21 are turretless (mines, bombs, `mindgun`, two junk
sections). A turret weapon with no key accepts a fire-time drift of 256 angle units
(about 1.4 degrees) in RWE and, in the original, 2000 (about 11 degrees) for a moving unit
and 150 (about 0.8 degrees) for a stationary one. The practical effects are a moving tank
that cancels a shot the original would take, and a stationary one that takes a slightly
looser shot than the original. The turretless group is mostly unaffected (bombs and mines
do not use the check).

**Remaining uncertainty.** Which RWE state corresponds to the mask `0x0c` is not
established by the register ("moving" is the natural guess).

## 5. The launch-angle solver, its ceiling and the muzzle correction

**Reference (sections 1, 4, 5, 7).** The solver takes the horizontal hypotenuse, the
vertical difference and the map gravity; a negative discriminant returns the no-solution
sentinel `0x8000`. It forms two candidate angles from the speed ratio, treats a
nonpositive root as `pi/2`, **accepts the first candidate that is above `minbarrelangle` and at most
`pi/4`, otherwise the second under the same limits**, and nothing otherwise: there is
no high-arc fallback above 45 degrees. A turret handler also treats the sentinel as "out of
reach". Separately, the slot initialiser caches
`trunc(1.25 * (Query z - AimFrom z))` for each slot (the world Z difference between the
muzzle piece and the aim-from piece), and the ballistic spawn **subtracts
`(cached value / speed) * gravity` from the launch vertical velocity**.

**Register.** Section 12 says `minbarrelangle` is inert (a non-negative `acos` can never be
below a negative bound), the answer is always within 0 to 45 degrees, and what refuses a
shot is a negative discriminant or both roots above 45 degrees. `compatibility.md` says
shells fly flat for that reason. Section 12 "Recorded, not implemented" records the launch
correction and says it is **left out of RWE** because the meaning of `[slot+0x10]` was not
established. `TOTALA-EXE-EXTERNAL.md` ("`[slot+0x10]`") supplies the same formula as the
chapter and a checkable consequence: a stationary stock light cannon lands its group 34 to 40 world
units short at every range from half to full.

**RWE.**

- **The 45 degree ceiling: DIFFERS.** `computeFiringAngles` (`UnitBehaviorService_util.cpp`,
  about 744) returns both roots whenever the discriminant is nonnegative, and
  `computeBallisticHeadingAndPitch` (about 721) takes the second (the flat root)
  unconditionally, substituting an eighth of a turn on no solution. There is no test that
  the flat root is at most 45 degrees. The reachability gate (`weaponCanHitUnit`,
  `GameSimulation_visibility.cpp` about 417-426) likewise asks only for a solution. The two
  agree with the original for any target whose flat root is under 45 degrees, which is
  every target at or below the shooter and every target above it at a gentler slope than 45
  degrees; they differ for a target above the shooter at a slope steeper than 45 degrees
  (a cliff top, an aircraft almost overhead), where the original refuses and RWE fires at a
  steeper angle than the original allows. Rare in play.
- **The muzzle correction: DIFFERS, and the register's "left out" is stale.** RWE does
  have a per-slot value (`ballisticZOffset`, `UnitBehaviorService.cpp` about 195, set to
  the local firing-point Z minus the local aiming-point Z, with no 1.25 factor and no
  truncation), but it is used in a different place: `computeBallisticHeadingAndPitch`
  **subtracts it from the horizontal distance fed to the solver** (about 733). The original
  applies it to the launch vertical velocity after the solver, so the shell leaves flatter
  than the solved angle. The two are not the same arithmetic and were not compared. This
  is a **CONFLICT inside the repository**: the register says the term is left out,
  the code has a term, and neither is the chapter's.
- **Zero distance: UNCLEAR.** The flat-root expression divides by gravity times the
  (corrected) distance; a target straight above the muzzle gives a zero divisor. The
  chapter says the original does not guard zero distance either.

**What it changes.** Ballistic weapons are the tank, artillery and ship guns, so this is
where the grouping of a shell pattern comes from. The register's own weapon-oracle
replay (section 12 tables, the Vulcan and Intimidator figures) was computed without the
correction and the spread is dominated by `accuracy`, so a 34 to 40 unit systematic short
landing on a stationary light cannon is visible in the original and absent in RWE only if
the RWE solver's use of the offset does not already produce a similar bias. Not computed.

## 6. Map gravity

**Reference (section 4).** The solver and the flight integration use "current map
gravity".

**Register.** Map gravity is the OTA `gravity` key (`TOTALA-EXE-WRECKS.md` about 274,
`TOTALA-EXE-MISSIONS.md` about 412, parsed at `0x436542`). `TOTALA-EXE.md` (section 91's
smoke entry) says the smoke lift "will not track a map that sets gravity to something
else". Nothing records the same limitation for projectiles.

**RWE: DIFFERS.** `io/ota/ota.cpp` about 51 parses `gravity` with a default of 112 and
nothing reads it. The value is hard-coded as `112 / (30 * 30)` per tick squared at
**GameSimulation.cpp about 4063, 4143 and 4154** (self-propelled coast, ballistic, bomb),
about 4749 (a sinking wreck), `GameSimulation_visibility.cpp` about 421 (the arc test),
`UnitBehaviorService_util.cpp` about 1259 (the aircraft lift term), `AirMovement.cpp` about
34 and 66, `UnitBehaviorService.cpp` about 1294 and 1390 (the aim solver) and, in the scene,
`GameScene_commands.cpp` about 1145 and 1199 (shard debris) and an unrelated 0.3 for whole
pieces.

**What it changes on shipped data.** Of the 272 shipped `.ota` files, **189 say
`gravity=112`**; **83 (31 percent) say something else**: 150 (16 maps), 70 (9), 80 (8),
105 (7), 120 (6), 20 (6), 200 (6), 125 (5), 75 (5), 170 (3), 100 (3), 128 (3), 60 (2),
and singletons at 147, 132, 8 and 18. On a map with gravity 150 a shell falls a third faster
than in RWE; on a gravity of 20 it floats. Because the solver in the original uses the same
value, the *aim* adapts (the pitch changes), but RWE's pitch, time of flight, bomb drop
and wreck sinking rates are all the 112 figures. The two machines would not produce the same
impact point or the same flight time for the same shot, and the demo oracle (all played on
112 maps, as far as the register records) cannot see it.

**Fix shape.** A map constant stored beside `noSeaLevelTrigger` (saved, not hashed, per the
register's precedent), read from the OTA, and fed to the sites above. The shard debris is
presentation but should follow.

## 7. Burst emitters

**Reference (section 8).** A projectile with a nonzero burst counter is an **emitter
template**, not a flying shot. The projectile pass reads its count once before walking, so
children appended during the pass are not walked until the next. When the tick reaches
the last emission plus `burstrate` ticks, the template decrements its counter, advances the
timestamp and **attempts one child allocation**; only one emission per template per
update, however large the tick jump. The muzzle position is refreshed for `burstrate` at
least 5 ticks **or when the remaining counter is odd**. A child copies the template, gets
the current spawn tick, a zero counter and a new expiry from the weapon's lifetime or
from **`(stored horizontal range + 16 world units) / scalar speed`**; `randomdecay` jitters
the expiry around half its interval. **`sprayangle` modifies the template's horizontal
velocity after the child has been copied**, so a spray draw shows up in the *next* children
and **accumulates**. When the counter reaches zero the template is removed even if the last
child failed to allocate. A full pool drops the child while the template still advances.
The Fire script and `RockUnit` are started by the three spawn families, that is, at the
template's creation.

**Register.** Section 7 describes the template and the late children (`0x49B810`),
"`sprayangle`" records that the first shot of a burst is not sprayed and the width is
`rand(sprayangle) - sprayangle/2`, and section 21 records that a burst is paid for once.
Section 14 ends with "the original starts the fire script inside the projectile spawn
routine, so a burst weapon runs `FirePrimary` once per projectile, where RWE runs it once
per burst".

**RWE.**

- **Template versus per-shot: DIFFERS by design.** RWE has no template. A burst is a state
  on the weapon (`UnitWeaponStateAttacking::FireInfo`: `burstsFired`, `readyTime`) that
  calls `tryFireWeapon` once per child (`UnitBehaviorService.cpp` about 1405-1725). Each
  child is a fully-formed projectile from the start, built from the muzzle position at that
  moment (the firing piece is cached for the burst, its world position is read each time).
  This is the register's recorded and accepted shape; the items below are what that
  shape does not reproduce.
- **Cumulative spray: DIFFERS (new).** The chapter's order means child *k* carries the sum
  of the previous *k-1* spray draws, a random walk with a standard deviation growing as
  the square root of *k*. RWE draws each child's spray independently around the unchanged
  aim (`changeDirectionByRandomAngle`, about 1597, applied per shot). Five shipped weapons set
  `sprayangle`, all 1024: `armwar_emg` (burst 2), `emg` (3), `vtol_emg` (4), `vtol_emg2` (3) and
  `flamethrower` (17). The two-to-four shot bursts differ by a small factor; the
  **flamethrower's plume widens along the burst in the original (about 1.6 degrees of
  standard deviation on the second child growing to about 6.5 degrees on the seventeenth)
  and does not in RWE**. This sits on the register's section 14 ("sprayangle") reading
  of where the draws are made, which says nothing about accumulation; it is the chapter's
  addition.
- **Fire script once per burst: MATCHES the chapter, and the register's closing sentence is
  suspect.** RWE starts the Fire script when `burstsFired == 0` (about 1685). On the
  chapter's reading the original does the same, because the script is started where the
  template is spawned and the children are made by the projectile pass without calling it.
  The register's statement that the original runs the script once per projectile is not
  consistent with its own account of the template in section 7 (the spawn routine runs
  once and the pass emits the rest). **CONFLICT inside the register; no winner picked.**
- **`RockUnit` per child: DIFFERS.** RWE starts `RockUnit` for every non-bomb child
  (about 1722). On the chapter's reading it is started once at the template. Seventeen of the 200
  shipped scripts define `RockUnit`; which of those units carry a burst weapon was not
  counted. Visual (the hull heel) but the COB thread is created inside the simulation.
- **Child expiry: UNCLEAR.** RWE gives every child the template's full-range lifetime
  (`createProjectileFromWeapon`, about 2443-2455) and the register's section 7 says "nothing
  shortens the flight to suit the target". The chapter says burst *children* get a lifetime
  of the stored aim distance plus 16 units, divided by the speed. These are different
  statements about different projectiles (the first projectile versus its copies) and the
  register's section was derived from single-shot weapons. Effect: an EMG child that
  misses stops 16 units past the aim distance in the original and flies to its full range in
  RWE, so it can hit something behind the target.
- **Muzzle refresh condition, one emission per update, template removed on a failed last
  child: not applicable** to a design with no template. The visible consequence of the
  muzzle rule (a child fired between refreshes is placed where the template last was, not
  where the muzzle now is) is small for a stationary shooter.
- **Failed allocation: see rule 8.**

## 8. The 300-record projectile pool

**Reference (section 6, 8).** The pool is 300 records of 107 bytes. Allocation returns
nothing when the count is 300, and a shot that cannot allocate fails (the fire callback
returns failure; the register's section 7 records the same array). A burst child that finds
the pool full is dropped while its template carries on. Effects, shards and detached
pieces have their own pools (`BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 1).

**RWE: MISSING.** `projectiles` is a `VectorMap` with no cap (`GameSimulation.h` about 576).
A battle of any size keeps every shot.

**What it changes.** Projectile lifetimes are short but not tiny: a ballistic shell lives
for seconds, a missile for several, a flamethrower child for a second or so. A two-hundred
unit battle (the `battle_test` default shape) can plausibly put more than 300 live rounds
in the air, at which point the original **stops spawning new ones until old ones land**,
which throttles damage output in exactly the largest fights, while RWE does not. Whether
the retail game reaches the cap in a normal skirmish was not measured; the cap matters most
for flak, lasers (very short life) and flamethrowers (many children).

**Fix shape.** Reject the spawn at 300 live (not dead-flagged) records and have the
fire path treat it as a failed shot that retries next tick without paying, as the original's
failure return does. This is a hashed behaviour change and wants the replay gate.

## 9. Flight, guidance and the cruise aim point

**Reference (sections 9, 10).** Per-flight-family behaviour as the register's section 7
describes it, plus two details. The cruise aim point "uses the cached destination, not live
target tracking: at full 3D distance over 1024 world units it returns the destination with
height whole part 700; nearer it sets the height to max(ground, sea)". The turn routine
moves each of heading and pitch by at most `turnrate`, and a `burnblow` missile whose desired
difference exceeds 27000 fails and detonates.

**RWE.**

- **Selfprop motor, burn time, `startvelocity`, acceleration, twophase turnover, `tracks`:
  MATCHES the register** (`createProjectileFromWeapon` about 2480-2518,
  `updateSelfPropelledProjectile` about 4049).
- **Burnblow abort angle 27000: MATCHES** (`BurnBlowAbortAngle`, about 2397).
- **Beam drawn length: not audited here** (presentation).
- **Cruise, far branch: MATCHES** (`CruiseAltitude` 700, `CruiseHandoverDistance` 1024,
  about 4019-4023).
- **Cruise, near branch: DIFFERS in two small ways.** RWE falls through to the target
  projectile, then the **live target unit**, then the stored point unchanged
  (`getSelfPropelledAimPoint`, about 4015-4045). The chapter says a cruise missile uses the
  cached destination with the height floored at max(ground, sea) and does not follow a live
  unit. The register (section 7 and the 2026-10-16 section 116 text) says the floor applies
  "with no live target unit" and that "a live unit is always aimed at wherever it stands".
  So on the question of the floor the chapter and the register agree and RWE lacks it; on
  the question of a live unit the chapter and the register **CONFLICT** and RWE follows the
  register. Census: `cruise` is set on two shipped weapons, `nuclear_missile` and `crblmssl`; the
  practical case is a nuke fired at a ground point, where the near branch changes only
  the final height.
- **Ballistic and bomb wind: MATCHES** (register section 7 "Wind", `currentWindVector`).
- **Wreck sink rate and shard gravity:** rule 6.

## 10. Collision, detonation and direct damage

**Reference (sections 11, 12).** Covered by the register's section 7 "Where a round stops"
and section 116 and by `NAVAL-SYSTEMS-PARITY.md` rule 7. The chapter adds, and RWE
matches:

- **Player equality, not alliance, suppresses direct collisions**, while the blast does hurt
  same-player units (`checkProjectileCollision` compares `owner`; `applyDamageInRadius`
  exempts only the firer). **MATCHES.**
- **The first unit slot tests an upper bound only, the second a floor and a ceiling**:
  already recorded as not implemented in `NAVAL-CONSTRUCTION-DGUN-FINDINGS.md` ("the
  upper-bound-only test on the first unit slot").
- **`noexplode` leaves the projectile alive after the effects**: **MATCHES** (the comment at
  about 4210).
- **Damage authority is skipped for a projectile whose owner is a remote controller**
  while the visual effects still happen: **MATCHES in effect** (`isRemoteRound` in
  `doProjectileImpact`, about 3992), RWE's lockstep reading of the owner-authoritative
  model.
- **A direct hit is chosen when `areaofeffect` is at most 16 and a direct target exists;
  otherwise the area routine runs.** See rule 11: RWE has no direct-hit path.
- **Impact on the ground over a wet cell, entry splash:** `NAVAL-SYSTEMS-PARITY.md` rule 7.

## 11. Area damage: the dedupe lists and the small-blast path

**Reference (section 13).** The ordinary radius is half the area of effect. Distance is the
3D distance to the nearest point of each unit's axis-aligned box, truncated to whole units;
strictly inside the radius qualifies. **Deduplication remembers at most 20 unit pointers and
64 feature-origin pointers. Once a list fills, further objects still qualify without being
recorded, and a later occupied cell of an unrecorded object applies the damage again.** The
chapter states the consequence: "this capacity limits duplicate suppression, not the total
number of targets damaged". With a direct target and `areaofeffect <= 16`, **only that unit
is hit, at full strength**; otherwise the area routine runs.

**Register.** Section 22 says "up to twenty distinct units are de-duplicated per blast" and
does not say what happens past the twentieth. Section 6 and the 2026-10-16 correction
record the `<= 16` single-target path at scale 1.0 (`0x49A049`).

**RWE.**

- **Dedupe past the cap: DIFFERS (consequence unrecorded).** `applyDamageInRadius`
  (`GameSimulation.cpp` about 3795-3986) keeps unbounded `seenUnits` and `seenFeatures`
  sets, so every unit and feature is hit exactly once however many are in the blast. In the
  original, every unit after the twentieth in the scan order is hit once **per covered cell
  that is inside the radius**. A unit with a footprint of N cells in the blast therefore
  takes up to N times its damage once the list has overflowed (a footprint-3 unit up to 9
  times), and a feature past the sixty-fourth takes up to its cell count. The scan order
  decides who is in the first twenty: row by row over the clamped rectangle, first occupant
  slot before second, which is not established for the original. This bites in the blasts
  large enough to have more than twenty units in range: nukes, the commander's
  self-destruct, the heavy artillery, a Krogoth tremor. Whether it is a bug the original's
  players saw (the heaviest weapons killing clustered units faster than their listed damage
  says) is exactly what an original check would show.
- **Single-target path for a small blast: DIFFERS in shape.** `blastDamageScaleForUnit`
  (about 3775) returns full strength for every unit inside a radius of at most 8 (half of
  16), and the grid scan then visits every cell within 8 units of the impact. A shot that
  lands on a cell edge therefore hurts **every unit whose box lies within 8 units of the
  point**, not the one it struck. The original hits the struck unit only. Forty-three of the
  136 weapons the code comment counted (the census here counts 62 of 184 names at
  `areaofeffect <= 16`, including variants and junk sections) are in this group, "including
  the twenty-eight at AreaOfEffect eight": machine guns and light lasers. In a packed group
  (units touching or one cell apart) each such shot damages its neighbours at full strength
  too, which inflates the damage of exactly the most-used weapons. The `radius <= 8`
  shortcut is a documented simplification (the comment above `SmallestSpreadingBlastRadius`
  and the DGUN findings' "keeps the old single-target treatment"), but the comment describes it as preserving
  single-target behaviour, which it does not when a neighbour is within 8 units.
- **A small blast with no direct target** (a shot into the ground): the chapter sends it to the
  area routine with the quadratic falloff; RWE gives it the flat 1.0. Unresolved against
  the register, which says the path is taken for any `areaofeffect <= 16`: **CONFLICT**, no
  winner picked. It affects ground-targeted small shots only.
- **Feature handling:** one origin cell per feature, the weapon's default damage unscaled,
  `damagesFeatures` as a mod switch: **MATCHES the register and BT-47** (about 3877-3918).
  The 64-feature cap rides with the unit cap above.
- **Friendly and firer: MATCHES** (`firer` exemption only).

## 12. Interception

**Reference (sections 13, 14).** Acquisition: the first projectile of a different player,
with a targetable definition, whose **stored aim** lies in an axis-aligned square of the
weapon's coverage, not already claimed by another interceptor's target slot; the launcher
needs stockpiled ammunition; no alliance, circular, Y, dead-flag or approach test. In flight,
an interceptor detonates when its 3D distance to the target projectile is **strictly less than
`areaOfEffect`**. On a blast from an interceptor weapon, **every other non-dead projectile
within the full `areaOfEffect` 3D radius is detonated**, with no `targetable` or hostility
re-check. The chapter stresses that the interception radius is the *full* area of effect and
"distinct from ordinary half-area blast radius".

**Register.** Section 21 records every step, with the strictly-inside tests at `0x49B1A4` and
`0x49A757` and the square-versus-sphere remark, and says the radius is `areaofeffect`.

**RWE.**

- **Acquisition: MATCHES** (`findInterceptTarget`, about 2819-2882): own player skipped,
  `targetable` required, stored aim in the square, one claimant, magazine required.
- **Radius: DIFFERS.** Both the proximity fuse (about 4194-4200) and the blast that detonates
  caught projectiles (`detonateProjectilesInBlast`, about 2884-2899, called from
  `doProjectileImpact` with `projectile.damageRadius`) use **`damageRadius`, which is half
  the area of effect**. The shipped interceptors (`armscab_weapon`, `cormabm_weapon`, `amd_rocket`, `fmd_rocket`) all
  have `areaofeffect=96`, so RWE catches a missile within 48 units where the register and the
  chapter say 96. The `radius * radius`
  comparison is strict as in the original. The register's comment text
  ("within `areaofeffect`") was read as the raw TDF value by the original's audit and by
  the chapter, and as the halved one by the port.
- **A projectile caught in the blast cannot chain: DIVERGENCE (documented)** in the
  register section 21 (caught projectiles are marked dead first).

**What it changes.** The anti-nuke engages a nuke at half the distance it should, so a
fast or high nuke is more likely to slip past or detonate closer to the defended base,
and a salvo fired from distinct points is cleared less thoroughly than the original's.
Everything else about interception is already right.

## 13. Replication, compaction and drawing

Not applicable or presentation:

- **The fire and detonate packets** (36 and 14 bytes), the remote-projectile matching by
  exact aim coordinates, and the receiver's precedence are TA's owner-authoritative
  netcode; RWE is lockstep. The TA-host path reconstructs shots from the peer's `0x0d`
  records (`docs/TA-NETWORK.md`) and was not audited for the chapter's details (an
  unbounds-checked slot byte, an uninitialised interceptor bit).
- **`CompactProjectiles` and `RemoveUnitProjectiles`:** the chapter documents stale
  interception references after compaction and a skipped adjacent record in the owner
  cleanup. RWE keys projectiles by id in a `VectorMap`, so neither fault exists; they are
  original defects with no counterpart to port. A unit removed with a burst in progress
  stops firing it in both.
- **Projectile drawing** (eight render types, the type-2 early return that can suppress later
  projectiles, type 3's uninitialised angle record, the type-4 colour table): RWE's
  `parseWeaponMediaInfo` (`LoadingScene_util.cpp` about 683-790) handles types 0, 1, 3, 4, 5,
  6 and 7 with its own renderers; type 2 (the lens, used by one shipped weapon, `mindgun`)
  falls to the default black laser. Presentation only; the original's type-2 quirk is a
  defect to leave out.
- **Smoke trail cadence** (once per update, next-smoke tick strictly less than the current
  tick) and **splash on crossing the surface:** presentation, covered by
  `NAVAL-SYSTEMS-PARITY.md` rule 7 for the second.
- **Meteors, `SpawnProjectile`'s ownerless entry:** the meteor shower is the register's
  recorded and not implemented material (section 7).

---

## 14. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or
capture run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | The OTA `gravity` key is parsed and ignored; 83 of 272 shipped maps (31 percent) are not 112. Rule 6 | `io/ota/ota.cpp`, `sim/GameSimulation.h` (a saved, unhashed map constant), `sim/GameSimulation.cpp` (about 4063, 4143, 4154, 4749), `GameSimulation_visibility.cpp`, `UnitBehaviorService.cpp`, `UnitBehaviorService_util.cpp`, `AirMovement.cpp`, `game/save_util.cpp`, `GameScene_commands.cpp` | No: the register decodes the source of the value. A one-shell replay on a 150 map confirms the flight time. Hashed; replay gate applies. |
| 2 | Area-damage dedupe stops at 20 units and 64 features; past that a unit takes its damage once per covered cell. Rule 11 | `sim/GameSimulation.cpp` (`applyDamageInRadius`), `sim/dgun.test.cpp` and a new test | **Yes.** A nuke on a base of forty footprint-2 units; compare the damage per unit past the twentieth. Static reading only; hashed. |
| 3 | The interception fuse and the blast that detonates caught projectiles use half the area of effect. Rule 12 | `sim/GameSimulation.cpp` (about 4194-4200, `doProjectileImpact`), `sim/interception.test.cpp` | No: the register already says `areaofeffect`. Small, one constant. |
| 4 | A zero tolerance should mean 2000 (moving) or 150 (stationary), not 256; 95 of 184 weapons name none. Rule 4 | `io/weapontdf/WeaponTdf.cpp`, `sim/UnitBehaviorService.cpp` (about 1308, 1394), `sim/WeaponDefinition.h` | No for the figure (register section 11); which RWE state is the `0x0c` mask needs the register. |
| 5 | Small blasts (`areaofeffect <= 16`) hurt every unit within 8 units of the impact instead of the one struck, and have no ground-impact falloff. Rule 11 | `sim/GameSimulation.cpp` (`blastDamageScaleForUnit`, `doProjectileImpact`, the collision info), `sim/ProjectileCollisionInfo` | **Yes** for the no-direct-target case (the register and the chapter disagree); no for the neighbour case, which both agree on. |
| 6 | No 300-record cap on live projectiles. Rule 8 | `sim/GameSimulation.cpp` (`spawnProjectile`), `UnitBehaviorService.cpp` (a failed spawn is a failed shot) | **Yes.** Count live projectiles in a large retail battle before adopting a cap. Hashed. |
| 7 | Flamethrower plume does not widen along the burst; spray should accumulate on the template. Rule 7 | `sim/UnitBehaviorService.cpp` (`tryFireWeapon`, the burst state) | Static first: the register's spray decode should say what the continuation writes back. |
| 8 | The muzzle correction `trunc(1.25 * (Query z - AimFrom z))` is applied to the launch vertical velocity in the original and to the solver distance in RWE. Rule 5 | `sim/UnitBehaviorService.cpp` (about 195), `UnitBehaviorService_util.cpp` (about 721), `sim/UnitWeapon.h` | **Yes**, and the register's transcription-replay method: replay the 34 to 40 unit short landing of a stock light cannon. |
| 9 | The flat solution is not refused above 45 degrees; a steeply elevated target is fired at. Rule 5 | `UnitBehaviorService_util.cpp` (`computeBallisticHeadingAndPitch`), `GameSimulation_visibility.cpp` (about 417) | No (register section 12). Rare in play. |
| 10 | `RockUnit` per burst child versus per burst; burst-child expiry from the aim distance plus 16. Rule 7 | `sim/UnitBehaviorService.cpp`, `sim/GameSimulation.cpp` (about 2443) | Static first: the child's expiry in the register's burst decode. |

### Documented divergences this audit leaves alone

The one-handler-per-tick order of unit updates; the lockstep owner-authority model for
remote rounds; a caught interceptor marked dead before its own blast (section 21); the
`noexplode` handling and the D-gun's explicit exemption of the firer; the per-burst
rather than per-shot template (rule 7's first bullet); presentation randomness drawn from
`effectsRng`; `damagesFeatures` as a mod switch; the stockpile advanced from the weapon pass.

### Conflicts with existing RWE findings, in one place

1. **The launch muzzle correction.** Register section 12: left out of RWE. Code: a
   `ballisticZOffset` exists and shortens the solver's distance. Chapter and
   `TOTALA-EXE-EXTERNAL.md`: `trunc(1.25 * (Query z - AimFrom z))`, applied to the launch
   vertical velocity. Open; the register's own line needs updating whichever is right.
2. **Fire script per burst or per projectile.** Register section 14: once per projectile.
   Chapter: started at the template's spawn, once per burst. RWE already does once per
   burst. Open until a spec-room read.
3. **The cruise near branch.** Chapter: cached destination, no live-unit tracking, floor
   at max(ground, sea). Register: live unit followed, floor only with no live unit. Open.
4. **A small blast with no direct target.** Chapter: falls to the area routine with
   falloff. Register: single-target at 1.0 for any `areaofeffect <= 16`. Open.
5. **A stale line in the code**, not a reference conflict: the comment in
   `parseWeaponDefinition` says TA bombs set both `ballistic` and `dropped`; the shipped
   data sets `dropped` alone.
6. **Interceptor radius.** Not a conflict between sources, but the register's phrase
   "within `areaofeffect`" is ambiguous between the TDF value and the halved blast radius;
   the chapter fixes it as the full value.
