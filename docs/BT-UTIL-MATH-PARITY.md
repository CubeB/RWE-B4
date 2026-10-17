# Checksum, random state, vector and angle arithmetic parity audit

Spec-room finding, 2026-10-17. Like `BT-WEAPONS-WEAPONS-PARITY.md`,
`BT-ORDERS-UNIT-ORDERS-PARITY.md` and `TA-SYSTEMS-PARITY.md`, it allocates no section
number in the main register. This is the finding that implementation work cites for
the original's arithmetic primitives: the four-byte checksum, the random generator and
the rule for when it advances, the approximate distance, the angle unit and the sine
table, the heading function, and the two rotation helpers. Per `AGENTS.md` and ADR-0004
a change to any of them cites this audit and, where it names one, the register section.

Most of this audit is corroboration. The checksum, the generator's algorithm, the 512-entry
sine table, the heading convention and the sine and cosine rounding are already in
`TOTALA-EXE-DATA.md` (sections 8 and 117), `TOTALA-EXE-MOVEMENT.md` (near 1749),
`TOTALA-EXE-WEAPONS.md` (near 1438) and `TOTALA-EXE-MISSIONS.md`. What the chapter adds is
(a) a second, independent statement of those that agrees with them, (b) the plain
(unscaled) sine and cosine entries, the wide multiply and the multiply-divide, none of
which the register records, and (c) the approximate distance, which nothing in the
repository mentions. Because this repository's determinism rules (`CLAUDE.md`,
"Determinism") make RWE deliberately different in float details, every difference below
is classified as either **behavioural** (a loaded or simulated value changes) or
**representation** (the same rule in a different number format, with no value a player
or a replay can see).

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\util-math.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source
  links were followed. Addresses are the ones the chapter names; rules are restated in
  prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only, so that
  resolved rows are not raised again; it holds no row on this module.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play, no demo oracle. The Gaps
  table says which items an original check or a spec-room read settles.
- **The RWE half** is a read-only audit of `src/rwe/sim/SimRandom.h`, `SimAngle.*`,
  `src/rwe/math/det_math.*`, `src/rwe/net/ta/TaChecksums.cpp` and the sim's random call
  sites at the working tree of 2026-10-17. Nothing was built, run or changed. Another
  agent had uncommitted edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The four-byte checksum
3. The random generator: algorithm, seeding and when it advances
4. Draw ranges: exclusive upper bounds at the call sites
5. The approximate distance
6. Angle units, the 512-entry table and the two sine flavours
7. Heading from a vector: operand order and rounding
8. In-place rotation, the three-angle sequence and the shared 13-bit matrix
9. Wide multiply, multiply-divide and the vector helpers
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1 checksum | `TOTALA-EXE-DATA.md` section 117 | **MATCHES** (rule 2) |
| 2 generator, seed | `TOTALA-EXE-DATA.md` section 8; `TOTALA-EXE-EXTERNAL.md` near 154 | algorithm agrees; RWE's generator differs (rule 3) |
| 3 approximate distance | none | rule 5 |
| 4, 5 table, scaled sine and cosine | `TOTALA-EXE-MOVEMENT.md` near 1749, `-WEAPONS.md` near 1438, `-MISSIONS.md` near 24 | agrees; RWE is documented as more precise (rule 6) |
| 4 plain sine and cosine entries | none | rule 6 |
| 6 heading | `TOTALA-EXE-MOVEMENT.md` near 1752 (`HeadingTo`), `-MISSIONS.md` near 52 | **MATCHES** (rule 7) |
| 7, 8 rotation, matrix | `TOTALA-EXE-SHADING.md` near 165 | representation (rule 8) |
| 5 wide multiply, multiply-divide | none | rule 9 |

## 2. The four-byte checksum

**Reference (section 1).** Four byte accumulators start at zero. For each byte at zero-based
index `i`: the first adds the byte, the second exclusive-ors the byte, the third adds
`i xor byte`, the fourth exclusive-ors `i + byte`. Each keeps eight bits. The result packs
the first as the low byte, then the second, third, fourth in increasing significance. An
empty or negative length returns zero. It is not a CRC or any standard digest and has the
256-byte period the index introduces. The chapter adds that COB image loading computes it
before relocating offsets and keeps it, and that map metadata supplies its own byte ranges.

