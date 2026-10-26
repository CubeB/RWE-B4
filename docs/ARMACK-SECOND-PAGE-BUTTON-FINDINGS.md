# ARM advanced construction kbot: second-page pictures and click geometry

Spec-room finding BUILD-BUTTON-850, 2026-10-25, ADR-0004. Documentation
only; no RWE implementation or tests were inspected or changed. The reported
mod is unspecified. Stock ARM T2 construction kbot means **ARMACK**; ARMCK
is the T1 construction kbot.

## Ordinary button geometry follows its resolved native GAF image

**Scope correction, 2026-10-26:** the ordinary binding rule in this section
applies to the three products named in ARMACK2.GAF. Valid download overlays
use a separate per-product GAF route, documented as BUILD-DOWNLOAD-850 below.
The absence of an overlay product from ARMACK2.GAF does not send that product
through the ordinary BUTTONS0 fallback.

Original GUI asset binding at `0x4A81E0` finds a button's named image in the
page's GAF, then the common GUI GAF. When a frame is resolved, its native
width and height replace the gadget's width and height. Its position remains
the GUI gadget's position. The later renderer at `0x4A5F40` draws the frame
without stretching it. It adds the frame's hotspots before the sprite blit,
which subtracts them: the visible frame's upper-left therefore lands at the
gadget's upper-left despite large authored GAF hotspots.

Image dimensions and hit bounds consequently agree after binding. An
adaptation that displays a native-size image but keeps unrelated smaller
GUI dimensions for mouse routing would not reproduce this relationship.
Conversely, uniformly scaling every real build picture to an assumed size
is not the original rule: native resolved frame dimensions supply the size.

When no named image is found on the ordinary route, this binding routine proceeds through the
ordinary common button-art fallbacks, including BUTTONS0 with the closest
size group. It does not, in this traced binding path, substitute an arbitrary
unitpics PCX and retain mismatched GUI geometry. Other unit portrait PCX
consumers exist elsewhere; they do not establish a PCX fallback contract for
the three stock ARMACK2 buttons checked here.

## Real shipped ARMACK pages

The v3.1 `rev31.gp3` provides `guis/ARMACK2.GUI` and `anims/ARMACK2.GAF`.
The GUI panel is 128 by 352 at screen position (0,128). Its product buttons are:

| Product | Panel-relative upper-left | GUI size | GAF size |
| --- | --- | --- | --- |
| ARMANNI | (0,27) | 64 by 64 | 64 by 64 |
| ARMAMD | (64,27) | 64 by 64 | 64 by 64 |
| ARMASP | (0,91) | 64 by 64 | 64 by 64 |

All three are active and initially not greyed. Each has three GAF state
frames; every frame is 64 by 64. Their normal/grey frame hotspots are (281,171)
and their pressed-frame hotspot is (280,171). These are origin metadata, not
coordinates at which to draw a picture in the panel. The original button
renderer cancels them as described above.

Three remaining product cells, at (64,91),(0,155),(64,155), are active
IGPATCH placeholders initialized greyed out. Their being unclickable is
expected unless a valid download-menu overlay replaces/enables them.
PREV and NEXT are at (8,222) and (72,222), each 44 by 16 in the GUI data.

The base archive's ARMACK1 page uses the same six-cell 64 by 64 arrangement,
with products ARMLAB, ARMARAD, ARMFUS, ARMMOHO, ARMBRTHA and ARMSILO.
ARMACK2 has no special smaller-button or larger-picture convention. The
second page's three genuine pictures have the same native size as page one's
pictures; switching pages does not inherently change the rendering algorithm.

## Page routing and input

The selected unit's build-page state chooses its named GUI page. The
control-panel refresh constructs the unit-name-plus-page filename and opens
that page via `0x41ACE0`; next/previous controls request a page change through
the same panel machinery. This uses distinct authored page resources rather
than merely replacing images on page one's existing buttons. Download overlay
records map their MENU number to the current page using MENU minus one; their
BUTTON value selects a gadget offset. See
`DOWNLOAD-MENU-BUILD-LINK-FINDINGS.md` for that separate path.

