# Front-end resource reuse and match-loading presentation

Spec-room findings for #882, 2026-10-26. These findings supplement
`MENU-PERFORMANCE-FINDINGS.md` MPR-001 and the existing GUI/interface registers.
They authorize no unrelated behavior changes.

## Evidence and room boundary

The reference was inspected read-only at `D:\bytetactics`, revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, with a clean reference worktree.
Its matching original is the GOG v3.1 `TotalA.exe`; the local original's MD5 was
independently calculated as `8e74a1dffa1f5988624c52048f5b20cd` on this date.
The relevant matching routines and their systems-manual chapters were read
statically. No original executable, original menu interaction, demo oracle,
RWE executable, or RWE implementation/test source was executed or inspected
for these findings. This is **static reference evidence**, not an executed
original-game verification or a measured original loading time.

Work is in the separate documentation checkout `D:\RWE-spec-menu-loading`,
based on `b4/revival` commit `1a6613a6e767f5e5f0256a2e5b585bf2070d4397`.
Only this prose document crosses to implementation under ADR-0004. Existing
shipped-data census results below are attributed to the earlier registers;
they were not independently rerun here.

## MLR-001: Ordinary GUI art uses retained whole-file allocations

**Reference evidence:** `0x4A75D0`, `0x4A7F70`, `0x4A81E0`, `0x4A9660`,
`0x4AEEE0`, `0x4AEF80`, `0x4B8C60`, `0x4B8D40`, application initialization
`0x491200` and shutdown `0x4916A0`.

The original's ordinary screen initialization retains one loaded screen GAF
allocation in the screen's resource ownership. Its gadgets resolve their art
as entries within that retained allocation, or within the retained common GUI
GAF. Entry lookup searches loaded names case-insensitively and returns a
borrowed entry; it does not reload or copy the whole file. The common GUI GAF
is initialized with application resources and released with them. Closing a
screen frees that screen's GAF allocation. Reopening that screen is allowed to
load it again; this inspection establishes no cross-screen cache for ordinary
screen GAFs.

Loading a GAF reads the complete file and resolves its entry/frame references
inside that allocation. The ordinary gadget-name lookup path does **not**
reopen and decompress the same screen/common GAF separately for each button.
This does not assert that loading eagerly expands every compressed pixel:
compressed frame pixels remain supported by drawing. Whole-file archive
loading and pixel-run decoding are different operations.

There is a distinct per-gadget resource option. When selected by the GUI data,
it loads a name ending `_gadget.GAF`, retains that allocation on that gadget,
and looks for the gadget's named entry. Cleanup releases those allocations
separately. The ordinary screen/common reuse finding must not erase this
resource option or treat every GAF on every path as one shared object.

### Lookup and absent-resource semantics

For ordinary buttons, named art is sought in the screen GAF, then in the
common GUI GAF. If neither contains that name, the common GUI supplies the
applicable `CHECKBOX`, `stagebuttn1` through `stagebuttn4`, or `BUTTONS0`
fallback. The stage suffix is capped at four. Fallback selection uses groups
of four frames, choosing the first group to improve the sum of absolute width
and height differences; successful art binding replaces gadget dimensions
with the selected frame's dimensions. The fallback also clears its frame
hotspots. One-stage fallback art changes the button to two stages with its
existing special-art mode. These are existing visible data rules, not license
to replace all absent entries with the same generated graphic.

The panel background likewise prefers the panel's named entry in the screen
GAF, then the common GAF, then common `BackTile`. Other gadget families have
their own binding rules: for example sliders seek `SLIDERS`, while text input
uses common `TEXTINPUT`. Preserve each consumer's lookup/fallback contract;
button fallback is not a universal entry-lookup policy. See the existing
`BT-GUI-GUI-PARITY.md` rules 5 and 9 and `BT-GUI-GUI-FILE-PARITY.md` rule 9.

