# Map artwork restoration and feature-shadow controls, 2026-10-23

**Historical map-art iteration:** the composed enlarged selector described
below was subsequently removed after MAP-FONT-01/02 identified the actual
font error. Current map-selector behavior and validation are recorded in
`MAP-DESCRIPTION-FONT-VALIDATION.md`. The feature-shadow correction remains.

Implementation-room work from MAP-ART-01 and FEATURE-SHADOW-01 in
`MAP-TEXTURE-AND-FEATURE-SHADOW-FINDINGS.md`. Its reference revision, build,
static evidence and limits apply; no decompilation was read here.

The enlarged map dialog accidentally requested an empty background name,
selecting a generic tiled plate instead of `DSelectmap2`. Restore source
artwork: reuse its heading, outer border, list/picture/text/button frames and
dark material at native pixel scale. Repeating clean interiors and edge
segments accommodates the already authorized expanded description layout.
This composition is an RWE adaptation of the existing layout divergence;
the original has no dynamically enlarged counterpart. Do not stretch the
whole bitmap or replace the readable description with truncation.

The world renderer currently queues flat and standing GAF feature shadows
and model-feature shadows independently of `shadowsEnabled`. Gate all three
paths with that master setting. The original UI action synchronizes its
separate shadow flags, so hiding tree shadows with Shadows Off is conformance,
not a new divergence. Keep rendering the visible feature when its shadow is
disabled. The separate vehicle-shadow preference must not gate trees.

## Executed checks

- Optimized Release `rwe` and `ui_probe` build successfully.
- `ui_probe --map-layout` exercises all 99 installed maps with the actual
  menu dialog and GUI font. Every description is fully visible, no summary
  or button overlaps it, and Cancel dispatch still works after construction.
- The rendered 640 by 480 selector was inspected against the decoded shipped
  bitmap. An initial repeated-border seam was corrected by keeping the whole
  metal edge outside the repeated black interior. The final screenshot is
  `D:/RWE-ui-shadow-check-20261023/map-selector.png`.
- An independent PNG comparison of the authored heading checks 6,052 pixels
  against the same native-scale rectangle in the rendered dialog: zero RGB
  mismatches. This checks both original colours and preservation of art scale.
- All 63 real-GL antialias regression checks still pass with `GL_NO_ERROR`.
- Source review confirms the master setting gates flat sprite shadows,
  standing sprite shadows and projected model-feature shadows, while each
  visible feature render remains independent. No live game toggle was
  executed in this headless check; that limit is distinct from compilation
  and the documented original UI behavior.

The rebuilt Release suite, run as `rwe_test '~*checksum*'`, passes all 196,915
executed assertions: 1,923 cases, 1,921 passed and two data-dependent cases
skipped. Checksum tests were excluded as instructed. Logs and the original
asset comparison are in `D:/RWE-ui-shadow-check-20261023`.
No executed original-game check is claimed.

## Original-size description audit

On the user's subsequent faithfulness question, the real-data probe was
extended to measure each full description at the original width 235 and
height 31 using RWE's active GUI font. Excluding the three safety pixels
added by `UiLabel::getTextHeight`, five of the 99 installed maps need 43
pixels (three wrapped rows): Mounds of Mars, Ror Shock, Steel Jungle,
Temblorian Mist and The Barrier Reef. Ordinary two-row cases occupy 30
pixels and fit; counting the helper's allocation padding would incorrectly
report them as 33-pixel overflows. This audit measures RWE at the original
rectangle size, not an executed original-game render. The initial enlarged-
layout validation did not identify these original-size cases, and the broad
redesign was not required by that evidence. Log: `original-overflow.log`.
