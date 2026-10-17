# Particle pools, emission, wakes, nanolathes, smoke and visibility parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md`
and `NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers
one chapter of the reference, `graphics-particles` (module group `graphics/particles`, 127 entries
from `0x470a40` to `0x476710`): the fixed emitter pool, the ten presentation lists, and the six
emitter families (teleport, nanolathe, thrust, wake and bubbles, smoke, and the persistent
"TimedSub" emitter), with the visibility and projection rules their drawing shares. This is the
finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

Live projectiles, debris and explosion sprites are separate systems and not covered. **Everything
here is presentation**: the register states that the original's effects are not simulation
(`TOTALA-EXE-DATA.md`, "The steam is engine particles"), RWE keeps them render-side with their
own random generator (`effectsRng`, or `std::rand`) and never in the hash, and none of the
differences below can desynchronise a game. They can change what a player sees.

Most of the families are already decoded in the register (`TOTALA-EXE-RENDER.md` sections 4 and 5;
`-DATA.md` "What a vent is"; `-ECONOMY.md` "The spray"; `-WRECKS.md`), and RWE implements them with
the original's constants. This audit cross-checks the chapter against those, finds two places where
the chapter and the register disagree in a number, one in RWE's code, and several the register had
not examined.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-particles.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links were
  followed. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; no row concerns
  particles.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.**
- **The RWE half** is a read-only audit of `src/rwe/game/GameScene_commands.cpp` (smoke, vent steam,
  nano spray, wake and thrust emitters about 2006-2100 and 3264-3560), `GameScene_util.cpp`
  (`drawNanoParticle` about 1207, `updateWakeDots` about 1465), `GameScene_render.cpp` (the particle
  passes about 1600-1670 and 1995-2115), `Particle.h` and `Particle.cpp` at the working tree of
  2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in the tree,
  so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Lifetimes and the expiry comparison
3. The pool, the lists and their caps
4. Ten lists, update order and layers
5. Visibility and projection
6. Teleport streams
7. Nanolathe and reverse nanolathe
8. Thrust streams
9. Wakes and bubbles
10. Ordinary smoke
11. The persistent emitter (a thermal vent's steam)
12. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register | Verdict |
| --- | --- | --- |
| 1, 2 emitters, pool of 1000 | none | rules 2, 3 |
| 3 lists, 400 cap, update order | `-DATA.md` "While decoding" (400-entry cap, `0x472CD9`) | CONFLICT on 400 versus 401 (rule 3) |
| 4 visibility, projection, layers | `-RENDER.md` section 5 | agrees on the ten buckets; rule 4, 5 |
| 5 teleport | none | rule 6 |
| 6 nanolathe | `-ECONOMY.md` "The spray" | agrees; one RWE factor of two (rule 7) |
| 7 thrust | `-RENDER.md` section 4 (VTOL, the Atlas exhaust) | agrees; one emitter has no known caller (rule 8) |
| 8 wakes, bubbles | `-RENDER.md` section 4 | agrees |
| 9 smoke | `-RENDER.md` section 4 | agrees; one range stated differently (rule 10) |
| 10 TimedSub | `-DATA.md` "The steam is engine particles" (it is the vent emitter) | agrees; the fog gate differs (rule 11) |
| 11 containers | none | not applicable |

## 2. Lifetimes and the expiry comparison

**Reference (section 1, then each family).** An emitter's lifetime bounds only its **emission**;
individual particles expire on their own deadline. Init emits once before the system enters its list,
so even a lifetime of zero gives one particle. The expiry tests are written as *keep while now is not
past the end*: teleport `now > end` expires, nano `now > end`, thrust `now > end`, wake keeps only
`now <= end` **and** ground height under the particle strictly below the sea.

**Register.** `-RENDER.md` section 4: one `emit-sfx` is one puff, because a zero lifetime emits once and
the next emission is refused past the end; "the countdown starts at the frame period".

**RWE: DIFFERS by at most one tick, UNCLEAR as to the step order.** `updateWakeDots` drops a dot when
`currentTime >= finishTime` (`GameScene_util.cpp` about 1472) and `drawNanoParticle` stops drawing at
`currentTime >= finishTime` (about 1215). The original's tests keep the particle through the tick
equal to its end. For a nano particle that is one more tick on the target; for a wake dot it is the
tick on which it shows the **last** colour of its ramp (the `WakeDot` comment in `Particle.h` says the
dot "reaches the deepest blue on the tick it dies", which is exactly the tick RWE no longer draws). Whether
the original steps before it tests, which would move this the other way, is not stated by either
source. One thirtieth of a second; low rank, recorded so a conformance replay does not chase it.

## 3. The pool, the lists and their caps

**Reference (sections 2, 3).** The emitter pool is **1000 slots of 0x4c bytes**; it never grows by
itself; allocating at capacity returns null, and every creation wrapper then **silently omits the
effect** with no rollback of anything else; a console command (`SFX`) blocks all new allocations and
leaves existing emitters running. Each of the ten lists **evicts its oldest system only when its size
is greater than 400, then appends**, so a busy list reaches **401**. Indices are trusted. The 1000-slot
pool can run out across lists before any list reaches its threshold.

**Register.** `-DATA.md` ("The steam is engine particles") says "each of the ten layers of §5 holds at most
400 entries and drops its oldest to make room (`0x472CD9`)".

- **CONFLICT: 400 or 401.** The register says at most 400; the chapter's wording (evict when
  *greater than* 400, then add) gives 401 as the steady maximum. Neither quotes the comparison
  instruction. No observable difference at that scale; listed in `BT-NEW-CONFLICTS.md` for completeness.
- **RWE: MISSING (no pool, no cap), presentation only.** RWE's `particles` vector, `wakeDots` and
  `smokeEmitters` are unbounded (wake dots alone reach "ninety thousand" in the `WakeDot` comment, with a
  per-frame cull). So the original's practical limit, in a 200-unit fight a thousand emitters and then
  **no new smoke, spray or wake at all** until old ones die, does not exist here; RWE shows every
  effect. Not a defect, and the likelier player complaint runs the other way, but a replay-matching
  claim ("the screen looks like the original's in a big fight") cannot hold. The `+SFX` command is not
  built (`-COMMANDS.md` lists it as an unbuilt option command).

## 4. Ten lists, update order and layers

**Reference (section 3, 4).** The update visits lists in index order, tests **finished before
update**, and deletes a finished system; for teleport, nano, thrust and wake "finished" means an empty
vector, smoke needs an empty vector and `now` strictly past the deadline, TimedSub never finishes. Most
expiry loops erase a record without assigning the returned iterator and so **revisit the shifted
record**; nano assigns it. Drawing never advances time. The battle frame draws lists 0, 1, 2 before low
features, 3 and 4 after, 5 inside the row pass, then 6, 7, 8, 9 around the airborne units and labels.
Observed call sites: **layer 2 wakes, 6 construction and reclaim nanos, 7 scripted thrust and bubbles,
9 weapon and smoke work**; "teleport and feature callers use other layers".

**Register.** `-RENDER.md` section 5 agrees on wakes (2), VTOL and thrust (7), damage smoke (9) and the
"airborne pass after layer 7" explanation of the Atlas exhaust, and puts "weapon impacts on 6".
`-DATA.md` gives the vent steam layer 4 and burning-feature smoke layer 5.

- **Update order, finish rule, iterator revisit: not applicable.** RWE steps by age. The revisit
  quirk skips no particle in effect (the chapter itself says it is not a skip-every-other rule).
- **Layer 6 for nanos: UNCLEAR** (the register names 6 for impacts only; both may be true).
- **Layers 4 and 5 (vent steam, feature smoke): DIFFERS, presentation.** These are drawn before the
  row pass in the original, so a unit or tall building in that row covers them; RWE draws every sprite
  particle over the finished frame. `-RENDER.md` section 5 endorses over-everything on evidence from layer
  9 only. The sentence in `-DATA.md` (near 248) saying RWE depth-tests particles is stale: `inWorld`
  is set by nothing.
- **Layer 2 (wakes): MATCHES in effect.** RWE draws the wake batch right after the flat features and
  before the unit passes (`GameScene_render.cpp` about 1632-1662), so ships cover their own wake.
- **Layer 6/7 interleaving with the airborne pass**: RWE draws the nano spray with the depth test
  on, so the builder covers the part of the stream behind it (a documented choice, comments at about
  2022-2030 and in `spawnNanoParticles`); not the original's layering.

## 5. Visibility and projection

**Reference (section 4).** Teleport, nano, thrust, wake and ordinary smoke draw only where the
displayed player may see the **particle's own point**: whole X over 32 and (whole Z minus whole Y over 2)
over 32 on the information grid, out of bounds hides; with dynamic sight on, the current sight byte must be
nonzero, otherwise the player's bit in the permanent mask. It is a **point test at draw time**, not of the
sprite rectangle, and not of the source unit: a visible shooter does not keep a drifting particle visible.
**TimedSub drawing omits the gate.** Screen position: whole X - scrollX + 128, whole Z - Y/2 - scrollY + 32,
several helpers narrowing to a signed short. Sprite families use the blended GAF draw; **nano and wake draw
a solid 2 by 2 rectangle**.

**Register.** `-MOVEMENT.md` section 16 records one gate: `EmitSfx` asks the can-see predicate
`0x465AC0` about the **emitting unit** at `0x480EEA` and returns doing nothing if it fails, "so the whole
SFX system is client-side"; the fog-effects work (`fogeffects.test.cpp`) generalises it to "the original
refuses **every effect** for a place the local player cannot see". The address is in `EmitSfx` alone, so
the general claim is an extrapolation, which `BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 9 already marks
UNCLEAR for explosions. For a class not made by `EmitSfx` (the vent emitter, made once at feature
placement) the register's gate does not apply.