A missing/zero-length screen GAF does not itself terminate ordinary binding:
without a loaded screen allocation, common and fallback resources can still
supply art. A missing named entry returns an absent result, enabling the next
lookup. If a button has no usable sequence even after fallback, the original
has primitive button drawing. This is different from accepting a structurally
corrupt GAF. The whole-file loader does not comprehensively validate its
signature, byte length, counts or offsets before reference adjustment, and
some downstream frame consumers assume their frames exist. A corrupt file
can fault; no general original rule of silently treating corruption as an
absent file was established.

No claim is made here about archive priority, loose-file precedence, error
messages from the lower archive service, operating-system file caching, or
hot replacement of resources. Those were not traced. Reuse must retain the
active VFS's resolution result and its existing case/path behavior.

### Boundary for an RWE optimization

A bounded cache of successful whole-GAF decoding and of successfully
established absent entries, owned no longer than a resource service whose
VFS/mount set is fixed, is an **RWE-original performance policy**. It is
consistent with the original's reuse within ordinary screen binding but is
not a claim to reproduce its exact allocation lifetime. An absent entry may
be remembered while retaining the distinction between absent file, absent
entry and decode failure, and while allowing each caller's existing fallback
to run. This finding does not support converting an exception/decode error
into a cached miss, changing archive precedence, retaining a result across a
VFS change, or modifying selected pixels, geometry, captions or states.

No gameplay divergence follows merely from eliminating redundant reads and
decodes with identical resolved data and observable failure behavior. Any
intentional alteration to those visible rules requires its own finding and,
where applicable, an entry in the divergence register.

**Shipped-data corroboration:** the earlier census in
`BT-GUI-GUI-PARITY.md` identifies 186 distinct shipped GUI paths, including
`MAINMENU.GUI` and `SINGLE.GUI`; its rule 5 records the screen/common/fallback
binding and geometry selection. `BT-GUI-GUI-FILE-PARITY.md` rule 9 records
application loading of `commongui` and the two glyph GAFs. This confirms the
resources concern ordinary retail interfaces rather than only builder menus.
It supplies no original latency measurements. The stock ARMACK page and
per-product resource distinction was also cross-checked against the existing
`ARMACK-SECOND-PAGE-BUTTON-FINDINGS.md` BUILD-BUTTON-850,
BUILD-DOWNLOAD-850 and BUILD-DOWNLOAD-BEVEL-850 findings. That build-menu
route must remain separate from ordinary front-end binding.

## MLR-002: Loading has its own presentation while battle work proceeds

**Reference evidence:** accepted skirmish Start handling `0x47AE60`, menu
handoff `0x496BB0`, loading presentation `0x497F40`, worker entry `0x497C70`,
match construction `0x497180`, battle-resource initialization `0x4917D0`,
progress networking `0x456DE0`.

A valid skirmish Start prepares player/mission/settings state and requests
match startup. The menu coordinator subsequently selects the loading mode.
This does not itself build all battle resources inside the Start click.
There is preparatory work in the Start path, including data-archive
registration and validation; this finding supplies no upper bound on the
click-to-loading interval.

Loading initialization closes existing GUI screens, disables GUI key
commands, selects the hourglass cursor, blanks the display and prepares a
640 by 480 loading presentation. It selects `loadgame2bg` and starts a worker
for match construction. The main application path subsequently draws the
background and progress bars and presents the screen while the worker builds
the battle. Thus the loading state has a maintained presentation rather than
relying on the previous skirmish menu to remain visible throughout asset work.

**Ordering limit:** selecting the loading background precedes worker launch,
but the loading screen's draw/unlock/present occurs after worker launch.
There is no static guarantee that the first loading frame is presented before
the first expensive asset operation. The worker could also finish before the
main path reaches that draw. Therefore the original supports a loading
presentation concurrent with substantial startup, but does not prove a
mandatory fully drawn zero-percent frame before all resource creation.
An RWE policy that explicitly presents a loading frame before its expensive
phases is an RWE-original scheduling choice serving that presentation intent.
It must not be described as an exact original first-frame ordering rule.

