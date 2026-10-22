# Unit sound category binding (#584)

## Reference and accepted finding

This change implements **BT-133**, the prose finding in
`D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md`, under
"The missing or unmatched sound category". It resolves the uncertainty in
`BT-SOUND-PARITY.md` section 4.

The reference is byte-tactics revision
`97d7ac59b460417bcdfa629cf90da99f31818c65`, the matching decompilation of
retail GOG v3.1 `TotalA.exe`. The spec room inspected the category binding at
`0x42CC3E`–`0x42CCB8` and the case-insensitive comparison at `0x4F8A70`.
The implementation session read only those prose findings, never the
decompilation.

The documented result is that category names ignore case. An unmatched
value undergoes integer conversion; missing and non-numeric values select
index zero. Numeric values select the corresponding category in file order.
In shipped SOUND.TDF, category zero is ARM_KBOT. The finding rates the
fallback high confidence; the category order is corroborated by the shipped
data census in `BT-SOUND-PARITY.md`.

## Implementation and limits

The sound loader retains top-level file order rather than iterating the TDF
block hash map. The media database resolves names without case, then resolves
numeric indices. Empty tables and out-of-range indices safely return an empty
sound class. **The reference finding does not establish the original's
out-of-range behaviour**; that guard is defensive handling, not a parity claim.
Duplicate-category definitions retain the existing parser's treatment and are
outside this finding's acceptance scope.

This affects presentation only. It adds no simulation state and does not
change the sync hash, saved games or replay command streams.

## Shipped-data checks

The implementation session extracted entries using `tools/hpi.py` from
`D:\RWE-Data`. `rev31.gp3`'s `gamedata/sound.tdf` starts with ARM_KBOT
(`select1=kbarmsel`, `ok1=kbarmmov`), then COR_KBOT
(`select1=kbcorsel`, `ok1=kbcormov`). These values form the bounded regression
fixture. The shipped `units/cormex.fbi` in `totala1.hpi` declares CORE_MEX;
`units/corvoyr.fbi` in `rev31.gp3` declares COR_Kbot. Tests cover those
spellings, the other unmatched shipped category names, missing bindings,
numeric indices and bounds.

No original executable was run, no listening check was performed, and no demo
oracle was scored. A listening check remains available: select a Core Metal
Extractor in the original and compare its acknowledgement with ARM_KBOT.
This record distinguishes the static reference finding from executed RWE
checks; the latter are recorded below when complete.

## Executed RWE validation (2026-10-22)

- Debug `rwe`, `rwe_test`, and `make -j6 -k all`: passed.
- Release `rwe`: passed; `build-release/rwe.exe` rebuilt.
- `RWE_SCENARIO_DATA=D:/RWE-Data build/rwe_test "[soundcategory]"`:
  **285 assertions, 3 cases passed**. This includes loading the real archives
  through `constructVfs`, checking all 120 loaded categories' numeric selection
  slots and named acknowledgement slots, and resolving the actual CORMEX and
  CORVOYR FBIs. The other two cases use the bounded shipped-value fixture and
  an empty table.
- Broad suite with the single filter `~*checksum*`: **1,914 cases, 1,912
  passed and 2 failed; 196,457 assertions, 196,455 passed**. All seven
  checksum-named cases were excluded. The earlier two-exclusion filter did
  not exclude them as intended; that initial run reached the map-checksum
  case and skipped it for missing HOME data. No checksum data verification
  was completed.

The two broad-suite failures are in untouched `sim/worksounds.test.cpp`:
"a builder announces a feature reclaim exactly once, on the tick the work
starts" (line 152) and "each new job is announced again" (line 234). Both
require reclaim completion within 60 ticks. The existing uncommitted
`UnitBehaviorService.cpp` changes implement the RA-04 countdown; the fixture's
100 metal and 50 energy give a 90-tick timer before the additional ground
visit. Those simulation changes predated this audio change and are not in its
commit. A baseline rebuild with them removed was not run. This is a reported
suite limitation, not a claim that the whole workspace passes.

Logs from this session are in `D:\tmp\rwe-584-{debug-build,test-build,
all-build,release-build,suite-final}.log`. No original-game execution or
listening check was added by these RWE runs.
