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

## Continued watching

Ordinary multiplayer elimination uses the per-seat countdown. Eligible humans
receive the corrected Yes/No prompt while other opponents remain. Watching is
local presentation: shared visible/explored masks and radar/sonar contacts,
with cloak and underwater restrictions retained. It neither changes ownership
nor permits defeated seats to command units. No takes the local end sequence;
Yes closes the prompt and keeps the peer in lockstep. Ordinary multiplayer
ends through the match countdown when every participating human is out,
even if computer units remain. Deathmatch bypasses this fallback.

`allowWatching` defaults true as an explicit RWE policy and is retained in
save/replay headers and host announcements. Direct launches accept
`--allow-watching false`; the built-in lobby currently advertises its default
policy. RWE lockstep has no AI dependent on a particular human host, so it
does not need the original's forced-watch hosted-computer exception. The
reference eligibility helper retains that distinction for ownership modes.

## Verification

The final focused Debug run passed 98 cases (3119 assertions). It exercises
the real AI controller with two idle factories competing for one remaining
constructor slot, forbids further constructors at zero cap, and verifies that
a reduced rocket multiplier changes the combat mix. Commander-mode tests cover
repeated replacement, preserved resources, base storage, removal, surviving
high-HP veterans, and a wipe blast killing another seat's commander. Watching
tests cover eligibility, host announcements, save/replay policy and the
all-humans-eliminated fallback.

The complete permitted Debug suite passed 1846 cases (193885 assertions)
using `~*checksum*`; the five packet-checksum cases then passed separately
(33 assertions). The two TaChecksums tests were excluded under the standing
repository instruction.
All Debug targets built successfully. A direct COFF-header inventory checked
1061 CMake objects; the largest was `GameScene.cpp` at 24276 sections against
the 24575-section budget.

Native RWE command replays on shipped Coast To Coast and Lava Run each ran
900 ticks with seed 7. At tick 870 the two seats had respectively respawned
twice and once. The Lava Run replacement positions were above its sea level
21 (heights 45, 44.375 and 44.6875). Native RWE AC02 schema-0 runs completed
1800 ticks at each of easy, standard and hard, explicitly selecting the RWE AI;
all three logs confirmed loading `ai/MISSIONS.txt`. These runs verify loading
and execution; they do not establish the original AI's build sequence.

Reference comparisons are static, as recorded in the findings. No original
executable or visual multiplayer watching check was run. Rendering findings
and the owner's accepted submarine construction height are outside these
three implementation issues. The divergence register update is preserved in
spec-room commit `55a834434d6820f1caee6d862652717ae4bc1eb1`.
