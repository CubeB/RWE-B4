# Unit behaviour port recovery, 2026-10-14

Implementation uses verified register findings under ADR-0004. Recovered patches
came from `agent-ac6297150b54e7f21` (air movement and flat range) and
`agent-aa9ff5151c3fb406e` (aim lead, pads and constructor patrol).
Their worktrees remain intact. `agent-aefc00658d36bb04a` had no pending code.

| Section | Result |
| --- | --- |
| 125 | Air patrol goal 320 beyond waypoint; strict 336 arrival; 30-tick re-aim |
| 126 | Six-kill fixed-point aim lead; cruise and zero-speed exclusions |
| 127 | Shared flat range arithmetic; per-axis floored scores; targets retained outside range |
| 128 | Loaded airborne transport hops around its idle anchor; three draws in order |
| 129 | Cumulative gunship misses; reroll consumes a draw without installing a bearing |
| 130 | Mission-specific repair diversion; occupied-pad circle; no advance reservation |
| 131 | Repair/help-build before sampled reclaim; independent jobs, leash and return |
| 132 | Deferred on purpose, retained at the user's explicit request |
| 133 | Factory posture at spawn and completion; valid rally; local human control groups |
| 134 | Cancellation and decay metal refunds; external product death flushes queue/rally |
| 135 | No new climb goal required; inferred hung Loading state awaits observation |
| 136 | First matching Shift order toggles; mouse Move/Patrol formation with outlier threshold |
| 137 | Existing append/back removal is equivalent; node representation retained |
| 138 | Deferred on purpose, retained at the user's explicit request |

New navigation state, repair leashes and pad rebuild time participate in saves,
dumps and sync hashes. Command replay version 2 refuses older command streams,
since replaying them under changed simulation rules would change the game.
Full-state saves retain their schema with defaults for the new fields.

## Remaining uncertainties

The repair port uses per-unit phase checks rather than original mission-event
wakeups, reads pad candidates live rather than caching a 30-tick snapshot,
uses unit id for unknown within-cell chain order, and draws from the metal list
before energy where the register does not specify the order. Repair-job leash
testing and waypoint retirement retain `[open]` notes. Unarmed aircraft circling
a busy pad use the existing unarmed radius. These are approximations, not new
executable findings. Section 135's hung mission remains explicitly inferred.

The classic AI was already merged. Its `[open]` choices remain unchanged:
weapon/work flags, low-wind threshold, net-income definition, candidate ordering,
unspecified placement distances and absent water-key handling, sequential score
multipliers, scout locations, group centroids and movement-leash representation.
Threshold and determinism tests check this implementation; they do not establish
those choices as original behaviour. Further fidelity changes need a register
finding or black-box evidence.

## Validation and watchable match

Debug `rwe`, `rwe_test` and `ai_arena`, and Release `rwe` and `ai_arena`
built successfully. The full suite passed 1,534 cases and 172,459 assertions;
one external game-data checksum case skipped because the Windows test shell
has no `HOME`. All 97 affected cases also passed in focused runs.

A Release game used the classic engine with both seats explicitly set to the
Original TA personality, Standard difficulty: ARM versus CORE on Great Divide,
seed 7, random starts. It reached the 3,600-second limit with both commanders
alive (ARM 169 units, CORE 95; 108 and 106 units lost respectively).

The version-2 recording is in the normal Windows `RWE/Replays` folder as
`Original TA - ARM vs CORE - Great Divide - ported 2026-10-14.rwereplay`.
`Watch Original TA ARM vs CORE.cmd` beside it opens the Release replay viewer.
Headless Release playback exited successfully under `--strict`; all 107,998
playback hash samples match the recorded game. The existing recorder's terminal
marker is tick 107,999, so playback stops one tick before the recording's
108,000-tick arena cutoff. This is a command replay, not a TA network demo.
