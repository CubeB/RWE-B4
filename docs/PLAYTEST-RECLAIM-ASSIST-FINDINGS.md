# Play-test findings: reclaim and constructor assistance

Spec-room comparison, 2026-10-21. This document contains prose findings only,
prepared from the separate `D:\RWE-spec-room` documentation checkout under
ADR-0004. No RWE implementation or tests were inspected or changed in this
investigation. These findings document the original; they do not identify the
cause in the user's running RWE match or authorize a particular fix.

## Reference and validation status

All findings below compare against byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, inspected read-only. The referenced
original is the GOG v3.1 TotalA.exe, SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
Evidence addresses are original executable addresses. This is a static
reference check. No original game was launched, no new demo oracle was run,
and no executed-original result is claimed.

## RA-01: Wreck reclaim finishes by elapsed work, including aircraft jobs

Evidence: feature reclaim handlers at `0x404AD0` (ground) and `0x414770`
(aircraft); final resource transfer and removal at `0x4237D0`. Comparison with
existing economy finding section 97 confirms its ground timer but narrows its
claim that every builder has an identical timer.

A reclaim order resolves a feature through its map footprint at the order's
horizontal location. A missing or non-reclaimable feature terminates the job.
The ground constructor approaches the feature footprint, starts its build
script with a heading relative to its hull, and waits for its build stance.
The aircraft takes off if necessary and approaches the order's position.

Once work begins, the ground job has a countdown initialized to the truncated
value of `15 + (metal + energy) / 2`. The aircraft job instead uses the
truncated value of `30 + (metal + energy) / 2`. Both subtract two every two
ticks until the result is nonpositive, then attempt the final transfer and
removal. This countdown belongs to the individual reclaim order. WorkerTime,
feature hit points and the builder's distance in height do not enter either
countdown. There is no repeated height/depth or spherical-distance rejection
in the working portion of either handler. In particular, an aircraft which
has begun reclaiming a ship wreck cannot be kept working forever merely by
the wreck sinking farther below its flight altitude.

The final operation identifies the feature from its horizontal map cell,
credits its resources and requests its removal with the reclaim removal
reason. A special map-feature restriction can refuse this final operation;
that restriction is distinct from height and needs its own investigation if
encountered. The countdown handlers do not restart their work indefinitely
following a normal completed transfer/removal attempt.

The reverse-nanolathe particle box is derived from footprint, ground height
and feature height. It is presentation evidence of an active job, not a
measurement of accumulated resource removal.

Remaining uncertainty: initial aircraft approach tolerance and precise
interaction of final removal with the special map-feature restriction were
not decoded here. Neither submerged ship reclaim nor simultaneous reclaim
by several constructors was executed in the original. Existing RWE divergence
register section 88 explicitly records a different feature-work formula
(`metal + energy + hitPoints/4`, scaled by builder work); these findings do not
remove that deliberate divergence or require changing its pacing.

## RA-02: Ground repair approaches before deploying, and range is horizontal

Evidence: mobile repair at `0x405300`; build-script dispatch at `0x438590`;
stance wait at `0x438700`; repair credit at `0x41BD10`. Static comparison
corroborates economy section 94 and the flat-range conclusion in weapons
section 127.

A mobile constructor repairing a completed unit measures horizontal centre
distance and subtracts footprint allowances for both units. Each allowance
is the truncated value of `8 * sqrt(footprintX^2 + footprintZ^2)`, using the
unit's current footprint. It approaches when that remaining gap exceeds its
BuildDistance. Height does not enter this test. A constructor outside that
range must receive an approach goal before the build script is started.
Approach is reconsidered on a randomized interval of 30 through 59 ticks.

Once within range, the constructor reserves its working weapons, invokes
COB `StartBuilding` with the target bearing relative to its hull heading,
and waits for COB `INBUILDSTANCE` before crediting repair work. A raised
fabricator arm alone is therefore insufficient evidence that repair work
has started. The stance is script supplied; it is not a generic weapon
AimPrimary return value or a universal elapsed-arm-animation timeout.

