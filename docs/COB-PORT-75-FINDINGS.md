# COB GET 75: local-computer authority query

Spec-room extension finding, 2026-10-25, under ADR-0004. Documentation only;
no RWE implementation or tests were inspected or changed.

## EXT-COB-75-01: primary author documentation establishes the query

Rime's own [TADR extension manual](https://rime.ddns.net/dev/ta/ta-rime.pdf),
page 14 (zero-based page 13), names top-level GET port 75
`UNIT_IS_ON_THIS_COMP`. Its parameter is a UnitId; it reports whether that
unit is local to the computer. This is an authority/locality predicate,
distinct from port 74's alliance query and port 72's player-owner identity.
The manual does not specify an invalid-ID result, omitted-argument behavior,
exact true/false integer encoding, or capture-transition timing.

The same document, pages 27-29, separately assigns 75 to `AntiWeapons` within
the UNITINFO-property selector table. That selector is an argument to the
UNITINFO extensions, not an alternative meaning for top-level GET 75. These
numeric namespaces must remain distinct.

The author identifies Recorder 4.0.0.538 on the manual's opening page and
directs modders to release `exptype.h` definitions. No third-party source or
decompilation was read here. The PDF was opened directly and the relevant
table text checked, rather than relying only on search-result snippets.

## Original-v3.1 boundary

The previously documented vanilla comparison remains unchanged: byte-tactics
revision `e89721c46c3fa06e59558419b54fe26061fafb73`, representing GOG/v3.1
`TotalA.exe` MD5 `8e74a1dffa1f5988624c52048f5b20cd`, has unit-value GET
handling for 1-20 at `0x480770`; an unrecognized value returns 0. Supporting 75
is consequently a third-party extension compatibility change, not vanilla
conformance. See `TA-MOD-COMPATIBILITY-FINDINGS.md` MOD-COB-01.

## RWE interpretation and limits

Inference for an RWE adaptation: locality should concern whether this machine
simulates the referenced unit's owner, rather than whether that owner is the
human player, belongs to the human's team or is allied. A locally simulated
computer player's units can therefore qualify. A seat simulated elsewhere
should not qualify merely because it is allied. This follows the project's
Local/Remote distinction but has not been checked against a running patched
original, and is not an assertion that RWE's lockstep and TADR's ownership
models are identical.

Boolean normalization, missing/invalid targets and authority-transfer timing
need an explicit adaptation decision or further primary/runtime evidence.
The implementation adaptation, recorded in TOTALA-EXE section 88, uses 1/0
and returns 0 for an absent target. It reads the referenced unit's current
owner each time, so capture follows current simulation authority. These are
explicit RWE policies, not additional claims about the author's implementation.
Do not promote guessed edge cases into an exact TADR contract. No evidence
here establishes a SET75 operation.

The implementation room reports a conservative constant-stack census finding
GET 75 in 190 desktop Escalation script copies, GET32 in 2, and no unknown
opcodes across 550 COB copies. These are reported implementation-room data
results, not an independently repeated census. They establish priority for
this port but do not determine its return values or prove complete script
compatibility. The census counts copies rather than necessarily distinct
script behaviors and can miss dynamically computed port numbers.

No original or patched game was launched; no mod script was executed, binary
decompiled, screenshot measured, matching checker run or demo oracle used.
The contract above is **primary extension documentation plus a previously
established static vanilla comparison**, not executed patched-engine parity.
