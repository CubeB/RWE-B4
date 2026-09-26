# Findings from another project's reading of the same binary

Everything else in this corpus was read out of `TotalA.exe` by this project.
This file is the exception, and exists so that the difference is never
invisible: it records what has been taken from the **Nanolathe** project's
independent analysis of the same executable, what state of verification each
item is in, and what the comparison has already corrected on both sides.

- Project: `nanolathe-gg/nanolathe` — a Total Annihilation engine
  reimplementation in Go.
- Licence: MIT. Their research prose is theirs; nothing here is copied from
  it. What is recorded is the *facts about the binary* their analysis states,
  written in this corpus's own words.
- Their research lives under `research/retail-executable-spec/` in eight
  category documents, with `research/formats/` for file layouts.
- Read on 2026-09-24.

## Why the two are comparable at all

**Because it is the same file.** They record the binary they analysed by
hash, and it is ours exactly:

| | |
|---|---|
| MD5 | `8e74a1dffa1f5988624c52048f5b20cd` |
| SHA-1 | `764dc919c3bd0365751aefba8e9a667299a3ce2e` |

That was checked before anything here was written, against the copy on this
machine as well as against the hash recorded at the top of
[`TOTALA-EXE.md`](TOTALA-EXE.md). Had it been a different build — a v1.0, a
different v3.1 release — the constants would describe a different program and
none of this would transfer. It is worth re-checking if their corpus ever
moves to another binary.

## The two readings are complementary, not redundant

They are deliberately **address-free**: their writing rules forbid executable
addresses, decompiler names, offsets-as-layout and register narration, on the
grounds that a behaviour which cannot be described without an address is not
yet understood. Ours is the opposite — a finding here is usually anchored to
the routine that implements it, because that is what makes it checkable and
re-checkable.

So the corpora fail in opposite directions, which is exactly what makes the
comparison worth doing:

- **They cover subjects we never opened** — the tick phase order, the second
  random stream, the save container, the computer player.
- **We cover arithmetic they state more loosely**, and we can settle a
  disagreement by disassembling, which they have ruled out of their own
  documents.

Their evidence markers are **Established**, **Supported inference** and
**Unknown**, and every claim carries one. Those markers are reproduced
wherever a claim is recorded here; a Supported inference is a lead, not a
fact.

## The discipline for anything in this file

1. **Nothing here has been verified by this project** unless its own text says
   so. The corpus's standing rule applies with extra force: several plausible
   readings of this binary have turned out wrong, and a claim that has not
   been replayed against real data is not yet a finding.
2. **A claim graduates** by being checked here — against the binary, or by
   replaying its arithmetic against shipped data. When it does, it moves into
   the owning subject document as an ordinary numbered finding and leaves a
   pointer behind.
3. **A disagreement is a result.** Where their reading and ours conflict, one
   of the two is wrong, and the conflict is worth settling rather than
   smoothing over. The first one found is below, and it was ours that was
   wrong.

## Settled: the end-of-mission score divisor (ours was wrong)

`TOTALA-EXE-INTERFACE.md` §104 stated the outcome chart's score as

```
score = (int)(kills * killmul) + (int)((gameTicks / 30) * timemul)
```

and their reading gave the same shape with a divisor of **60**. Checked here
on 2026-09-24 and **they are right**:

- `0x41DDC4` loads `0x88888889`, `0x41DDCD` multiplies the tick count by it,
  `0x41DDD9` shifts the high dword right by five. That is the compiler's
  unsigned divide-by-constant idiom, and `2^37 / 0x88888889` is
  `59.99999998777639` — sixty.
- The quotient is then taken as a 64-bit integer (`fild QWORD`, the high dword
  having been zeroed at `0x41DDC9`), multiplied by `timemul`, and truncated by
  `__ftol`; the kills term is converted separately, as §104 already said.

§104 has been corrected in place. The shipped data sets `timemul` to 0, so
nothing observable changed — but it would have been wrong for any map that
sets it, and it is a fair illustration of why the cross-check was worth doing.

## The retail computer player

The largest single subject they cover that we had never opened. It has its own
file: [`TOTALA-EXE-AI.md`](TOTALA-EXE-AI.md). The headline is a negative —
the original's computer player has no transport policy, never attacks with
aircraft, and never issues guard or capture — which closes the question §103
recorded as untraced, and reframes a good deal of RWE's AI work as deliberate
divergence rather than an unclosed gap.

## The twelve-phase tick order

**Established (theirs).** We have nothing equivalent; our own determinism work
describes RWE's tick, not the original's. The global tick increments before
any phase runs, and the order is:

1. multiplayer frame and network drain (multiplayer only)
2. the per-unit sweep — players ascending, units ascending in pool order. Per
   unit: general update, then weapon update (reload, acquisition, aim latch,
   spawn), then a COB drain of **eight** threads in ascending order plus one
   piece-interpolation pass, then the primary order queue (head-blocking,
   restart-from-head), then the secondary queue (skip not-due, front to
   back), then movement integration, then slot-end death handling with a
   synchronous `Killed` query
3. projectile integration and collision — the active count is captured at
   entry, so a zero-burst root spawned in phase 2 moves and collides on the
   **same** tick while burst clones appended during iteration wait for the
   next; the pool compactor runs at this phase's tail
