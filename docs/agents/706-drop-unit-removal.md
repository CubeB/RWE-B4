# Issue #706: removing a dropped player's units

## Reference and scope

Owner decision, 2026-10-17, on CubeB/RWE-B4 #706: match the original's
removal of a dropped player's units. The implementation source is BT-232 in
`D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md`, with its resolved
entry in `BYTE-TACTICS-DISCREPANCIES.md`. Reference revision: byte-tactics
`97d7ac59`, GOG v3.1 original. This is a **static reference comparison**;
the original game was not executed for this change.

BT-232 records player removal at `0x452CC0`, its call to the unit wipe at
`0x452E17` (`0x486F10`), and the rebroadcast disconnect message at `0x454EC4`.
Every surviving peer removes the remote player's live units, using cause 3.
Confidence is high in removal and medium-high in the exact remote death path.
The original-game check remains optional and unperformed: drop a peer and
inspect the surviving player's map. See also `TOTALA-EXE-WRECKS.md`, the
game-end wipe finding.

The agreed tick and the pending wipe's saved state are RWE's lockstep
mechanism, with no original counterpart. Network receipt closes the command
stream; command consumption schedules the wipe in simulation time. The command
service filters unauthorized, invalid and repeated drops out of the queued
set at receipt. Once accepted, a queued drop remains a simulation input even
if a future rejoin has already arrived. Presentation
does not close the stream a second time when it announces the drop. This
separation prevents later packet arrivals from cancelling the unit wipe.
Scheduling accounts for a loaded game's different scene-time origin. Drop
selection also leaves the margin beyond already submitted local command sets,
so a deep latency buffer does not put the declaring command after its own
deadline. The simulation performs it at the agreed tick before any units act.
Save files and replay keyframes keep
the pending wipe, and the sync hash and desync dump include it. The headless
replay uses the same accepted-command function as the game scene.

The wipe uses RWE's existing immediate cause-3 self-destruct operation. This
change establishes army removal, not complete death-effect or corpse
conformance. The original's self-destruct tail and corpse remain separately
unported in `TOTALA-EXE.md` section 91; this change does not resolve them.
Old recordings containing player drops can produce different outcomes.

## Fixtures and verification

`playerdrop_sim.test.cpp` uses the shipped `totala1.hpi` values from
`units/ARMSOLAR.FBI` (326 hit points, build time 2495, footprint 5 by 5) and
`weapons/UNITS.TDF` (`SMALL_BUILDING`: area 205, damage 900;
`SMALL_BUILDINGEX`: area 205, damage 40). The building's 5 by 5 ground yardmap
is the shipped `o` mask; units go through the real completed-unit spawner.
Models and COB are inert stand-ins.
These test tick scheduling, finished and unfinished unit removal, isolation
of a distant surviving player's unit, authorization, duplicate handling,
simulation-clock offsets, hashing, and a save/load before the wipe.

Focused validation: 43 cases, 347 assertions passed (drop and mission tests).
The completed suite passed 1800 cases and 182946 assertions: 1795 cases with
checksum-named tests excluded, then the five permitted packet/lobby checksum
cases selected explicitly. Both TaChecksums cases were excluded from this
completed run. An earlier interrupted invocation mistakenly selected them;
its map case reached the missing-data skip. Test selection was then checked
with `--list-tests` before execution.

All Debug targets built successfully with `make -j6 -k all`. The section
budget passed over 1048 objects: the largest is `GameScene.cpp` at 24276
sections against the 24575 limit. Both Debug and Release `rwe` linked
successfully; the owner's play-test executable is `build-release/rwe.exe`.

Executed shipped-data state check: `ai_arena` ran a seed-7 replay on
Coast To Coast with ARMCOM and CORCOM loaded from `D:\RWE-Data`. A drop
command consumed at tick 30 names tick 90 as the cut. Both drop runs show
CORCOM alive at tick 89 with `removeUnitsAt=90`, absent at ticks 90 and 91,
its owner dead with one loss, and ARMCOM still alive. A control replay keeps
both commanders. The two drop hash logs are byte-identical over all 179
recorded ticks; their first 29 ticks also match the control. The missing
180th hash is the arena's existing end-cap behavior. Replays, state snapshots,
hash logs, run metadata and logs are in `D:\tmp\rwe-706-verification`;
`D:\tmp\rwe-706-inspect.py` asserts the observations. These are executed RWE
checks with shipped data, not executed original-game checks.

Executed RWE network check: the three-peer loopback harness, seed 7, killed
player 2 after five seconds. Both survivors accepted tick 1478 as the cut and
continued; the harness reported agreement on all 2398 compared ticks and
exited successfully. Logs and replays are under
`D:\tmp\rwe-706-network-final`. The scratch wrapper
`D:\tmp\rwe-706-headless-net.py` runs `tools/net-test.py` with the real scene's
headless arena option and a 120-second startup allowance. This is an executed
RWE check, not an original-game check.

## Draft wording for the owner's registers

The current section 88 and compatibility register describe the timeout
policy separately and do not contain an army-retention entry to remove.
Leave those protected files untouched. Proposed addition after their existing
timeout-policy paragraph:

> Both engines remove a dropped player's units. RWE performs the cause-3 wipe
> on the agreed drop tick on every peer (BT-232, #706); packet arrival only
> closes the command stream. Its shorter automatic timeout remains a deliberate
> divergence from the original's prompt and timeout policy (BT-233).
