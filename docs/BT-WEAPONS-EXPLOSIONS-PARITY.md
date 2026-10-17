# Explosion effects, model fragments and debris parity audit

Spec-room finding, 2026-10-16. Like `BT-ORDERS-UNIT-ORDERS-PARITY.md`,
`TA-SYSTEMS-PARITY.md` and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number
in the main register. This is the finding that implementation work cites for what the
original draws and simulates *after* something explodes: the effect pool, the procedurally
generated explosion images, the extra smoke an explosion adds, whole detached model pieces,
and the face shards a piece breaks into, per `AGENTS.md` and ADR-0004. Which weapon makes
an explosion and what damage it does is `BT-WEAPONS-WEAPONS-PARITY.md`; the art a weapon
names is `BT-WEAPONS-WEAPON-TYPES-PARITY.md`; the COB `explode` opcode and what flags it
carries is `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 7; the water-versus-ordinary art
choice at an impact is `NAVAL-SYSTEMS-PARITY.md` rule 7. This audit restates none of them.

**Almost all of this subject is presentation.** The chapter says so itself: "no unit health,
feature damage, pathfinding occupancy or reclaimable wreck is created by this movement".
RWE's debris and effects run in the scene (`GameScene_commands.cpp`), draw from the scene's
own `effectsRng`, and never touch `simulation.rng`, so nothing here can desync a game. The
findings below are therefore ranked by how visible they are, and none needs the replay gate.

The register already holds the shatter path (`TOTALA-EXE-WRECKS.md`, "What SHATTER does":
the 300-entry pool, the four-corner filter, half the unit's velocity, the scatter, the spin,
the thirty ticks of gravity added upward, the sea removal and the halved rebound) and it was
ported on 2026-09-25 (#28). The chapter agrees with it on every number it states.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\weapons-explosions.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its
  source links were followed. Addresses are the ones the chapter names; rules are restated
  in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that rows
  already resolved there are not raised again; the rows this audit touches are BT-1 (the
  separate debris phase) and BT-9 (water art).
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no screenshot comparison
  was run. Everything in the Gaps table is a visual check; the project's rule is that the
  user does visual testing.
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene_commands.cpp`
  (`spawnDebris`, `spawnShatterFragments`, `updateDebris`, `spawnExplosion`),
  `GameScene_util.cpp` (`throwShatterFragment`, `stepShatterFragment`,
  `computeWeaponImpactEffects`), `GameScene_audio.cpp` (`spawnWeaponImpactExplosion`) and
  `mesh_util.cpp` (`fragmentSourcesFrom3do`) at the working tree of 2026-10-16. Nothing was
  built, run or changed, and tests were not run; a MATCHES below means the code reads as the
  rule says. Another agent had uncommitted edits in the tree while this was written, so
  every line number below is "about".
- **A census** of the shipped weapon TDFs (184 names, retail plus patches) and 272 `.ota`
  files supplies the "what would this change on shipped data" figures.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. The three pools
2. Procedurally generated explosion images
3. The extra smoke an explosion adds
4. The splash, the lava alternative and the suppression flag
5. Whole detached model pieces
6. Breaking a piece into face shards
7. Exploding a whole unit
8. Feature lookup beside the effects
9. Visibility and the viewport
10. Gaps to fix

---

## 1. The three pools

**Reference (section 1, 3, 4).** Three separate finite stores exist and are not the 300
live projectiles: **300 effect/debris entries**, **300 reusable eight-vertex, six-face shard
objects**, and **100 detached-piece slots** (each with a copied piece record and a 100000-byte
memory cache). An allocation that fails is dropped: `AddExplosionEffect` silently ignores an
effect when 300 are live, **together with its associated smoke request**; a shatter that runs
out of shard objects stops, leaving the entry it had already counted partly initialised; a
whole-piece request that cannot get a slot or cache space leaves the piece hidden with no
replacement.

**Register.** "Stopping when the pool holds 300" (`0x42176F`) is recorded for shards, with the
pool described as shared with the effects (`globals+0x1491B`, 84-byte records).

**RWE.**