**Register.** Section 117 states the same function by its effect on the final word: the
exclusive-or of `index + byte` sits in the **top** byte, the sum of `index xor byte` in
bits 16 to 23, the exclusive-or of the byte in bits 8 to 15, the plain sum in the bottom
byte. The chapter's four accumulators map one to one: its add-the-byte is the register's
bottom byte, its xor-the-byte is bits 8 to 15, its add-of-xor is bits 16 to 23, its
xor-of-sum is the top byte. **The two agree.** This is a corroboration of section 117 from
a direction that did not read it.

**RWE: MATCHES.** `taChecksum` (`src/rwe/net/ta/TaChecksums.cpp`, about 125-144) is the same
four accumulators and the same packing, and `TaChecksums.test.cpp` pins two real hosts'
map checksums. The empty-input and negative-length results agree (a span cannot be
negative). The map range rules (header, attribute block, feature block, with the final two
bytes of the OTA header block dropped) are section 117's and are not restated here.

- **COB image checksum: not applicable.** The chapter says the value is kept "for
  interpreter-state compatibility". RWE's COB loader computes none, and no RWE feature
  compares one. Nothing observable depends on it.

## 3. The random generator: algorithm, seeding and when it advances

**Reference (section 2).** Seeding stores the supplied value exclusive-or `0x66e29572`
with the low bit forced to one, with no further restriction to a clean positive domain.
The draw returns zero without touching the state for a range below two. Otherwise it runs
the 16807 multiplicative recurrence by Schrage's quotient method in 32-bit unsigned
arithmetic, adds 2147483647 when the signed result is nonpositive, stores the new state
and returns the state modulo the range. "Modulo range is not an independent rejection
sampler." The chapter also says other systems (speech, particles) use the C library's
`rand`, and that COB's random instruction, unit initial headings and scripted debris are
draws on this generator. Changing call order changes later decisions.

**Register.** `TOTALA-EXE-DATA.md` section 8 has the same: Park-Miller with multiplier
16807 and modulus 2^31 - 1, the state at `ds:0x51FC88`, the seed rule, "uniform over
`[0, n)`, exclusive of the top", and the early return for `n < 2` that does not advance.
**The chapter and the register agree entirely.**

**RWE: DIVERGENCE (representation), and one behavioural detail.**

- **The generator is a different one.** The simulation's stream is `std::minstd_rand`
  (`GameSimulation.h` about 477), whose multiplier is **48271**, not 16807. The same
  modulus, the same family, a different sequence. `SimRandom.h` takes the raw output
  modulo the range, which is the original's reduction, and its comment ("what the
  original's own rand-below-n does anyway") is correct about the *reduction* and silent
  about the *generator*. Nothing a lockstep peer, a save or a replay can see depends on
  this, because RWE's streams only have to agree with each other. It would matter only
  for something that wants to reproduce the original's actual sequence from a seed, such
  as reproducing a recorded TA game's random draws; the demo tools do not, because a demo
  records outcomes and not seeds (`TA-DEMOS.md`). Seeding by the original's rule is
  likewise absent; RWE seeds from the game seed.
  Classification: **representation**, not a gap, unless a bit-exact TA stream is ever
  wanted. Reaching it would be a change to the generator type and the seed mapping
  together, and would change every existing replay and save.
- **The range-below-two rule: DIFFERS in a way no result shows.** `randomBelow`
  (`SimRandom.h`) returns zero without drawing only for a range of **zero**; a range of
  **one** takes a raw draw and returns zero. The original advances on neither. The
  returned values are identical (zero) and a stream that includes such a draw is
  simply one draw further on, so the only effect is that RWE's later draws differ from a
  hypothetical stream that skipped it. `GameSimulation.cpp` about 4954 guards the one site
  the register calls out (the building twist, `buildangle >= 2`), which is the
  observable case and is **MATCHES**.
- **Stream separation: MATCHES the discipline.** The chapter says the speech and particle
  draws are on the C library's generator, not the game's. RWE's presentation draws use a
  separate `effectsRng` (`GameScene.h` about 1125), which is the same separation and keeps
  presentation off the sim stream, as `CLAUDE.md` requires.

## 4. Draw ranges: exclusive upper bounds at the call sites

