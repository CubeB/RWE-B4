# COB unit-value interface, piece changes and scripted effects parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for what a COB `get` and `set`
return and change, how a script's packed coordinates are encoded, what `emit-sfx`,
`explode`, `attach-unit` and `drop-unit` ask of the engine, and how an aim thread's
answer is taken, per `AGENTS.md` and ADR-0004. The chapter is filed under
`units/unit_script_calls`; it is the engine's half of the script interface that
`BT-UNITS-UNIT-SCRIPTS-PARITY.md` (steering, geometry, cursors) and
`BT-UNITS-UNIT-POSITION-PARITY.md` (activation transitions, attachment) touch from
the other side. Already in the register and only cross-referenced: the dispatcher
covers ids 1 to 20 and answers zero for the rest (`TOTALA-EXE-WEAPONS.md` line 198),
`BUGGER_OFF` is write-only (`TOTALA-EXE-ECONOMY.md` section 112), the emit-sfx
dispatch (`TOTALA-EXE-MOVEMENT.md` section 16, `TOTALA-EXE-RENDER.md`), what
`explode` draws (`TOTALA-EXE-WRECKS.md`, "What SHATTER does"), `ATTACH_UNIT` and
`DROP_UNIT` (`TOTALA-EXE-TRANSPORTS.md` section 37) and the aim thread's return
(`TOTALA-EXE-WEAPONS.md`, `0x49D580`).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-script-calls.md`,
  sections 1 to 12). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/cob`, `src/rwe/sim` and
  `src/rwe/game` at the working tree of 2026-10-16 (HEAD `404f3c4d`, branch
  `bt-sim-conformance`). Nothing was built, run or changed, and tests were not run;
  a MATCHES below means the code reads as the rule says. Another agent had
  uncommitted edits in `GameSimulation.cpp`, `GameSimulation.h` and
  `UnitBehaviorService.cpp` while this was written, so line numbers in those files
  may have moved.
- **A census of the 272 shipped COB scripts** (retail install through `tools/hpi.py`,
  totala1 to totala4, ccdata and rev31, decoded with the opcode table of
  `tools/exe/coblist.py`) supplies the "who calls this" figures. It looks at the
  constant pushed just before each `get`, `get-with-args`, `set` and `emit-sfx`; a
  port computed at run time is invisible to it, so it can undercount and never
  overcount.
