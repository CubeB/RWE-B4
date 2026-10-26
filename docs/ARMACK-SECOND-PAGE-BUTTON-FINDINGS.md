# ARM advanced construction kbot: second-page pictures and click geometry

Spec-room finding BUILD-BUTTON-850, 2026-10-25, ADR-0004. Documentation
only; no RWE implementation or tests were inspected or changed. The reported
mod is unspecified. Stock ARM T2 construction kbot means **ARMACK**; ARMCK
is the T1 construction kbot.

## Original button geometry follows its resolved native GAF image

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

When no named image is found, this binding routine proceeds through the
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
