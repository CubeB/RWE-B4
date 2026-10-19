# Lava ground exclusion

Owner-authorized correction, 2026-10-19; reported map: Lava Run.

Implementation authority is spec-room findings BT-264 (commit 0d0d05f8) and
BT-265 (fd8ad838), docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md in the separate
D:/RWE-spec-room documentation checkout. Both compare byte-tactics revision
97d7ac59b460417bcdfa629cf90da99f31818c65, retail GOG v3.1. Implementation
has read only prose, never the decompilation.

The static reference evidence is map preparation at 0x4833B0 and 0x483610,
four-corner bounds at 0x483210/0x483370, path-cell and footprint rejection at
0x47DE60/0x47DFC0, and feature placement/removal at 0x423C50/0x4246B0.
On lavaworld maps, a cell is excluded when the minimum of its four unsigned
height corners is at or below sea level, unless it holds a real feature
origin. Empty and continuation cells are marked after initial features.
Ground classes, including commander, amphibious and hover classes, reject
exclusions before depth and slope. Aircraft use separate navigation.
Ordinary removal never regenerates the mask; removal of an exempt origin
leaves that origin exempt, and marked continuations remain excluded.
Normal feature placement refuses a marked cell during its row-major clearing
pass; earlier removals are not rolled back. No new border or landing rules.

RWE represents this result separately from its feature occupancy grid, once
after initial features and before building movement caches. The retained
feature references are internal bookkeeping; exclusion takes precedence for
ground traversal and subsequent ordinary placement. RWE JSON save state,
hashes and diagnostic dumps preserve the exclusion indices. JSON saves are
RWE-original, not the original binary save protocol: this work does not copy
the original's feature-restoration losses described by BT-265. Older JSON
saves without exclusion state retain the freshly loaded map's exclusions;
there is no claim of identical continuation for an old lava game played
before this fix. Command replay format is advanced for changed semantics.

Confidence is high for the static interior-cell rule and marker lifetime.
No original-game movement or save was executed. Border initialization,
forced placement, air landing and modded terrain changes remain unverified.
Verification results will be recorded after implementation.

## Shipped fixtures and before/after check

Lava Run from D:/RWE-Data/totala2.hpi has lavaworld=1, a 400x80
height grid and sea level 21. Shoreline patches at (133,42) and (60,41)
provide the nine height bytes in lava.test.cpp. The first has a four-corner
minimum exactly at sea level; the second is five units below it. Both pass
ordinary water/slope checks for the shipped TANKDS2 (2x2, max depth 100,
max slope 32) and TANKSH2 (2x2, max depth 12, max slope 15) classes in
totala1.hpi gamedata/moveinfo.tdf. Smudge01 and RockTeeth feature fields
come from the same archive's green/smudges.tdf and all worlds/dragonsteeth.tdf.
Test terrain sizes/positions only isolate documented marker lifetime.

The pre-fix native ai_arena replay on Lava Run (seed 7, schema 0) orders
ARMCOM at tick 30 from (48,45,-304) to (48,21,304). After 896 ticks it
reaches (51.8877,44.5768,298.6703), across the central lava strip. This is
an executed RWE reproduction, not an executed original-game check.

## Validation

- Debug game and tests built. Focused lava/save-load/replay checks: 49 cases,
  2,266 assertions passed.
- Full allowed suite: 1,830 cases / 193,670 assertions, plus five packet cases /
  33 assertions; 1,835 distinct cases / 193,703 assertions total, all passed.
  The two prohibited TaChecksums tests were not run.
- Four lava regressions cover shipped commander/kbot classes, four-corner
  inclusive boundaries, non-lava water controls, cached walkability, direct
  traversal/search, feature-origin/continuation lifetime, destructive placement
  refusal, state hashing/dumps/save-load and cache rebuild after restoration.

- All Debug targets built. Native ai_arena repeated the identical commander
  order using shipped Lava Run data. The generated 3,716-cell mask exactly
  matches the interior four-corner <=21 classification on this map (no low
  retained-origin exceptions here). The pre-fix commander occupied mask cells
  in 39 of 180 five-tick samples; the fixed commander occupied none and stayed
  on the dry bank. This verifies RWE movement, not original runtime behavior.
  It follows a partial route along the bank rather than crossing the strip.
- Two fixed native runs produced identical hashes for all 899 recorded ticks:
  SHA256 92c4f48cfe9d5eb0d02d5c6f17d83706850dfa6d0eaed014f77b0d453b3d80c3.
  Scratch evidence: D:/tmp/rwe-lava-native/after/result.json and repeat/.

- COFF section scan passed for all 1,055 Debug objects at the 24,575-section
  budget. Worst remains GameScene.cpp at 24,276; save_util.cpp is 24,107 and
  GameSimulation.cpp is 23,277. No object exceeds the budget.

- Release game built 2026-10-19 15:25:52 local time; --help smoke check exited 0.
  Playable executable: D:/RWE/build-release/rwe.exe, 48,760,298 bytes.
  SHA256 F52F8D4168B0BD7815194244DC15C36B6175DE242EC9D6041F2197A14B6783CA.
  Built before committing this correction: generated source identity is
  c42c44cffd8a07ef4efb430596e13fc20d18f2df-d, including all lava changes
  in this commit. No original-game runtime or visual play check was run.