- A note on the other outside reading in the tree: `TOTALA-EXE-EXTERNAL.md`
  (the Nanolathe project's findings, **not read by this project**) says the same as
  the chapter about the packed coordinate (rule 2). It is mentioned where it agrees
  and is not relied on.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Reading unit values, id by id
2. The packed X/Z coordinate
3. Writing unit values
4. The dirty bit every `set` raises
5. Piece state, visibility, caching and shading
6. Script effects: the SFX dispatch
7. Exploding pieces and the legacy no-ops
8. Script transport operations
9. Aim completion
10. Grid and angle helpers
11. Gaps to fix

---

## 1. Reading unit values, id by id

**Reference (sections 1, 2).** The unit script binds to the unit's model-state
record; methods "generally assume valid binding, piece indices and axis indices".
`GetUnitValue` implements ids 1 to 20; **unknown ids and failed optional unit lookups
return zero**; optional unit ids are narrowed to unsigned 16 bits and zero means no
unit. Health is not clamped. Model maximum Y is definition geometry, not the target's
ground-relative top.

| Id | Chapter | RWE | Status |
| ---: | --- | --- | --- |
| 1 | activation bit 0 | `unit.activated` (`sim/cob.cpp:293-296`) | MATCHES |
| 2, 3 | standing move, fire (two bits) | the enums are 0, 1, 2 (`UnitMovementOrders.h`, `UnitFireOrders.h`) | MATCHES |
| 4 | health x 100 / max, unclamped | integer divide of hit points by max (`:305-309`) | MATCHES; a zero `MaxDamage` would divide by zero (`LoadingScene_util.cpp:534` copies it unchecked), see Gaps rank 6 |
| 5, 6 | COB state bit 0 (build stance), bit 1 (busy) | `inBuildStance`, `cobBusy` (`:310-317`) | MATCHES |
| 7, 8 | piece world X/Z packed; piece world Y | `getUnitPiecePosition`, bounded piece index answers 0 (`:318-340`) | MATCHES; geometry is `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 9; packing is rule 2 below |
| 9, 10 | live unit's packed X/Z; Y | a missing unit answers 0 (`:341-362`, the FIXME is right) | MATCHES (packing: rule 2) |
| 11 | live unit definition's model maximum Y | `modelDefinition.height` (`:363-374`) | MATCHES |
| 12 | angle of a packed vector relative to this unit's heading | `atan2` minus the unit's rotation less a half turn (`:375-388`) | MATCHES in sign convention (RWE's rotation zero faces +z, hence the half turn, per the comment); unpacking is rule 2 |
| 13, 15 | magnitude of a packed vector; hypotenuse of two components | `cobHypot` (`CobExecutionContext.cpp:42-61`) | MATCHES (detMath path, `det_math.h`) |
| 14 | angle of two components, masked to one turn | `cobAtan` (`cob_util.cpp:26-30`) | MATCHES |
| 16 | fixed-point ground height at a packed point | `terrain.getHeightAt` (`:389-393`) | MATCHES |
| 17 | quantised construction remaining (below) | `getBuildPercentLeft` (`UnitState.cpp:117-120`) | DIFFERS by at most one |
| 18 | COB state bit 2, yard open | `yardOpen` (`:399-402`) | MATCHES |
| 19 | COB state bit 3 | `buggerOffActive` (`:403-406`) | MATCHES (name from the scripts; the register's reader census is section 112) |
| 20 | activation bit 1, armoured | `armored` (`:407-410`) | MATCHES |
| 21 up | zero | **throws "Unknown unit value ID"** (`CobExecutionContext.cpp:91-92`); the interpreter loses that script thread and logs (`sim/cob.cpp:105-118`) | DIFFERS |
| 32, 68 to 74 | not in the original | answered or throws as RWE's extension (`CobValueId.h`) | DIVERGENCE (documented, `sim/cob.cpp:411-428` for 32) |

- **Id 17, the quantised percentage.** The chapter: zero when the remaining fraction is
  exactly zero; otherwise one minus the truncation of (fraction times minus 99), that
  is `1 + trunc(99 x remaining)`, a discrete 1 to 100. RWE computes
  `100 - floor(100 x completed / buildTime)` from the integer progress
  (`UnitState.cpp:117-120`). Both are zero only when complete and both read 100 at the
  start; they differ by one on some fractions (remaining 0.995 reads 99 in the original
  and 100 in RWE; remaining 0.505 reads 50 and 51). The census finds 257 plain `get 17`
  sites across the 272 scripts and no `get-with-args` use; the usual shape is the
  `while get BUILD_PERCENT_LEFT` wait in `SmokeUnit`, which only tests zero (not
  verified site by site). So no shipped script can see the one-off difference.
- **Unknown ids.** The register already says the original returns zero
  (`TOTALA-EXE-WEAPONS.md` line 198, with `VETERAN_LEVEL` as the worked case) and RWE
  answers `VETERAN_LEVEL` on purpose. For an id RWE does not know at all, the thread
  dies instead of reading zero. The fault is deterministic and costs one thread
  (`docs/SECURITY-AUDIT.md` rule); the census finds no shipped read outside ids 4, 17,
  18 (plain) and 7 to 16 (with arguments), so only a mod can reach it.
- **Failed optional unit lookup returns zero: MATCHES** for the four unit-argument ids
  and for the extension ids (`:344-346`, `:455-458`, `:466-470`, `:476-480`).

## 2. The packed X/Z coordinate

**Reference (section 2, last paragraph).** Packed X/Z "combines the upper X word with
the signed whole Z contribution". Vector and ground queries "compensate the high
component when the reconstructed low component is negative". "These legacy packed
values must be decoded with that signed convention, not treated as a pointer or two
arbitrary unsigned screen coordinates." In plain terms: **the pair is X times 65536
plus Z by addition**, so a negative Z borrows one from the X word, and the decoder adds
that one back when the low word, read as signed 16 bits, is negative. (The Nanolathe
summary, `TOTALA-EXE-EXTERNAL.md` line 473, says the same: addition not OR, and a plain
shift is "off by one world unit of X for any negative Z".)

**RWE. DIFFERS, and reachable on shipped scripts.** `cobPackCoords` ORs the two
halves (`cob_util.cpp:10-15`) and `cobUnpackCoords` reads the two halves independently
as signed 16-bit values with **no compensation** (`:17-24`). That is a self-consistent
codec, and it differs from the original in exactly the case the chapter warns about:
the script subtracts one packed position from another (`get UNIT_XZ(target) -
get PIECE_XZ(piece)` is the idiom), the Z difference is negative, and the subtraction
borrows from the X word. The original's decoder gives the borrow back; RWE's does not,
so `XZ_ATAN` and `XZ_HYPOT` see an X difference **one world unit short** whenever the
Z difference is negative (about half of all subtractions). It also differs in the raw
integer a script reads back from `get PIECE_XZ` when Z is negative (OR leaves the X
word alone, addition lowers it by one), which matters only to a script that compares
raw values.

**Who is exposed.** The census finds `get-with-args` on ids 7 to 16 only in
`ARMTHOVR`, `ARMTSHIP`, `CORTHOVR` and `CORTSHIP`, the four boom-and-crane transports
(`TOTALA-EXE-TRANSPORTS.md` section 37 and the `BoomCalc` reach check, line 604: "the
script's reach check is the real range gate -- the engine has none"). A one-unit error
in the difference vector moves the boom geometry by up to a unit at the reach edge, so
the observable is a pick-up or drop that succeeds or fails one step from where the
original's does. No shipped non-transport script uses the vector ids.
**Do not fix by guessing:** the register's `TransportDrop` packing
(`TOTALA-EXE-TRANSPORTS.md` line 250, `(intX<<16)|intZ` of non-negative whole numbers)
is a different, engine-side pack that is correct as an OR because the operands are
never negative; only the get-side and the arithmetic on it follow the addition rule.

## 3. Writing unit values

**Reference (section 3).** `SetUnitValue` supports a subset: 1 (activation bit 0, through
the state-bit setter with activation callbacks and replication), 5 and 6 (the stance and
busy bits), **18 (the yard setter, which checks occupancy before changing the claims and
refreshing passability)**, 19 (COB bit 3), 20 (activation bit 1). **Every call marks the
unit's dirty bit, including an unsupported property that does nothing.** A blocked yard
transition need not complete just because the script asked.

**RWE.**

- **Supported set: MATCHES.** `setGetter` accepts exactly these plus the extra ports
  below (`CobExecutionContext.cpp:96-119`); activation goes through
  `activateUnit`/`deactivateUnit` (`sim/cob.cpp:492-501`), stance and busy through
  `setBuildStance` and `cobBusy`, armour straight to the flag.
- **Yard gate: MATCHES.** `trySetYardOpen` refuses when `isYardmapBlocked` says a mobile
  unit stands in the way and leaves the claims untouched (`GameSimulation.cpp:2695-2716`);
  see `BT-MAP-TERRAIN-PARITY.md` for the occupancy rule itself.
- **Ports 2 and 3 are honoured: DIFFERS (extension).** `set STANDING_MOVE_ORDERS` and
  `set STANDING_FIRE_ORDERS` change the unit's posture (`sim/cob.cpp:502-535`); the
  original's setter has no case for them (the register's twenty-byte port table
  collapses to seven handlers, `TOTALA-EXE-ECONOMY.md` section 112). The census finds
  **no shipped script setting port 2 or 3**: the only constants set are 1, 5, 6, 18, 19
  and 20. Left as an extension; a mod script that sets them behaves differently from the
  original.
- **Any other port throws and kills the thread** (`:116-117`); the original ignores it
  (and marks the dirty bit). Same remedy as rule 1.
- **`set BUGGER_OFF` sweeps: DIVERGENCE (documented).** Port 19 writes the flag and, when
  true, sweeps the footprint and hands each mobile unit a `BuggerOffOrder`
  (`PieceController.cpp:111-118`, `GameSimulation.cpp:2718-2748`). The original only
  writes the bit (section 112); `TOTALA-EXE.md` section 88 records RWE's
  sweep-on-every-blocked-attempt. That entry says the one-shot sweep on the set "was
  itself an invention" yet the set path still performs it; harmless, since the blocked
  attempt sweeps again, but it is the one place a `set` acts on the world in RWE and not
  in the original. **UNCLEAR** whether to remove it.

## 4. The dirty bit every `set` raises

**Reference (section 3, last sentence of the first paragraph; section 12).** The write
marks bit 2 of the unit's pending word whatever the port. Script-set stance and busy bits
unblock the order wait gates, and the same bit wakes an order that waits on mask bit 2
(`BT-ORDERS-ORDER-QUEUE-PARITY.md` rule 4).

**RWE. DIVERGENCE (structural, with a pinned consequence).** There is no pending word;
orders poll `inBuildStance` and `cobBusy` each tick. The consequence the register pinned
is the aircraft builder's extra increment on its creation tick, which depends on a stale
bit 2 left by the previous job's `StopBuilding` (`TOTALA-EXE-ECONOMY.md` section 110,
"Why the event is always there in Escalation"); RWE reproduces the result through the
build-timing arithmetic, not through the event, and `tools/tad-buildtime.py` scores it.
A **write to an unsupported port** (a `set` that changes nothing) would raise the bit in
the original and waits one more tick in RWE; no shipped script writes one.

## 5. Piece state, visibility, caching and shading

**Reference (sections 4, 5).** Piece translation and rotation setters update only when
the value changes, clear the piece's cached value, mark the model dirty and, for a
cached piece, invalidate an additional cache field; rotations compare and store as
unsigned 16-bit angles (one-turn wrapping). The visibility setter does **not** set the
model dirty field in exactly the same way. The cached and shaded setters assign their
bits and reset the cached-picture field. Visible, cached and shaded are three distinct
things and none is unit visibility or build progress. These callbacks change model
state, not the unit's position or footprint.

**RWE. DIVERGENCE (structural).** A piece's state is on `UnitState`'s mesh records;
`PieceController::showObject`, `hideObject`, `enableShading`, `disableShading`,
`enableCaching` and `disableCaching` flip the flags (`PieceController.cpp:37-87`) and
the renderer reads them. There is no picture cache and no dirty field, so the
invalidation rules have no counterpart; the "original caches the building bitmap" side
is `TOTALA-EXE-RENDER.md`. The rotation's wrapping at one turn is
`BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 9. Nothing to port.

## 6. Script effects: the SFX dispatch

**Reference (sections 6, 7).** `EmitSfx` first asks whether the **viewing player** can
see the unit and returns before any preparation if not; it then refreshes the model. For
ids with bit 0x100 it takes one world point (unit position plus the piece's computed
position, model Z reversed); for others, two points from the piece's six-integer offset
array. The dispatch is by **exact id**: 0 thrust (parameters 1, 6, 7); 1 thrust
(1, 7, 7); 2 wake from point 1 to 2, amount 16; 3 the same, 8; 4 and 5 the reverse,
16 and 8; 0x101 white smoke at point 1; 0x102 black smoke; 0x103 bubbles from point 1 to
a copy of it at sea level, amount 8, index 7. Unknown ids emit nothing. Wake direction
follows the endpoints and the id, never an assumption about heading. The simulation is
unaffected.

**RWE. MATCHES the register and the chapter**, with one gate that differs.

- **Ids and parameters: MATCHES.** `CobSfxType` (`CobSfxType.h`) is 0 to 5, 257, 258,
  259; thrust 6 and 7 (`GameScene_commands.cpp:1663-1666`), wake 16 and 8 and the
  reverse (`:1647-1660`), bubbles through the wake routine with the ramp reversed
  (`TOTALA-EXE-RENDER.md`, "there is no bubble emitter"). The sim pushes an event for
  each id and none for an unknown one (`sim/cob.cpp:248-279`, no default arm).
  The "six-integer offset array" is the register's first two transformed vertices of the
  piece (`TOTALA-EXE-MOVEMENT.md` section 16): the same object under two descriptions.
  The names differ (the register calls id 0 `Vtol` and 1 `Thrust`; the chapter calls both
  thrust and gives the parameters 6 and 7); RWE's parameters are the chapter's.
- **The viewing gate: DIFFERS in kind, MATCHES in the common case.** The original asks
  the full can-see predicate (own unit passes, cloak rejects, a submerged unit needs a
  sonar contact, then the fog grid; `TOTALA-EXE-VISION.md`, `0x465AC0`). RWE asks only
  whether the unit's **position** is lit for the local player
  (`positionIsVisibleToLocalPlayer`, `GameScene_commands.cpp:2009`): a cloaked enemy
  standing on lit ground still emits wakes and smoke in RWE and does not in the original.
  Presentation only; the sim is the same either way (the event is pushed
  unconditionally, so the gate is correctly outside the sim).
- **Which of the nine ids the shipped scripts name.** The census finds immediate
  constants 0 (7 sites), 2, 3, 4, 5 (39, 102, 4, 96) and none of 1, 257, 258 or 259; the
  smoke and bubble ids are spelled by OR-ing at run time
  (`TOTALA-EXE-RENDER.md`), so this undercounts them. Id 1 is unused by shipped
  scripts, which is why the 6-versus-7 parameter naming has never mattered.

## 7. Exploding pieces and the legacy no-ops

**Reference (section 8, 5).** `ExplodePiece` has two independent branches. When bit 0x20
(BITMAPONLY) is **absent** it builds a debris request: three random spins below 3000,
sideways velocities in quarter-unit steps about zero, an upward velocity below ten units,
lifetime parameter 900, and flag bits remapped from the low input bits (bit 0 distinct,
the rest shifted). The local flag word is **merged before it is initialised**; "the
source identifies surviving high bits from prior stack contents as a suspected original
bug". Independently, any of the bits 0x100 to 0x2000 requests an explosion effect at the
piece's world position from a six-entry source table; several bits append several
effects. Unlike `EmitSfx` it does **not** return early for an unseen unit.
`ExplodeLegacy` and `PlaySoundNoop` are empty overrides: calling them neither creates
the modern piece explosion nor plays a sample.

**RWE.**

- **Throw and lifetime: MATCHES the register** (`TOTALA-EXE-WRECKS.md`, "What SHATTER
  does": spins `rand(3000)`, `(20 - rand(40)) << 14` sideways, `rand(10) << 16` up,
  lifetime 900). The chapter's quarter-unit increments are `<< 14` in 16.16.
- **Bitmap effects independent of debris: MATCHES.** `PieceExplodedEvent` carries the
  flags (`sim/cob.cpp:216-238`) and the scene turns bits 8 to 13 into sprites
  regardless of BITMAPONLY, as the register says.
- **No viewing gate on the explosion: unverified.** The event is pushed always; whether
  the scene gates the debris on visibility was not traced (presentation only).
- **The uninitialised flag bits: do not port.** A flag word built from stack residue is
  not determinism-safe; `CLAUDE.md`'s rule on initialised members is the same lesson.
  RWE builds its flags from the script's argument alone.
- **Random draws: DIVERGENCE (by design).** The original's debris draws advance the
  shared game generator (`TOTALA-EXE-EXTERNAL.md` line 478 stresses the order: three
  below 3000, then 40, 10, 40). RWE's presentation uses `effectsRng`, never
  `simulation.rng` (`CLAUDE.md`, determinism section), so the sim's stream is not
  advanced by a piece breaking up. Correct for lockstep; not comparable to an original
  stream.
- **The legacy slots: no counterpart needed.** COB `play-sound` is not an opcode this VM
  runs (`CobOpCode.h` has none); nothing to compare.
- **Piece hidden after a non-BITMAPONLY explode:** RWE hides the piece
  (`sim/cob.cpp:235-238`); the chapter does not say whether the original's piece
  disappears from the model. The register's debris reading implies the piece leaves the
  model as debris. **UNCLEAR**, presentation only.

## 8. Script transport operations

**Reference (section 9).** `AttachUnit` resolves the id, requires a **live target that is
uncarried or already carried by this unit** and requests attachment with the piece and
mode; the shared attachment service repeats structural guards and broadcasts. It does not
enforce all capacity or unit-type policies of pickup orders. `DropUnit` requires the live
target to be carried by this unit, **tests placement at the target's stored cell in mode
1**, and only then detaches with piece minus one and mode 1; **a failed footprint test
leaves the unit attached**. `IsCarryingUnit` searches the child chain; `GetTransporterId`
returns the carrier's id or zero. These are runtime relationship queries.

**RWE.**

- **Attach: MATCHES, plus an ownership test the chapter does not have.**
  `attachUnitToTransportPiece` (`PieceController.cpp:7-25`) accepts a live target that is
  uncarried or already carried by this transport (re-attaching moves it to the new piece),
  and additionally refuses a unit **not owned by the transport's owner** (`:20`). The
  original's pick-up mission has no owner test (`BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 4,
  own units only is S:88), and the script call has none either. No shipped difference: the
  shipped order path already restricts to own units; a mod script that attaches a stranger
  works in the original and not here.
