# Model-height floors and scroll bars with no travel

Spec-room findings, 2026-10-22, for issues #646 and #669. These findings
supply the byte-tactics comparison required by AGENTS.md and ADR-0004; they
do not authorize implementation of unrelated model or UI changes.

## Evidence and limits

The reference was inspected read-only at byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, with a clean working tree.
The local GOG v3.1 reference executable has SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`,
independently hashed in this investigation. The executable was not rebuilt.
The evidence is a static reading of the matching decompilation and its systems
manual. No original-game execution, demo oracle, screenshot, or RWE test was
performed. Documentation was written in a separate RWE documentation checkout;
no RWE source or tests were changed.

## Height: a floor at every recursively measured list

**Accepted static finding.** At `0x4CB5F0`, height is measured in native 3DO
integer Y coordinates over a sibling-list. Its result has a zero lower bound
at every recursive depth. Each vertex contributes its Y plus its owning
piece's Y offset. A nonempty child-list contributes its recursively measured
height plus the parent's Y offset. Siblings share their parent's coordinate
system; a sibling's offset is not inherited from the previous sibling.
Runtime rotations and script translations are absent from this measurement.

The zero lower bound is applied after adding the current piece's offset to
its vertices and to the measured child-list. It is also applied within the
child-list before that result receives the parent's offset. Consequently a
single clamp after flattening the entire model's vertices is insufficient.
It is also incorrect to clamp a piece's local vertices before adding that
piece's offset.

**Empty pieces.** An empty leaf contributes no vertex and no child result;
its own offset alone never supplies height. Thus an empty leaf at Y=+20
measures zero. A parent with an existing child-list receives that list's
zero result even if the child-list contains no vertices anywhere. An empty
parent at Y=+20 with one empty child therefore measures 20. A negative parent
offset cannot lower the final list result below zero. An absent child-list
supplies no candidate; it is not an implicit point at the parent's origin.

These arithmetic examples distinguish the readings. All unspecified pieces
have no vertices, and all dimensions are native integer Y units:

| Model | Reference height | Reason |
| --- | ---: | --- |
| One leaf, offset -20, vertex +5 | 0 | Translated vertex is -15 |
| One leaf, offset +20, vertex -5 | 15 | Vertex is translated before the floor |
| Parent offset +20, child offset 0, child vertex -5 | 20 | Child measurement is zero before parent translation |
| Parent offset -20, child offset +5, child vertex +30 | 15 | Positive child height 35 receives parent offset |
| Empty leaf, offset +20 | 0 | A leaf origin is not geometry |
| Parent offset +20, empty child offset +50 | 20 | Child's own leaf offset does not count |
| Parent offset -20, empty child offset +50 | 0 | Parent contribution -20 is floored |

This refines `BT-GRAPHICS-PCX-PARITY.md` section 5. Its 610-model shipped-data
census remains the recorded retail comparison; it was not rerun here. Its
statement that an empty child with a positive offset counts was ambiguous:
it is the **parent's** offset that receives a zero child-list result. Its
recommendation to floor only where model height is consumed does not express
the recursive rule established here. RWE's existing `findHighestVertex`
helper also treats an empty leaf as its translated origin; that is a distinct
edge case from merely returning a negative model height. The conformance
quantity is the scalar height; this finding assigns no meaningful X/Z point
to the zero-floor result. Native integer overflow and malformed/cyclic trees
are outside this accepted arithmetic.

## Scroll bar: original integer offsets, RWE defensive percentages

**Accepted static finding.** At `0x4A32A0`, configuring an ordinary text list
computes whether its rows fit and deactivates its associated slider when
scrolling is unnecessary. At `0x4A3EF0`, text-list slider sizing explicitly
sets the travel range to zero when the row count is at most the visible-row
count. These establish a genuine original representation of no scroll travel;
they do not establish that the original always draws an exactly full-height
thumb for every short list. Thumb sizing is proportional and has a minimum
of ten pixels, without a universal upper-size clamp in this routine.

At `0x4A4170`, enabled slider dragging uses an integer pointer displacement
added to the saved integer thumb offset. It does not divide that displacement
by thumb travel. Positions are bounded above by range minus one and below by
zero, with the lower bound winning for a nonpositive range. For a zero-range
slider already at zero, dragging leaves it at zero and does not dispatch a
position-change callback. Disabled or locked sliders reject input before
capture. Thus the original input handler has no equivalent of RWE's
floating-point 0/0 while dragging a full-track thumb.

This is **not** a finding that the original protects all GUI divisions.
Associated-list synchronization at `0x4A2BE0` and other list/drawing paths have
incomplete zero-divisor protection; the existing GUI audit records these
separately. No claim of original crash freedom follows.

RWE exchanges viewport and position percentages through group messages and
converts pointer displacement by division by available travel. That mechanism
is RWE-original, as already described in `BT-GUI-GUI-PARITY.md` section 9.
Suppressing a drag-position update when travel is nonpositive is RWE-original
defensive handling for an undefined percentage, supported by the original's
no-movement result but not an instruction copied from its mechanism. It should
leave the stored position unchanged and emit no drag-position update for a
full-track thumb; no fabricated NaN or infinity should reach the list. The
same principle covers a track reduced to no usable height by its margins.
The original's behavior for malformed undersized widgets was not established,
so the latter case must not be advertised as verified original parity.

No deliberate departure from an established original result is proposed by
this defensive guard, so no new divergence-register entry is warranted.
Original-runtime interaction checks and comparison of a rendered short list
remain outstanding; RWE regression tests alone cannot supply that evidence.