The original works once a tick while repair continues. For the shipped
repair rates, the repair calculation awards one hit point and charges one
energy per accepted tick, subject to the builder's energy resource account.
A target in the original's moving/turning work-blocking condition causes
StopBuilding and a 15-tick retry delay. Target loss, completion, failed
approach and an optional patrol leash are separate reasons to stop/retry.

Remaining uncertainty: the precise names of the target motion conditions
were not established here; they must not be equated with every visual
animation or weapon aiming action. No shipped constructor script was run
and no repair episode from a demo was scored in this investigation.

## RA-03: Ground assistance targets the unfinished product and waits for stance

Evidence: unfinished-unit assistance at `0x403F70`; guarding/following work
selection at `0x406300`; script dispatch and stance gate at `0x438590` and
`0x438700`; aircraft assistance at `0x414380`.

Guarding a constructor or a factory is a continuing order. If the guarded
unit is damaged, a repair-capable guard first issues an appropriate repair
job. When the guarded builder has a current construction target, the guard
issues a help-build job aimed at that unfinished unit. For a factory this
is the product under construction. A healthy completed factory is not the
unit to which construction progress is credited. Other eligible work of a
guarded builder can be followed as a temporary work order; the continuing
guard order remains available afterwards.

The ground help-build job first attaches an approach goal around the target.
The outer radius is BuildDistance plus a target footprint allowance, with
that allowance equal to `trunc(16 * sqrt(footprintX^2 + 2*footprintZ)) / 2`
using the target definition's footprint and integer division. The unusual
second term really is twice the second footprint dimension, rather than its
square. This is a specific original quirk, distinct from the symmetric
footprint subtraction in mobile repair.

After successful approach, assistance rejects a target which has already
finished, starts COB StartBuilding toward the target relative to the hull,
and waits for INBUILDSTANCE before contributing construction work. The work
credit uses the assisting constructor's own WorkerTime and the ordinary
construction/economy calculation. It continues once a tick until the target
completes or the job is interrupted. Thus a ground constructor remaining
outside approach range or remaining below the script stance gate cannot
make progress merely because its arm has begun to move.

Aircraft help-build approaches within its BuildDistance and invokes
StartBuilding but does not wait for INBUILDSTANCE before work. This agrees
with existing economy section 110's aircraft/ground distinction. The ground
stance gate must not be applied indiscriminately to aircraft.

Remaining uncertainty: guard job eligibility flags and exact command-cursor
routing for every repair/assist gesture were not exhaustively decoded here.
The user's ground-constructor symptom could arise before approach, at the
COB stance gate, at target selection, or at resource acceptance; this static
finding does not choose between those RWE causes. No executed original-game
factory assistance check was performed.

## RA-04: Exact feature-reclaim timing, payout and job lifetime

Follow-up static comparison, 2026-10-21, against the same revision/build
recorded above. Evidence: ground work at `0x404AD0` (timer initialization
`0x404BBA`, subtraction `0x404C7C`); aircraft work at `0x414770`;
order scheduling at `0x43B7C0`; deadline calculation at `0x439E80`;
final payout at `0x4237D0`; removal/animation handling at `0x423550`.
This refines RA-01 and economy section 97; the shorthand 'one tick of work a
tick' is insufficient to reproduce the final removal tick.

### Arithmetic and timing boundaries

Let S be the feature's metal plus energy resource value. The original uses
the parsed floating resource quantities, adds half their sum to a base,
and truncates the positive result toward zero into an integer countdown T.
The ground base is 15 and the aircraft base is 30. Thus a sum of 1 produces
T=15 on the ground and T=30 in the air; it does not round 15.5 or 30.5 upward.
The mathematical statement is `T = trunc(base + S/2)`, subject to the
original's floating arithmetic for fractional or extreme modded values.
There is no term for damage, remaining hit points, WorkerTime or kills.

