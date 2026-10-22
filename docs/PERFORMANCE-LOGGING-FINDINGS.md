# Performance logging findings

## PERF-LOG-001: RWE's default diagnostic verbosity is RWE-original

Date: 2026-10-21. Status: static reference comparison; no executed original-game check.
Scope: default verbosity of RWE's own diagnostic text, not simulation scheduling,
pathfinding decisions, audio behaviour, AI decisions, or original-game debug UI.

### Reference and evidence

Reference: the local byte-tactics checkout at revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, inspected read-only under ADR-0004.
Its reference executable is the GOG v3.1 `TotalA.exe`; the local executable's MD5
was independently read as `8e74a1dffa1f5988624c52048f5b20cd` on this date.
No rebuilding, matching checker, or original executable execution was performed
for this finding. The comparison below comes from direct static inspection of
reference routines, not merely a string-search absence claim.

At address `0x4e1b10`, the original persists performance-report preferences in
the registry. The release-enable preference defaults to disabled, debugger
output defaults to disabled, and window output defaults to enabled. These are
preferences for the original's performance-report facility, not an assertion
that every diagnostic in the executable is disabled by default.

At `0x4e1e50`, elapsed-time reporting is conditional on performance reporting
being enabled and hardware performance-counter availability. Its observable
destinations are debugger text and a named report held for a diagnostic window.
At `0x4e1990`, updating that named report uses a critical section. These examined
reporting paths do not write and synchronously flush one ordinary disk-log line
per rendered frame or per unit path. This statement is restricted to these
paths; it does not establish absence of disk diagnostics elsewhere.

At `0x4da120`, a separate diagnostic facility sends text to the debugger and a
modal message box. Thus the original does contain diagnostics with potentially
visible execution cost; reducing RWE logging is not justified by claiming the
original has no diagnostics.

### Comparison and implementation boundary

The implementation room reports that RWE currently enables Debug diagnostic
messages in Release builds and flushes its log after each line, including
frequent frame-buffer and path diagnostics. That RWE-side premise was supplied
for this investigation and was not established by this spec-room agent reading
implementation code.

RWE's logger severity names, `NDEBUG` build selection, `RWE_LOG_LEVEL` build
override, message format, and per-frame/per-path diagnostic records are RWE's
own development facilities. Selecting Info as their Release default, retaining
Debug for debug builds, and preserving an explicit `RWE_LOG_LEVEL=0` build
override is an RWE-original maintenance choice. There is no original counterpart
to this particular RWE compile-time severity choice. The inspected original
performance diagnostics have their own runtime preferences and destinations;
they do not supply a conformance requirement for RWE's logger configuration.

This finding supports changing diagnostic visibility only. Suppressed messages
must be observers: their argument evaluation and output must not be required to
advance simulation, consume simulation randomness, emit commands, or change
saved state. No change to simulation ordering, logging of protocol or save-file
data, or original diagnostic UI is specified here. This is not a deliberate
departure from decoded gameplay and requires no new gameplay divergence entry
in TOTALA-EXE section 88. It makes no original-parity claim for RWE logging.

### Verification and remaining uncertainty

The static comparison establishes the narrow distinction between the examined
original diagnostic facilities and RWE's severity-selection policy. It does not
prove that logging caused the user's lag spikes or establish any performance
benefit. It does not inventory all original diagnostic routines, and it does
not establish their behaviour when run on this machine.

Implementation acceptance should check Release and debug defaults plus an
explicit level-0 build override. Performance acceptance should compare the
same RWE workload with the frequent diagnostics enabled and suppressed,
preferably using the user's shipped-data save and controlling concurrent CPU
and disk work. Compare simulation hashes or equivalent deterministic output
when available to establish that diagnostic suppression changes no simulation
result. Such checks establish RWE behaviour only. No original runtime, demo
oracle, or shipped-data performance check was executed in this spec room.
