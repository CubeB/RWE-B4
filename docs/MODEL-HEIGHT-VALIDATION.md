# Recursive model height (#646)

The implementation follows the accepted scalar-height finding in
`BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS.md`, based on byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, GOG v3.1 `TotalA.exe`, routine
`0x4CB5F0`. That register records the executable hash, evidence and limits.
Only the prose finding passed to implementation; the implementation session
did not inspect the decompilation.

Both unit and projectile mesh loading now use a scalar height measurement.
Each level floors its translated vertex and child-list contributions at zero.
An empty leaf's offset supplies no geometry; an existing empty child-list can
still contribute its parent's offset. The existing highest-vertex helper
retains its point-valued contract.

The regression cases use all seven arithmetic examples in the finding and
separate sibling-offset and parent-vertex cases. These represent mod-only
geometry, so synthetic values are necessary. The retail check loads actual
models through the VFS, compares their scalar heights with the previous
measurement, and checks both real mesh-loading paths without a GL context.

This can change simulation geometry for affected mods: visibility and other
height-dependent outcomes, including old replays, may change. It adds no
saved or hashed fields and changes no file format. Retail stability is checked
separately from conformance on the synthetic edge cases.

No original executable or demo oracle was run. These are static reference
findings plus executed RWE checks; native integer overflow and malformed tree
semantics remain outside the accepted reference finding.

## Executed RWE checks (2026-10-22)

In the isolated `D:\RWE-issues` checkout based on `822678f6`:

- Debug `rwe`, `rwe_test` and `make -j6 -k all` passed.
- `[modelheight]` with `RWE_SCENARIO_DATA=D:/RWE-Data`: **3,050 assertions
  in 2 cases passed**. All 608 model filenames exposed by the VFS retained
  their previous height, and both unit and projectile loaders returned it.
  This checks the VFS-selected files, not all archive versions of each name;
  it is distinct from the older audit's 610-distinct-content census.
- Full Debug suite using the single filter `~*checksum*`: **198,581
  assertions in 1,906 cases passed**. Checksum-named tests were excluded per
  the standing instruction.