**RWE: DIFFERS, presentation only.** RWE gates at spawn on the emitting unit or point (wakes, thrust,
bubbles through `emitWakeFromPiece` and friends, which is `0x480EEA`; nano spray on the builder; smoke and
vent steam on the point) and draws what it spawned. The chapter adds a second,
independent gate on every particle's own position at draw time. Effects: (a) a puff or wake dot that
drifts into fog stays on screen in RWE and disappears in the original; (b) a nano stream from a
visible builder to a target standing in fog is whole in RWE and cut at the fog edge in the original;
(c) effects created by something other than a script, such as explosion smoke, are gated at spawn on their
point only. The drift distances are a few world units for smoke and wakes, so (a) is nearly invisible;
(b) needs the target in fog while the builder is lit, which is an allied or enemy structure and rare.
The solid 2 by 2 pixel draw for nano and wake is **MATCHES** (RWE draws a square of half-size 1 for
spray and the wake dot as a one-pixel-class quad; `Particle.h` "one screen pixel" for wake is the
register's reading, the chapter says 2 by 2 for both: **UNCLEAR**, the `WakeDot` comment and the chapter
disagree on size).

## 6. Teleport streams

**Reference (section 5).** `AddTeleportParticles` and `EmitTeleportParticles` create a system; Init
computes the full 3D displacement and a signed-short count of **distance over five world units**,
divides, then emits at once; one record per ten ticks; each record uses the `flameStreamSeq`
animation from a **random start frame**, a constant travel vector, expiry `now + count`, frame advance
modulo `frameCount - 1`. A distance under five units gives a count of zero and a **division by zero**;
a very large distance wraps the short.

