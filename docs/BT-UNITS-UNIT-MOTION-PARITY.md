# Extraction callbacks, wind callbacks and the bitmap cache parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for the two per-unit script
inputs this chapter owns (the extractor's metal figure and the wind generator's
direction and speed) and for what the chapter's allocator does and does not mean for
RWE, per `AGENTS.md` and ADR-0004.

**The chapter's label is wrong, and the reader should know it.** It is filed under
`units/unit_motion`, but the chapter itself says (and the code range confirms) that
its twelve entry points "do not implement a unit's complete steering or pathfinding
loop": they are an extraction callback, a wind callback and an evicting memory arena
used by the model renderer. **Nothing in it is evidence about how a unit moves.**
Steering, acceleration, turn rate and the path follower are `TOTALA-EXE-MOVEMENT.md`
(sections 1, 87, 95, 102, 132), which this chapter neither supports nor contradicts.
A gap in "unit motion" parity is therefore not closed or opened by this audit; the
module that holds those rules is a different one.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-motion.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of
  its source links were followed. Addresses are the ones the chapter names; rules are
  restated in prose with constants and arithmetic, and nothing is copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with
  no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle was
  run.
- **The RWE half** is a read-only audit of `src/rwe/sim` and `src/rwe/render` at the
  working tree of 2026-10-16 (HEAD `7c163690`, branch `ai-envoy-unload-commander-flee`).
  Nothing was built, run or changed, and tests were not run.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

Already covered, and only cross-referenced: the extractor rule and its plus-one
question (`TA-SYSTEMS-PARITY.md` rule 17 and its conflict 3); wind selection, the 5000
divisor and the generator's income (rules 10, 18 there; `TOTALA-EXE-ECONOMY.md`
section 23).

## Index

1. Metal extraction input
2. Wind-driven script inputs
3. Arena state, ownership, initialisation, allocation, eviction
4. Bitmap wrappers
5. Flush and owner release
6. Gaps to fix

---

## 1. Metal extraction input

**Reference (section 1).** The updater does work only for a positive extracts-metal
value. It walks the unit's footprint rectangle from the stored cell origin, asks the
map for each cell and **sums each existing cell's metal byte plus one**; a missing
(off-map) cell contributes nothing. The sum is held in the signed whole half of a
16.16 accumulator; the stored rate is that sum times the definition's coefficient
(the 1/65536 cancels the scaling), so it is limited by the narrow accumulator's
range. The rate is stored on the unit; then, **if a script exists, `SetSpeed` is
started with the unscaled summed whole value**. The script's animation speed input and
the economic rate are related but different. For a non-positive definition the
function returns without zeroing a stored rate. It tests nothing about activation,
completion, energy or competing extractors; those gates are downstream.

**RWE.**

- **The cached-versus-live split: MATCHES in effect.** The `SetSpeed` call is made once
  at creation (`UnitBehaviorService.cpp:175-180`, inside `onCreate`) with the
  footprint's metal sum, and the economy credit reads the grid live at each settle
  (`GameSimulation.cpp:4400-4405`). The original computes once at initialisation and
  caches; RWE's live read gives the same number because the metal grid is written
  only when features are placed at load. **MATCHES**, unless a mod changes metal at
  runtime.
