# Allied visibility optimization validation

2026-10-21. Implementation-room verification of
[PERF-VIS-001](PERFORMANCE-ALLIED-VISIBILITY-FINDINGS.md). This preserves
existing RWE team sight policy; it does not establish original live-sharing
parity. The reference comparison is static GOG 3.1 analysis through the
spec-room finding, not an executed original-game check.

## Change and consumer review

Trace each live sight source into its owner's fresh byte-count grid once.
Combine each symmetric team exactly once, using saturating sums, then copy
its completed grid to its other members. Teamless players do no grid merge.
No new persistent state, allocations per source, worker threads, detector
sharing, sight range changes or changes to source eligibility are introduced.

`arePlayersAllied` is same-owner or equality of present lobby team IDs, so
its groups are equivalence classes. Canonical first-member processing works
with interleaved teams and never reads an already combined member as input.
For nonnegative fresh contributions, min(255, sum(min(255, owner subtotal)))
equals min(255, sum(all source contributions)). Counts are rebuilt after
source death rather than subtracting from saturated totals.

`updateVisibility` clears all current grids first. Its source loop builds
sight and detector/jammer lists without consulting current sight; contacts
are computed afterwards. The merge precedes permanent-sight promotion and
contact evaluation. GameSimulation::tick publishes completed sight at the
same point as before. PlayerVisibility's ray-dedup stamps are private scratch,
not saved or hashed, and the next source starts a new stamp. No mid-loop
reader is moved across publication. `visibleCount` production use is only
its definition, while tests inspect exact counts; rendering also reads byte
grids directly, so exact count preservation is tested, not assumed irrelevant.
Explored writes keep their existing team bit and become idempotent once per
source. Radar/sonar detectors, jammers and contact loops are unchanged.

## Executed RWE checks

- Before and after: 32 visibility/save-load cases, 658 assertions pass. The
  new coverage has three nonadjacent allies, an enemy team and a teamless
  player; 300 overlapping sources from one owner plus 70 from allies force
  owner and team saturation. Death reduces counts from 255 to 31, then an
  unfinished frame alone contributes 1. Tests cover terrain, circular and
  permanent sight, explored retention and reconstruction after save/load.
- Shipped-data Full Moon, seed 6202, 1,200 simulated seconds, Hard RWE AI,
  ARM/ARM team 1 versus CORE/CORE team 2: all 36,000 completed ticks have
  identical simulation hashes and identical deterministic FNV-1a 64-bit
  digests of every player's exact current byte grid, sorted radar and sonar
  contact IDs, and the shared explored grid. Final unit count: 196.
- Shipped-data Full Moon, seed 6203, 300 simulated seconds, two opposing
  teamless Hard RWE AI seats ARM and CORE: all 9,000 tick hash/digest records
  match. This checks the no-team path as well as the allied workload.

The comparison harness observes completed ticks and uses no gameplay random
numbers. Hashes/digests are regression evidence, not a mathematical proof
against collisions. Timing runs omit the extra verification digests.

Both the active-tree and isolated-tree full Release `rwe` targets built and
linked successfully after applying the optimization. The active playable
`D:/RWE/build-release/rwe.exe` includes it; profiling remains disabled.

## Performance measurements

Release MinGW64, -O3, NDEBUG, ffp-contract=off. The baseline is a fixed snapshot
of the active development build (including the current AI work, retaliation
and assist fixes). Candidate links the same archive with only the visibility
translation unit replaced. Both use identical temporary arena instrumentation
and existing SIMPROF timers in GameSimulation and UnitBehaviorService only.
Normal playable builds keep profiling disabled. All runs are sequential.

The timed workload is the 36,000-tick four-seat scenario above. Run order was
baseline, candidate, candidate repeat, baseline repeat. Times are milliseconds
per tick; each row averages the full run, except the final-window row.

| Metric | Baseline 1 | Candidate 1 | Baseline 2 | Candidate 2 |
| --- | ---: | ---: | ---: | ---: |
| Total mean | 0.9962 | 0.8542 | 0.9989 | 0.8123 |
| Visibility mean | 0.5088 | 0.3325 | 0.5106 | 0.3202 |
| Total p99 | 3.6526 | 3.4534 | 3.6853 | 3.3685 |
| Final 9,000 ticks total mean | 1.7269 | 1.5088 | 1.7305 | 1.4352 |
| Final 9,000 ticks visibility mean | 0.8020 | 0.5125 | 0.8044 | 0.4916 |
| Total maximum | 26.0984 | 34.1050 | 26.2183 | 27.0990 |

Total mean improved 14.3-18.7%; visibility mean improved 34.6-37.3%.
The first 9,000 ticks also improved: total 0.2056 to 0.1807 ms and 0.2063 to
0.1796 ms. Team-grid merging did not erase the benefit at low unit counts
in this workload. This is a CPU simulation measurement, not a rendered FPS
benchmark. Background development and scheduling noise remain; the candidate's
first maximum was worse. Startup AI accounts for approximately 26 ms in both
builds. No claim is made that the user's occasional long stall is eliminated,
or that this workload reproduces the saved match or a 430 ms gap.
The existing profiler periodically resets totals inside a reporting tick;
that row can omit a phase. Both builds use the same instrumentation.

Local artifacts: D:/tmp/rwe-perf-20261021, including phase CSVs, arena logs,
fixed baseline/candidate archives, observer source, verification CSVs and test
executables. The workload invocation is:

```powershell
ai_arena --map 'Full Moon' --ai-arena 1200 --seed 6202 --ai-engine rwe `
  --ai-difficulty hard --player 'A;Computer;ARM;0;1' `
  --player 'B;Computer;ARM;1;1' --player 'C;Computer;CORE;2;2' `
  --player 'D;Computer;CORE;3;2' --out <directory> --log <file> `
  --watchdog 180 --strict
```
