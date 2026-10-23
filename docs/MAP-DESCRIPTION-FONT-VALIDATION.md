# Correct label font and original map layout, 2026-10-23

Implementation-room work from MAP-FONT-01/02/03 in
`MAP-DESCRIPTION-FONT-FINDINGS.md`. That finding records the byte-tactics
revision, original build identity, static addresses, shipped-data measurements
and limits. The implementation room did not inspect the decompilation.

RWE's ordinary label factory incorrectly loads hattfont12, the larger font
used by buttons/lists. It must use hattfont11, whose entry is also named
`Haettenschweiler (120)`. Font frame widths are the advances; horizontal
hotspots affect ink position only. RWE's existing `bounds.right()` adds a
negative hotspot to the advance, so that is also corrected for this label font.
The raw GAF remains unchanged: a baseline offset of I-height 10 reproduces
the original load-time hotspot shift, and wrapped label rows advance by 14.
Other font resources retain their existing layout metrics unless explicitly
configured; this change does not claim to complete type-7 FNT overrides.

Restore the data-defined SELMAP dimensions and the unmodified `DSelectmap2`
bitmap. Remove the enlarged-dialog layout and composed replacement artwork;
the user's request to emulate the original supersedes the earlier larger-box
workaround. The five alleged overflowing descriptions were measured with the
wrong font, so they did not justify changing the original screen.

Acceptance uses the actual GUI and installed GAF/map data: all five exact
two-row breaks/widths, unchanged 235x31 description area, 14-pixel row spacing,
complete visible glyphs above the size label, unchanged original panel/art
placement, and the remaining installed maps. Static reference findings plus
RWE checks must not be described as an executed original-game comparison.

## Executed RWE checks

- Release `rwe` and `ui_probe` build successfully.
- `ui_probe --map-layout` loads the actual MainMenuScene, installed GUI,
  hattfont11 and 99 selectable maps. The unchanged dialog is (84,12),
  494x420; DESCRIPTION is (60,300),235x31 and SIZE is (60,330),235x18.
  All 99 full description strings fit. The five target row widths exactly
  match MAP-FONT-02: 235/133, 219/225, 224/160, 207/182 and 222/226.
  Their raw-GAF baselines are 310 and 324, equivalent to normalized original
  pen rows 300 and 314. Visible frames remain above SIZE.
- Two final-period frame overhangs in the broader map set are clipped at the
  original horizontal edge; a frame's bearing does not add to its pen advance.
  A frame rectangle can include transparent pixels, so this count is not a
  measurement of lost opaque ink. No extra width was invented to avoid it.
- The original selector and a separate render for each target map were
  captured. The original layout and Ror Shock render were visually inspected.
  Local artifacts: `D:/RWE-font-check-20261023/map-font-final.log`,
  `map-selector-original.png`, `Ror-Shock-original.png`, and the five
  `build-release/probe_map_font_*.ppm` files.
- The 63 Release GPU antialias checks continue to pass, with `GL_NO_ERROR`.
- The Release suite passes 197,206 assertions: 1,923 cases, 1,921 passed,
  two data-dependent cases skipped. An earlier run hit six Windows socket-bind
  access errors in existing TA-host tests; the isolated host rerun passed all
  25 cases/159 assertions, followed by a fully passing complete suite.
  Checksum tests were excluded as instructed.
- Debug `rwe`, every target in `make -j4 -k all`, and the Debug suite pass:
  1,924 cases, 1,922 passed, two data-dependent skips, 197,211 passing
  assertions. The smaller Release count reflects configuration-specific cases.
- The Debug COFF budget check passes across 1,072 objects. The largest is
  `GameScene.cpp` at 24,348 sections, below the 24,575-section budget.

The portable `GuiLabelFont.test.cpp` fixture uses actual shipped ASCII frame
widths and independent expected row breaks, including exact equality at235
and the period's negative horizontal hotspot. Other fonts keep their prior
metrics. This does not claim complete conformance for FNT overrides,
localization, arbitrary unbroken words or newline extensions outside the
reported fixtures. No original executable was launched for these checks.