For ARMACK, internal page zero is the orders view, internal page one opens
ARMACK1.GUI and internal page two opens ARMACK2.GUI. Consequently the stock
download records' MENU=3 belongs to the human second build page, ARMACK2;
the subtraction is not followed by a second page-number subtraction.

Original mouse processing converts screen coordinates to panel-relative
coordinates by subtracting the panel position. Button handling at `0x4A6AE0`
tests the gadget rectangle after image binding. Bounds include the left/top
pixel and end at x plus width minus one, y plus height minus one. A 64-square
button at (0,27) therefore covers panel x 0..63 and y 27..90; on this normal
panel its screen y bounds are 155..218. Adjacent x 64 starts the next button.

Inactive controls are skipped by the ordinary GUI update; greyed buttons
reject button input. Enabled button press/release handling retains focus and
requires the relevant release position to remain inside its rectangle for
normal activation. The selected gadget then reaches the build-panel handler
at `0x41AA00`, which distinguishes PREV/NEXT/ORDERS/BUILD by their control
names. A product button resolves its unit name; an immobile product arms
construction-placement mode. The handler is shared across these builder
pages, not replaced by a second-page-specific mouse rule.

## Evidence and limits

- Read-only byte-tactics revision:
  `e89721c46c3fa06e59558419b54fe26061fafb73`; original GOG/v3.1
  `TotalA.exe`, MD5 `8e74a1dffa1f5988624c52048f5b20cd`.
- Addresses: GUI binding `0x4A81E0`, button draw `0x4A5F40`, button mouse
  handling `0x4A6AE0`, shared GUI update `0x4A9FD0`, build-panel handler
  `0x41AA00`, page opening `0x41ACE0` and selected-unit panel refresh
  `0x41B2E0`. Geometry is derived from the ordinary gadget rectangle.
- Direct shipped-data inspection used the existing archive reader and GAF
  headers. `rev31.gp3` `guis/armack2.gui` SHA-256:
  `5149f9b62541cc2e8b979cadb94b4fb7ef3f005c6cf4ecffbb423c5dc580711f`.
  `anims/armack2.gaf` SHA-256:
  `8605407cfccf112f54502e6c18d799eea1019f7fbbb450779ba910df520e6cdc`.
  Base ARMACK1/2 data were also checked; v3.1 ARMACK2 frame dimensions agree
  with the base copy.
- Existing prose consulted: `BT-GUI-GUI-PARITY.md`,
  `BT-GUI-GUI-FILE-PARITY.md`, `BT-INGAME-CONTROL-PANEL-PARITY.md`, and
  download-menu findings. No PR implementation source was opened.
- This is a **static original-reference and real shipped-data comparison**.
  No original game was launched, no original screenshot measured, no
  executable matching checker or demo oracle run. The reported mod's actual
  page, button images and overrides remain unidentified. This finding does
  not diagnose that mod solely from the stock fixture.
- Subsequent acceptance should verify the effective ARMACK2 GUI/GAF identities,
  draw and hit rectangles for each enabled product, click centers and boundary
  pixels, page 1-to-2 transitions, pressed/grey states, and the intentional
  disabled placeholders. Record an RWE render separately from an executed
  original-game observation.

## BUILD-DOWNLOAD-850: stock download products have their own gadget GAFs

Spec-room extension, 2026-10-26. This corrects the earlier investigation's
coverage of the disabled-looking stock ARMACK2 placeholders. It authorizes
no implementation by itself and was prepared without opening RWE
implementation source or tests.

### Product mapping and picture source

The stock v3.1 download records identify ARMACK, MENU=3 and BUTTON=3/4/5
for ARMFLAK/ARMFORT/ARMAMB respectively. Original loading at `0x42DCF0`
stores MENU and BUTTON as bytes without decrementing either. Panel opening
at `0x41ACE0` compares MENU minus one with the current internal page and
addresses GUI entry BUTTON plus four, counting the panel header as entry
zero. Internal page two opens ARMACK2.GUI through the refresh at `0x41B2E0`.
The three records therefore replace entries seven, eight and nine, in that
order. These are precisely the three IGPATCH cells, not the first three
products or another page.