- **Drop: DIFFERS, and the register already says how.** The chapter and the register
  (`TOTALA-EXE-TRANSPORTS.md` lines 397-402 and 613: "`DROP_UNIT` itself must enforce the
  same per-cell legality ... and silently keep the unit aboard when it fails") agree that
  an illegal cell leaves the unit attached. RWE's `dropUnitFromTransport`
  (`PieceController.cpp:27-35`) calls `unloadUnitFromTransport`, which **searches outward,
  ring by ring up to twelve cells, for the nearest footprint that fits and puts the unit
  there** (`GameSimulation.cpp:914-1000`); the test is the movement class's
  `canBeBuiltAt`, not the drop-legality test of section 37 (water depth against
  `maxwaterdepth`/`minwaterdepth`, slope). A ground transport asked to set a tank down
  over deep water therefore relocates it up to about 190 world units to dry ground in RWE
  and keeps it aboard (the script retries after moving) in the original. The ROADMAP
  entry of 2026-08-28 ("unload onto the nearest clear ground") is where the behaviour was
  chosen; the register's 2026-09 transports section postdates it.
- **`IsCarryingUnit` and `GetTransporterId`: no COB-visible counterpart.** The chapter
  lists no port for either, so no COB `get` reaches them (not otherwise checked); RWE's `carriedBy` and
  `carriedUnits` are the same relationship held on the unit.

## 9. Aim completion

**Reference (section 10).** The aim callback sets `nAimCobDone` to 1 when the script
returns a nonzero value; **zero does not clear an already set flag**, and the firing
code must reset the callback state for each aim request. The aim stub returns zero. A
nonzero return means ready; it does not spawn a projectile or debit resources.

**RWE. DIVERGENCE (documented), agreeing with the register.** The aim thread's return is
reaped once (`UnitBehaviorService.cpp:1335-1352`): nonzero moves the weapon to the aimed
state, zero returns it to idle, and a thread that vanished without answering is also
retried (`:1353-1363`). The comment records that RWE "knowingly parts" from `0x49D580`,
which leaves the weapon not asked again until something clears the bit; the chapter's
sentence is the same fact seen from the callback (the flag is sticky). No change.

## 10. Grid and angle helpers

**Reference (section 11).** `StepTowards` moves a scalar and clamps at the target (for
ordinary positive steps); `StepPointTowards` adds or subtracts the step independently on
each differing coordinate **without** an overshoot clamp; `ChebyshevDistance` is the
larger absolute difference; `ClampPointToSegment` picks an endpoint by those distances,
limits the step by the endpoint separation and calls the unclamped point step, so it is
"a grid-step construction, not an exact Euclidean projection" and can leave the straight
segment; `OctantBetween` encodes **only the signs** of the coordinate differences into
0 to 7 (negative Y alone 0, negative X and Y 1, negative X alone 2, negative X positive Y
3, positive Y alone 4, positive X and Y 5, positive X alone 6, positive X negative Y 7),
**coincident points also return 0**, and is not a magnitude-weighted sector.

**RWE. MISSING as named helpers; UNCLEAR whether anything depends on them.** The chapter
names no caller, no register finding mentions these six entry points, and a search of
`src/rwe` for an octant or a Chebyshev step finds nothing. The pathfinder uses its own
octile heuristic (`pathfinding/`, `TOTALA-EXE-MOVEMENT.md` section 87) and the register
has the original's bug-walk; if `OctantBetween` is that walk's direction picker, its
sign-only table is a behaviour to check there. Spec-room follow-up: find the callers
before anyone ports a sector function from the names alone.

## 11. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or
capture run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | `DROP_UNIT` must keep the unit aboard on an illegal cell, using the section 37 legality test, not relocate it (rule 8) | `src/rwe/sim/PieceController.cpp:27-35`, `GameSimulation.cpp:914-1000` (`findUnloadSpot`, `unloadUnitFromTransport`), the transport tests (`transport.test.cpp`) | **Partly.** The register already states the original's rule (`TOTALA-EXE-TRANSPORTS.md` section 37); a Hulk or Envoy dropping a tank over water in the original confirms the retry. Air unload shares the same function; check what the air path (which calls the legality test directly) does at a bad cell. |
| 2 | Packed X/Z with the addition convention and a compensating decoder (rule 2) | `src/rwe/cob/cob_util.cpp:10-24`, `cob_util.test.cpp`, every caller of `packCoords`/`cobUnpackCoords` in `sim/cob.cpp` | **Yes.** Pick up and drop a unit with an Arm or Core hover or ship transport at maximum boom reach, with and without a negative Z difference to the cargo, in the original and in RWE; a spec-room read of the decode helper at `0x480770` (ids 12 and 13) would also settle it. |
| 3 | An unknown `get` id answers zero and an unknown `set` port is ignored, instead of killing the thread (rules 1 and 3) | `src/rwe/cob/CobExecutionContext.cpp:14-93,96-119` | No: both are in the register (`TOTALA-EXE-WEAPONS.md` line 198, section 112 port table). Keep the extension ids (32, 68 to 74) answered. |
| 4 | Decide ports 2 and 3 on `set` (rule 3) | `CobExecutionContext.cpp:102-105`, `sim/cob.cpp:502-535` | No: no shipped script sets them; a mod-compatibility decision. |
| 5 | Id 17 as `1 + trunc(99 x remaining)` (rule 1) | `src/rwe/sim/UnitState.cpp:117-120` | No; no shipped script can see the difference. |
| 6 | Guard the health read against a zero `MaxDamage` (rule 1) | `sim/cob.cpp:305-309`, `LoadingScene_util.cpp:534` | No. A divisor from content is checked before use (`docs/SECURITY-AUDIT.md`); the same defect would hit `getBuildPercentLeft` for a zero `BuildTime` (`UnitState.cpp:119`). |
| 7 | `emit-sfx` viewing gate through the can-see predicate (rule 6) | `GameScene_commands.cpp:2009` and the other emit helpers | No; presentation only. |
| 8 | Find the callers of the grid helpers (rule 10) | spec-room | Spec-room. |

### Documented divergences this audit leaves alone

`VETERAN_LEVEL` and the other extension ids; the sticky aim flag; the BUGGER_OFF sweep;
presentation-only piece caches and dirty fields; debris drawn from the effects generator.

### Conflicts with existing RWE findings, in one place

1. **Packed X/Z convention.** RWE (`cob_util.cpp:10-24`): OR of the two halves, plain
   signed unpack. Reference: addition with borrow and a compensating decode. The register
   is silent except for the engine-side drop pack (an OR of non-negative values, not in
   conflict). The Nanolathe summary agrees with the reference but is not verified here.
2. **Drop onto a bad cell.** Register (`TOTALA-EXE-TRANSPORTS.md` section 37, line 613):
   keeps the unit aboard. RWE: relocates to the nearest fitting footprint within twelve
   cells. This is a register-versus-code mismatch, not a reference-versus-register one;
   the reference agrees with the register.
3. **Port 19's name.** The reference gives no name for id 19 ("COB state bit 3"); the
   register and RWE call it `BUGGER_OFF` from the scripts. Wording only.
