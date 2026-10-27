# Playtest report: 2026-10-26

Work order: [importance and implementation effort](PLAYTEST-2026-10-26-PRIORITIES.md).

Recorded and triaged 2026-10-27 from the owner's
`Desktop/RWEbugsandimprovements.txt` (file modified 2026-10-27), followed by
the owner's pasted [TA Wiki cheat-code page](https://totalannihilation.fandom.com/wiki/Cheat_codes).
The owner described the gameplay reports as occurring "yesterday". The played
build identity, map, active mods/archives, exact units and reproduction steps
were not supplied. These are reported observations and requested improvements;
none was reproduced or compared with an executed original game during triage.

The owner requested documentation and implementation issues for all commands,
including the wiki's developer/menu shortcuts. No implementation accompanies
this report. Existing static findings are investigation starting points;
ADR-0004 and `AGENTS.md` govern any later code change. In particular, a fog
target being retained or a model extending past an edge is not by itself proof
that the original behaves differently.

## Reported gameplay problems

| Report | Tracking issue | Evidence still needed |
| --- | --- | --- |
| Aircraft stop in midair after engaging, attacking, destroying a target or patrolling. | [#894](https://github.com/CubeB/RWE-B4/issues/894) | Unit/mission, order and movement state at the stop. |
| Units go off the map; compare boundary rules with byte-tactics. | [#895](https://github.com/CubeB/RWE-B4/issues/895) | Map, movement class, edge/corner and origin versus model overhang. |
| Multiplayer lag spikes reach the waiting-for-player state. | [#896](https://github.com/CubeB/RWE-B4/issues/896) | Peer logs, timings, build/data identities, latency/loss and named waiting peer. |
| Invite codes and /accept are unreliable or confusing; joining should be simpler. | [#897](https://github.com/CubeB/RWE-B4/issues/897) | Failed join stage, NAT conditions and direct-code versus reply-code flow. |
| Underwater object placement does not match the build grid, especially tidal generators. | [#898](https://github.com/CubeB/RWE-B4/issues/898) | Exact unit, depth, preview/footprint/world position and camera. |
| Underwater metal extractors are invisible while their particles are still visible. | [#899](https://github.com/CubeB/RWE-B4/issues/899) | Unit/ownership/vision, model render and water-clipping evidence. |
| Pathfinding problems. The source gives no specific route. | [#900](https://github.com/CubeB/RWE-B4/issues/900) | One concrete failed route and selected backend before choosing a remedy. |
| Nanolathe particles do not look right. | [#901](https://github.com/CubeB/RWE-B4/issues/901) | Equivalent original/RWE captures and a specific visible difference. |
| Targeting persists after a previously seen enemy enters fog. | [#902](https://github.com/CubeB/RWE-B4/issues/902) | Identity, last-known position, radar/sonar, pursuit and firing distinguished. |
| Battleroom message-box text sits too high. | [#903](https://github.com/CubeB/RWE-B4/issues/903) | Affected input/history field, font, GUI data, resolution and scale. |
| A spectator cannot see any player's underwater units. | [#904](https://github.com/CubeB/RWE-B4/issues/904) | Eliminated watcher versus replay/demo playback, reveal settings and unit types. |

Each issue records an investigation and acceptance criteria. Similar symptoms
remain separate until evidence establishes a shared cause. In particular, the
underwater extractor and spectator reports are not assumed to be the already
corrected submarine factory-height problem. The generic pathfinding report is
linked to the existing backend investigation #454 without assuming a new
backend solves the reported failure. In-game stalls are distinct from #882's
menu/pre-skirmish loading report.

## Requested improvements and commands

- **Smaller downloads / Electron alternative:** existing
  [#745](https://github.com/CubeB/RWE-B4/issues/745) is the canonical issue,
  updated with the owner's renewed requirement for a tiny RWE download. Its
  old package measurements are identified by release; they are not a fresh
  measurement of the current build. Evaluate size and feature costs before
  selecting a replacement. Related native-library exposure work is #841.
- **All requested commands and cheat codes:** [#905](https://github.com/CubeB/RWE-B4/issues/905)
  and the complete [command implementation backlog](CHAT-COMMAND-IMPLEMENTATION-BACKLOG.md).
  Parser recognition, a settings slider or a debug tool does not count as a
  complete implementation of the requested typed interface.

## Merge review accompanying triage

Oscar Bailey (`ozgb`) authored #885, the 3.9.01 selection-key PR. It was reviewed
and merged on 2026-10-27 as `fdd33b643e1efea4c4de13d185c93d265711ccb1`;
its branch was deleted. Its deliberate original-game departures are recorded
in the divergence register, section 88. GitHub's Linux Debug, class-tag and
launcher checks passed; Windows jobs were skipped. The author's reported
scenario runs used shipped data; this triage did not rerun those scenarios
or the untested drag-filter interaction.

The separately open AMD PR #890 changes modern OpenGL texture/program lifetime
and optional debug reporting. Those API operations have no counterpart in the
original software renderer: they are maintenance of RWE's graphics backend,
not a claim of original-game parity. Its Linux Debug check passed, and its
author records before/after execution on the affected AMD hardware; this triage
reviewed that evidence and patch without rerunning the hardware experiment.