4. general effects and feature motion
5. per-player orders, path, economy and occupancy — players 0 to 9 ascending.
   Contains the AI coordinator, then the occupancy re-stamp, then the
   settlement deadline compare, then `tick + 30`, then the nine-step
   settlement pass
6. feature lifecycle, reclaim and death — **reclaim credit becomes visible at
   the next settlement, not this one**
7. sequence and effect-strip advancement
8. wind change
9. the meteor-shower scheduler
10. camera and scroll — the camera steps toward its target clamped at ±320 a
    tick, half-step when closer
11. ten object-list update sweeps, the effect strips
12. an every-eight-sub-tick cadence flip

With it, four visibility rules they mark Established:

- a unit created this tick is visible to later phases only if its player and
  slot lie **ahead** of the scan position; behind it, it waits for the next
  tick;
- free slots are reused immediately by lowest-free scan, with **no generation
  counter**;
- build completion publishes `GetBuilt` and the builder links before the next
  trigger poll, and **victory is polled every 30 ticks**, so it takes the next
  poll to see it;
- a kill sets a dying latch and defers pool clearing to slot-end, so a victim
  stays observable through the current tick's settlement.

## The two random streams

We already have the simulation stream, and ours is the sharper of the two:
[`TOTALA-EXE-DATA.md`](TOTALA-EXE-DATA.md) records it as Park–Miller with
`a = 16807`, `m = 2^31 − 1` by Schrage's method at `0x4B6C30`, the state at
`ds:0x51FC88`, seeded at `0x4B6CA0` by XOR with `0x66E29572` and forcing the
low bit — **including** the quirk that a bound below 2 returns 0 without
advancing the state, which we tie to the fortification walls coming out in a
straight line. Their reading agrees on all of it and adds the Schrage
constants explicitly (quotient 127,773, remainder 2,836) and that the bound
test is a **signed** compare, so every bound with its top bit set takes the
same early exit.

What is new to us is that there is a **second** stream, and which is which:

- **Established (theirs).** A per-thread Microsoft CRT stream,
  `state = state × 214013 + 2531011`, returning `(state >> 16) & 32767`,
  living in the thread's TLS block. Seeded at startup from time of day at
  about one-second resolution.
- **Battle entry seeds the loading thread's block, not the main thread's**,
  so every tick-side CRT draw continues from the process-start seed.
- The **widening sampler** used for CRT bounds above 32,767 takes exactly one
  draw whatever the bound, so above 32,767 the low fifteen bits are always all
  ones and only the top bits vary. A bound of 1 consumes its draw and yields
  0; a bound of 0 reaches the divide and faults.
- Census: 129 simulation draw sites in 59 functions, 61 CRT sites in 29.
- **Neither stream's state is saved.** A load reseeds the simulation stream
  and the loading-thread CRT, so there is no bit-identical continuation across
  a save, ever.

Which stream a behaviour draws from is the useful part: wind consumes **both**
(interval from CRT, speed and heading from the simulation stream), the meteor
shower only CRT, and the camera shake only CRT — two draws per active tick.

## Wind, in arithmetic

**Established (theirs).** We record at §111 that RWE has the wind ported and
that smoke still does not drift; we do not have the original's arithmetic.
Theirs:

- interval between changes: `((CRT × 10) / 0x8000 + 5) × 30` ticks;
- new speed `simRand(maxWind − minWind) + minWind`;
- new heading `simRand(0x10000)` truncated to 16 bits, drawn **only when the
  speed is nonzero**;
- the direction vector is −2 × the fixed-point trig of the heading, with the
  speed as magnitude;
- the published ratio is `speed / 5000`, clamped at exactly 1.0.

The briefing screen's wind is **display only** — it has no heading at all, and
the battle's initial wind is drawn by the wind-change routine at tick 1.

## A wall-clock leak into authoritative state

**Established (theirs), and the most interesting thing in their corpus.**

The engine keeps an animation counter `floor(GetTickCount() × 30 / 1000)` —
the rate word is 30, written once at boot and not configurable. We already
have that counter, at `0x4B6340`, but only in an interface context
([`TOTALA-EXE-INTERFACE.md`](TOTALA-EXE-INTERFACE.md), the outcome screen's
timings).

Their finding is that the same counter is read **inside the simulation**, in
the post-move terrain conform, in the hover-bob branch that runs for every
unit whose definition sets `canhover`. Its low five bits phase a per-corner
cosine offset of at most two height units, and the four perturbed corner
heights are averaged into the unit's **integer height word** — authoritative
state. Four samples per hovering unit per tick.

It crosses a real threshold rather than being cosmetic: the medium-band
classifier's band-2 test is an equality against sea level, and 10 of the 13
`canhover` definitions in the stock data author `waterline` 0, which puts them
exactly on it. Those units alternate between bands with elapsed real time.
The below-water half-speed branch and the water damage both exempt `canhover`,
so the band classifier is the entire exposure — and our own
[`TOTALA-EXE-MOVEMENT.md`](TOTALA-EXE-MOVEMENT.md) agrees about the exemption
(`0x43CCAE`), which is a small independent corroboration.

They state it is the only clock or non-tick input anywhere in the mover chain.

**For RWE this is a §88 divergence and not a gap.** A lockstep simulation
cannot read the wall clock and stay in step; CLAUDE.md's rule that nothing a
frame knows may reach the simulation is the same rule stated from our side.
We do not conform units to the ground at all yet, so there is nothing to
change today — but when terrain conforming is written, this is the part of the
original that must deliberately **not** be reproduced.