**Register.** Not found. `-MISSIONS.md` and the interface's mission vocabulary list **"Teleporting"**
as a mission (`-INTERFACE.md` section 99), but nothing connects it to a particle.

**RWE: MISSING; no caller known.** No teleport order, particle or `flamestream` use for it exists. The
chapter names no caller either ("the particles illustrate a stream between points"). **UNCLEAR whether
any shipped unit can teleport.** A spec-room question: which callers reach `0x471340` and `0x471fd0`,
and does a shipped FBI or script ever do so.

## 7. Nanolathe and reverse nanolathe

**Reference (section 6).** Forward and reverse use one system; the emitter point is a degenerate source
box (both ends equal) with a one-tick emission lifetime. Init narrows each axis of each supplied box to
the interval from **4/11 to 7/11 of its extent**, a central **3/11**. Each callback makes **five**
independent attempts: random XYZ in both narrowed boxes, velocity target minus source divided by a
signed-short count of **distance over four world units**; a zero count is skipped; expiry `now + count`;
start colour `0xA1` to `0xA5` across the five; next emission `now + 1`. Step cycles the colour bits through
1..7; expiry is `now > end`; draw is a solid 2 by 2 rectangle after the visibility test.

**Register.** `-ECONOMY.md` "The spray": palette 161-167, one entry a tick and wrap, **4 world units a
tick**, a burst of five per tick with two bursts in flight (ten a tick), reclaiming a feature runs two
emitters, **the landing point is uniform in the middle three elevenths of the target's bounding box on
each axis**, a two-pixel square, a target nearer than one step gets none.