Initialization occurs after approach has succeeded. For ground constructors
it precedes StartBuilding and the subsequent stance wait. For aircraft it
coincides with the working sound after successful approach. The aircraft
feature-reclaim handler neither invokes StartBuilding nor waits for
INBUILDSTANCE. Its reclaim particles obtain the nano-piece position, but
that query does not add a stance gate.

Take tick zero to mean the first visit which actually decrements this
initialized countdown, excluding approach and any ground script delay.
The first decrement happens immediately at tick zero. Later work visits
are spaced two simulation ticks apart. Each work visit subtracts two before
checking for nonpositive remaining time. The two-tick deadline is installed
even on the visit which first reaches zero, so reaching zero does not pay
out on that same visit.

For ordinary positive T, the first nonpositive result occurs on visit
`ceil(T/2)`, at elapsed tick `2 * (ceil(T/2) - 1)`. Aircraft then pays out
and terminates on the next two-tick deadline. Ground instead has one
additional two-tick countdown visit before its final two-tick deadline
and payout. The final elapsed times from first decrement to payout are:

| Constructor | Initialized T | Elapsed ticks to payout/removal attempt |
| --- | --- | --- |
| Ground | trunc(15 + S/2) | 2 * ceil(T/2) + 2 |
| Aircraft | trunc(30 + S/2) | 2 * ceil(T/2) |

These times intentionally count tick zero as the first decrement, rather
than charging an extra tick before that first work visit. Examples which
pin the odd/even and scheduling distinctions:

| S | Ground T | Ground elapsed | Aircraft T | Aircraft elapsed |
| --- | --- | --- | --- | --- |
| 0 | 15 | 18 | 30 | 30 |
| 1 | 15 | 18 | 30 | 30 |
| 2 | 16 | 18 | 31 | 32 |
| 4 | 17 | 20 | 32 | 32 |
| 100 | 65 | 68 | 80 | 80 |

The ground handler's extra visit is directly present in the static reference;
it must not be silently flattened into the aircraft completion boundary.
This table assumes uninterrupted normal timer wakes and nonnegative shipped
resource values. Negative or overflowing modded values have not been
characterized. No runtime timing measurement was made here.

### Payout and refusal boundaries

There are no partial resource payouts during the work countdown. At the
final operation, the original looks up the feature again by horizontal
map cell. No feature means no payout. An existing active feature animation
combined with an animated feature also refuses the operation before payout.
For an otherwise eligible feature it adds the feature's full energy and
metal to the reclaiming constructor's resource account, then requests
reclaim removal. This credits the constructor's account rather than directly
bypassing the normal settlement into its player's storage.

The original's active computer-player difficulty adjustment scales this
credit to 0.5 on Easy, 0.7 on Medium and 1 on the remaining difficulty; human
credit is unscaled. This is a payout policy, not a reclaim-duration factor.

Removal can start the feature's reclaim/death animation, with actual map
replacement deferred until that animation finishes. For features with no
such animation the replacement is immediate. Resource credit precedes the
removal routine. Consequently, exhaustion of the 2048-entry feature-animation
pool can refuse an animated removal after credit has already occurred.
That exceptional case must be distinguished from the pre-credit animation
refusal. The reclaim order terminates after its final attempt; it does not
wait indefinitely for either an animation or a refused removal. This is a
static finding about the original's behavior, not a requirement to reproduce
its resource-credit failure case without an explicit conformance decision.

### Progress belongs to an order

The timer is stored in the reclaim order, not in the map feature. Multiple
builders do not pool feature-reclaim work. Replacing a reclaim command with
a fresh command requires a fresh approach/work timer; a new job cannot inherit
work because another builder previously reclaimed that feature for a while.
A surviving order suspended without replacement retains its own timer.
Final removal by one builder causes another's later missing-feature check
to end that other job without an additional payout.

A stored RWE shared feature-progress value has no original-game counterpart.
How older RWE saves migrate it, and whether to reset an already-started RWE
job's elapsed count, are RWE-specific compatibility policies. This finding
does not prescribe a save migration and does not label such a migration
original parity. Unit reclaim uses a different damage/bite mechanism;
nothing in RA-04 changes that finding or its sixteen-tick cadence.