## The numeric contract

**Established (theirs).** Useful because it bounds how exactly a clone must
imitate the arithmetic:

- x87 throughout, no SSE path. The environment is 53-bit precision, round to
  nearest, exceptions masked — and it is **installed at startup**, not
  inherited from whatever the hardware defaulted to. **No game routine writes
  the control word.** So authoritative doubles can be computed at plain
  binary64 with no 80-bit emulation.
- Persistent world positions are **signed 16.16 fixed point** — which our own
  [`TOTALA-EXE-DATA.md`](TOTALA-EXE-DATA.md) independently records for unit
  position at `unit+0x6A/+0x6E/+0x72`, so the two agree.
- Double-to-float stores round at the store boundary.
- `__ftol` sets round-toward-zero for the store and restores the control word
  afterwards: **integer casts in the simulation truncate**, never floor and
  never round-to-nearest.
- A trap they flag explicitly: the scaled-clock factor (30) and the default
  x87 control word `0x27F` are different fields and are easy to conflate.

## The save container

**Established (theirs).** We have nothing on this; RWE's own save format is
its own design (`src/rwe/game/save_util.*`), so this is reference rather than
obligation. The shape of it:

- a `HAPIBANK` bank: 34-byte header, 32-byte account headers, typed items
  (int, double, string) plus named and numbered binary boxes, and a string
  pool. **No checksum anywhere in the format.**
- `SAVEGAME\<name>.SAV`, truncate-write, no temp file, no backup, no fsync;
  the write result and the delete result are both ignored by the interface. A
  name containing a dot is truncated at it.
- Load order is Players, Camera, Features, Metal, PlayerFeatures, Mapping,
  Units, Meteor, triggers — with **no transactional rollback**, so a
  short or wrong-sized box skips just that family and leaves the earlier
  mutations applied.
- The unit record is 184 bytes and includes an AI group index whose loader
  re-enrols the unit into that group.
- **The AI is not restored.** The world rebuild constructs a fresh AI record,
  with fresh draws, before the restoration dispatcher runs — so a loaded
  computer player restarts as at session entry. Unit *group membership*
  survives anyway, through that per-unit group index.
- Two consequences they record: a wreck saved mid-sinking resumes suspended
  and never descends further, and the alliance table saves only row A, so the
  mirror of other players' declarations is not restored.

## Victory, defeat and skirmish setup

**Established (theirs).** We have the rule evaluator and four condition names
at §105; they have eighteen conditions, and more usefully the *structure*:

- **Only a campaign session polls the trigger queues at all.** Skirmish and
  multiplayer build the triggers and fire the notification sites, but decide
  the game by separate predicates.
- **Skirmish defeat is the local live-unit count reaching zero**, not
  commander death as such. The commander-death rule (0, 1 or 2) decides
  whether that count gets there: 0 sweeps nothing, 1 sweeps every other unit
  to death on the commander's loss (30,000 damage over several ticks, not
  instantly), 2 does that and then runs a commander respawn.
- A condition that becomes true arms a countdown and only fires the ending
  latch about **150 ticks** later; a false poll in between neither resets nor
  advances it.
- Skirmish start placement shuffles eligible slots with the **CRT** stream,
  not the simulation stream. A missing `StartPos` is **fatal** in skirmish —
  a modal box and process exit — while multiplayer falls back to two
  simulation draws for an interior jitter.

## Six conflicts settled against the binary — all six against us

This is the return on the exercise, and it is larger than expected. Where the
two readings disagreed and the disassembly could decide it, **our corpus was
wrong every time**. Six findings corrected, one of them a live simulation bug
that had a passing test pinning the wrong number.

| # | Subject | What ours said | What it does | Checked at |
|---|---|---|---|---|
| 1 | End-of-mission score divisor | 30 | **60** | `0x88888889` / `shr 5` |
| 2 | Aircraft pitch input | the longitudinal component | **the same component as bank** | `0x43D1A0` / `0x43D1D5` |
| 3 | Accuracy veterancy divisor | `kills / 3` | **`(uint16)kills / 12`** | `0x49D6EA` |
| 4 | Ballistic pitch solver | `asin` | **`acos`** (our own §11 agreed) | `0x4E67F0` |
| 5 | Selection primitive | the exe drops a real face on wreck models | **the loader swaps the plate to index 0 first** | `0x4CB37B` |
| 6 | Backface culling | the exe has none | **an implicit winding cull** | `0x4C79DF` |

Items 5 and 6 were both recorded in §88 as deliberate divergences where RWE
does the better thing. Neither was a divergence: RWE already matched the
original in both cases, and §88 said otherwise for years. That is the failure
mode to take from this — **a negative result is the easiest kind of finding to
get wrong**, because "there is no test here" is what you conclude when you have
searched the routine you are reading and not the one that ran before it.
Item 5's swap is in the loader, not the renderer; item 6's cull is in a loop
bound, not a predicate.

Item 3 was a real defect in shipped code. `computeAccuracyCone` divided by
three, so a unit's aim tightened after six kills where the original wants
twenty-four, and `accuracy.test.cpp` pinned the wrong behaviour under the name
"kills tighten the cone, but only past six". Both are corrected, the count is
truncated to a word as the original truncates it, and the suite is green.

