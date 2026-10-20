# Approved mission and elimination work

Owner decisions, 2026-10-20: #383 option B; #699 all three modes;
#700 continued watching. Reference evidence is in
[the spec-room supplement](383-699-700-findings.md); deliberate adaptations
are recorded in [the divergence appendix](383-699-700-divergences.md).

## Mission profile translation

Only mission seats using the RWE AI receive this translation. Classic seats
retain their existing profile reader. Named profiles fall back to DEFAULT.
The existing classic directive parser provides difficulty gates, category
membership, direct-name locks, per-type limits and clamped multipliers.

Structure priorities are stably sorted by decreasing effective multiplier;
equal multipliers retain the planner's priority order. Factory combat mix
shares are multiplied by the profile percentage. Zero-weight types and types
at their cap are unavailable to every builder, including unknown/mod builders.
The final production-command gate reserves each accepted order in emission
order, preventing simultaneous planners from exceeding a cap. Live units,
nanoframes, outstanding construction orders and factory queues count toward
the cap; a factory's held frame is counted once. Existing units are retained.
These choices are RWE-original, not a port of the original AI's build scores.

Shipped profile fixture: extracted `rev31/ai/MISSIONS.TXT`, SHA-256
`a48c3791d36d77705e85abc72a857a0c9b56ca539dec5828b839f3eb8dd2619d`.
The test retains its directive sequence at easy/medium/hard. The parser's
separate tests cover name/category locks and overlapping directives.

## Commander modes

The existing GameEnds/Continues enum and save names are retained; Deathmatch
is a third value. Both setup screens advertise it. Command recordings move
to version 6 because mission policy and elimination outcomes have changed.
`--commander-death gameends|continues|deathmatch` is available to the game
and headless arena. Missions continue to use their own rules.

Each defeated seat has its own six-observation countdown. Deathmatch suppresses
ordinary victory for both skirmish and multiplayer, respawns human and computer
seats, and uses the initial side-resolved commander type. Candidates use the
documented bounds, order, 9999-attempt limit and widely spaced nine probes,
with RWE's established deterministic RNG. Removed peers never respawn.
Successful replacement preserves the bank/statistics, credits starting resources
through the new unit, and enables unscaled base storage of at least 200 plus
normal unit storage. The local human's camera follows a replacement.

The wipe uses direct 30000 damage with the documented victim-veterancy reduction
and the existing cause-3 death path. The existing missing Killed-script ladder
for cause 3 remains tracked separately by #409; this work does not claim it
reproduces that ladder. Safe failed creation is retried on later polls.

## Verification status (work in progress)

The initial mission-profile build linked the game and passed 13 focused cases
(109 assertions). Further verification is required after the final changes.
No original executable was run. Rendering findings and the owner's accepted
submarine construction height are outside these three implementation issues.
