# Moho assistance corner stall

Implementation-room investigation, 2026-10-21.

## MA-01: Assistance must approach until it can contribute

Original comparison: `PLAYTEST-RECLAIM-ASSIST-FINDINGS.md` RA-03, a static
byte-tactics comparison at revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, against GOG 3.1 executable SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
The ground help-build job approaches the unfinished product, deploys its
construction script and then contributes work. It does not deliberately park
outside work range. No original executable check was run here.

RWE's rectangular navigation goal and its eight-world-unit arrival tolerance
can declare arrival outside the circular footprint-distance construction
check. This happens near corners: the assisting bot repeatedly requests the
same already-reached goal without ever starting construction.

Correction: if the expanded approach rectangle is already reached but the
construction reach check still fails, navigate closer to the unexpanded target
footprint. Normal approach and the work-range gate remain in place. Factory
assistance still measures approach to the factory containing its product.

The fallback is an RWE-specific navigation implementation detail with no
direct original counterpart. It implements RA-03's approach-before-work
requirement; it does not claim original-radius conformance for RWE's existing
footprint-based reach approximation. No deliberate new behaviour departure is
introduced. Other reasons for stalled work, including resource shortage,
unreachable terrain and construction-script stance, remain distinct.

## Shipped-data reproduction

The offline probe loads the installed TA archives, models, unit definitions
and COB scripts, using flat terrain and ample resources. An ARMCK guarding
an ARMACV building ARMMOHO stops near a corner and contributes no work;
the analogous CORCK/CORACV/CORMOHO scenario also stalls the assistant.
The primary constructor continues building, distinguishing this from a
resource stall. Direct approaches along an edge do contribute, while a
direct click approached diagonally reproduces the same geometry mismatch.

Artifacts: `D:/tmp/rwe-retaliation-20261021/moho-*.txt`. These execute RWE
only; they are not an original-game oracle.

## Verification after correction

The focused corner regression makes zero construction progress before the
fix; after it the helper enters its building state with an active nano origin
and advances the frame. The combined targeting, guard assistance, repair,
repair patrol, builder facing, aircraft construction and water construction
checks pass: 56 test cases, 463 assertions.

The main and isolated Release game targets build successfully. Shipped ARM
and CORE checks cover direct orders in both interface schemes, guarding the
primary constructor and direct diagonal approaches. Both construction bots
now move closer, enter construction stance and contribute. For example, the
ARM guard case previously advanced 900 points per 150 ticks from the primary
alone; it now advances 1200, including the helper. CORE shows the same
6-to-8-points-per-tick change. No work-distance increase is needed.
