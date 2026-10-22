# Stored bookmark recall and camera tracking (#653)

This implements the accepted bookmark-recall finding in
`BT-CATEGORY-CAMERA-KEY-FINDINGS.md`, based on byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, GOG v3.1 `TotalA.exe`, routine
`0x41D3F0`. The register records the executable hash, evidence and limits.
Only prose passed to implementation; the implementation session did not read
the decompilation.

A successful stored-bookmark recall leaves unit following and clears its
retained unit target. Saving a bookmark does not leave following. The general
camera-position setter is unchanged: the direct reference check refutes the
old audit's claim that F3 and n cancel following. No such departure is added.
RWE's existing unsaved-slot handling and zoom behavior are outside this fix. Manual
middle-mouse panning retains its existing behavior; only unit-follow mode is
exited by the new code.

The regression uses the real scene's input handlers in the headless scenario
harness, a shipped ARM commander, and held Control during bookmark storage.
Split key-down/key-up injection and a camera-follow inspection are RWE-only
test instrumentation, with no original-game counterpart or parity claim.
The scenario checks that saving retains following, recall stops it through
subsequent updates, the saved position is retained, and n/F3 input retain
following. No simulation state or file
format is added.

The source comparison is static. No original-game execution or demo oracle
was run; an executed RWE scenario is not an original-game check.

## Executed RWE checks (2026-10-22)

- Debug `rwe`, `rwe_test` and `make -j6 -k all` passed, as did Release `rwe`.
- `scenario --all --data-path D:/RWE-Data --seed 7`: **all four scenarios
  passed**, including `653-bookmark-exits-tracking`. The new case exercises
  saving with Control held, successful recall, saved-position retention over
  updates, and later n/F3 behavior. Its F3 check has no recorded
  attack/message; the qualifying original F3 jump is established by the static
  finding, not this runtime case.
- Full Debug suite with `RWE_SCENARIO_DATA=D:/RWE-Data` and `~*checksum*`:
  **198,896 assertions in 1,911 cases passed**. Checksum-named cases were
  excluded per the standing instruction.
- Changed COFF objects remain below the 24,575 budget: `GameScene.cpp` 24,292,
  `GameScene_input.cpp` 14,738, `ScenarioDriver.cpp` 4,543, and `scenario.cpp`
  3,996. No new `GameScene` data member or simulation state was added.