**Reference (section 2 with register section 8 and the weapons section's wind rule).** The
original's draw is half-open: the largest value it returns is the range minus one. A
caller that wants an inclusive upper bound has to add one, and a caller that does not
gets an exclusive bound.

**RWE.** `randomBetween` (`SimRandom.h`) is **inclusive** of the upper bound, and most call
sites were written with that in mind. Where the register says what the original asks for,
the sites are:

- **Building twist: MATCHES.** The register rule is a draw below `buildangle` minus half of
  it, taken only when `buildangle` is at least two for a non-mobile unit
  (`GameSimulation.cpp` about 4947-4956; `buildangle.test.cpp`).
- **Wind direction: MATCHES.** The register draws below 0x10000; RWE draws `[0, 65535]`
  inclusive, which is the same set.
- **Wind speed: DIFFERS (small).** The register (`TOTALA-EXE-WEAPONS.md` near 494) says the
  speed is the minimum plus a draw **below** `max - min`, so the maximum is never
  reached. RWE draws `randomBetween(min, max)` (`GameSimulation.cpp` about 4383), so the
  **maximum is reachable** and each speed is one draw more likely to be higher. The mean
  moves by half a unit on a range of hundreds to thousands, and a map with equal minimum
  and maximum gives the same speed. Effect: the wind generator's output factor and the
  wind push on shells; both too small to see in play, but it is a literal one-off.
- **Wind change interval: UNCLEAR.** RWE changes the wind every `randomBetween(5, 14)`
  seconds (about 4381). Neither the chapter nor the register states the original's
  interval; the register's wind material is about the speed and direction only.
- **COB `RANDOM`: UNCLEAR.** RWE's handler (`cob.cpp` about 291) is `randomBetween(low,
  high)` with both ends included and `low` returned when `high <= low`. The chapter names
  the COB random instruction as a consumer of the original generator but does not give its
  range convention, and neither does the register. Whether the original draws below
  `high - low + 1`, below `high - low`, or something else is the question; the two
  readings differ at the top of every script range. This is the one with reach: every
  shipped script's `random(low, high)` (smoke timing, wobble, idle animation) is built on
  it. Settled by a spec-room read of the instruction's handler.
- **`randomdecay`: covered elsewhere.** `BT-WEAPONS-WEAPON-TYPES-PARITY.md` rule 4 records
  it as MATCHES in shape; the bound there (`randomBelow(decay + 1)`) is inclusive of the
  decay, and the register does not say which the original uses. Not re-raised.
- **The AI's draws** use `rng() % n` with RWE's own ranges; `BT-AI-AI-PLAYER-PARITY.md`
  already marks them `[open]`.

## 5. The approximate distance

**Reference (section 3).** `0x4b6e40` returns the larger of `|dx|` and `|dy|` plus one
quarter (an integer shift) of the smaller. It is not a square root. It does not guard the
most negative integer through the absolute value, and does not guard the final addition's
overflow.

**Register.** No mention. Neither `ApproxDistance` nor `0x4B6E40` appears in any
`TOTALA-EXE*.md` or in the spec-room findings.

**RWE: UNCLEAR, and potentially behavioural.** RWE has no such function; its range,
radius and distance tests use true Euclidean distance (`hypot`, squared comparisons). The
two differ by a known amount, which is the whole content of this rule: for a vector at
angle `a` from an axis, the approximation divided by the true length is 1 on an axis, rises
to about **1.031** (3.1 percent too long) where the smaller component is a quarter of the
larger, and falls to **0.884** (11.6 percent too short) on the diagonal. So, if some range
test in the original uses it, a target on the diagonal is accepted out to about 13 percent
beyond the stated range, and one at a quarter slope is refused 3 percent short.

**Whether it matters is unknown, because the chapter lists no consumer.** The register's
range findings (`TOTALA-EXE-WEAPONS.md` near 1424 and the `NAVAL-SYSTEMS-PARITY.md` reach
rules) say what the weapon range test measures and none of them mentions this function, so
the weapon test probably does not use it; where else it is called (pathfinding cost,
sight, AI scans, scroll or sound culling) is a **spec-room cross-reference of `0x4B6E40`**
and is the only way to size this item. Until that read, RWE's Euclidean distance is not
known to be wrong anywhere.

## 6. Angle units, the 512-entry table and the two sine flavours

**Reference (sections 4, 5).** A turn is 65536 units. The conversion constants are pi over
32768 and its inverse. The table's amplitude is 8192 (13 fractional bits). Two kinds of
entry exist:

- **The plain sine and cosine entries** (`0x4b70a0`, `0x4b70c0`) take the low 16 bits of the
  angle, shift right by six, clear the low bit of the byte offset, and read the word: the
  element index is the angle shifted right by seven, so **truncation** to 512 samples per
  turn with **no bias**. The cosine entry reads a cosine table that sits 256 bytes (a
  quarter turn of 128 words) after the sine table.
- **The scaled entries** (`0x4b70ef`, `0x4b7123`) sign-extend the angle (the cosine one
  zero-extends and adds a quarter turn), **add `0x20`**, shift right by six and mask the
  offset to `0x3fe`, then multiply the table word by a 32-bit scale, add `0x1000`, and shift
  right by thirteen. The bias is a quarter of a table step. The rounding is
  half-up on the arithmetic shift, "asymmetric for negative half cases". Nothing clamps a
  result that leaves 32 bits.

**Register.** `TOTALA-EXE-WEAPONS.md` near 1438 gives the scaled entries exactly (the same
bias, the same mask, "512 entries per full turn", entry 128 is 8192, one step is 128 of
65536, 0.703 degrees) and records that RWE's `sin` and `cos` are the correctly rounded
`detSin` and `detCos`, "more precise here than the original, not less", with a measured
effect on fall of shot (`TOTALA-EXE-WEAPONS.md` near 1560: the two solvers agree to one part
in 65536, a Big Bertha's solved pitch being 4167 in the original and 4168 in RWE, and the
rounds landing at 4065 and 4071 against an aim of 4096; the residual comes from where each
engine takes gravity in the tick, not from the table). That is the same reading as the chapter's.

**RWE: DIVERGENCE (documented, representation).** `sin(SimAngle)` and `cos(SimAngle)`
(`SimAngle.cpp` about 24-31) convert to radians and call `detSin` and `detCos`; there is
no table. This is the intended exact-rather-than-coarse replacement and `CLAUDE.md` forbids
the C library here. The behavioural consequence of the coarse original is that
a shell leaves a barrel on one of 512 pitches and a moving unit's velocity is built from
one of 512 headings; RWE's are continuous. The register records the weapon effect; **the
unit-movement effect is not recorded**. A tank ordered along a heading that falls between
two table entries is moved by the original along the entry the biased index picks, so its
direction error runs from 0.17 degrees short to 0.52 degrees long, which is up to about 0.9
world units of sideways drift per 100 travelled. That is below a map square over any
realistic leg and not worth modelling, but it is the honest size of what the replacement
hides.

- **The plain, unbiased entries: MISSING from the register.** The chapter distinguishes the
  truncating, unbiased lookup from the biased, rounding one. The register knows only the
  biased pair. Where the plain pair is used (rendering projection, particle direction,
  COB trigonometry, minimap) is not stated. If a consumer of the plain pair affects a
  simulation value, RWE's exact sine differs from it by up to a full table step (0.703
  degrees) rather than the biased pair's up to three quarters of one. A **spec-room
  cross-reference** of `0x4b70a0` and `0x4b70c0` names the consumers.
- **Wrap rule: MATCHES.** The 16-bit angle wraps modulo 65536 in both (`SimAngle` is a
  `uint16_t` and its arithmetic wraps).
- **The cosine-after-sine table offset: MATCHES in meaning.** Cosine as sine a quarter turn
  on is what `detCos` is, and the register's wind vector (`TOTALA-EXE-WEAPONS.md` near 494)
  already depends on it.

## 7. Heading from a vector: operand order and rounding

**Reference (section 6).** `0x4b715a` loads its first integer, then its second, runs the
two-argument arctangent so the **first** is the numerator, scales by the inverse constant,
and converts with the processor's current rounding mode. So the result is `atan2(first,
second)` in angle units: first zero and second positive gives zero; first positive and
second zero gives a quarter turn. The chapter says the source file's own overview has the
operands reversed and corrects it from the instructions. The routine does not mask the
result to an unsigned word, does not check a zero vector, and does not set a rounding mode.

**Register.** `TOTALA-EXE-MISSIONS.md` near 52 and `TOTALA-EXE-MOVEMENT.md` near 1752
record `Angle(v) = atan2(v.x, v.z)` as a 16-bit turn at `0x4B715A`, `HeadingTo` computing
`atan2(unit - aim)` with the two negations cancelling, and `atan2(0, 0)` being bearing 0.
**Same reading.**

**RWE: MATCHES, with two representation details.**

- **Operand order: MATCHES.** `atan2(a, b)` in `SimAngle.cpp` (about 20-23) calls the
  two-argument arctangent with `a` as the numerator and `fromRadians` produces the unit. The
  sim's callers pass `(x, z)` (`UnitBehaviorService.cpp` about 2378, `TaWireTape.cpp` about
  92), the register's order.
- **Rounding: representation.** `fromRadians` rounds half away from zero (`std::round`) then
  converts to a 16-bit value; the original rounds by the ambient x87 mode (to nearest, ties
  to even, under the default control word; truncation if some earlier code left it so). The
  two differ only on an exact half unit, which a float arctangent essentially never
  returns, and `atan2(0, 0)` is zero in both.
- **Negative results: UNCLEAR, robustness.** `fromRadians` casts a possibly negative rounded
  float to an unsigned 16-bit value. The conversion of a negative float to an unsigned type is
  undefined in the standard, though it wraps correctly on the three compilers CI uses,
  and `SimAngle.test.cpp` and the replay hash tests would show it if one stopped. The
  original keeps the signed value and the caller narrows it. A cast through a signed integer
  would remove the question; no value changes.

## 8. In-place rotation, the three-angle sequence and the shared 13-bit matrix

**Reference (sections 7, 8).** The two-coordinate helper (`0x4b7173`) returns immediately
for a zero low-word angle, otherwise converts the signed angle to radians and uses
`fsincos` (not the table), computing `u cos - v sin` and `u sin + v cos` from the original
pair, **converting each to an integer by the processor's rounding mode** and writing both
back in place. The three axis rotations apply it to X/Y, X/Z and Y/Z. The three-angle
routine (`0x4b6cc0`) rotates X/Y by the first angle, then Y/Z by the third on the resulting
Y and the original Z, then X/Z by the second, **with integer rounding after each step**: "not
an arbitrary interchangeable Euler order or one unrounded floating matrix product". The
shared matrix builder (`0x4b71a7`) reads three angle words from offsets `0x0c`, `0x0e`,
`0x10` of its argument, uses the coarse table values, **shifts every product right by 13
before combining**, stores nine coefficients in a global, and the apply entry (`0x4b72e2`)
shifts each of three products by 13 and adds. The apply writes X before rereading input X
for Y and Z, so identical in and out pointers are not safe.

**Register.** `TOTALA-EXE-SHADING.md` near 165 records the model vertex rotation as
`0x4B6CC0` then `0x4B7173` with `fsincos` and an integer conversion, on 16.16 vertices.
The matrix path and its 13-bit truncation are not recorded.

**RWE: DIVERGENCE (representation) with one behavioural question.** Piece posing uses float
matrices (`Matrix4f`, `matrix_util`) whose trigonometry goes through `detSin` and `detCos`
for determinism; no stage rounds to an integer and none is coarse. A rendered model is
therefore a few thousandths of a unit away from the original's per-step integer rounding,
which the register accepts for shading ("the geometry is exact enough"). The question is
the **simulation-visible** uses: where a piece's rotated position is read back as a number
the simulation keeps (a weapon's muzzle point from `QueryPrimary`-style queries, a
nanolathe nozzle, a transport attach point, the ground-contact vertices). If the original
computes such a point with the 13-bit matrix path, its coordinates are integers truncated
at every product, and RWE's are not. The difference is a fraction of a world unit per
point, which can matter only where a figure is later compared against a threshold or hashed;
it has no effect on the lockstep hash because both peers are RWE. Settled, if it ever needs
settling, by naming the callers of `0x4b72e2` and `0x4b71a7`, which the chapter does not.

- **Which of the two rotation back-ends a simulation point uses: UNCLEAR.** Fsincos-based
  in-place pairs (rounded to integer per step) versus table-based matrices (truncated per
  product) give results one or two units apart in the worst case on a large model. The
  chapter names no consumer of either for a sim value.
- **Shared mutable matrix: not applicable.** The original builds one global matrix and a
  following apply uses whichever was built last, with no save and restore. RWE has no
  global transform; every caller has its own.

## 9. Wide multiply, multiply-divide and the vector helpers

**Reference (sections 3, 5).** The entry at `0x4b70e0` returns the high 32 bits of the
signed 64-bit product (not an ordinary low-word multiply). `0x4b7381` forms the signed
64-bit product of two integers and divides by a third **truncating toward zero**, returns
zero for a zero divisor without dividing, and traps if the quotient does not fit 32 bits.
The vector helpers are the dot product (three float products summed in the stated order,
returned as a double), the component-wise difference on float triples, the difference on
integer triples **subtracted before conversion to float** (so it can overflow), the cross
product, normalisation (squared components, square root, divide; **no zero guard**) and the
float length.

**RWE.** The register has none of these. RWE's `Vector3f` and fixed-point types
(`Vector3x.h`, `SimScalar`) are its own; their zero-vector behaviour was not audited.

- **Multiply-divide: UNCLEAR.** The only behaviour that could reach a sim value is its
  truncation toward zero (and the zero-divisor result of zero). RWE computes the
  fixed-point products it needs in `int64_t` (for example the projectile lifetime
  in `GameSimulation.cpp` about 2455-2460, which divides in 16.16 integers and explicitly
  keeps the round-number result), which truncates toward zero as C++ integer division does.
  Whether each of RWE's call sites corresponds to a call of this entry is not checked and
  cannot be without the chapter naming its callers.
- **High-word multiply: UNCLEAR.** Same: the entry is a fixed-point multiply used by
  unnamed callers.
- **Zero-vector normalisation: not applicable to sim.** The original divides by zero and
  carries non-finite floats on; RWE's sim uses guarded helpers and the fixed type. No shipped
  data produces a zero vector that reaches it as far as the register shows.

---

## 10. Gaps to fix

Ranked by how likely each is to change a value the game uses. None changes a shipped game's
result that anyone has reported; the first two cost a spec-room read, not code.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **COB `RANDOM` range convention**: RWE draws the inclusive range `[low, high]`; the original's convention is not stated in the chapter or the register. Every shipped script that calls `random` is affected at its top value. | UNCLEAR, behavioural | spec-room read of the instruction handler |
| 2 | **Consumers of the approximate distance (`0x4B6E40`)**: RWE has no counterpart. If any range test uses it, the original accepts about 13 percent further on a diagonal and refuses 3 percent short at a quarter slope. | UNCLEAR, possibly behavioural | spec-room cross-reference of the address |
| 3 | **Wind speed draw is inclusive of the maximum** where the register draws below `max - min`. One-off in the top value. | DIFFERS, small | trivial code change plus the register's wording; changes the wind sequence of every existing replay and save |
| 4 | **Consumers of the plain (truncating, unbiased) sine and cosine entries**: unrecorded; RWE's exact sine differs from the table by up to one step (0.703 degrees). | UNCLEAR | spec-room cross-reference of `0x4b70a0` and `0x4b70c0` |
| 5 | **The wind change interval** (5 to 14 seconds) has no source. | UNCLEAR | spec-room read |
| 6 | **Unit movement heading is continuous in RWE, quantised to 512 in the original**: a recorded divergence for shots, not for walking. | DIVERGENCE (documented for shots) | add one sentence to `TOTALA-EXE.md` section 88 if desired; no code |
| 7 | **Which rotation back-end a simulation point uses** (fsincos pairs versus the 13-bit matrix). | UNCLEAR | spec-room naming of the callers of `0x4b72e2` and `0x4b71a7` |
| 8 | **The generator is the 48271 minstd, not the 16807 Park-Miller**, and the original's seed rule is not reproduced. Not a gap for lockstep. | DIVERGENCE (representation) | decision only; do not change without wanting every replay and save to change |
| 9 | **`fromRadians` casts a negative rounded float to an unsigned 16-bit value.** Works on all three CI compilers. | robustness | a cast through a signed integer; no value changes |
| 10 | `randomBelow(1)` draws where the original does not. | DIFFERS, no visible effect | none |

Everything in the table is read-only analysis. No source file was changed.