- **Shard cap: DIFFERS in what it counts.** `MaxShatterFragments` is 300
  (`GameScene.h` about 1095-1119) and `spawnShatterFragments` stops when the count of
  *shard* debris reaches it (`GameScene_commands.cpp` about 1148-1153). In the original the
  300 are shared with every live explosion sprite, so a battle in full cry has fewer than
  300 left for shards; in RWE the explosion sprites and smoke puffs are in `particles`,
  which has no cap at all. The effect is more shards than the original on a busy screen.
- **Whole-piece cap: MISSING.** `debris` is a `std::vector` with no 100-slot limit.
  Only buildings throw whole pieces in RWE (rule 5) and a base dying piece by piece does
  not approach 100 on a normal map.
- **Particle cap: MISSING.** Nothing bounds `particles` (the only reference to its size is
  a profiling counter, `GameScene_render.cpp` about 1633). The original's 300 limit is why a
  huge fight loses explosion sprites; RWE draws them all. Presentation, but it is a
  per-frame cost that a cap would also bound.
- **Order of failure:** not applicable.

## 2. Procedurally generated explosion images

**Reference (section 1, 2).** `InitExplosions` builds a 22 by 22 lens frame and **three
procedural sequences** with nominal duration, start and end sizes of 24/64/8, 30/128/16 and
30/200/32, each of `duration / 2` frames lasting two ticks, with a shade assigned afterwards
(20, 50 and 100). Each frame is an elliptical, noisy radial gradient: vertical squared
distance times 1.33, plus integer noise 0 to 9, mapped through a 32-step intensity to
transparent (`0xff`), an edge colour (`0x6e`) or a palette run starting at `0x4f`. The sizes
step by an integer `(to - from) / count`, so the last frame is not exactly `to`. These are
**palette-indexed sprites, not a physical fire**: a secondary built-in sequence is selected
by a nonnegative index passed to `AddExplosionEffect` and drawn through the shadow routine.

**RWE: MISSING (UNCLEAR what is lost).** There is no procedural image generator. Explosions
are the GAF animations a weapon or script names (`spawnExplosion`,
`GameScene_commands.cpp` about 3217) plus a short white `FlashEffect` (`spawnFlash`, about
3234: 15 ticks, radius 30). Whether the original's built-in sequences are the same pictures
as the shipped `FX` GAF entries (the shape the names suggest: `explode2` to `explode5`), a
glow under them, or something no GAF holds is not established by the chapter or the register.
`TOTALA-EXE-RENDER.md` does not mention them. **Needs a spec-room read of the callers of
`AddExplosionEffect` with a nonnegative index.**

## 3. The extra smoke an explosion adds

**Reference (section 3).** `AddExplosionEffect`, **when its flag argument is zero and the
effect's whole Y is strictly above sea level**, also requests the smoke emitter with
parameters 7, 15 and 9 (interval, lifetime, layer). A nonzero flag, as used for water and
lava splashes, suppresses it. So the smoke is a property of the *explosion effect call*, not
of any weapon flag. (The weapons chapter adds, at detonation: an `endsmoke` weapon spawns
white smoke and a non-`endsmoke` one the ordinary explosion animation, "either ... or".)

