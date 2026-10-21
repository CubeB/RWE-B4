# Allied visibility performance comparison

## PERF-VIS-001: Reuse owner LOS work when preserving RWE team sharing

Research date: 2026-10-21. ADR-0004 spec-room documentation only, from
`D:/RWE-spec-room`. Reference inspected read-only: byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, matching GOG Total Annihilation
v3.1, binary SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
No RWE source or tests were inspected or changed. No original executable,
shipped-data scenario or demo oracle was run. The RWE profiling figures and
proposed algorithm below are supplied implementation-room context, not
measurements made by this researcher.

### Original comparison: owner grids, not unconditional live team groups

The original has per-player byte current-visibility counts and one shared
16-bit explored mask with a bit per **player index**. The older description
"bit per LOS group" must not be read as proof of automatic allied grouping.
The normal LOS update at `0x4827B0` supplies the owning player to the sight
operations. Current-count addition at `0x482270` and subtraction at
`0x481D50` address that player's own byte grid. Permanent reveal at
`0x481930` sets that player's index bit in explored cells. The count is used
as a nonzero/currently-visible predicate; the explored bit remains set after
ordinary source removal.

The original's counts themselves are **not RWE-specific**. They are byte
increments/decrements without an explicit saturation guard in these original
stamp operations. The proposed full-grid recomputation, saturating team sum,
and symmetric team-group bit are RWE scheduling/representation choices, not
a decoded original group-combination algorithm. Do not claim original
high-overlap byte-overflow conformance from the proposed saturating sum.

Existing finding BT-57 in `BYTE-TACTICS-SPEC-ROOM-FINDINGS.md` describes the
actual explored-map copy at `0x485420`: it copies one player's explored bit
to another without clearing either or copying current counts. Its callers
are the manual share dialog at `0x4934B0`, received mapping requests through
`0x453D40`, and the preference-gated network sharing tick at `0x457D30` every
450 ticks, targeted at eligible remote human allies. Commands and handshake
flags exist, but the current-count routines above do not merge allied grids
merely because players are allied. BT-57 leaves a wider live-unit-sharing
question open; this investigation does not close it by inference.

Therefore unconditional continuous sight sharing among RWE team members is
an existing RWE policy, documented as such in the command register and BT-57,
not original parity established by these routines. Optimizing that existing
policy does not authorize changing it. The original-versus-RWE sharing-policy uncertainty remains outside this
change. This finding establishes no new departure, changes no sharing policy,
and does not require inventing a new divergence for unchanged behaviour.

### Original detector scope is separate

The sensor pass at `0x467440` sweeps radar/sonar sources from the viewing
player's own unit range. Sources must be live, not in the excluded unit state,
and activated. The pass does not gather every allied player's detectors into
a symmetric detector union. It separately grants picture flags to own units
and to allied units whose owner's radar-sharing preference allows it; this is
not an allied-detector terrain reveal. ShareRadar is preference controlled
(`0x418FD0`). Thus "the original never shares any radar information" is too
broad; "the original detector sweep does not merge allied detector lists"
is the supported result.

Jammers are another separate pass: live activated units not owned by the
viewer can jam, including allies. They run after detection and can remove
contacts. Sonar's underwater visibility veto, cloak, direct ownership and
unit-model visibility checks remain separate from whether a terrain cell has
a nonzero current count. The terrain work must not become a shortcut that
bypasses those rules. Existing section 17/18 records the detailed predicates.

### Gates that owner-work reuse must preserve

Normal per-player unit processing at `0x464F80` invokes LOS for every live
unit. An unfinished nanoframe is initialized live (`0x485A40`) and supplies
normal sight without waiting for completion, as recorded in CV-01 of
`PLAYTEST-CONSTRUCTION-VISIBILITY-FINDINGS.md`. Ordinary LOS has no activation
requirement equivalent to the radar/sonar source gate. Dead unit slots cease
to be ordinary live sight sources. The original also has temporary sight
sources such as the death sight patch (BT-56, `0x482910`); an optimization must
preserve whatever separate sources RWE already includes rather than assume
all sight necessarily comes from live units.

Preserve the exact existing source eligibility, owner, position, height,
sight mode, range quantization, terrain fan/horizon arithmetic and explored
writes. The optimization is not a reason to remove nanoframe vision, merge
inactive sources, alter transport policy or apply detector activation to LOS.

### Proposed optimization classification and acceptance limits

The supplied proposal preserves RWE's existing symmetric team current-visibility
policy; it introduces no new sharing and makes no original-parity claim.
It evaluates each eligible unit's terrain sight once into
its owner's fresh scratch grid, then combines current counts among the
existing symmetric team members cell by cell. Each team member receives
`min(255, sum of member current counts)` for each cell. Explored writes retain
the existing owner-to-group mapping. Radar, sonar, contacts and jammers retain
their existing per-player processing. This is **a scheduling and derived
scratch optimization of existing RWE behaviour**, with no original equivalent
for this exact team-combination schedule.

For fresh grids and nonnegative contributions with existing saturation at
255, saturating each owner's subtotal before the final saturating sum gives
the same result as adding all team contributions directly with saturation.
That arithmetic equivalence is independent reasoning, not a decompiled
algorithm. It does not hold as an exact-count substitute for arbitrary
incremental removal after saturation. Fresh recomputation and absence of a
reader before combination are material conditions. Team groups must preserve
the existing symmetric team membership rather than introduce transitive
sharing across arbitrary directed alliances. Combining into a destination
must not cause that combined total to be counted again for later members.

Acceptance should compare completed-tick visible and explored outputs before
and after the optimization on the same shipped-data state, including a team,
a singleton, overlap/saturation, a nanoframe, source death and save/load.
Retain current fog-mode and separate detector/contact outputs. The comparison
must inspect actual RWE consumers before treating exact byte counts as
unobservable: nonzero equivalence alone is insufficient if any consumer uses
count magnitude. Derived scratch must not silently change saved/hash state,
publication timing or visibility read earlier within a tick. Measure the
performance change on the supplied multi-AI map after correctness checks.
None of these RWE acceptance checks has been executed by the spec room.

