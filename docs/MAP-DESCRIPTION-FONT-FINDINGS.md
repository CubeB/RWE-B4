# SELMAP descriptions: the smaller label font makes the original text fit

Spec-room findings, 2026-10-23, under ADR-0004. Documentation only: no RWE
implementation or tests were inspected or changed. This resolves the font-slot
uncertainty in `BT-GUI-GUI-PARITY.md` rule 7 and supplements
`MAP-DESCRIPTION-LAYOUT-FINDINGS.md`. The latter's general clipping discussion
did not establish that these five actual descriptions clip in the original.

## Finding MAP-FONT-01: ordinary labels use hattfont11, not hattfont12

Original initialization loads `anims/hattfont12.GAF` into glyph slot zero and
`anims/hattfont11.GAF` into slot one. The ordinary label renderer explicitly
selects slot one. Ordinary button, list and text-input rendering normally uses
slot zero. Both archives name their glyph entry `Haettenschweiler (120)`;
the shared entry name does not make their metrics interchangeable.

This is a general ordinary-GUI-label selection, not a map-description-only
special case. A counted type-7 font record selected by the control's
`fontnumber` can override rendering with an FNT font. `fontnumber=0` does
**not** mean glyph slot zero. SELMAP has no type-7 font record, so its
description takes the ordinary slot-one GAF path. COMIX is the fallback
default FNT, not the actual description glyph resource on this shipped path.

Measured v3.1 GAF metrics, in native pixels:

| Glyph | hattfont11 advance | hattfont12 advance |
| --- | ---: | ---: |
| Space | 7 | 7 |
| I | 3 | 5 |
| i | 4 | 5 |
| l | 3 | 5 |
| m | 8 | 10 |
| M | 7 | 11 |
| W | 8 | 11 |
| Period | 3 | 5 |

Each glyph advances by its stored frame width. There is no extra interglyph
spacing added by the ordinary GAF label text drawer and no pair kerning.
The glyph's stored horizontal hotspot affects where its ink is drawn,
not its advance. For example hattfont11's period has horizontal hotspot -1,
so its frame starts one pixel right of that glyph's pen position. The space
has an advance but is not visibly drawn. Missing glyph frames have no advance.

## Finding MAP-FONT-02: two rows fit in the unchanged 235 by 31 rectangle

The description rectangle is at dialog-relative (60,300), width235, height31.
With hattfont11, the I glyph is 10 pixels high. The GUI's reported GAF line
height adds two, giving 12; wrapped text adds another two between pen rows,
giving a **14-pixel row advance**. Since the label's inclusive-rectangle
height difference 30 exceeds twice 12, this label takes the wrapped path.

It starts its pen at (60,300), with the next pen row at (60,314). Font loading
subtracts the I glyph's height 10 from every frame's stored vertical hotspot;
the sprite drawer subsequently subtracts that adjusted hotspot from the pen Y.
For I, the shipped hotspot 9 becomes -1, so its visible frame begins at Y301
on row one and Y315 on row two. Lowercase and descender frames retain their
own relative bearings. The strings below fit above the size summary at Y330.
The dialog's origin (84,12) is added on screen, so the corresponding pen
positions in the normal original layout are (144,312) and (144,326).

The earlier GUI-file prose's characterization as a signed-byte Y adjustment
is superseded for this operation: the directly inspected loader uses the I
frame's stored height and adjusts the stored vertical hotspot. In these
actual assets the result is the 10-pixel normalization just described.

Wrapping measures whole candidate spans in proportional glyph advances.
Spaces and carriage returns are break opportunities; a candidate of **exactly
235 pixels is accepted**. It fills the line through the last fitting word
boundary. The delimiter used for a soft wrap is consumed. Interior repeated
spaces are preserved and counted; there is no general whitespace collapse.
Thus dimension-prefix double spaces and Ror Shock's double space after
`map.` contribute their full widths when retained inside a line.

No ellipsis is inserted. This path is word wrapping, not a general
character-by-character long-word layout. An overwide word can reach the
horizontal clipping behavior and the wrap routine has awkward initial-word
edge cases; these five fixtures contain no such word. Their full-fit result
must not be generalized to arbitrary unbroken text. Carriage-return breaks
are recognized by this wrapper; arbitrary newline equivalence is not claimed.

