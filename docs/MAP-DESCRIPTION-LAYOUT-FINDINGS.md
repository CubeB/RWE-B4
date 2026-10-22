# Map-description layout and readable expansion

Spec-room findings, 2026-10-22. Documentation-only handoff under
[ADR-0004](adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md).
No RWE implementation or tests were inspected or changed in this pass.

## Finding MAP-LAYOUT-01: the original uses fixed map-description rectangles

The original's skirmish map selector loads its dimensions from shipped GUI
data. Selecting a map replaces the description text, size/player text and map
picture. The description update does not enlarge the description rectangle or
move the size label underneath it. There is no adaptive description-panel
expansion in this traced path, and no insertion of an ellipsis.

Both the base `totala1.hpi` and the v3.1 `rev31.gp3` copy of
`guis/SELMAP.GUI` specify these dialog-relative rectangles:

| Element | X | Y | Width | Height |
| --- | ---: | ---: | ---: | ---: |
| Dialog | 84 | 12 | 494 | 420 |
| Map list | 60 | 86 | 230 | 193 |
| Description | 60 | 300 | 235 | 31 |
| Size/player summary | 60 | 330 | 235 | 18 |
| Select Map button | 357 | 325 | 96 | 20 |
| Cancel button | 356 | 360 | 96 | 20 |

The description's nominal final row coincides with the size label's first row.
These are file dimensions, not measured bounds of the actual visible glyphs.
They are evidence against assuming that the supplied artwork reserves unlimited
space for descriptions.

The base `guis/VIEWMAP.GUI` likewise has a fixed description rectangle at
(49,269), 235 by 20, followed by size at (49,288), 235 by 20, in a 330 by 390
dialog. This confirms that the original has more than one map-information
layout; a single universal description height must not be inferred from SELMAP.

## Finding MAP-LAYOUT-02: bounded wrapping does not promise complete text

For the ordinary label-font path, a sufficiently tall rectangle uses
word wrapping within its supplied width and remaining height. Spaces are
wrapping opportunities and carriage returns request a line break. Rendering
stops when its height budget is exhausted. A short rectangle takes the
single-line path. With the graphical font used on this traced path, horizontal
rendering stops when a glyph no longer fits the width. No ellipsis is appended
by either of these routines.

The separate font-bearing-control path renders differently and should not be
generalized into the ordinary label rule. This pass does not establish
well-behaved wrapping of every overlong unbroken word, every localization, or
every custom GUI/font combination. The description label's text assignment
also has a 128-byte storage limit in the original. Therefore an RWE requirement
to display arbitrary full descriptions exceeds the original's guarantees even
before considering rectangle size.

A real shipped example was read directly from `totala2.hpi`,
`maps/Coast To Coast.ota`: its mission description is
`7 X 4  Water between two coasts. Weak tides.` and its size is `7 x 4`.
This is a useful ordinary-data fixture, not a demonstrated overflow fixture.

## User-directed divergence: show the full description in a larger box

The owner requested on 2026-10-22 that the map text box grow to accommodate
the text instead of overlapping, cutting content off or substituting `...`.
Because the original counterpart has fixed rectangles and bounded rendering,
adaptive enlargement is a **deliberate divergence**, rather than conformance
or a feature with no original counterpart.

Suggested entry for `TOTALA-EXE.md` section 88, to record before implementation:

> **Map descriptions expand their presentation area to remain readable**
> (owner instruction, 2026-10-22; MAP-DESCRIPTION-LAYOUT-FINDINGS.md,
> MAP-LAYOUT-01 and MAP-LAYOUT-02). RWE preserves the complete description,
> wraps it at the available readable width, and enlarges its box and surrounding
> layout as needed so subsequent labels and controls do not overlap it. It does
> not replace the undisplayed remainder with an ellipsis. The original's map
> information dialogs use fixed data-defined description rectangles, bounded
> rendering and a limited text buffer. RWE chooses readability over those
> original presentation limits; original layout parity is not claimed.

Acceptance should measure the rendered full text with the actual active font,
include padding, and keep the size/player summary below the description.
The enclosing panel must accommodate its children and remain usable on the
supported viewport. Check ordinary shipped descriptions, the longest real
available description, explicit line breaks, narrow windows, and long words.
Any finite-screen fallback for exceptionally large descriptions remains a
product choice; clipping or an ellipsis does not satisfy the owner's request.

## Evidence, reference identity and remaining uncertainty

- Byte-tactics was inspected read-only at revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`; its checkout was clean.
- The reference is the GOG/v3.1 `TotalA.exe`, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`. The installed executable at
  `D:/SteamLibrary/steamapps/common/Total Annihilation/TotalA.exe` was hashed
  independently in this pass and has that MD5.
- Static evidence: map information refresh at `0x444A20`, skirmish selection
  refresh at `0x47AAA0`, GUI rectangle parsing at `0x4AD350`, translated text
  assignment at `0x4A09C0` and `0x4A0BF0`, label rendering at `0x4A56B0`,
  bounded graphical-font text at `0x4A50E0`, and wrapping at `0x4A51D0`.
  The inspected reference's progress table records the principal routines as
  matched. This pass did not rebuild or independently run its matching checker.
- Shipped GUI and OTA data were extracted read-only with the existing
  `tools/hpi.py` from a separate documentation checkout. Archive SHA-256:
  `totala1.hpi` =
  `c3890a78e627e6e334f671f65ec418cd40d15ad2ad4110e931bb3dfced0814c3`;
  `rev31.gp3` =
  `fa0da77b8fb4400ce606995703dec42eeab5b05d04ca8c0e43be324f04ce0441`.
- This is a **static reference and real shipped-data check**. No original game
  was launched, no original screenshot was measured, and no demo oracle was
  executed. Exact visible clipping, line counts and localized/custom font
  behavior remain unmeasured. UI descriptions are not established by a
  simulation demo oracle.
- Existing edits in `D:/RWE-spec-room` were preserved. This new finding was
  written in the separate documentation worktree `D:/RWE-spec-map-layout`.