**RWE: MATCHES, with one probable error of a factor of two.** Constants, colours, speed, burst of
five, the two-emitter reclaim, the zero-step skip and the origin-free nozzle end all match
(`spawnNanoParticles`, `spawnNanoParticle`, `drawNanoParticle`). But `centralThird(-3/11, +3/11)` times the
target's full footprint extent (`GameScene_commands.cpp` about 3412 and 3431) samples a window **6/11 wide**
centred on the target, where "the middle three elevenths" (4/11 to 7/11) is **3/11 wide**. The spray fans
twice as wide as the original's. Footprint extent is also used where the original uses the model's own
bounding box, and the height is the roof, not a random point inside (documented). Presentation only.
The first and last tick of work also differ trivially: the original gives 5 particles on the first
tick and 5 more one tick after work ends, RWE 10 on every active tick.

## 8. Thrust streams

**Reference (section 7).** Two points, an animation period and a lifetime; Init sets the deadline and the
displacement scaled by the reciprocal of the lifetime (zero lifetime fails, zero period fails later); one
`flameStreamSeq` record per due update, expiry at the emitter deadline; the animation advances only when
the tick wraps the period, modulo `frameCount - 1`. The **jittered** emitter (`0x472ab0`) perturbs each
coordinate by -1, 0 or 1, lifetime 1 to 3, the same point at both ends and period 1: zero drift, the
animation and short expiry make the effect.

**Register.** `-RENDER.md` section 4, "`SFXTYPE_VTOL`": `flamestream` frames, drift
`(second vertex - first vertex) / 6` a tick, six-tick life; thrust the same with 7.

**RWE: MATCHES** for the two scripted types (`emitVtolFromPiece`, `GameScene_commands.cpp` about 3486).
The jittered emitter has **no caller in the register or the chapter**: UNCLEAR whether anything reaches it.

## 9. Wakes and bubbles

**Reference (section 8).** Wakes and bubbles are one class with lifetime 1; Init derives the displacement from
the two points; Emit adds one record, scatters each whole source coordinate by **-3 to +3**, sets expiry
`now + period x 6`, colour range `0x61` to `0x67` (ascending for a wake, descending for bubbles), steps the
colour every period; the record names `smoke 1` but **DrawParticle draws a coloured 2 by 2 rectangle**; the
next emission is `now + 1`, to the emitter's short deadline. Expiry keeps the record only while
`now <= end` **and the ground height at its position is strictly below the sea** (equal to the sea expires).

**Register and RWE: MATCHES.** The six steps of eight ticks, the ramp, the two dots a call, the scatter, the
bubbles being the wake class run backwards, and the layer-2 draw are in `-RENDER.md` section 4 and implemented
(`emitWakeFromPiece`, `emitBubblesFromPiece`, `updateWakeDots` including the "ground comes up to sea level"
death at about 1474). The chapter's wording "the test is terrain height under the particle, not the particle's own
Y" is exactly RWE's. Size: see rule 5. Expiry by one tick: rule 2.

## 10. Ordinary smoke

**Reference (section 9).** Five wrappers; Init sets the deadline, picks `smoke 1` or `smoke 2`, takes the asset
count minus one, caps it by a nonzero frame limit, defaults the hold period to 7, emits at once. Emit makes one
record with timer = hold period, count 0 and a random limit starting at 2 from `maxFrame - 2`. Step adds wind
times **8** to X and Z and gravity times **4** to Y. When the timer reaches zero the animation advances and the
timer reloads with a random value "from **half the hold period through approximately the full period minus
one**". Expiry: count reaching the limit. Finished: empty and the deadline strictly before now.

**Register.** `-RENDER.md` section 4: the same wrappers and arguments, wind x 8, gravity x 4; stopping frame
`2 + rand x (frameCount - 3) / 0x8000`; reload `half + rand x half / 0x8000`, "**3 to 5 ticks per frame after
the first, which gets the full 7**". `smoke 1` has twelve frames, `smoke 2` sixteen. `0x472720` is unreferenced
("dead in this build"), as the chapter's own table implies.

