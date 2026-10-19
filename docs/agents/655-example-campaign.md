# #655: hide the example campaign

Owner decision, 2026-10-17: hide `camps/EXAMPLE.TDF` from campaign choices.

Reference: BT-254 in `D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md`,
documentation commit `f7fd0577`. Static comparison against byte-tactics
`97d7ac59b460417bcdfa629cf90da99f31818c65`, retail GOG v3.1. The original
discovers loose campaigns and accepts EXAMPLE; no original-game run was made.
The spec-room section 88 records this as an owner-directed divergence before
implementation. Its commit also touches the owner's protected register, so it
has not been cherry-picked into this implementation checkout.

Implementation: skip a case-insensitive EXAMPLE.TDF filename before reading
campaign data. Other campaign files, side filtering and layout stay unchanged.
Presentation only; no simulation state or hash changes.

Draft addition to the implementation checkout's protected section 88:

> **Example campaign omitted (#655, BT-254).** RWE hides `camps/EXAMPLE.TDF`
> from campaign choices by owner decision (2026-10-17). The original discovers
> loose campaign files and accepts this ARM example. This is a deliberate menu
> policy rather than conformance; other campaign discovery and layouts remain.

Verification: Debug and Release rwe builds passed. Read-only inspection confirmed
the installed 216-byte EXAMPLE.TDF has an ARM header and two example.ufo missions.
The six-line filter was reviewed to omit that filename before parsing while
retaining the existing loop for other names. No new automated test was added for
this small menu policy. The original menu has not been executed; its mounted
archive set and live display remain unobserved. Build logs:
`D:\tmp\rwe-655-debug.log` and `D:\tmp\rwe-655-release.log`.