### How each was decided

**Score divisor (1).** `0x88888889` with `shr edx,5` is divide-by-60:
`2^37 / 0x88888889` = 60. A divide-by-30 would be the same magic at `shr 4`.

**Pitch (2).** `0x43D1A0` reads `[esp+0x8]` and scales by `BankScale`
(`def+0x1A2`); `0x43D1CE` pushes `edi`, so `esp` falls four; `0x43D1D5` reads
`[esp+0xc]` and scales by `PitchScale` (`def+0x1A6`). The displacement grew by
exactly what `esp` fell, so both reads name the same slot. Inert on shipped
content, where every `PitchScale` is 0, and wrong in a visible way the moment
pitch is ported — which CLAUDE.md lists as a thing RWE has not done.

**Accuracy divisor (3).** `mov eax,0x2AAAAAAB; imul edx; sar edx,1`. The `imul`
leaves the product's high half in `edx`, and the `sar` takes it to bit 33:
`715827883 / 2^33` = 1/12. A `/3` is `0x55555556` with no shift and a `/6` is
`0x2AAAAAAB` with no shift, so the shift is the whole difference. The count is
zero-extended from a word (`xor edx,edx; mov dx,[edi+0xb8]`), so it is
`(uint16)kills`. With the `if (vet > 1)` gate the first effect is at 24 kills.

**The solver (4).** `0x4E67F0` is `fld1; fadd` → `(1+x)`, `fld1; fsub` →
`(1−x)`, `fmulp`, `fsqrt` → `√(1−x²)`, `fxch; fpatan` → `atan2(√(1−x²), x)`.
That is `acos`, and the |x|=1 arms returning `fldz` and `fldpi` confirm it. Our
§11 had read it correctly and §12 had not; the two sections had been
contradicting each other in the same document.

**The selection plate (5).** `0x4CB370`, reached once per object from the
recursive relocation walk at `0x4CB552`, tests `[obj+0xc]` against the
all-ones sentinel and then does three eight-dword `rep movs` — a 32-byte
three-way exchange of `prims[selprim]` with `prims[0]` — before writing
`[obj+0xc] = 0`. Every object of every loaded model goes through it, so by draw
time index 0 *is* the declared plate.

**The cull (6).** `0x4C7580` builds two edge chains from the topmost vertex by
index direction and never compares them: backwards (`eax-1` wrapping to 3) into
span slot `+0x00`, forwards (`(ecx+1) & 3`) into `+0x04`. The emission loop is
`mov ecx,[edi+4]; mov edx,[edi]; sub ecx,edx; test ecx,ecx; jle`, so a row is
skipped whenever the right edge is not strictly right of the left. A quad wound
the other way on screen fails that on every row and paints nothing.

## Settled: aircraft pitch takes the same component as bank (ours was wrong)

`TOTALA-EXE-MOVEMENT.md` §1 said the original pitches an aircraft "from the
longitudinal component of the same accumulator", and that the longitudinal
component the rotation produces is never read. Settled above, item 2: pitch
takes the same component as bank. §1 has been corrected in place.

## Open conflict: the Atlas after a pickup

**Unsettled.** `TOTALA-EXE-TRANSPORTS.md` §36 describes `VTOL_Pickup` state 4
as installing a goal at the transport's own position at `cruisealt` — "climb
away loaded". Their reading says the climb-away marker is **built and never
installed**, so after a successful attach the aircraft simply holds the
phase-3 lowering marker on the cargo and stays at the cargo's altitude until
some later order moves it.

They mark it Established and give a code-flow argument, and note the identical
allocate-then-leak shape at `AirToGroundHover`'s miss-counter reroll — which,
if real, is a second place our own §6 may have read an install that is not
there. Worth settling at the state-4 arm of the pickup handler, because the
two readings look different in play: one climbs, the other hovers.

## Movement, orders and scripts

From their document 04, the parts we had nothing on. All **Established**
(theirs), none verified here.

**The terrain conform.** We have none of this — `TOTALA-EXE-MOVEMENT.md` §16
says only that a clear unit "samples four rotated footprint corners". Theirs:
the four corners are the first four vertices of the compiled model root's
**selection primitive**, not the footprint, rotated by heading and sampled
bilinearly against raw terrain height bytes; an index of −1 disables terrain
conform outright. The averaging is asymmetric and truncating —
`A = (h0+h1)/2`, `B = (h2+h3)/2`, `Y = (A+B)/2` — and pitch and roll are taken
from different corner pairs.

