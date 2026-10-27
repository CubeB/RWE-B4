# Build preview, queued footprint and placement sweep heights

Spec-room finding for [#898](https://github.com/CubeB/RWE-B4/issues/898),
2026-10-27. The reported mismatch concerned underwater placement grids,
including tidal generators. This finding establishes which height the
original uses at each stage; it does not claim to reproduce the reported
screen image.

## Evidence and limits

- Reference: byte-tactics revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`, read-only inspection from the
  separate `D:\RWE-boundary-spec` documentation checkout under ADR-0004.
- Build: matching GOG v3.1 `TotalA.exe`, 1,178,624 bytes, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`, executable SHA-256
  `3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
- Static checks: preview and click at `0x4197D0` and `0x419670`; queued
  footprint drawing at `0x438C00`; battle preview rectangle at `0x468CF0`;
  placement height at `0x47D2E0`, `0x47D820` and `0x47DDC0`; ground mobile
  construction at `0x403A20`; definition footprint/model bounds at
  `0x42BF40` and `0x42D2E0`.
- Real data: extracted v3.1 overriding FBI snapshots in
  `D:/RWE-extract/rev31/units/ARMTIDE.FBI` and `CORTIDE.FBI`, compared with
  their base-game counterparts. Relevant fields agree. SHA-256 of the v3.1
  snapshots is recorded below. These snapshots do not prove which archives
  were active in the owner's particular game.
- **No original-game runtime, demo oracle, RWE executable or RWE test was run
  by the spec room.** No RWE source or tests were changed. The reported
  mismatch still needs an implementation-room reproduction.

This refines rule 2 of
[the build-placement parity audit](BT-INGAME-BUILD-PLACEMENT-PARITY.md#2-the-preview-and-which-units-a-placement-click-reaches)
and [the placement-box finding](TOTALA-EXE-RENDER.md#27-the-building-placement-box).
The handoff contains prose, behavioral arithmetic and addresses only.

## BUILD-GRID-001: preview and issued order share the site height

The cursor position selects a footprint-relative grid origin at `0x4197D0`.
The preview rectangle covers that footprint, snapped to the sixteen-world-unit
grid. It is a **flat** rectangle: both Y bounds receive the same height.

For a valid site, the preview uses the height established by the successful
placement test at `0x47D2E0`. For an invalid site, it uses the footprint-height
query at `0x47D820`. The latter still consults the definition's yard-mask
support cells; it is not simply the bilinear terrain sample at the cursor or
footprint centre. Out-of-border invalid queries can return zero under that
query's own bounds rule.

When the placement click is accepted, `0x419670` snaps X/Z to the product
footprint and sets the issued order's **world Y to the preview height**.
Therefore the ordinary original path retains the chosen site height in the
order. A command carrying the seabed height where the preview used a
water-relative height differs even if final unit creation recomputes Y later.

The battle drawing at `0x468CF0` projects the preview's retained bounds;
it does not independently sample terrain beneath the rectangle. Standard
projection is world X minus horizontal scroll plus 128, and world Z minus
half whole world Y minus vertical scroll plus 32. A height discrepancy can
thus look like a vertically displaced build grid even when its X/Z cells
are identical.

## BUILD-GRID-002: water-only yards use sea level minus WaterLine

The placement height at `0x47D2E0` is the minimum low height of the
yard-mask's support cells when a support interval exists. If that interval
is empty, it is **sea level minus the definition's draft**, supplied by the
FBI `WaterLine` key. The original narrows that result to a byte; any
underflow behavior on unusual content is not replaced with a clamp by this
finding. Separate slope, depth, occupancy and maximum-height checks decide
whether the site is valid.

The fallback footprint-height query at `0x47D820` uses the same support
selection and water-relative fallback, while omitting the complete placement
permission check. A computed height alone is not permission to build there.

The original maps yard character **`w` to mask `0x37`**, which has no support
bit `0x08`. Both shipped tidal generators have water-only yards. Their site
and preview heights are consequently water-relative, **not seabed-relative**:

| Product | Footprint | Yard character | MinWaterDepth | WaterLine | Site Y |
| --- | --- | --- | --- | --- | --- |
| `ARMTIDE` | 3 × 3 | `w` throughout effective footprint | 20 | 8 | sea level − 8 |
| `CORTIDE` | 4 × 4 | `w` throughout effective footprint | 20 | 10 | sea level − 10 |

The ARM FBI contains sixteen yard characters despite its nine-cell footprint;
the footprint controls the effective scan. Neither file sets `Floater`.
The water-relative placement result follows yard support and `WaterLine`,
not an inference that every water building must be a floater.

Snapshot SHA-256:

- `ARMTIDE.FBI`:
  `a790d1f34574f3c4350c84b743a4f7820e3f694241229d85fb7def423b2f6e75`
- `CORTIDE.FBI`:
  `42c3a85363ebcfebd520693c47d97cfd84b7ad47652cdbe8646bfbcc06c25eeb`

## BUILD-GRID-003: queued outline and ten-tick sweep use stored order Y

The queued-build marker at `0x438C00` adds the product definition's footprint
bounds to the **stored order position**. The definition's low Y bound is
zero after model loading (`0x42D2E0`). Both projected corners use that low
corner's Y, so the marker lies flat at **the order's world Y**. It does not
replace that height with a fresh terrain-centre sample, sea level, or the
highest model vertex.

The same routine draws the animated horizontal and vertical sweep lines.
Their offsets are fractions of the projected rectangle spans over the first
ten simulation ticks after the order timestamp. The sweep and settled
footprint therefore have the **same underlying plane**. The one-pixel outer
contrast lines do not establish a different world height.

This animation concerns placement feedback, not construction progress or the
under-construction model's wireframe. It is also distinct from the range
circle at `0x438EA0`, whose vertices can be raised using their own terrain
samples. That range-circle terrain rule is not authority to lower a queued
building footprint to the seabed.

Ground mobile construction at `0x403A20` initially snaps X/Z while retaining
order Y. After its real placement permission succeeds, the shared footprint
snap at `0x47DDC0` derives placement Y again for creation. The queued marker
draws whatever order position is currently stored. It is therefore reasonable
for an eventual placement re-evaluation to update that position; the marker
callback itself does not perform such a re-evaluation.

## Implementation contract and remaining checks

- Keep accepted placement height in the build order. Use the stored position
  for queued outlines and their sweeps. An immediate local feedback outline
  standing in for that order should preserve the same height; resampling
  terrain there can make the box jump as it passes from local feedback to
  the queued order.
- RWE's lockstep interval before a local click enters the sim is its own
  architecture. A temporary local feedback record has no separate original
  object identified in this check. Its geometry can follow BUILD-GRID-003
  without claiming that the original used an equivalent network-delay ghost.
- Preserve the distinction between valid-site preview and invalid-site
  footprint-height fallback. Do not recompute a queued site's validity through
  an omniscient renderer query; that can disclose hidden occupancy and change
  a previously accepted marker.
- Verify actual `ARMTIDE` and `CORTIDE` definitions on a shipped water map:
  hover preview, immediate click feedback, queued outline, ten-tick sweep,
  and eventual unit origin should agree on the relevant placement plane.
  Include depth sufficient for both products and record sea level, terrain
  height and `WaterLine`.
- Also verify an underwater extractor whose support mask places it on the
  seabed, and a land building on sloped support. The result must follow each
  product's placement rule rather than lifting every underwater grid to one
  water-surface height.
- Rendering a box at a different height does not alone prove wrong sim
  placement. Check command Y, marker geometry and final unit Y separately.
  No runtime evidence resolving those RWE stages is supplied here, and no
  changes to naval factory construction or D-gun behavior are authorized by
  this finding.
