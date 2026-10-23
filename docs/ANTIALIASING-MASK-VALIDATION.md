# Antialiasing mask regression, 2026-10-23

## Follow-up: reported grid remains after the first correction

The user still observes a grid when toggling AA. The earlier raster fixture
explicitly disabled mipmapping, so its success did not establish identical
terrain sampling. AA-PHASE-01 and AA-ELIGIBILITY-01 require terrain to bypass
the original's cached-image filtering; RWE's mipmapped OpenGL tile arrays have
no counterpart in that reference path. The next check uses shipped TNT tiles,
the production terrain vertex/fragment shaders and tile draw service, comparing
native output against the sharp supersample at several zooms and camera
positions. A doubled viewport changes implicit texture derivatives and hence
mip selection even when the final resolve selects just one sharp sample.
Any footprint correction is RWE-only renderer maintenance, not a new claim of
original-game mipmapping parity.

The production test reproduces differences at both minified and slightly
magnified zooms (including 168 differing pixels at zoom 1.01, summed across
three camera positions). Correcting mip bias alone cannot guarantee identical
tile-boundary rasterization. The chosen repair renders terrain once at native
resolution with the unchanged native projection, then copies colour and its
categorical mask into the supersampled world with nearest sampling. It leaves
object AA in the supersampled pass. This implements the documented requirement
that toggling cached-object filtering must not rerasterize/filter the ground;
the intermediate OpenGL framebuffer/copy is RWE-only machinery.

The follow-up Release probe reproduces the old path's differences at zooms
0.5, 0.75, 1.01 and 1.25; 1.0 and 2.0 happen to align. With the native ground
pass, all four supersamples of each terrain pixel and each categorical label
match the native source exactly at all six zooms and camera offsets 0, 1 and
17. The complete postprocess is additionally checked with both Units Sharp
and Units Smooth against the original native RGB pixels. These are executed
RWE GPU comparisons using real shipped tile colours, not original-game runs.
The fixture arranges the first sixteen `Coast To Coast.tnt` tiles into a
repeating mosaic to expose tile boundaries; it does not claim to display the
original map layout. Logs: `grid-baseline.log` and `grid-fixed.log` in the
local validation directory; the final complete-resolve checks are in
`grid-final.log` (zero failures, `GL_NO_ERROR`). The playable Release `rwe`
target was rebuilt successfully with this terrain correction. The earlier
full unit-suite count below predates
this terrain follow-up.

Implementation-room investigation from AA-ELIGIBILITY-01 and AA-PHASE-01 in
`ANTIALIASING-ELIGIBILITY-FINDINGS.md`. The implementation room did not open
byte-tactics. Its reference identity, evidence and static-only limitations
are recorded in that finding.

## RWE-specific findings and intended correction

The second world colour attachment stores categorical labels, not opacity.
`attachFrameBufferMaskBuffer` disables blending for draw buffer 1 once, but
Dear ImGui's OpenGL backend restores the global `GL_BLEND` enable after UI
drawing. That enables blending on every draw buffer, including the mask.
Later terrain/sprite/model writes blend their labels and palette indices.
For example, repeated terrain overdraw changes green and alpha, then a sprite
inherits some green; the postprocess can classify otherwise identical objects
differently depending on the background. Live-piece labels also drift.

Restore the unblended-mask invariant when selecting both world draw buffers,
after any global state restore. Preserve ordinary colour blending. This is a
repair to RWE-only MRT machinery, not an original-game algorithm. Indexed
blend enable/disable is supported by OpenGL 3.0 (section 4.1.7 of the core
specification), so no OpenGL 4 per-buffer blend functions are required.

Billboard features get an explicit sharp marker, independent of Units Smooth.
They must remain sharp even over a cached building. The optional units
extension remains available for mobile models and nanoframes; narrowing its
former sprite scope follows the user's 2026-10-23 report. Actual 3D features
are distinguished from billboard features by AA-ELIGIBILITY-01.