- **The plus one: MISSING.** Neither the `SetSpeed` argument nor the credited rate adds
  one per cell. This is `TA-SYSTEMS-PARITY.md` rule 17 and its conflict 3 and is not
  reopened: the chapter now makes the same statement a second time, in plainer words
  ("a valid zero-metal cell still contributes one before the coefficient; an off-map
  missing cell does not"), which is corroboration from the same source, not
  independent evidence. The chapter adds one detail the earlier audit lacked: the
  plus-one applies to cells that **exist**, so a footprint hanging off the map edge
  loses the cells that are off it.
- **The summed value is signed and narrow: UNCLEAR, immaterial.** A 3 by 3 footprint of
  metal 255 is 2304, well inside a signed sixteen-bit range.
- **Stored rate not zeroed for a non-extractor: N/A.**

**Remaining uncertainty.** The same as rule 17: whether the map's surface-metal default
is applied before the plus one. RWE initialises the grid to the OTA `SurfaceMetal`
(`GameSimulation.cpp:237`).

## 2. Wind-driven script inputs

**Reference (section 2).** The wind callback requires a positive wind-generator value
on the definition **and the global wind switch**. It starts `SetDirection` with the
stored wind direction and `SetSpeed` with the stored wind speed **shifted left four
bits** (times sixteen). It adds nothing to a player's account and does not multiply by
the generator's rating (that is the economy's). Unlike the extraction callback it
**does not guard a null script pointer**; the wind-unit definition and runtime must
supply the interface.

**RWE.** `UnitBehaviorService::updateWind` (`UnitBehaviorService.cpp:197-209`) is called
from the wind change (`GameSimulation.cpp:4298-4321`) and starts `SetSpeed` and
`SetDirection` on every unit whose definition has a wind generator rating.

- **Speed scale: DIFFERS, UNCLEAR how far.** RWE sends the **normalised fraction**
  (`min(1, speed / 5000)`) as a 16.16 value, i.e. 65536 at a full gale and no more
  however hard the map blows (`GameSimulation.cpp:4312-4313`, `toCobSpeed` in
  `cob.cpp:26-29`). The chapter says the argument is the stored wind **speed times
  sixteen**. On the register's scale (speeds up to the 5000 divisor,
  `TOTALA-EXE-ECONOMY.md` section 23) that is 16 times the speed, up to 80000 at 5000
  and more beyond, against RWE's 13.1 times the speed up to 65536. The two differ by
  a factor of about 1.22 below the cap and are unbounded apart above it. Nothing in the
  register records which argument the shipped wind-turbine scripts expect, so the
  visible effect (turbine spin rate) is not known to match. The chapter does not say
  whether "wind speed" there is the raw drawn speed or the normalised one; the second
  would make the argument 16.
- **The wind switch: UNCLEAR.** The original does nothing when the global wind switch is
  off (`TOTALA-EXE-MOVEMENT.md` section 132 names the same switch). RWE has no switch;
  what turns it off in the original (a map option, a zero range) is not in the chapter.
- **Direction argument: MATCHES in shape** (an angle in script units).
- **Timing: MATCHES in effect.** The notification fires on a wind change only, as the
  register has it (`TA-SYSTEMS-PARITY.md` rule 10); a generator completed mid-interval
  gets nothing until the next change in both.
- **No rating multiplication: MATCHES.**

**Remaining uncertainty.** The scripts' use of the argument. A disassembly of one shipped
wind generator's `SetSpeed` (ARMWIN, CORWIN) settles it; that is a spec-room action, not
an implementation one, and it can be done without a run of the original.

## 3. Arena state, ownership, initialisation, allocation, eviction

**Reference (sections 3 to 5).** `CMemoryCache` holds a capacity, a base, a cursor, an
internal handle and a scratch-image pointer. Each arena record begins with an eight-byte
header (the address of the owning pointer variable, and the chunk size) and the data
follows. The owner is a pointer to a handle location, not the unit. Reuse of a chunk
**clears that handle** if it still points at the chunk, so the model renderer detects the
cleared handle and rebuilds the picture. Allocation takes a handle address and a payload
size; a non-positive request clears the handle; an existing chunk that already satisfies
the size is kept; a request larger than the arena clears the handle and fails; otherwise it
allocates at the cursor, **wrapping to the base when it would run past the end**,
consuming adjacent chunks and clearing their owners' handles until the size is met, and
creating a free remainder only if more than eight bytes are left. This is cursor-driven
eviction, **not a least-recently-used cache**, and it does not compact. Initialisation
frees an existing arena, allocates, and writes one unowned chunk; it has no failure
recovery and no minimum-size check. The size query returns the stored size, which includes
the header and any absorbed remainder.

