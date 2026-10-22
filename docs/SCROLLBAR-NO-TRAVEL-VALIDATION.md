# Scroll bars with no drag travel (#669)

The byte-tactics comparison is the accepted scroll-bar finding in
`BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS.md`: revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, GOG v3.1 `TotalA.exe` (hash and
routine evidence are recorded there). The implementation session read only
the prose, never the decompilation.

The original disables an ordinary list's slider when scrolling is unnecessary
and uses integer pointer displacement for enabled slider input. A zero-range
slider at zero does not emit a position change. RWE instead exchanges
percentages; dividing pointer movement by zero travel could send NaN through
a group message to a list's integer conversion.

The guard suppresses a drag-position update whenever travel is nonpositive,
leaving stored position unchanged. This is defensive handling in RWE's own
percentage mechanism, not a claim that the original uses the same guard or
that malformed widget geometry has verified original parity. It does not
alter ordinary positive-travel dragging, arrow input, or background paging.
The accepted finding proposes no deliberate departure requiring a divergence
entry. No simulation state, hash, save or replay command stream changes.

Tests deliver mouse-down and mouse-move events to a real `UiScrollBar` with
valid, untextured sprite frames. The geometry is constructed because the
fault depends on travel, not bitmap pixels; no GL context or rendered-parity
claim is involved. Full-track and oversized thumbs receive stationary and
moving events, emit no position/group messages, and retain a finite stored
position. A positive-travel control still emits movement and clamps at both
ends. The oversized case is explicitly RWE defensive validation.

No original-game execution or rendered short-list comparison was performed.

## Executed RWE checks (2026-10-22)

On the isolated #646 implementation (`549d31e4`) plus this guard:

- Debug `rwe`, `rwe_test`, and `make -j6 -k all` passed.
- `[scrollbar]`: **30 assertions in 2 cases passed**.
- Full Debug suite with `RWE_SCENARIO_DATA=D:/RWE-Data` and the single filter
  `~*checksum*`: **198,611 assertions in 1,908 cases passed**. The checksum-named
  cases were excluded per the standing instruction.
- The changed scrollbar object has 2,038 COFF sections and its new test has
  1,634, both below the 24,575 budget. The preceding whole-tree scan passed
  across 1,054 objects, with the largest at 24,276; no production header changed.

The first test build found an invalid null texture-handle construction in the
fixture; it was corrected to an empty engine handle before these passing runs.