### Measured fitting rows from real shipped font data

Each of the five owner-supplied strings was independently found verbatim in
the installed shipped map archives. Widths below sum the actual v3.1
hattfont11 frame widths; line breaks follow the original's space-boundary
selection at 235. All five require only **two rows**, with no truncation.

| Map | First row | Pixels | Second row | Pixels |
| --- | --- | ---: | --- | ---: |
| Mounds of Mars | `8 X 17  For more than two players, play` | 235 | `teams, North vs. South.` | 133 |
| Ror Shock | `26 X 26 Ocean Battles are a must for` | 219 | `this fine map.  Big surprise in center!` | 225 |
| Steel Jungle | `20 x 10  Wake up and smell the coffee` | 224 | `with the Morning War Show.` | 160 |
| Temblorian Mist | `12 X 15  Make your way around the` | 207 | `edges or fly across the center.` | 182 |
| The Barrier Reef | `19 X 19  Body of water separates two` | 222 | `land masses.  Great underwater starts!` | 226 |

This is quantitative shipped-data evidence supporting the owner's observation
that the descriptions fit the original. A larger font or additional spacing
would change those breaks. Enlarging boxes is not necessary to fit these five
strings when the correct original label font and metrics are used.

## Finding MAP-FONT-03: the dimensions are already in the description

The five dimension prefixes, their case and spacing are part of each OTA's
`missiondescription`. Original mission parsing reads that field and applies
its description translation fallback; map-info refresh assigns the description
to the label. It does not prepend the map's separate size field to that text.
The size/player summary is independently formatted for the lower label.
Duplicating the prefix or normalizing its spacing would alter the fixture.

Original front-end setup requests a 640 by 480 drawing surface. These glyph
sizes and coordinates are native framebuffer pixels; the traced label path
does not scale its font or available width per display DPI. Desktop, monitor
or wrapper output scaling can enlarge the final image, but does not add new
logical text width. No particular modern output-scaling configuration was
measured in this pass.

## Evidence, identities and limits

- Byte-tactics read-only revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`; reference GOG/v3.1
  `TotalA.exe`, MD5 `8e74a1dffa1f5988624c52048f5b20cd`, independently
  measured earlier in the same spec-room session.
- Static addresses: GUI initialization/slot assignment `0x491200`; glyph
  slot load/hotspot normalization `0x4AEDD0`; label rendering/slot choice
  `0x4A56B0`; type-7 font selection `0x4A1810`; proportional width
  `0x4A5030`; line metric `0x4A50B0`; GAF text rendering `0x4A50E0`;
  wrapping `0x4A51D0`; sprite hotspot placement `0x4B8500`; description
  parsing `0x435DA0`, description access `0x435900`, map-info refresh
  `0x444A20`; native front-end surface request `0x491A70` from main-menu
  setup `0x4263B0`.
- Direct archive-data measurements used the existing `tools/hpi.py` reader.
  Mounds of Mars was found in `Mndsmars.ufo`; the other four OTA entries
  were in `ccmaps.ccx`. Base and v3.1 glyph headers were both checked;
  the metrics listed above agree in both versions.
- v3.1 `anims/hattfont11.GAF` SHA-256:
  `ff774bb017d1f929b1c3cb0366f34692545ec537ee1389cb7f44ea51d71fbd41`.
  v3.1 `anims/hattfont12.GAF` SHA-256:
  `e5bc2e9df9f9056129f7ce8dcb004d44d011aab6f2fc7d00278c4d8ab93c0134`.
  Base hattfont11 SHA-256:
  `f0aa18aa33a12b91156582a5468debbf0d9e7974c88a1c772e7b2c9d46edd8c3`.
- This is a **static original-reference check plus executed arithmetic over
  real shipped font/map data**, not an executed original-game render. No
  visible game was launched, screenshot measured, demo oracle used or matching
  checker run. The owner reports that these descriptions fit the original;
  that observation and the quantitative static explanation agree, but their
  exact screen pixels were not captured here.
- Localization, active mod overrides, unsupported glyphs and pathological
  unbroken words remain outside the five-fixture result. The earlier deliberate
  adaptive-box divergence remains a separate product decision for genuinely
  longer content; it should not substitute for reproducing these font metrics.