**The bug-walk, byte-exact.** Our §87 admits its own gap in as many words:
`BugWalk.cpp` "is written from the structure ... rather than transcribed: the
agent that decoded it could not make that section byte-exact." Their
`R-PATH-01 §15` supplies exactly that — two cursors rotating their probe
direction from the blocked direction, alternating one turn each, with the
sentinel, meet test and rejoin leg given in full. **This is the single most
directly actionable thing in their corpus for us**, because it closes a gap we
already know we have. *Closed 2026-09-25 (B4 #309), from our own reading of
`0x40E2AC`-`0x40E600`, which agrees with their shape: two tracers from the
blocked direction, turning opposite ways, alternating a step each; §87 has the
port.*

**Repair pads.** `TOTALA-EXE-MOVEMENT.md` §91 records this as the piece left:
what the pad does once an aircraft is on it "was not followed past the mission
push at `0x411ECE`, so RWE's pads still mend nothing". Their `R-AIR-01 §6`
gives the seven-phase `VTOL_Landing` pad-reservation machine — loiter at
weapon-0 range with 128 tolerance, approach 160, final 48, a 15-tick pad-hold
deadline, and a "free pad" predicate — and says **phase 6 is what pushes the
`SELFREPAIR` order**, gated on the lander being damaged, the pad's owner being
`isairbase` and `builder`, and the pad not being under construction. The
handler itself: phase 0 checks the repairer is a complete `builder` and the
patient is activated; phase 1 heals once per visit and draws the nanolathe
spray from the repairer's nano piece to the patient's box; phase 2 announces
and completes. That closes the open question.

**Cruise altitude is two-mode.** Beyond 160 world units from its goal, an
aircraft's commanded altitude is `cruisealt + sectorHeight`, where
`sectorHeight` is the **maximum terrain height over a 3×3 block of
128-world-unit sectors** centred on the aircraft, from a coarse grid
box-filtered at map load and re-sampled every tick. Inside 160 it descends to
the goal's own terrain height. Our §1 describes only the clamped vertical step
toward `goalY` and never says how `goalY` is chosen in transit — so this is a
first-order gap for long flights.

**Pathfinding detail we lack:** the first expansion tries all nine slots with
the reverse direction attempted both first and last (ours says a five-slot fan
after the first node); the terrain term is `(passability > 1) ? 0 : 30`, so the
+30 applies to blocked-but-ray-exempted cells as well as to "tight" ones; the
full eight-entry turn penalty is `0, 40, 60, 80, 100, 80, 60, 40`; route
reconstruction uses a 64-entry ring before the follower's 20-point cap; and the
route-acceptance gates both require the follower to already hold **three or
more** points, else it drops to the synthetic two-point line.

**Occupancy ageing:** the watermark is `max(tick, 30) − 30`, so a unit
committed at tick *c* only becomes a search-time wall at the first same-class
request at tick ≥ *c*+31 — and nothing blocks by occupant age at all for the
first 31 ticks of a battle.

**A mapping/LOS shear bug.** The path probe indexes the per-player explored
grid in flat ground coordinates while the LOS publisher writes it in a
height-sheared screen-tile frame. Ground at the foot of a south-rising slope
therefore reads permanently unexplored — and so passable — to the search, so a
unit ordered into it paths blind and jams, permanently, because nothing ever
sets the missing bit. They mark the mechanism Established and the visible
symptom unverified.

**COB, which we have never documented as a system.** Eight thread slots per
unit with a 32-word window each; opcode dispatch is a 57-value binary search
with **no default handler**, so an unrecognised opcode silently kills the
thread; there is **no modulo and no shift opcode**, so an implementation
offering either is inventing surface the original lacks; divide by zero is
unguarded and faults the process, as are the `/30` tick denominators and the
per-tick divisions by `TurnRate`, `BrakeRate` and `MaxVelocity`. The engine
port set is exactly 20 wide and closed. The piece-position pack puts **X in the
high half and Z in the low half by ADDITION, not OR**, so unpacking with a
plain shift is off by one world unit of X for any negative Z. `explode` draws
six values in a fixed order — three bounded 3000, then 40, 10, 40 — before any
shatter-path draws, and miscounting them desynchronises the simulation stream
from that tick on. `BUGGER_OFF` (port 19) is a write-only flag with **zero
engine readers**, confirmed by census — which is worth knowing beside our own
issue #190, where RWE's one-shot sweep is the whole of the behaviour.

Two corrections of community lore they record: `MotionControl` does not exist
in the binary at all, and neither do `StartUnload`, `RequestState`, `Demoted`,
`Promoted` or `Go` as engine-invoked names.

**Orders.** The queue is two segments, not one: only `BuildWeapon` and
`SelfDestruct` use the rear segment, which is pumped front-to-back with no
head-blocking. Our own return-code table in `TOTALA-EXE-MISSIONS.md` §2 is
independently confirmed almost entry for entry, including the `30 + rand(15)`
wait — a good sign for both corpora — and theirs adds that codes 6 and 7 mean
something different on the rear segment.

**Factory egress explained.** A finished ground product with no rally point is
given a `Park` order with a rectangle goal centred on its own cell, and walks
to the nearest border — which is why stock factories visibly disperse their
output with no rally set. The aircraft version becomes `VTOL_Move` to its own
position, which is where the stack of idle planes over an airfield comes from.
Neither is a bug.

## Rendering, visibility and the palettes

Their document 03 against our `-RENDER`, `-SHADING`, `-VISION` and `-MUSIC`.
Two of the six corrections above came from this comparison. What follows is the
rest: first the places it closes a question we had left open in writing, then
the new material.

### Three open questions of ours, answered

- **What table the shadow darkening uses.** `TOTALA-EXE-RENDER.md` §100 says
  in as many words that "the blit it reaches was not followed". Theirs: every
  model shadow and every cloak goes through one shared tinted blitter doing
  `dst = ALP[src*256 + dst]`, and a shadow's source pixel is always index 0,
  so it is `ALP[0*256 + ground]` — **the same `PALETTE.ALP` the anti-alias
  downscale uses**, with no separate table, no SHD, no stencil and no dither.
- **The anti-aliasing path.** §101 says outright that it "was **not** followed
  in the binary" and that ALP was identified by elimination. Theirs decodes
  it: gated on the `Anti_Alias` option, the structure class and the not-live
  pass, the model rasterizes at 2x into scratch and downsamples 2:1 through
  **exactly three ALP lookups per pixel** in a fixed row-first order
  (`top = ALP[s00,s01]; bottom = ALP[s10,s11]; dst = ALP[top,bottom]`), while
  the key plane downsamples nearest-sample with no blend. That is what RWE
  already ships, now confirmed rather than inferred. Two things fall out that
  we had got right for other reasons: the gate is the same `BMcode=0` bit the
  shading uses, so mobile units are never anti-aliased; and live pieces are
  excluded through the same cache-bit split RWE maps to `DONT_CACHE`.
- **The building shadow is a silhouette, not a rectangle.** §100 reads
  `0x45A790` as "a projected rectangle filled with a single palette index" and
  only gestures at cutting the building's shape out of it. Theirs: it is a
  **second complete re-rasterization** of the model's cached pieces through the
  flat filler at colour index 0, under a distinct **quarter-height** shear
  (`sx = hi16(vx)+q`, `sy = hi16(-vz)-q`, `q = hi16(vy)>>2`) rather than the
  body's half-height one, with the finished body then composited in to punch a
  transparent hole where it will sit, RLE-encoded and cached. The punch-out
  uses a +5 shift that the draw-time blit undoes, so the hole lands exactly
  under the body. This matches **RWE's actual implementation** far better than
  our own low-level trace of it does — §100's own prose describes a shear of
  `(y - groundHeight) x 0.25`, which is theirs and not the rectangle.

### The composite transparent index: 253 or 1

**Open, and we should probably concede it.** §101 identifies the composite
background key as palette index **253** by scoring the shipped `PALETTE.ALP`
for purpleness, and says so with its own caveat: "The score alone would not
settle this", with 252 close behind. Theirs gives **index 1**, as a literal
constant written at every image-allocation site, from three independent
places in their corpus.

Structural evidence of that kind beats a statistical argument about a palette,
so the presumption is theirs. It is load-bearing for the whole purple-fringe
mechanism, so it is worth re-deriving the fringe colours from index 1 — they
supply the worked values, `ALP[1][255]` to entry 20 (dusty pink),
`ALP[1][0]` and `ALP[1][240]` to entry 207, `ALP[1][208]` to entry 200 — and
comparing them against a rendered frame before §101 is rewritten. Left open
rather than corrected because unlike the six above it has not been checked
here.

### Visibility

The headline is corroboration, not correction. `TOTALA-EXE-VISION.md` §2's
terrain-occlusion formula — reveal at `max(seaLevel, (2*max + min)/3)`, occlude
at `max(seaLevel, (max + 2*min)/3)` — comes back **exactly**, fraction for
fraction, from an independent derivation. So does the radar formula
`R = min(RadarDistance + 2*floor(y), max(RadarDistance, SonarDistance))`
including the point our own bug history turns on, that the elevation bonus
applies inside the squared-distance test and never widens the search radius.
Two independent readings landing on the same thirds is the strongest evidence
either corpus has for anything.

New beside it:

- **How the occlusion table is built**, which §2 does not describe at all: a
  per-tile word written once at map load by scattering *two* values per
  attribute cell — a perspective-scaled term `((tileZ*32+31)*height)/(zs+31)`
  and the raw height — into up to two tile slots through the beam shear, with
  a carried pair from the previous row so a shear jump leaves no gap, and an
  odd-column collapse.
- **The decloak-suppression deadline is shared, and last-writer-wins.** §17
  presents the mincloak breach's `+90` as self-contained. The same word is
  written `tick+150` by repair, `tick+300` by build, reclaim and resurrect,
  `tick+600` by **every weapon shot**, and `tick+900` by capture — and there is
  no maximum taken, so a later short write can *shorten* a longer reveal
  already running. Firing keeps a unit visible twenty seconds; a proximity
  breach, three.
- **The mincloak proximity test is not live.** It walks the per-side primary
  candidate list, which can be up to thirty ticks stale — "an enemy I could see
  up to a second ago is within range".
- **`LOS.TDF`'s ninth table is dead data.** `numtables=9` clamps the reachable
  group to `TABLE(numtables-1)` through a one-based accessor, so `TABLE9`
  never loads, and nor do the three undeclared `TABLE10`-`TABLE12`. Our §2
  says "TABLE1..TABLE9" and "index capped at 8" without noticing that those
  two statements are in tension.
- **There is no allied vision sharing, and they name a candidate reason.** The
  sensor phase's ally disjunct tests a bit that a 26-site census shows is never
  written anywhere in the image, while the structurally parallel bit in the
  *adjacent* word is the defeated/observer flag, which is written. Whether that
  is a retail typo they leave Unknown; that no sharing happens they mark
  Established, which agrees with us.
- **A one-off underflow at tile (0,0).** Circular-LOS mode's first refresh for
  any unit decrements the byte grid at the origin with no guard, wrapping it to
  255 — permanently "visible" near the map origin until a bulk rebuild.
- `init_cloaked=1` cloaks a unit while it is still an unfinished nanoframe; the
  upkeep gate is not construction-gated.

`SHADING.md` §09 measures identity at rows **14 and 15** of `PALETTE.SHD`,
correcting an earlier "row 16" reading, with row 14 at a mean ratio of 0.9948.
Theirs groups row 14 into the darkening bucket and singles out only row 15. It
does not contradict us — theirs is a mean over fifteen rows, ours a per-row
measurement of the shipped file — and their independently measured "232 of 256
self" for row 15 matches ours exactly. Ours is the more direct measurement and
stands; worth a second look only if something depends on row 14.

### Effects, audio, music and the films

The `emit-sfx` type to strip to palette table in `RENDER.md` §4/§5 comes back
one for one across all six event families, including the layer-7 explanation
for why the Atlas's exhaust draws under the aircraft. Sharper: types 0 (VTOL)
and 1 (thrust) have **different** lifetimes, six ticks over seven sprites
against seven over eight, where §4 gives only "six-tick life".

New: `LHT` is fully decoded, where §54 calls it "partially traced only" — an
elliptical ground-brightening disc under every explosion, compressed by
sqrt(1.33) vertically and drawn after every unit pass through
`LHT[(discByte-0x4F)*256 + dst]`, measured at 1.140 against a predicted 1.153
on a rendered impact. Also new: the gray (fog desaturation) table's
construction, which is a windowed sum-sorted nearest match and **not** the
simple L1 search used for GUIPAL to display; and a note that the console
light-vector setter invalidates the whole model-image cache rather than
re-shading in place.

`MUSIC.md` §42-48 is the most thoroughly corroborated part of our corpus. The
registry layout, the exact 2720-byte `CDLISTS` record, the `(i%4)+1` fallback,
the seven-Battle/nine-Building real-disc default, all five mode formulas
including the situational scan's `(u+1)`-th-match budget, the fade timings and
the whole battle/peace evaluator with its 30-second ring and its thresholds
all come back with matching constants. So does the negative finding that the
Victory and Defeat categories are never requested by anything.

Two things it adds and one it disputes:

- **A cross-battle retention bug.** Battle start resets the activity ring and
  its index but **not** the chooser's retained last-requested category. A game
  that ended at Battle intensity leaves that stale, so the next game's opening
  Building request can be immediately re-evaluated to Battle, match the stale
  value, and never call `SetDesired` at all — the transition silently does not
  fire. They are explicit that a conformant chooser must not repair this.
- **The main menu has its own music**, a looping `BGM` alias ("drone2") through
  the mixer's single exclusive-loop voice, with the CD explicitly silenced.
  There is no CD-based menu music and no intense menu state. §42-48 covers only
  in-battle behaviour.
- **Disputed: when `CDLISTS` is written back.** §44 says on leaving the music
  settings screen (`0x490F80` at `0x49173D`); theirs says on disc eject and at
  shutdown, with the `TRACKTYPE` gadget writing only the in-memory object.
  Both call sites may exist. Unsettled, and cheap to settle.

Their audio and video sections have no counterpart in our corpus at all. Worth
having: the eight-slot voice-cue arbitration with its 30-frame audible window,
four-instance-per-sample cap and 32-voice mixer with oldest-non-loop stealing;
that **no `waveOut` backend exists** — DirectSound, `PlaySoundA` or silence,
with WinMM used only for volume and MCI; that `1.zrb`-`4.zrb` are natively
640x240 and reach 480 by an **alternate-scanline** mode that leaves the
intervening rows as whatever the cleared backbuffer held rather than
duplicating them, while `5.zrb` is 640x304 starting at y=88, all five at a
fixed 33.33 ms and advancing only while the window has focus; and that the
`MOVIE%03i` capture path is the only caller passing a zero render mode, so a
captured frame omits strips 6, 7 and 9, all projectiles, the effect pool,
selection quads, the airborne pass, health bars and labels — an ordinary
screenshot is not reduced that way.

## Weapons, damage and wrecks

Their document 06 against our `-WEAPONS`, `-WRECKS` and parts of `-DATA`. Two
of the six corrections came from here. The rest:

### Two more open questions of ours, answered

- **`[slot+0x10]`.** `WEAPONS.md` §12 says the ballistic launch's `velocity.y`
  correction divides it by a speed and multiplies by a per-tick gravity, so it
  "has to be a distance, but ... no write to `[slot+0x10]` was found to settle
  it." Theirs: the slot initializer writes it **once, at unit construction**,
  as `trunc(1.25 * (queryPoint.z - aimFromPoint.z))` — the world-space Z
  difference between the slot's `Query*` muzzle piece and its `AimFrom*` piece
  at the unit's spawn heading, never rewritten except by a save restore. The
  creator then subtracts `(slotDistance/weaponvelocity)*gravity` from the
  launch's vertical velocity, which is our formula's shape exactly. They offer
  a reading of the intent — resume the solved arc at the muzzle, the muzzle
  sitting roughly that many ticks ahead along the trajectory — and mark the
  intent itself unproven. They also report a checkable consequence: a
  stationary stock light cannon lands its group **34-40 world units short** at
  every range from half to full, a launch-time error rather than a solver one.
- **`unit+0xF7`.** `WRECKS.md` flags it twice as "still unidentified ... no
  known writer anywhere in the binary" — the second term of the `Killed`
  severity. Theirs identifies it as the health percentage retained from the
  previous **30-tick sampling boundary**, giving
  `severity = clamp((health*-100/maxHealth + previousSamplePercent)/2, 1, 100)`.
  Our first term is numerically the same once "overkill" is read as the
  magnitude of the negative health. The periodic health sampler is a mechanism
  nothing in our corpus mentions, and finding its writer is the follow-up.

### Two conflicts left open

- **Cause 7.** `WRECKS.md` reads the two fragments at `0x41B9FE` and `0x486167`
  as *relabelling* a dying `IsFeature` unit's death cause at the moment it
  dies, whatever actually killed it — which is how the document explains why
  such a wreck never sinks or burns. Theirs says cause 7 has exactly two
  producers and **both are at construction**: the creator asked for an already
  finished `isfeature` unit, and the build service completing an `isfeature`
  nanoframe. On that reading such a unit is converted to its feature form the
  instant it finishes and never has an ordinary unit death at all, anything
  later destroying it going through the feature damage path instead. These are
  materially different mechanics and RWE has a `killUnit` special case riding
  on ours, so this is the conflict most worth settling next.
- **The `+ShootAll` option bit.** Our screen-shake section puts `NoShake` at
  bit 4 of `[globals+0x37F2F]`; our target-acquisition section puts the
  `ShootMe` exemption at `0x37F30 & 4`. Theirs has both in one session
  mode-flags word, with bit 4 camera-shake — matching us exactly — and bit
  **10** the `+ShootAll` toggle. The agreement on bit 4 suggests the word is
  the same one and that our second citation has a transcription error in both
  the address and the bit.

### New, and a determinism item among it

- **The 30-tick target-registry rebuild draws from the simulation RNG.** We
  have the rebuild itself (§88, `0x40AA40`). Theirs adds that the gate is
  `if (lastRebuild + 30 <= tick) { rebuild; if (simulationRandom(30) == 0)
  refreshStrategy() }` — so **every** side, human slots included, consumes one
  simulation-stream draw every thirty ticks. That is exactly the class of fact
  CLAUDE.md's determinism section exists for.
- **`groundbounce`.** `DATA.md` has the bit position and nothing else. The
  mechanic: terrain contact is `point.Y < cell.minHeightByte` — the
  neighbourhood *minimum*, not a corner or a maximum — and on contact the flag
  replaces only the vertical velocity with `-(velocityY >> 2)` and returns. No
  position correction, no horizontal damping, no restitution key, no counter,
  no impact effect, and the branch never reaches central impact, so `noexplode`
  has nothing to do with it.
- **The order-event word's upper bits.** We have `0x400` and `0x800` (§85/§21,
  `0x49E4F2`). New: `0x1000` could not fire, `0x8000` under-construction wait,
  and a **blast-feedback pair** — `0x4000` when the blast's nominal enemy
  damage exceeded twice its friendly damage, `0x2000` otherwise, published
  after every hit with a non-null shooter. That is how an attack order learns
  its shot mostly hit friends, and we have no equivalent.