The overlay changes the control name to the product, clears its grey bit,
marks it as a unit entry (commonattribs=4), and selects per-gadget resource
binding by setting runtime resource bit zero at gadget byte offset `0xB4`.
That marker is separate from the GUI file's commonattribs unit-entry flag;
commonattribs=4 alone does not make an ordinary named product use this route.
The marker is attached to the particular overlaid gadget. The overlay
retains the authored position, dimensions, ordinary attributes,
association and active state. The panel resources are then released and
initialized again, so the renamed entries acquire their new pictures. A
later-page availability pass can grey a product whose loaded unit definition
cannot be resolved; a valid name does not remain disabled merely because its
cell originally said IGPATCH.

The separate binding at `0x4A81E0` requests
`anims/<PRODUCT>_gadget.GAF` and the entry named `<PRODUCT>` inside it.
This route bypasses ordinary page/common GAF matching, BUTTONS0 and the
ordinary image-driven geometry replacement. It does not request a unitpics
PCX. For these real stock products, the requested assets exist:

| Product | Stock resource in rev31.gp3 | Entry | Frames and native size |
| --- | --- | --- | --- |
| ARMFLAK | anims/ARMFLAK_GADGET.GAF | ARMFLAK | Three, each 64 by 64 |
| ARMFORT | anims/ARMFORT_GADGET.GAF | ARMFORT | Three, each 64 by 64 |
| ARMAMB | anims/ARMAMB_GADGET.GAF | ARMAMB | Three, each 64 by 64 |

Every frame in these three resources has hotspot (281,171). The button draw
at `0x4A5F40` adds the frame hotspot to the gadget position before the sprite
blit subtracts it. Their visible upper-left is therefore the cell upper-left,
with no extra hotspot displacement. They draw at native 64 by 64, without
stretching, fitting or cropping to a different button size. The retained
GUI geometry is also 64 by 64. Their 96 by 96 unitpics PCX portraits are not
these buttons' source. Normal, pressed and grey states select the respective
GAF state frames; no PCX-derived pressed/grey image is needed.

For a modified data set with this resource flag but a missing individual
GAF or missing matching entry, the traced path leaves no picture bound and
does not try ordinary BUTTONS0 or unitpics. The ordinary no-picture button
draw produces its bevel using the retained gadget rectangle. This is a
static failure-path finding, not an executed missing-asset experiment.
Likewise, a per-gadget frame of a different native size would not acquire
the ordinary route's geometry replacement. The stock case does not exhibit
that mismatch, and this finding does not prescribe a mod-specific repair.

### Coordinates, hit bounds and the surrounding controls

The panel is at screen (0,128). Product positions stay panel-relative. Mouse
processing at `0x4A6AE0` subtracts the panel position and tests the current
gadget rectangle, including both ends: left through left+width-1, top
through top+height-1. These products consequently have these exact bounds:

| Product | GUI entry | Panel upper-left | Panel hit bounds, inclusive | Screen hit bounds, inclusive |
| --- | --- | --- | --- | --- |
| ARMFLAK | 7 | (64,91) | x64..127, y91..154 | x64..127, y219..282 |
| ARMFORT | 8 | (0,155) | x0..63, y155..218 | x0..63, y283..346 |
| ARMAMB | 9 | (64,155) | x64..127, y155..218 | x64..127, y283..346 |

The upper row and ARMASP at (0,91) retain the ordinary named page-GAF
binding described above. All six cells thus form one 64-square grid. There
is no alternate download-product grid or automatic reflow around a PCX.

ORDERS, BUILD, PREV and NEXT remain controls in this same loaded GUI page.
They use ordinary named common GAF art, with native geometry replacing
their authored size. Direct inspection of stock totala1.hpi's commongui.GAF
gives the final values below; rev31.gp3 has no commongui.GAF override.

