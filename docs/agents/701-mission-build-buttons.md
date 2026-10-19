# Issue #701: keep mission-excluded build buttons greyed in place

Owner decision, 2026-10-17: match the original, reversing issue #381's removal
of the buttons. The implementation source is BT-103 in
`D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md` and its resolved
entry in `BYTE-TACTICS-DISCREPANCIES.md`. Reference revision: byte-tactics
`97d7ac59`, GOG v3.1 original. This is a **static reference comparison**;
the original game was not executed for this change.

The finding records `0x41AC90` walking the focus unit's build entries after
menu construction, looking each build-unit name up through `0x488B10`, and
greying entries absent from the loaded unit table. Confidence is high.
Whether this path separately refuses the order was not traced; ordinary grey
gadgets ignore input. No original-game check is recommended by the finding.

RWE keeps excluded definitions for other purposes. `applyMissionUnitList`
therefore marks their existing GUI entries `grayedOut` rather than erasing
them. `UiFactory` already creates disabled buttons from that field, and
`UiStagedButton` already ignores input while disabled. Page order and gadget
positions remain intact. Existing command and mission-placement checks still
refuse excluded units. No simulation behavior or sync hash changes.

The existing mission-list test checks that the excluded entry stays in its
original slot and is greyed, while the permitted entry and page-flip gadget
are unchanged. Its command and placement sections continue to check refusal.
An additional fixture transcribes the ARMSOLAR, ARMWIN and ARMNEXT rectangles
from `totala1.hpi`'s `guis/ARMCOM1.GUI`. Projecting `totala4.hpi`'s
`camps/useonly/AC02.TDF` onto those two unit definitions permits ARMSOLAR and
excludes ARMWIN. The test preserves every rectangle and the page-flip gadget,
and greys only ARMWIN.
Focused drop/mission validation passed 43 cases and 347 assertions, including
the shipped-page fixture. The completed suite passed 1800 cases and 182946
assertions, excluding the two TaChecksums cases. All Debug targets and Release
`rwe` linked successfully. The section budget passed over 1048 objects, with
the largest at 24276 sections against the 24575 limit.
The owner retains the visual check of AC02's commander
build page; no original-game execution or screenshot comparison was run.

## Draft for the owner's findings register

In `TOTALA-EXE-DATA.md` section 105, "The mission's unit list", replace the
"Ported (#381)" paragraph's statement that buttons come off every build menu:

> Mission-excluded unit definitions remain in RWE's table with construction
> and mission placement refused. Their build buttons remain greyed in their
> original slots (BT-103, #701), as in the original, superseding #381's button
> removal. This change affects presentation only.