The existing screen-space resolve is an approximation to per-image filtering.
Its sharp sample currently lies a quarter native pixel away from the native
raster centre. AA-PHASE-01 establishes that the original does not introduce
that global phase change. Any correction must be checked against a native
render, including both axes, rather than inferred from a CPU predicate test.

## Executed GPU checks

The hidden SDL/OpenGL `ui_probe --antialias <shipped-data-directory>` runs
the production postprocess, billboard and unit shaders. Its controlled labels
reproduce the UI backend's global blend restore, then perform actual MRT writes
and framebuffer readbacks. This exercises pipeline state rather than a CPU
copy of the shader's classification predicates.

On the local NVIDIA GeForce GTX 1060, the baseline reproduced changing category
labels after terrain overdraw. After the correction, three frame resets preserve
terrain, mobile and live-piece labels, palette indices and ordinary colour
blending. Half alpha quantizes to 127 on this attachment; checks permit one
byte of conversion rounding, far below the classification thresholds.

The corrected probe passes cached-building, terrain, mobile, live-piece and
billboard resolve cases with both Units Sharp and Units Smooth. Additional
occlusion cases keep a sharp native billboard/mobile sample from inheriting
the filtering of cached building samples elsewhere in its block.

The actual shipped `anims/ArchiTrees.GAF/Palm01` frame writes sharp labels on
its opaque silhouette while preserving the underlying terrain in transparent
parts. Actual `PALETTE.PAL` and `PALETTE.SHD` data produce the expected unit
fragment colours at strengths 0, 40 and 100, and both AA resolve modes preserve
those shaded colours. The production vertex/fragment pair, fed shipped
`objects3d/CORSOLAR.3do` geometry and normals with a controlled constant
material, changes 3,561 pixels when forced from strength 0 to 40. That isolates
shader functionality; it intentionally does not emulate the unit script's
shade-disable commands or claim that stock CORSOLAR should be shaded.

SOLAR-SHADING-01 in `SOLAR-SHADING-FINDINGS.md` establishes why the reported
solar-collector shading is expected: its script disables shading on every
piece, while only the base remains cached. The probe also executes the
shipped `scripts/CORSOLAR.cob` creation function in RWE's COB VM and verifies
that it emits all ten shade-disable commands and nine cache-disable commands,
leaving the base cached. It stops at the first environment-dependent yield;
this is not a complete simulated game or an executed original-game check.

A native-versus-supersampled raster comparison reproduces the old quarter-pixel
phase error and finds no differing sharp samples after alignment, across four
fractional camera offsets on both axes. This fixture uses nearest texture
sampling to isolate raster phase; it does not establish equivalent mip choices
at every zoom or original per-image edge composition.

The corrected GPU run reports zero failures and no OpenGL errors. Logs are
local artifacts in `D:/RWE-aa-check-20261023` (`probe-before.log` and
`probe-cob.log`, including the final script checks).

The Debug `rwe` target links successfully. The Debug suite, invoked as
`rwe_test '~*checksum*'`, reports 1,924 cases: 1,922 passed, two skipped;
all 196,923 executed assertions pass. The skipped data-dependent cases request
`RWE_SCENARIO_DATA`. Checksum tests were excluded as instructed. The GPU
fixture independently reads the installed shipped archives.

The playable `build-release/rwe.exe` also builds successfully. The optimized
Release `ui_probe --antialias` passes the same GPU, script and native-raster
checks with zero failures and `GL_NO_ERROR` (`probe-release.log`). Every file
in `build-release/shaders` matches its source counterpart by SHA-256; there
are no stale runtime shaders. No visible game or original executable was
launched for these checks.

No executed original-game check has been performed; static reference findings
and RWE GPU checks do not establish complete pixel parity. The retained
screen-space approximation and its silhouette/phase limits are recorded in
`TOTALA-EXE.md` section 88. In particular, aligning the sharp sample moves the
centre of the four-sample cached filter by a quarter native pixel relative to
an unshifted supersampled scene; a per-object cache would be needed to remove
this approximation entirely.