**RWE. DIVERGENCE (structural), not documented in section 88 and not a gap.** RWE draws
with a GPU renderer and keeps unit and shadow pictures in textures and meshes it owns; it
has no software picture cache, no eviction and no owner-handle protocol. Nothing the
arena does is visible in play except one thing: **an evicted picture is rebuilt, so the
original pays a cost, never a result**. The register's treatment of the pictures that
this cache holds is `TOTALA-EXE-RENDER.md` sections 52, 100 and 101, and the divergences
that depend on it are already in section 88: the building box filter, the shadow passes
and the cloak composite. The hazard the chapter describes (destroying an object while its
handle address is still registered, so a later eviction writes through a stale address)
is the same family as RWE's own `Subscription` rule in `CLAUDE.md`, and needs no further
action here.

## 4. Bitmap wrappers

**Reference (section 6).** The one-plane wrapper requests a 0x18-byte frame header plus
width times height pixel bytes, filled with transparency value 1; the two-plane wrapper
requests the header plus two planes, the first at transparency 1 and the **scratch plane at
zero**. Both return 0 on a failed or null allocation and 1 otherwise, can reuse a large
enough chunk, and **do not check the multiplication or that the dimensions fit the narrowed
header fields**. The presence of a scratch plane matters to rendering (compositing and
depth behaviour) and not to build progress.

**RWE.** No counterpart (rule 3). The overflow and narrowing the chapter notes are a
fault to avoid in any RWE image-size path, which is already the rule of
`docs/SECURITY-AUDIT.md` ("every count, size and offset from outside is checked"). The
scratch plane is the original's way of giving a cloaked unit a private bitmap, which
section 88 already records as replaced by a depth prepass and alpha. **DIVERGENCE
(documented).**

## 5. Flush and owner release

**Reference (section 7).** Flush requests the capacity minus eight bytes through the
cache's own handle, evicting ordinary owners without freeing the memory. Owner-address
release walks every chunk and clears the owner field where it equals the given address; it
does not clear the caller's pointer or merge the chunk, and only unregisters it so later
eviction cannot write through it. Flushing invalidates tracked pictures while keeping
storage; releasing an owner protects an external object's lifetime; freeing destroys the
arena.

**RWE.** N/A, as rule 3. Cache flush on a video-mode change is the renderer's own business
in RWE.

---

## 6. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play, capture or
oracle run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Wind turbine `SetSpeed` argument scale (times sixteen of the stored speed against RWE's normalised fraction), and the wind switch | `src/rwe/sim/UnitBehaviorService.cpp:197-209`, `src/rwe/sim/cob.cpp:26-29`, `GameSimulation.cpp:4312-4313` | **Spec-room first**, no play needed: disassemble the `SetSpeed` handler of one shipped wind-generator script to learn which scale it was written for. Then a retail look at a turbine at low and high wind. Script state is not in the sync hash, but the argument is read by the sim's COB run, so make the change consistently on every peer. |
| 2 | Extractor plus one per existing footprint cell, in the rate and in the `SetSpeed` argument | `GameSimulation.cpp:4400-4405`, `UnitBehaviorService.cpp:175-180` | As `TA-SYSTEMS-PARITY.md` gap 5. Not repeated. |

Rows beyond these two do not exist: the rest of the chapter is the renderer's cache, which
RWE replaces.

### Documented divergences this audit leaves alone

The GPU renderer in place of a software cache and its owner handles (structural); the
cloaked-unit private bitmap (section 88); the building-only box filter and shadow passes
(section 88).

### Conflicts with existing RWE findings, in one place

None. The wind argument scale is an RWE departure from the chapter with no register
finding to disagree with. The extractor plus-one was already listed as
`TA-SYSTEMS-PARITY.md` conflict 3 and is only corroborated here.