- **Burst spray does not random-walk.** §12 has
  `rand(sprayangle) - sprayangle/2`. Theirs adds that the perturbation is
  computed into a register and discarded: the parent's *stored* heading is
  never rewritten, so pellets scatter around the original aim direction rather
  than drifting. Easy to get wrong when re-implementing.
- **A third `Killed`-bypass condition.** Our cause table has 4, 5 and 9 forcing
  severity 0 with `Killed` skipped, and 7 forcing level 1. Theirs adds **any
  cause while health is still positive**, which we do not document.
- **Collision band inclusivity.** §7 does not state it. Theirs: ground slot is
  strict `point.Y < unit.Y + modelTop` with no floor, air slot is
  both-inclusive `unit.Y <= point.Y <= unit.Y + modelTop` — ground occupants
  run base to top, flying ones sit in a band.
- **Corpse-depth census.** 153 of 157 shipped `Killed` bodies write the depth
  cell; the four that do not are the two commanders and the two dragon bosses,
  whose depth is then uninitialised stack unless the build-fraction-zero rule
  forces it to zero.

And one cross-check in our favour. `DATA.md` §106's surprising negative — that
no blast path writes an impulse or shove field, and that `impulsefactor` and
`impulseboost` do not exist in the image — is reproduced independently by the
same two arguments, a whole-image string search and a walk of the blast call
chain. Both find nothing. Given how badly negatives fared above, a negative
that survives two independent readings is worth the note.

## What they list as unknown that we have answered

**Nothing — and that is the honest result.** All four comparisons checked
their open-items list against the matching parts of our corpus, and none of
them found an item we resolve. Their unknowns are concentrated in renderer and
audio internals, projectile and effect minutiae, malformed-input edge cases and
multiplayer reconstruction, which is mostly territory our corpus does not
cover; where the subjects do overlap, the things they leave open are open for
us too.

The one near-miss is worth recording because it is a mechanism-level agreement
rather than a resolution. They ask whether retail carries any name for the
display-mode byte that forces every unit body through the tinted blitter;
`VISION.md` §17 documents the identical mechanism — the global at
`[0x511DE8 + 0x14280]`, cycled 0-4 by a key handler and zeroed at game start —
and has no name for it either, calling it the debug see-everything-translucent
toggle.

So the exchange ran one way. Six corrections to us, none to them. That is worth
stating plainly rather than softening: on the evidence of this comparison their
method is at least as reliable as ours, and the places ours failed were all
places where we had concluded something was absent.