| Control | Authored position | Authored size | Resolved size | Final panel bounds, inclusive |
| --- | --- | --- | --- | --- |
| ARMORDERS | (3,4) | 59 by 19 | 58 by 18 | x3..60, y4..21 |
| ARMBUILD | (65,4) | 59 by 19 | 58 by 18 | x65..122, y4..21 |
| ARMPREV | (8,222) | 44 by 16 | 45 by 17 | x8..52, y222..238 |
| ARMNEXT | (72,222) | 44 by 16 | 45 by 17 | x72..116, y222..238 |

Add 128 to each Y bound for screen bounds. Their GAF hotspots cancel in
the same draw arithmetic. Download overlays do not move these controls.
The six lower order controls at authored (5,247), (64,247), (5,282),
(64,282), (5,317), (64,317) resolve to 54 by 30 from their authored 55 by 31;
they also keep their positions. These are MOVE, STOP, DEFEND, PATROL,
ATTACK and BLAST respectively. Capability checks may disable individual
commands. Their presence does not require another independent panel below
the build grid.

Order/build state refresh at `0x41A120` lights BUILD for the build view and
ORDERS for the orders view. The common click handler at `0x41AA00` records
the corresponding request for ORDERS, BUILD, PREV or NEXT; the page update
then reopens the appropriate authored page. Forward stepping at `0x41BDE0`
and backward stepping at `0x41BF10` wrap among build pages for ordinary arrow
requests. The page limit includes the orders slot: download MENU=3 can raise
that count to at least three, giving build pages one and two. Neither
navigation direction changes the meaning of BUTTON=3/4/5. A product click
resolves its new unit name through the shared handler; an immobile product
arms construction placement. Greyed controls reject input, and normal
product activation requires its release inside the retained rectangle.

### Evidence and limits for this extension

- Read-only byte-tactics HEAD was reconfirmed as
  `e89721c46c3fa06e59558419b54fe26061fafb73`, with no local changes.
  Its target SHA-256 is
  `3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
  The installed original executable's MD5 was independently measured as
  `8e74a1dffa1f5988624c52048f5b20cd`, the GOG/v3.1 reference build.
- Original addresses: download storage/page-count augmentation `0x42DCF0`;
  selected-unit filename and internal page routing `0x41B2E0`; overlay
  application and resource reinitialization `0x41ACE0`; resource binding
  `0x4A81E0`; drawing `0x4A5F40`; rectangle and point arithmetic `0x4A1630`
  and `0x4A1920`; button input `0x4A6AE0`; state refresh `0x41A120`;
  shared click handling `0x41AA00`; page stepping `0x41BDE0`/`0x41BF10`.
- Real shipped entries were freshly extracted from
  `D:\RWE-Data\rev31.gp3` with an existing archive-reader executable;
  only the data output and binary headers were inspected. No extractor
  source or RWE tests were opened. ARMACK2.GUI SHA-256 is the value above.
  Product GAF SHA-256 values, in table order:
  `e274ae73243508f32ddd116ac62ec41733897c07b532e0edf11c8f051c40c413`,
  `3091d0bdd0280c319baab66bb2196ab6e5470e6ff92e4936043cb4eb75dc70f5`,
  `9629659624fa55dffd4ff1823f27d326f8bd81c3f627077e7474c108ab5269d7`.
  The three download TDF SHA-256 values in the same order are
  `ded377845184a6c718072fd62d1c03dc4e269c772ca241b34d3dc1b72175b6ae`,
  `199586fc53e7dd6cea653c9c4c8cabd6e819bd8f91946745b28285b727c7afdd`,
  `6b52e9700dd111d67bdb3acdefda6cf4692089c201c4eeb492351a34a526f6ff`.
  Freshly extracted totala1.hpi commongui.GAF SHA-256:
  `b2b8993a46afb975c31df91173f9cf77bdabb253d5863173e27eb51b1f4b7e24`.
- This is **static original-reference analysis plus direct stock shipped-data
  inspection**. No original game was launched, screenshot measured, matching
  build/checker run or demo oracle executed. Runtime acceptance still needs
  original and RWE renders and boundary-click observations on ARMACK2.
  The known stock Core Contingency product case is established by the shipped
  v3.1 overlay records and individual gadget resources; patched engines,
  mod overrides and missing individual assets were not executed.
