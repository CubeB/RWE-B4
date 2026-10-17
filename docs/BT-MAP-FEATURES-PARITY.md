# Terrain features, wrecks, reclaiming and fire parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for map features (trees,
rocks, vents, wrecks), their loading, placement, damage, reclaim, fire and
regrowth, per `AGENTS.md` and ADR-0004. The wreck spawner and the sink are
`TOTALA-EXE-WRECKS.md`'s and are not restated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\map-features.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none
  of its source links were followed. Addresses are the ones the chapter names;
  rules are restated in prose with constants and arithmetic, and nothing is
  copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable and in places checked against the demo corpus. Disagreements are
  marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo
  oracle was run.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/io/featuretdf`
  and `src/rwe/game/GameSimulationLoader.cpp` at the working tree of 2026-10-16
  (HEAD `7c163690`, branch `ai-envoy-unload-commander-flee`). Nothing was built,
  run or changed, and tests were not run; a MATCHES below means the code reads
  as the rule says. The shipped feature key census in `TOTALA-EXE-WRECKS.md` was
  taken as given, not redone.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

Covered elsewhere and only cross-referenced: the wreck sink, `IsFeature`, the
death causes (`TOTALA-EXE-WRECKS.md`); a blast against a feature, regrowth and the
vent steam (`TOTALA-EXE-DATA.md` sections 15 and 24); reclaim time and the
repair-patrol scan (`TOTALA-EXE-ECONOMY.md` sections 97, 131; `TA-SYSTEMS-PARITY.md`
rules 20, 25, 27 for the difficulty scale on credits); what blocks a unit
(`TOTALA-EXE-MOVEMENT.md` section 95).

## Index

1. Definition, footprint and spot layers
2. Loading definitions
3. Replacement links
4. The spot pool
5. Placement
6. Removal and replacement
7. Reclaim payout
8. Weapon damage
9. Ignition and the burn timer
10. Spreading and the burn weapon
11. Normal animation and reproduction
12. Moving wrecks
13. Save representation
14. Gaps to fix

---

## 1. Definition, footprint and spot layers

**Reference (section 1).** Three layers: a shared definition (name, footprint,
model or sprite sequences, resource values, damage threshold, flags, replacement
ids), a terrain footprint (the origin cell carries the type index, every other
cell carries a continuation marker and byte offsets back to the origin) and a
per-instance "spot" allocated only when state is needed (model state, position,
velocity, damage, transition animation). Empty is `0xffff`; `0xfffe` is a
continuation; the values from `0xfffb` up are reserved markers, not definitions.
The cell lookup used by most entry points resolves a continuation to its origin
and returns empty for other markers, but **not every entry point normalises**.

**RWE.** The grid stores a feature id in every footprint cell
(`writeFeatureToGrids`, `GameSimulation.cpp:370-385`) and the instance record is
`MapFeature` (`src/rwe/sim/MapFeature.h`), so there is no continuation marker and
no origin cell. **DIVERGENCE (structural)**: the representation differs and only
its observable consequence matters, which is rule 8 below (damage reaches an
origin cell only in the original). Collision against a feature is normalised in
the original (the projectile test follows the back-reference,
`TOTALA-EXE-DATA.md` section 24) and in RWE alike, so that half agrees.

## 2. Loading definitions

**Reference (sections 2, 3).** Feature files are found by recursive discovery of
`features\*.tdf`; a failed parse discards that parser; a lookup resets each parser
and takes **the first record that matches**, so duplicate names depend on file
enumeration order. A missing record is **fatal**. Resource integers go through
unsigned 16-bit storage before being held as floats; `sparktime` is converted from
seconds to 30 Hz ticks and held in a short. Flags are loaded one by one with
defaults: `autoreclaimable` is **1**, the others zero (animating, shadow
transparency, flammable, geothermal, blocking, reclaimable, indestructible,
no-display-info, no-draw-under-gray). **Four named fortification and dragon's
teeth definitions have no-draw-under-gray forced on** whatever the TDF says.

**RWE.**

- **First record wins: MATCHES**, with enumeration order **UNCLEAR**. The loader
  inserts each block into a map keyed by upper-cased name and `insert` keeps the
  first (`GameSimulationLoader.cpp:550-571`). The order is `getFileNamesRecursive`
  over the virtual file system, which is archive precedence
  (`TA-SYSTEMS-PARITY.md` rule 32), not directory order. Two blocks of one name in
  one file: RWE's TDF reader replaces the earlier block (rule 34 there).
- **A missing record is fatal in the original; RWE warns and drops the reference**
  (`GameSimulationLoader.cpp:202-207`). **DIVERGENCE (undocumented)**, harmless:
  a mod missing a feature costs that feature instead of the session. It should be
  one line in section 88.
- **Defaults: MATCHES.** `autoreclaimable` defaults to true, the rest false, and
  `damage` to zero (`src/rwe/io/featuretdf/io.cpp:29-60`, with the section 24
  note on the zero).
- **Sixteen-bit storage: DIFFERS, mod data only.** RWE keeps `metal` and `energy`
  as unsigned integers; a value of 65536 or more wraps in the original. The
  shipped maximum is far below it (4104 in the shipped data per the WRECKS
  survey). `sparktime` is likewise not narrowed.
- **Forced no-draw-under-gray: MISSING.** RWE does not read `nodrawundergray` at
  all (no hit in `src/`); the register records the flag and its draw rule
  (`TOTALA-EXE-WRECKS.md`, "The wreck still draws, but its shadow does not")
  with exactly two shipped features setting it (the floating teeth pair). The
  chapter adds that **four named definitions** have it forced on. The chapter does
  not name them in the passage read; by their description they are the dragon's
  teeth and fortification features and their `_core` twins, but that is a guess
  and must be confirmed. **UNCLEAR** which four.

**Remaining uncertainty.** The four names; whether the first-record rule sees
loose files before archives.

## 3. Replacement links

**Reference (section 3).** `featuredead`, `featureburnt` and `featurereclamate`
are resolved to indices up front, with `0xffff` for absent, and resolving may load
another definition; the loop rereads the growing count so appended definitions are
resolved too. Chains are therefore never walked recursively at runtime.

**RWE. MATCHES.** `resolveFeature` and the open set in `loadFeature`
(`GameSimulationLoader.cpp:179-250`) resolve the three links by name and append
what they find. Dangling names are tolerated (rule 2).

## 4. The spot pool

**Reference (section 4).** The pool has **0x800 (2048) spots** of 0x30 bytes. A
spot is needed for every model feature (every unit corpse is one) and for a
sprite feature that is burning, dying or being reclaimed. Allocation takes the
free head; **exhaustion returns the out-of-range value 0x800, and the callers
check it**. Removal returns the spot. Teardown walks the active and resting lists.

**RWE. MISSING.** There is no cap. `addFeature` (`GameSimulation.cpp:335-355`)
refuses only on an occupied footprint. Consequence in the original: after 2048
live stateful features, **no further wreck can be created** (placement of a model
returns null when no spot is free), and neither can a fire start (rule 9
requires a spot). Map features that are models (some Core Contingency maps use
model trees) draw down the same budget. In a long game with heavy attrition RWE
therefore keeps more wrecks than the original does. **Not documented anywhere in
the register.** Which map features use spots at map load is not settled by the
chapter; only "model features and active transitions" is.

**Remaining uncertainty.** Whether a feature that has come to rest releases its
spot (the chapter says a resting spot moves to a resting list, not to the free
list, so it does not). Needs one retail game with a few thousand kills to see the
limit, or a spec-room read of the spot allocator's callers.

## 5. Placement

**Reference (section 5).** Placement ignores `0xffff`, handles the special
`0xfffc` marker, checks that the footprint fits the map's right and bottom limits,
then **walks every footprint cell and tries to remove any feature already there;
if one refuses (an indestructible one, a reserved marker), placement returns
null**. Earlier removals are not rolled back, and a spot-allocation failure after
removals leaves them done. A model needs a spot and records type, zero damage,
origin, position and rotation (supplied or generated); a normal sprite stores its
type and zero static damage. Other cells get continuation markers. Geothermal
placement starts the vent emitter. The return is the model spot, or null for an
ordinary sprite as well as for failure. Mission features are placed with owner
value 10, with sprite features at the mission coordinates and model features
offset by half the footprint. Metal stamping writes an indestructible feature's
metal, narrowed to a byte, over its footprint.

**RWE.**

- **Displacing destructible features: DIFFERS**, and already recorded.
  `addFeature` returns nothing on **any** feature in the footprint
  (`GameSimulation.cpp:341`, `anyFeatureOccupies` `:1943-1954`), where the original
  removes destructible ones and refuses only on an indestructible one or a marker.
  `TOTALA-EXE-WRECKS.md` ("What RWE does today", placement row) says this in a
  table row; the chapter **corroborates** it and adds that the removals are
  permanent even when placement then fails. Practical effect: a unit dying on top
  of a tree, an older wreck or a scar decal leaves no wreck in RWE and replaces
  the old feature in the original.
- **Right and bottom limit: MATCHES.** `anyFeatureOccupies` reports blocked when
  the rectangle does not convert to a grid region (`:1945-1949`).
- **Default height: MATCHES for the loader path.** The generated position takes
  the centre of the footprint at the height there; RWE uses the integer mean of
  the four corners (`computeFeaturePosition`, `:399-420`), which is what the
  bilinear sampler returns at a cell centre. The corpse path supplies a position
  and so skips this.
- **Metal stamp: MATCHES on shipped data.** RWE stamps the metal grid only for a
  feature that is **non-blocking** as well as indestructible
  (`writeFeatureToGrids`, `:376-379`); the chapter names no blocking condition.
  Every shipped metal patch is both. The same extra guard is on the vent stamp
  (`:381-384`) and is recorded in `TOTALA-EXE-DATA.md` section 15.
- **Mission features, owner value, vent emitter at placement:** not audited here;
  the vent steam is render-side (`TOTALA-EXE-DATA.md` section 15).

**Remaining uncertainty.** Whether the new feature's footprint of cells outside
the map is clipped (RWE clips for the grid stamp, `:372`).

## 6. Removal and replacement

**Reference (section 6).** Removal normalises a continuation to its origin,
rejects reserved ids, and **refuses an indestructible definition unless forced**;
it releases model state, returns the spot, clears the origin and continuation
cells and refreshes pass maps. The wholesale removal at map teardown is forced.
`KillFeature` picks a death or reclaim **transition sequence** for a sprite, needs
an unused origin and a free spot, and calls the replacement directly when there is
no sequence. Replacement is removal then placement, **not a transaction**: it can
leave nothing, and the stateful case preserves position and rotation through it.

**RWE.** `deleteFeature` (`:470-494`) and `replaceFeature` (`:553-571`) match the
shape: **MATCHES**. RWE has no indestructible guard on removal; the only callers
that reach an indestructible feature would be blast damage (guarded at the call,
`:3809`) and reclaim (`reclaimable` is required, `:507`), so nothing observable
follows. **DIFFERS**, latent. One difference follows from the representation: a
replacement is placed with **zero velocity** (`:570`), so a wreck shelled apart
while still sinking leaves its heap hanging at the depth it had reached. The
original reallocates the freed spot and the record's velocity words are not
cleared by placement (`TOTALA-EXE-WRECKS.md`, the stale-value remark), so its heap
may keep sinking. **UNCLEAR**: the register itself calls the stale value a rare
corner.

## 7. Reclaim payout

**Reference (section 7).** The reclaim primitive resolves the feature at a
position, fails for a missing one and **for a sprite that is mid-transition**,
otherwise adds the definition's energy and metal to the reclaiming unit's
accumulators **at once**, invokes the reclaim transition and returns 1. For an
active type-2 player, difficulty 0 scales each payout to 0.5 and difficulty 1 to
0.7. The primitive does not test the reclaimable flag, range, stance or time; the
ground reclaim order does (reclaimable flag, approach, build script, stance, a
resource-dependent timer, reverse nano particles twice per work callback).
Payout precedes the transition and nothing verifies the footprint then
disappeared. In multiplayer the primitive broadcasts a six-byte packet.

**RWE.**

- **Timing: DIFFERS from the register and from the chapter, not recorded.**
  `GameSimulation::reclaimFeature` (`:496-551`) pays a slice of the value on every
  work step and the last slice on completion. `TOTALA-EXE-ECONOMY.md` ("Reclaim and
  death metal") and the chapter both say a feature pays **in one lump**. Section 88
  records the same difference for units ("A unit reclaim pays out as it goes") and
  for the reclaim time, but **not for features**. The total is identical; only
  when it arrives differs, which matters to a stalled economy by seconds.
- **Time: DIVERGENCE (documented)** (section 88: hit points and builder rate).
- **Difficulty scale: MATCHES via the settle** (`TA-SYSTEMS-PARITY.md` rule 20).
- **Reclaimable flag: MATCHES** (`:507`).
- **Mid-transition guard: no counterpart.** RWE has no sprite transition state in
  the simulation; the swirl is a presentation event (`FeatureReclaimedEvent`,
  `:543`). Two builders on one feature are handled by shared progress, which the
  original's per-order timer does not do the same way; not checked.
- **Packet: N/A.** RWE is lockstep.

## 8. Weapon damage

**Reference (section 8).** Damage requires the TreeDeath option bit, **a direct
normal feature id, and a destructible definition. It does not normalise a
continuation cell.** In multiplayer a non-host participant sends a six-byte
request to the host and returns. A flammable, unoccupied sprite-state origin hit
by a fire-starting weapon attempts ignition; otherwise a model spot accumulates
damage if its stored origin matches the supplied position, and a static sprite
accumulates in the origin cell's two-byte field. Reaching the definition's
threshold invokes the death transition. A stateful sprite takes neither
accumulation branch. A lethal result broadcasts a kill packet.

**RWE.**

- **Continuation cells: DIFFERS, and not recorded.** The blast walks every grid
  cell in its area and damages the feature found in any of them, once per blast
  (`GameSimulation.cpp:3788-3830`, with `seenFeatures` guarding the repeat). The
  original skips continuation cells (this agrees with `TOTALA-EXE-DATA.md` section
  24 step 2, "below `0xFFFB`"), so a multi-cell feature takes blast damage only
  when the blast area covers its **origin** cell. For the large shipped wrecks
  (3 by 3 to 5 by 5 footprints) that makes them markedly harder to clear with
  splash than in RWE. Which corner is the origin (the chapter says "origin";
  `TOTALA-EXE-WRECKS.md` says the footprint is stamped "from the square the
  caller gave", i.e. the top left) should be confirmed.
- **Fire first: DIVERGENCE (documented)** in `TOTALA-EXE-DATA.md` section 24
  ("Not ported: the fire branch"): RWE damages and separately rolls for ignition
  (`:3921`, `tryIgniteFeaturesInRadius`), where the original ignites a flammable,
  idle feature on a fire-starting hit and applies **no damage** to it.
- **TreeDeath: MISSING**, as a console toggle. The session set-up turns it on, so
  default play agrees.
- **Threshold: MATCHES** (`:3808-3825`): the weapon's default damage, unscaled,
  against the feature's `damage`, no falloff.
- **Authority: N/A.**

## 9. Ignition and the burn timer

**Reference (section 9).** Starting a fire needs a valid direct origin, **a burn
sequence**, no existing state and a free spot; it does not itself check the
flammable flag (the callers do). It chooses a timer from **half the configured
spark time plus a random amount below that half**, stored in a **byte**. It plays
the tree-burn sound and broadcasts unless suppressed. A received ignition is
suppressed, and the suppression also stops that instance starting its own spread
countdown. Each tick, an active burning sprite emits a white smoke puff on every
third tick; when the burn sequence ends the burnt replacement happens; while the
sequence still exists a positive unsuppressed timer counts down and **reaching
zero invokes the spread routine once**. If the animation ends first, the spread
is skipped.

**RWE.** `igniteFeature` (`:573-592`) requires only `flamable` and no current fire,
draws a duration of **`burnmin` to `burnmax` seconds** and arms a repeating spark
every `sparktime` seconds; `updateBurningFeatures` (`:645-686`) replaces the
feature with its burnt form when the duration ends and re-arms the spark each time
it fires. **DIFFERS** in four ways, none yet in the register:

1. The fire's length is the **burn animation**, not a rolled duration. RWE has no
   sprite sequence in the simulation.
2. The spread is **one-shot** at half-to-full `sparktime`, and is skipped if the
   animation ends first. RWE repeats it for as long as the fire burns.
3. A feature with no burn sequence cannot burn in the original; RWE ignores that.
4. The ROADMAP description of the RWE rule ("burns `burnmin..burnmax` s, tries to
   spread every `sparktime` s") reads as an RWE design rather than a decode, and
   the chapter does not mention `burnmin` or `burnmax` among the loaded fields.
   **UNCLEAR** whether the original reads them at all; the register's key census
   (`TOTALA-EXE-WRECKS.md`) lists them as keys present in the data, which does not
   say the executable reads them. A string check of the executable settles it (the
   same method that retired `hitdensity` and `permanent`).

**Remaining uncertainty.** The unit of the timer (the chapter says ticks held in a
byte, so a `sparktime` over about 8.5 seconds narrows), and what the narrowing
does to the shipped values.

## 10. Spreading and the burn weapon

**Reference (section 9).** Spread tests the **seven-by-seven cell square centred
on the burning origin, excluding the origin**. For each direct, non-stateful,
flammable candidate it draws 0 to 99 and compares it with **the candidate's own
spread chance**. It then takes **five wind-directed steps** in cell coordinates,
each adding twice the stored wind X and Z components, and tries ignition at each
changed sampled cell under the same rules. It is a square plus discrete downwind
samples, not an expanding circle. **If the source has a burn weapon, spreading
also invokes that weapon's area-damage effect at the feature's centre.** Burn
damage comes from the configured weapon and this event; smoke is not damage.

**RWE.** `tryIgniteFeaturesInRadius` (`:594-643`) tests a **circle** whose radius is
the burn weapon's area of effect (32 if absent), rolls against the **source's**
`spreadChance`, iterates in grid order for determinism and has no wind term. The
burn weapon contributes a radius and nothing else. **DIFFERS** in the shape, the
chance owner, the wind and the damage. **MISSING: burn damage.** A burning forest
in the original hurts what stands next to it through the weapon (and can ignite
more); RWE's fires hurt nobody. The wind vector exists in the simulation
(`currentWindVector`, `updateWind`, `:4298-4321`), so a downwind term is
expressible without new state. Any port must keep the draw order identical on
every peer (the existing grid-order collection is the model).

**Remaining uncertainty.** Whether the burn weapon's damage reaches units, other
features or both, and its attacker and owner (a fire has no firer). Spec-room.

## 11. Normal animation and reproduction

**Reference (section 10).** Each update advances the shared normal and shadow
sequences of animated types (presentation) and **decrements a scan cursor, wrapping
below zero to the last terrain index; the wrapping call does not process a cell**.
A direct, non-stateful feature reproduces when a seed-chance draw succeeds; the
target is offset randomly within the seed-spread interval; the target cell must
exist and have no feature; **the unit-occupancy check reads the source cell's
primary slot**; **the row is the scan index divided by the map height, not width
(a suspected original bug on non-square maps)**.

**RWE.** `updateFeatureRegrowth` (`:688-771`): **MATCHES** the cursor, the wrap and
the skipped wrapping tick, the percentage draw and the box offset
(`TOTALA-EXE-DATA.md` section 24). **DIVERGENCE (documented)**: RWE divides by the
width, and the chapter independently identifies the original's height division as
a likely bug, which strengthens the case for the existing choice. **DIFFERS** in one
small check: the target veto also refuses a target cell holding a unit or building
(`:764-767`), where the original (section 24: "the destination must be truly
empty", meaning the feature slot) looks at the feature slot of the target and the
unit slot of the **source**. Shipped data sets `reproduce=0` everywhere, so only a
mod can see it.

## 12. Moving wrecks

**Reference (section 10).** For an active model spot with nonzero velocity: add
velocity to position; at or below the sampled **mean** ground height, snap to it
and zero the velocity; below sea level but above ground, clear horizontal velocity
and apply the fixed `-0x2ccc` downward velocity (about -0.175 world units a tick);
above water, subtract the map gravity from the vertical velocity. A spot at rest
goes to the resting list. The corpse creator selects the corpse definition and
replacement depth and passes the unit's position and rotation; sinking a wreck
does not make a live underwater unit.

**RWE. MATCHES**, and the chapter **corroborates** `TOTALA-EXE-WRECKS.md`
independently on the constant, the three branches and the mean-height landing:
`updateFallingFeatures` (`GameSimulation.cpp:4635-4670`) and `trySpawnFeature`
(`:4595-4633`). One recorded difference stands: the landing uses
`terrain.getHeightAt` (`:4650`), where the original uses the cell mean
(`TOTALA-EXE-WRECKS.md`, "The original grounds against..."). That document asked
for a line in section 88; none is there. See `BT-UNITS-UNITS-PARITY.md`, rule 11,
for the two height functions. The velocity is saved and hashed
(`save_util.cpp:387`, `GameHash_util.cpp:383`).

## 13. Save representation

**Reference (sections 11, 12).** The container writes three boxes (normal 8 bytes,
animating 10, 3D 0x1a per record) plus a type-name list, skips unrecognised
active sequences, **does not preserve velocity, complete animation internals, every
timer bit or the original feature owner**, and reloads by name with an identity
fallback; the loader does not validate every coordinate or remap index and does not
check allocation before later writes.

**RWE.** **DIVERGENCE (structural).** RWE saves its own JSON state including the
velocity (`save_util.cpp:387`, restore `:920`) and so resumes a sinking wreck
exactly, where the original resumes it at rest. The hash-validated round trip is
`sim/saveload.test.cpp`. The loader's lack of validation is the
`docs/SECURITY-AUDIT.md` concern for RWE's own loader, not a rule to copy.

---

## 14. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.
Demo oracles cannot see features (the stream carries units), so none applies.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Blast damage reaches a multi-cell feature only through its origin cell. Rule 8 | `src/rwe/sim/GameSimulation.cpp:3788-3830` (needs the origin of a feature's footprint, which `MapFeature` has as its position) | Light. Static reading from two sources agree; confirm the origin is the top left, then one retail shell on the far corner of a big wreck. Hashed; replay-breaking. |
| 2 | No cap on live stateful features: the original stops creating wrecks at 2048. Rule 4 | `GameSimulation.cpp:335-355` and `:4595-4633`, a counter in `GameSimulation.h`, `save_util.cpp`, `GameHash_util.cpp`, `dump_util.cpp` if new state | **Yes.** One retail game past 2048 features, and a spec-room list of what takes a spot. Hashed. |
| 3 | Fire model: one-shot downwind spread over a 7 by 7 square, the candidate's chance, burn length from the animation, no `burnmin` unless the executable reads it. Rules 9, 10 | `GameSimulation.cpp:573-686`, `FeatureDefinition.h`, `io/featuretdf/io.cpp:40-43`, `GameSimulationLoader.cpp:239-243` | **Yes.** String check for `burnmin`/`burnmax` first (cheap), then a retail forest fire. |
| 4 | Burn weapon damage (a burning forest hurts neighbours) is absent. Rule 10 | `GameSimulation.cpp:645-686` | Spec-room: who is hurt and credited. Then a retail observation. |
| 5 | Placement displaces destructible features and is never rolled back. Rule 5 | `GameSimulation.cpp:335-355`, `:1943-1954`; the wreck spawner `:4595` | No: the register decodes it (`0x4246B0` refusal rule). Only the sequencing against RWE's grid is design work. |
| 6 | Four forced no-draw-under-gray definitions, and the RWE render rule for the flag. Rule 2 | `FeatureDefinition.h`, `io/featuretdf/io.cpp`, the feature draw in `game/GameScene_render.cpp` | Spec-room: name the four. No retail check; presentation only. |
| 7 | Feature reclaim pays in one lump, not per step. Rule 7 | `GameSimulation.cpp:496-551` | No. Total unchanged; moves every reclaim's economy by seconds, so decide with section 88's unit-reclaim entry and record it. |
| 8 | Reproduction target veto looks at units and buildings. Rule 11 | `GameSimulation.cpp:764-767` | No. Mod data only. |
| 9 | Wreck landing height by cell mean; replacement of a sinking wreck loses its velocity. Rules 6, 12 | `GameSimulation.cpp:4650`, `:553-571` | No for the mean (record it in section 88). Sinking-heap case: leave. |
| 10 | Missing record fatal / 16-bit narrowing / TreeDeath toggle. Rules 2, 8 | `GameSimulationLoader.cpp:202-207` | No. Record as deliberate. |

### Documented divergences this audit leaves alone

Fire-first on a blast (section 24, "Not ported"); a feature's reclaim time
(section 88); the regrowth row division (section 24); the mission-building scenery
clear (section 88); the burning-wreck plume and the sink (`TOTALA-EXE-WRECKS.md`).

### Conflicts with existing RWE findings, in one place

1. **Burn model.** Reference: a one-shot spread after half to full `sparktime`,
   fire length set by the burn animation, spread over a seven-by-seven square plus
   five wind steps, damage through the burn weapon. RWE's `ROADMAP.md` entry
   ("Burning features") and the code describe a `burnmin..burnmax` duration with a
   repeating `sparktime` spread over a circle. There is **no register finding**
   either way, so this is a conflict with an RWE design statement, not with
   `TOTALA-EXE*.md`. Open.
2. **Feature reclaim timing.** Not a conflict: the reference and
   `TOTALA-EXE-ECONOMY.md` agree (one lump). RWE differs from both and neither
   section 88 nor `docs/compatibility.md` says so.
