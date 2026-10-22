# Ctrl category selection and camera-jump side effects

Spec-room findings, 2026-10-22, for #654 and #653. Stable anchors in this
file allocate no main-register numbers. They supersede the narrower or
conflicting claims identified below, without authorizing unrelated changes.

## Evidence and limits

Byte-tactics revision `e89721c46c3fa06e59558419b54fe26061fafb73` was inspected
read-only with a clean working tree. Its local GOG v3.1 executable has SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`,
independently hashed in the preceding investigation recorded in
`BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS.md`. This is direct static analysis of the
matching decompilation and the supporting systems manual. No executable
rebuild, original-game execution, RWE build/test, demo oracle or screenshot
was performed. The RWE comparison is read-only inspection of
`GameScene_input.cpp` in the separate issue checkout on 2026-10-22. Findings
were written in a separate documentation checkout; no implementation or tests
were changed by the spec room.

## Ctrl categories: modifiers, names and selection pool

**Accepted static finding.** The input producer at `0x4C1D50` maps a physical
A–Z virtual key with Control held to the letter's uppercase character value
plus `0x69`. This mapping does not reject Alt or Shift. Both ordinary key-down
and system key-down messages reach it (window message handling at `0x4B5CC0`); Control determines
the letter event in
either route. Alt therefore does not take precedence over Control for this
mapping. Platform interception of particular keyboard combinations outside
the game was not tested and is not part of this guarantee.

The battle dispatcher at `0x495E90` samples Shift through engine key-state
code `0xF9` (the mapping to physical Shift is at `0x4C1B80`'s key-state
service). Shift is sampled when the queued event is consumed, not encoded
into the Ctrl letter event. For a category command, Shift means additive
selection. Without Shift it means replacement within the eligible pool.
Ctrl+Shift+letter and Ctrl+Alt+Shift+letter thus retain the additive meaning;
Alt alone does not generate a Ctrl category event.

**The generic letters have exceptions.** Generic category events are `0xAB`,
`0xAE..0xBB` and `0xBD..0xC2`: B, E through R, and T through Y. Their category
name is `CTRL_` followed by the uppercase letter, terminated after that letter.
They are not restricted to the six retail category letters W/F/P/V/B/R.
However A, D, S and Z are dedicated select-all, self-destruct, on-screen and
same-selected-types commands, respectively; adding `CTRL_A`, `CTRL_D`,
`CTRL_S` or `CTRL_Z` to mod data does not replace those commands. C is also
special: it selects category `CTRL_C` with the same Shift rule and then
sets commander following using the separate `Commander` category. The latter
operation is not an arbitrary-category side effect.

At `0x488C50`, category lookup is case-insensitive, and a previously unknown
name denotes an empty type set rather than an unhandled key. Therefore an
unknown generic category with Shift up deselects eligible local nonmembers;
with Shift down it retains them. An implementation that ignores an unknown
Ctrl category command entirely would differ on replacement selection.

At `0x48BF30`, category selection visits the local player's complete unit
range, not only units on screen, in LOS, or selected beforehand. Eligibility
requires unit flag `0x20`, remaining construction exactly zero, the four-byte
field at unit offset `0xFB` zero, and either no carrier or a carrier with flag
`0x40000000`. The field at `0xFB` is described as a post-transfer holdoff in
the supporting manual; its broader lifecycle was not re-investigated here.
There is no movement-class, building, idleness, screen-rectangle or LOS test
in this operation. The carrier exception is literal evidence; equating it
with a broader RWE transport class requires separate justification.

Eligible category members become selected. Eligible nonmembers retain their
prior selection in additive mode, and become unselected in replacement mode.
Units outside the eligible pool are untouched by this routine, including
already-selected ineligible units; replacement is not a blanket clear of all
selection flags. Afterwards the operation clears the single-unit UI index,
returns interaction to neutral STOP mode and requests orders-panel refresh.
It enqueues no unit STOP order and does not itself clear camera following.

This refines `BT-INGAME-KEYS-PARITY.md` section 4: its assertion about arbitrary
letters must retain the dedicated-key exceptions above. It confirms additive
Shift for generic categories; it does not establish additive semantics for
the separate A/D/S/Z services. No original-runtime or real-data selection
interaction was executed. The earlier audit's retail category census was not
rerun, and the exact RWE representation of the original eligibility flags
remains an implementation comparison.

## Camera jumps: bookmark cancellation and the F3/n refutation

**Accepted static finding for bookmark recall.** At `0x41D3F0`, recall clears
the snap countdown and both retained follow targets (unit and other tracked
object), loads the bookmark's actual scroll coordinates, marks the view dirty,
clamps actual scroll to the map, copies it into desired scroll, and clears map
flag `0x8`. It therefore leaves following as well as a pending snap. Saving
at `0x41D3B0` only stores the current actual coordinates and validity byte;
it does not cancel following or a snap. Function-key encoding at `0x4C1D50`
chooses save for Ctrl+F5–F8 and recall otherwise; Alt and Shift do not change
that choice in the producer. The dispatcher plays `SelectSquad` for save and
recall.

The source-level unsaved-slot quirk remains as in the earlier audit: recall's
guard does not test the selected slot's validity byte, so ordinary initialized
unsaved coordinates can be recalled as zero. This was not checked in a running
original. RWE's optional-bookmark policy is separate from the accepted
cancellation side effects for a successful stored-bookmark recall.

**Refutation of the prior F3/n claim.** The original does not clear follow or
snap in either of the following paths:

- F3's unmodified event `0xE4` invokes message-unit cycling at `0x464000` and
  `0x463F60`. A qualifying live message-associated unit supplies whole X/Z to
  the point-centering service at `0x41C7C0`, with target-only movement. The
  desired camera location is centered and clamped and map flag `0x8` cleared.
  There is no write to either follow target or the snap countdown. With no
  qualifying message unit, there is no camera target assignment. Ctrl+F3
  encodes a different event and is not this message jump.
- Plain n's event `0x6E` invokes local-unit cycling at `0x48D4D0`. When a unit
  is found, its projected position supplies the map-position centering service
  at `0x41C8E0`, also with target-only movement. This service likewise retains
  both follow targets and the snap countdown. With no candidate it leaves the
  camera unchanged. The choice and visited-unit bookkeeping are outside #653's
  camera-cancellation question.

This retention is not merely an omitted clear whose effect is unknown. At
`0x41CA10`, the next camera update gives a pending snap precedence over the
tracked object, then a live followed unit; any such source replaces the
desired target. An invalid/dead followed unit clears all three retained states
there. Thus a live tracking source can override the F3/n target in the next
camera update. No original-runtime timing or visible jerk was measured.

`BT-INGAME-CAMERA-PARITY.md` section 5 and its rank-1 gap overgeneralized
bookmark cancellation to F3 and n. Only the bookmark-recall part is supported
as a conformance fix by this comparison. Making F3 or n cancel tracking in RWE
would be a deliberate behavioral departure and must first be recorded in
`TOTALA-EXE.md` section 88; this finding does not silently establish one.
Do not add cancellation to every general camera-position setter: mission-start
positioning (`0x41D1F0`) and saved-position loading (`0x41D2B0`) also lack those
clears, whereas radar navigation (`0x41D0F0`) explicitly cancels all three.
RWE's zoom and camera state representation have no exact original counterpart;
leaving zoom unchanged on bookmark recall remains an RWE presentation choice.