**Register.** `TOTALA-EXE-RENDER.md` section 4 records the same emitter and constants
(`0x472630`, called from `0x420AE1`, interval 7, lifetime 15, layer 9, "emit nothing
underwater"). `TOTALA-EXE.md` (the 2026-09-24 entry, #113) ported it as "an impact that
smokes gets three puffs seven ticks apart".

**RWE: DIFFERS in what triggers it.** The three-puff emitter exists
(`GameScene::startSmokeEmitter`, about 3318) but is started only for a weapon impact whose
weapon sets `endsmoke` and which is not a splash (`computeWeaponImpactEffects`,
`GameScene_util.cpp` about 70; `spawnWeaponImpactExplosion`, `GameScene_audio.cpp` about
631), and is started in addition to the explosion animation. The chapter has the emitter
requested by every ordinary explosion effect above the sea, and has `endsmoke` choose
white smoke *instead of* the animation.

**What it changes on shipped data.** Census: **exactly one shipped weapon sets `endsmoke`**
(`flamethrower`). So in RWE only flamethrower impacts leave the three-puff trail, where on
the chapter's reading all of the other 179 weapons with an explosion animation, the
unit-death explosions and the piece bitmaps that call `AddExplosionEffect` with flag zero
would each leave a small smoke trail when they go off over dry ground or shallows above the
sea. It would be a visible difference in every fight. The register's line "an impact that
smokes" does not say which impacts smoke, and the port read it as `endsmoke`; whether the
original keys it on the effect call (the chapter) or on the weapon flag (the port) is an open
**CONFLICT** that one screenshot of a shell impact in TA settles.

## 4. The splash, the lava alternative and the suppression flag

**Reference (section 3, 4, 6).** A water-flagged effect (flag nonzero) is the splash. Which
art the splash uses is fixed at load time by the map's `+0xd44` word (lava or water), and an
impact effect from a shard or a whole piece picks lava or water by that same word. The
`nosealeveltrigger` map flag (`+0xd48`) suppresses the splash for shards.

**RWE.**

- **Lava choice: MISSING.** See `BT-WEAPONS-WEAPON-TYPES-PARITY.md` rule 5: the loader reads
  only the water pair, `lavaworld` is parsed and unused, 44 of 272 shipped maps are lava
  worlds, and 146 of the 184 weapons carry a lava pair.
- **Shard splash: MISSING, documented.** The 2026-09-25 note in
  `TOTALA-EXE-WRECKS.md` and the comment in `updateDebris` (about 1203) say the sea removal
  has no splash sprite.
- **`nosealeveltrigger` for shards: UNCLEAR.** Not consulted anywhere in the debris code;
  moot while there is no splash.
- **A water-flagged effect never smokes: MATCHES** (the splash returns before the smoke
  in `computeWeaponImpactEffects`).

## 5. Whole detached model pieces

**Reference (sections 5, 6, 7).** `StartExplodePiece` hides the piece (clears its visible
bit) **before** it allocates anything, then either breaks it into shards (flag b2) or takes
a detached-piece slot. The flag meanings the consumers establish are **b0 thrust-like
particles, b1 white smoke, b2 shatter, b3 gravity, b4 terminal explosion or splash**, and
shatter's own b5 impact-effect switch. The per-tick step:

- decrement the lifetime and finish when the **old** value was zero (a lifetime of N permits
  N updates before the following zero test ends it);
- **a position at or below sea level finishes at once** (with a splash first if b4 and the
  surface triggers are enabled), before any terrain query;
- above sea, **predict the next Y against the mean terrain height; on a collision reverse and
  halve the vertical velocity and halve both horizontal velocities; if the rebound is below two
  world units a tick the piece ends** (with an ordinary explosion if b4), otherwise it moves
  with the modified velocity;
- accumulate the three angular velocities; **apply gravity only with b3**;
- no restoring of the old position (unlike a shard) and no "whole velocity not positive"
  rule.

The register (`TOTALA-EXE-WRECKS.md`, "What SHATTER does") has the throw (three spins below
3000, a sideways `(20 - rand(40)) << 14`, an upward `rand(10) << 16`), the lifetime of 900 and
the flag folding, and says "the whole-piece path keeps its old numbers and RWE's own
buildings-only gate".

**RWE.** `spawnDebris` and `updateDebris` (`GameScene_commands.cpp` about 1039-1253):

- **Buildings only: DIVERGENCE (documented).** A mobile unit's pieces never fly as whole
  pieces (about 1090). Recorded in the register's 2026-09-25 note.
- **Throw and lifetime: DIVERGENCE (documented as "old numbers").** Sideways -2.5 to 2.5,
  upward 3 to 7, spin -0.3 to 0.3, lifetime 60 to 120 ticks, all from `effectsRng`
  (about 1092-1095). The register's figures are sideways about -5 to +5 per tick, upward 0 to 9 per
  tick, spin up to 3000 angle units a tick and a lifetime of 900.
- **Gravity: DIFFERS.** `updateDebris` subtracts a constant 0.3 a tick from every whole piece
  (about 1175) regardless of flags and of the map. The original applies the map gravity
  (112/900 = 0.124 a tick on the usual map) and **only with b3** (the COB `FALL` flag); a
  piece thrown without `FALL` coasts in a straight line. 0.3 is also 2.4 times the shard
  figure, so a whole piece and a shard fall at different rates in RWE.
- **Landing: DIFFERS.** RWE ends the piece the first tick its position is at or below the
  terrain (about 1226-1245), with an `Explode2` sprite on `EXPLODE_ON_HIT`. The original
  bounces it (halving the vertical and both horizontal velocities) and ends it only when the
  rebound falls under two units a tick. A thrown piece in TA therefore skips and rolls; in
  RWE it lands dead.
- **Sea: DIFFERS.** RWE tests the terrain height only. A piece thrown over water falls to the
  seabed. The original finishes the piece the moment it is at or below sea level. For a
  building on a coast or a floater this is the difference between a piece that vanishes at
  the surface and one that sinks out of sight in the draw.
- **Particles from a flying piece: DIFFERS in cadence.** The chapter's piece emits white
  smoke (b1) and "jittered thrust particles" (b0) **during the draw call**, so the cadence
  follows how often the draw service is invoked; RWE emits a smoke or fire puff every three
  ticks from the update (about 1181-1195), a hand-tuned stand-in. Presentation.
- **Hide before allocation: DIFFERS.** The original hides the piece even when the slot
  allocation fails; RWE hides the piece in the simulation (`sim/cob.cpp` about 235-238) and
  spawns the debris from the event, so a skipped spawn (`isMobile`) still leaves the piece
  hidden. Same visible result, so moot, and `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 7
  already records it as UNCLEAR.
- **Detached-piece draw rules** (face 0 skipped whenever `firstFace` is nonnegative, the 2000
  projected-vertex and 25-point local arrays, the owner-colour texture selection):
  original limits with no counterpart; RWE draws the piece's mesh directly.

## 6. Breaking a piece into face shards

**Reference (section 8).** `BreakPieceIntoDebris` takes the piece's transformed vertices,
a thickness scale (zero becomes one), and for each textured, non-solid **quad** that is not
the descriptor's `firstFace` face: counts an effect, places it at the piece position plus the
unit's, allocates a shard object (failing partway leaves a counted, half-initialised record),
sets the inherited drift to **half the unit's current velocity** (not zeroed when the unit has
no motion, so a reused record's old value shows), draws the velocity components from a
distribution "approximately centred on zero" with the gravity times 30 added to the vertical
component, a spin independently in -799 to 800, clears both animation references and sets b5
as the impact-effect flag. The face's four vertices are copied forward and reversed to make
**eight corners**; **a face normal is derived by dividing by 65535** (not 65536); **random
normal-directed X and Z impulses are added to the velocity**; the back four corners are
**extruded along the normal by the thickness scale** and all eight are centred by their
average (the world position is not translated by the centroid). The resulting six-face box
gets the source texture, flags and colour on every face, and an owner-colour animated texture
resolves to the owner's frame.

**Register.** `TOTALA-EXE-WRECKS.md` records the four-corner filter, the selection-plate
exclusion, the half velocity, the scatter `(80 - rand(160)) << 9` in each axis, the thirty
ticks of gravity, the spin `800 - rand(1600)`, and the pool step. It does not mention the
extrusion, the normal divisor or normal-directed impulses.

**RWE.**

- **Selection, velocity, spin, gravity add, pool step: MATCHES the register**
  (`fragmentSourcesFrom3do` in `mesh_util.cpp` about 305-357 keeps textured quads;
  `throwShatterFragment`, `GameScene_util.cpp` about 2018, is the throw;
  `stepShatterFragment`, about 2037, is the step). The scatter is `(80 - rand(160)) / 128`
  per axis, which is `<< 9` in 16.16; the spin is `(800 - rand(1600))` angle units; the
  rebound ends the fragment when its whole vertical velocity is below one (the register's
  "integer part no longer positive"); the thirty ticks of gravity are added to the vertical
  velocity; and half the unit's per-tick velocity is carried.
- **Shape: DIFFERS (new).** A fragment is the **flat quad** (`sources[i].mesh`) thrown from
  the quad's centre; there is no eight-corner box, no extrusion and no thickness scale.
  Viewed from the side the original's shard has depth, and the six faces mean no hole is seen
  through the back; RWE's is a one-sided plane that vanishes edge-on. Presentation.
- **Normal-directed impulses: DIFFERS (new), and an open CONFLICT.** The chapter adds a
  random impulse along the face normal's X and Z on top of the scatter; the register's
  scatter has no normal term. Either the register's decode stopped at the scatter or the
  chapter's description of the second step is richer than the instructions. Neither chosen.
  The effect is a bias of the shards outwards in the direction the face looks.
- **Unzeroed inherited drift, partly initialised records, the 65535 divisor: not
  applicable.** Faults of the original's record reuse; RWE builds each fragment fresh. The
  65535-versus-65536 difference would be a part in 65000 of the normal and is invisible.
- **Centring and the world position:** the chapter says the world position is *not*
  translated by the eight-corner centroid; RWE places the fragment at the quad's centre
  relative to the piece (`piecePosition + (turn * sources[i].centre)`, about 1155), so the
  fragment starts at the quad rather than at the piece origin with the quad offset inside.
  **UNCLEAR** whether those are the same place; presentation.
- **Owner colour on the shard:** RWE's shard mesh keeps the piece's texture
  mapping and the owner colour via `d.color` (about 1157); the chapter's owner-frame
  resolution is the same idea. **MATCHES in effect.**
- **Any unit shatters: MATCHES** (comment at about 1073 cites `0x481140`).

## 7. Exploding a whole unit

**Reference (section 9).** `ExplodeUnitPieces` (`0x420e50`) iterates **every** model piece and
builds a request with lifetime 900, scale one, b4 and b5 zero, a random smoke bit, **shatter
(b2) and gravity (b3) set**, and chosen velocities. Because b2 redirects immediately to the
face breaker, the whole-piece lifetime, velocities and smoke bit are never used: every piece
of the unit shatters with the breaker's own motion.

**RWE: UNCLEAR.** The only source of piece explosions is the COB `explode` opcode
(`sim/cob.cpp` about 216-238, the only producer of `PieceExplodedEvent`), which carries
per-piece flags from the script. There is no engine helper that shatters every piece. The
chapter does not say who calls `ExplodeUnitPieces` (it belongs to the unit chapters), so
whether a unit whose script has no `Killed` function, or a unit of some death type, goes
through it in the original is unknown. The register does not mention the routine. **Needs a
spec-room read of the callers of `0x420e50`.** If the original shatters every piece of a unit
on some deaths, RWE's units in that case show no shards at all.

## 8. Feature lookup beside the effects

**Reference (section 10).** `FindFeatureAtPos` converts the position to a 16-unit cell,
returns `0xffff` outside the map, resolves a continuation id `0xfffe` through the origin
offsets, treats any id at or above `0xfffb` as none, and returns the feature definition index
with the origin cell and footprint on request. It does not test reclaimability, visibility,
resurrection or resource amounts; those belong to the order handlers.

**RWE: MATCHES in effect.** Features live in the occupied grid with a reference to the origin
(`OccupiedGrid`, `tryGetFeature`); the reclaim and resurrect audits
(`BT-ORDERS-UNIT-ORDERS-PARITY.md` rules 13 and 14) carry the order-side conditions. The
sentinel range is not reproduced because the representation does not use sentinels. The
unchecked-index fault the chapter notes (the resolved continuation id is checked against the
sentinel range but not against the feature count) has no counterpart.

## 9. Visibility and the viewport

**Reference (section 3).** `DrawExplosions` includes an effect by the projected viewport
rectangle only and "does not perform a separate per-player line-of-sight test in this
function". `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 7 adds that `ExplodePiece`, unlike
`EmitSfx`, does not return early for an unseen unit.

**Register.** The fog-effects work (`fogeffects.test.cpp`, `TOTALA-EXE-VISION.md` "S:16") says
the original refuses every effect for a place the local player cannot see before it works
anything else out (`0x480EEA`).

**RWE.** Every effect is gated on `positionIsVisibleToLocalPlayer`: weapon impacts, piece
explosions (own units exempt, `spawnDebris` about 1045), projectiles drawn, deaths. So the
register's gate is applied to piece explosions too. The chapter's statements are about the
body of `DrawExplosions` and `ExplodePiece` and do not contradict a gate at `0x480EEA` placed
earlier on the call path; **UNCLEAR**, not a gap. It is recorded here because a reader of the
script-calls audit could take it as one.

**The separate debris phase (BT-1).** The original steps debris in the tick (after the
projectile pass and before the players, per BT-1's resolution at `0x420F30`). RWE steps debris
once per frame in the scene. Because the debris has no simulation effect, the phase order is
unobservable; recorded so that nobody ports a debris phase into `GameSimulation::tick`.

---

## 10. Gaps to fix

Ranked by player-visible impact times confidence. Every item is presentation; an "original
check" below is a screenshot comparison, which the project leaves to the user.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | The three-puff explosion smoke is keyed on `endsmoke` (one shipped weapon); the chapter keys it on every ordinary explosion above sea. Rule 3 | `game/GameScene_util.cpp` (`computeWeaponImpactEffects`), `game/GameScene_audio.cpp` (`spawnWeaponImpactExplosion`), `game/fogeffects.test.cpp`, and the other `spawnExplosion` callers | **Yes.** One shell impact over dry ground in TA; does a smoke trail follow it? Visible in every fight if the chapter is right. |
| 2 | Lava worlds splash water: the lava explosion pair is never loaded, 44 of 272 maps. Rule 4 | `LoadingScene_util.cpp`, `game/WeaponMediaInfo.h`, `GameSimulationLoader.cpp` (about 423), `GameScene_util.cpp` | No: the choice at load is in both the chapter and the register's data tables. A lava map screenshot confirms. |
| 3 | Whole pieces fall at 0.3 a tick unconditionally, land dead, ignore the sea, and are buildings only; the original honours `FALL`, bounces, finishes at the sea and flies for every unit. Rule 5 | `game/GameScene_commands.cpp` (`spawnDebris`, `updateDebris`), `game/GameScene.h` (`Debris`) | **Yes** for the numbers (the throw is in the register; the bounce is only the chapter); the buildings-only gate is documented. |
| 4 | Shard shape: a flat quad, no box, no normal impulses. Rule 6 | `game/mesh_util.cpp` (`fragmentSourcesFrom3do`), `GameScene_util.cpp` (`throwShatterFragment`), `GameScene_commands.cpp` | Static first: does the register's decode of `0x421700` have the normal term? |
| 5 | The 300-entry effect pool is shared between sprites and shards in the original; RWE caps shards alone and leaves sprites uncapped. Rule 1 | `game/GameScene_commands.cpp` (`spawnShatterFragments`), `game/GameScene.h` | No for the shard rule (register); a cap on `particles` is a performance question rather than a fidelity one. |
| 6 | `ExplodeUnitPieces` (shatter every piece) has no counterpart and nobody knows its callers. Rule 7 | none until answered | Spec-room read of the callers of `0x420e50`. |
| 7 | The three procedural explosion sequences and the lens frame have no counterpart. Rule 2 | none until answered | Spec-room read of the callers of `AddExplosionEffect` with a nonnegative index. |

### Documented divergences this audit leaves alone

Whole pieces for buildings only, with RWE's own throw numbers; the missing splash for a shard
in the sea; presentation randomness from `effectsRng`; the 900-tick backstop on a shard
(the original has no clock); debris stepped in the scene rather than in the tick; a missing
projectile or piece model drawn as nothing.

### Conflicts with existing RWE findings, in one place

1. **What triggers the explosion smoke.** Register (`TOTALA-EXE.md` section 91 entry, #113):
   "an impact that smokes gets three puffs"; the port keys it on `endsmoke`. Chapter: every
   ordinary explosion effect above sea level requests it. Open until a screenshot.
2. **Normal-directed shard impulses.** Register: scatter only. Chapter: scatter plus a random
   impulse along the face normal's X and Z. Open.
3. **`ExplodePiece` and visibility.** `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 7: no early
   return for an unseen unit. RWE and the register's `0x480EEA` reading: every effect is
   refused for a place the local player cannot see. Probably compatible (two different
   places on the path); recorded as wording.
4. **A stale line in the register** (not a reference conflict):
   `TOTALA-EXE-WRECKS.md`'s 2026-09-25 note says shatter "applies to any unit" and the
   whole-piece path "keeps its old numbers"; the old numbers are listed nowhere, and this
   audit lists them from `spawnDebris` (rule 5).