- **CONFLICT in the reload range.** For a hold period of 7 the register's formula gives **3 to 5**; the
  chapter's wording ("through approximately the full period minus one") gives up to **6**. The register is the
  explicit arithmetic and the chapter the loose description; neither is quoted. Listed in `BT-NEW-CONFLICTS.md`.
- **RWE: MATCHES the register.** `makeSmokePuffFrameSchedule` (`Particle.cpp` about 45-72) rolls `2 +
  rand(frames - 3)` and holds the first frame the full period, later frames `half + rand(half)`; wind and the
  lift of 0.5 per tick (four times gravity at the common 112) are applied (`spawnSmokePuff`, about 3264-3295,
  with the lift passed in rather than read from the map's gravity, a documented simplification for the
  handful of maps with another value). Explosion and wreck emitters, `startSmokeEmitter`, are the same class
  with the register's interval and lifetime. Zero emission period or hold period (division by zero in the
  original) cannot arise from the callers.

## 11. The persistent emitter (a thermal vent's steam)

**Reference (section 10).** `TimedSubParticles` is a smoke class that **never finishes**: emission period 5, the
150 it is given is not a lifetime, vertical drift **gravity x 16**, the same random frame limit and hold scheme,
and its draw uses blended `smoke 1` **without the visibility gate**. The chapter leaves its callers open.

**Register.** `-DATA.md` identifies the class: it is the emitter the engine builds when a geothermal feature is
placed (layer 4, `0x472C50`, vtable `0x4FD638`), immortal, 5-tick interval, drift x 16.

**RWE: MATCHES the emitter, DIFFERS on the fog.** `spawnGeoVentSteam` (about 3297-3316) puffs every 5 ticks at
a rise of 2 per tick (the original's gravity x 16 on the usual 112), with the common wind drift. But it begins `if
(!positionIsVisibleToLocalPlayer(...)) continue` with the comment "a vent under the fog of war keeps its steam
to itself". The chapter says the original's draw has no visibility test for this class, and nothing in the
register gates it either (it is made at feature placement, not through `EmitSfx`, so `0x480EEA` does not reach
it); the fog layer is drawn afterwards over the lists, so in **explored but not currently visible** ground the steam is still drawn, dimmed
by the fog frames, and in unexplored ground it is hidden by the opaque fill. So in the original a remembered
geothermal vent keeps steaming on the screen; in RWE it stops. That is a small piece of map information the
original gives a player (where the vents are, long after you passed). It is presentation only.

## 12. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Vent steam is hidden in remembered ground**; the original draws it, dimmed by the fog, because this class has no draw-time visibility test. | DIFFERS | code: spawn regardless of current sight, gate on explored only; check the dimming against the fog layer |
| 2 | **The nano spray fans twice as wide as the original's**: `centralThird` is +-3/11 of the footprint, the window is 4/11 to 7/11 (3/11 wide). | DIFFERS (probable) | code: halve the constants; compare a build with a screenshot |
| 3 | **No per-particle fog test**, so drifting smoke, wakes and the far end of a spray stay visible in fog. | DIFFERS | code: a draw-time point test with the existing predicate |
| 4 | **Vent steam and feature smoke (layers 4, 5) draw over units and tall buildings.** | DIFFERS | decision |
| 5 | **No pool and no list cap**, so a large fight never loses effects as the original does. | MISSING (deliberate-looking) | decision; document in `compatibility.md` |
| 6 | **Expiry is one tick earlier** for wake dots and nano particles. | DIFFERS, UNCLEAR step order | none unless a conformance replay needs it |
| 7 | **Teleport and jittered-thrust emitters** have no known caller. | UNCLEAR | spec-room question: who calls `0x471340`, `0x471fd0`, `0x472ab0` |
| 8 | **Wake dot size**: the chapter says a 2 by 2 solid rectangle; `Particle.h` says one screen pixel. | UNCLEAR | spec-room read of the wake draw (`0x4745e0`) |
| 9 | **400 or 401** and **3-5 or 3-6**: the chapter and the register state two numbers differently. | CONFLICT in text | spec-room read of the two comparisons |
| 10 | **Stale text** in `-DATA.md` near 248 (RWE depth-tests particles). | documentation | fix the sentence |

No source file was changed.