### Progress represents resource categories, not elapsed time

The original draws six independently reported bytes, at game-object offsets
`+0x38D6F` through `+0x38D74`. Their on-screen labels in order are `Textures`,
`Terrain`, `Units`, `Animation`, `3D Data`, `Explosions`. These are not six
strictly isolated sequential loads and their percentages do not estimate
remaining wall-clock time. The inspected writers establish these meanings:

| Label | Static writer evidence and limits |
| --- | --- |
| Textures | `0x42A440` reports progress while loading texture GAF files and finishes at 100. |
| Terrain | `0x429660` reports chunk-read progress, 9 through 90; `0x483610` finishes terrain construction at 100. This is not byte-perfect I/O validation. |
| Units | `0x42D2E0` reports progress through unit types. The work includes FBI interpretation, each unit's 3DO loading/texture binding, GUI-page discovery and COB loading. It finishes at 100. Models and scripts are therefore not confined to a separate bar. |
| Animation | `0x422EA0` reports feature replacement/link resolution. This can load additional feature types and their visual resources; the count can grow during resolution. The label alone must not be interpreted as a dedicated COB-script phase. |
| 3D Data | `0x440940` reports movement-class passability-map building and finishes at 100. Intermediate arithmetic is not a perfectly bounded fraction. The label alone is not evidence that all 3DO model loading occurs here. |
| Explosions | `0x420620` sets 20, 50 and 100 around construction of three procedural effect sequences. |

Battle-resource preparation also performs feature discovery, weapon loading,
unit-memory allocation, passability work and radar/player/pathfinder setup.
The bars do not individually expose every operation. General sound
initialization and definition/sample loading occur in application startup
`0x491200`; no seventh audio bar or equivalent separate match-audio progress
category was established. This does not prove that battle loading never
resolves any additional sound-dependent resource.

### Waiting, input and completion

While the worker runs, the loading presentation path continues network probes,
packet reception and queued sends, and performs start-position synchronization
when requested. Multiplayer progress is the integer average of the six bar
bytes in the two-byte packet type `0x2A`. A worker's multiplayer waits can
sleep in 50 ms intervals while the main path maintains network/display work;
those waits have no local timeout/cancel check in the inspected loading
routines. The still-loading presentation path sleeps 200 ms between passes,
including when its screen lock fails. This is not an original guarantee of
60 Hz progress animation or an unrestricted menu-event loop.

The old menu is closed and GUI key commands are disabled. No user-facing
loading Cancel or menu navigation dispatch was established inside the loading
callback. This inspection does not audit the outer Windows message pump or
prove that every window-management event is handled during every load.

The worker signals completion separately from the progress bytes. Six bars at
100 do not by themselves authorize entry into battle; multiplayer setup can
still wait. On completion, the presentation path blanks/releases the loading
picture, restores battle display dimensions when needed and transfers to
battle. A failed worker launch is fatal; these findings do not establish a
recoverable retry or rollback contract.

**Implementation boundary:** a responsive loading scene, early presentation,
per-phase progress updates and event/network service during RWE startup can
be implemented as RWE-original scheduling without changing simulation order,
RNG calls, resolved assets, start placement, handshake success conditions or
visible TA-data rules. The reference does not prescribe RWE's thread model,
GL-context ownership, stage percentages, cache lifetimes or a latency target.
Those require RWE measurements and verification with the shipped data.

## Remaining verification

No original runtime check was run. A later original-game recording should
measure the accepted stock Start click, first replacement of the old menu,
first visible loading background, bar changes and battle entry, including a
multiplayer peer wait. Equivalent RWE measurements should separate the time
before its first presented loading frame from its total resource/handshake
time. Cached/warm reads must not be reported as cold-load measurements.

