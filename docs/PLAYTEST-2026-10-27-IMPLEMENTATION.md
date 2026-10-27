# Playtest implementation and validation, 2026-10-27

This records work against the [ranked queue](PLAYTEST-2026-10-26-PRIORITIES.md).
Reference checks and RWE execution are separate evidence classes. No original-game
session or demo oracle was executed in this implementation pass.

## #895: commander border exclusion

Authority: [BOUNDARY-001/002](PLAYTEST-MAP-BOUNDARY-FINDINGS.md), byte-tactics
revision `e89721c46c3fa06e59558419b54fe26061fafb73`, matching GOG v3.1 build.
The implementation room read only that prose and RWE sources.

The missing rule was map preparation. RWE already rejects excluded cells in
movement-class grids and direct footprint collision, but initialized only lava
exclusions. Border preparation now runs after resolved initial features and
before the movement-class caches. It preserves real feature origins, marks
continuations, and uses the height-dependent top and preceding-row bottom scans.
Raw TNT void markers survive import rather than disappearing.

The persistent mask retains its historical `lavaExclusions` name and saved key;
it now represents lava, reserved border and raw void blockers. Existing hashing,
saving, diagnostic dumps and feature placement consume it. Loading an older
save restores its saved mask, including an empty pre-correction mask; this does
not silently reconstruct historical feature exemptions. The correction applies
to newly loaded maps and new saved masks. Command replay format is 7 because
new ground movement outcomes differ; version 6 command recordings are refused.

### Executed RWE reproduction

Data: `D:/RWE-Data/Cometctr.ufo`, `Maps/Comet Catcher.tnt`, 384 by 480 terrain
samples, sea level 0, TNT SHA-256
`c146eadda7e27b7a9f2146cc8e41de22576ab03d4bcb25112f1abc8245cec099`.
The centre of the sampled map edges has height 55. ARMCOM's effective footprint
is 2 by 2 (TANKDS2); shipped `rev31.gp3` movement values are velocity 1.2,
acceleration 0.15, braking 0.3 and turn rate 1044.

One human ARM seat, schema 0, fixed start, seed 7. At tick 30, order its commander
from approximately (-2560, 55, 3024) toward (0, 55, 3776). Inspect tick 5399 of a
5400-tick recording. The rendered lower cutoff is Z=3712; the proper constraint
is the projected exclusion cells and footprint, not a clamp at that coordinate.

| Backend | Before correction: final X/Z | After correction: final X/Z |
| --- | --- | --- |
| RWE A* | -6.1541 / 3775.9990 | -3.2856 / 3717.4741 |
| OpenAnnihilation | -5.7421 / 3770.2583 | -2510.0476 / 3035.8906 |

Both baseline routes enter the reserved lower border. The corrected load has
3535 excluded cells and neither final footprint intersects it. RWE A* approaches
the closest valid border cells. OA makes little progress toward the newly
unreachable destination; that is a distinct pathfinding limitation, not evidence
of a valid border crossing. Both retain the unreachable move for retry.

Control destination (0, 55, 3600) remains reachable: final X/Z are
-5.39 / 3600.00 (RWE A*) and -5.81 / 3594.19 (OA), within ordinary arrival
tolerance. No original-game route was executed.

A real two-peer lockstep run on Comet Catcher agreed on **all 608 compared ticks**
(0 through 607). The peers used the corrected executable and the same shipped
archive directory. This is a same-machine RWE network check, not a WAN test or
cross-compiler determinism proof.

### Remaining boundary work

Aircraft excursions beyond terrain are permitted in the original. No global air
clamp was added. BOUNDARY-003 identifies order-specific recovery absent from RWE;
the exact aircraft route from the report is still unknown. #895 remains open
for that part; do not claim that the commander correction completes every air
handler. The new ground-cell mask also affects ordinary naval ground-mode
movement through the shared collision rules; a naval original-runtime check has
not been run.

### Build and test results

- MinGW64 Release `rwe` and `rwe_test` built successfully. The baseline also
  rebuilt `scenario` and `ui_probe`; neither is presented as a new-border
  scenario run.
- New `[mapboundary]` regressions: **4 cases, 2917 assertions passed**.
- Full suite: **1977 cases passed, 3 skipped, 215226 assertions passed**.
  The three skips require retail data or a HOME-based installation location.
- Reran `[retail]` with `RWE_SCENARIO_DATA=D:/RWE-Data`: **2 cases, 3289
  assertions passed**. The HOME-path-dependent map checksum case remains
  skipped. There were no test failures in the final runs.
- The first regression run exposed a missing tile declaration in the synthetic
  TNT fixture and an invalid assumption that an unreachable order must advance.
  The corrected fixture declares its tile and verifies a reachable interior
  control before checking border refusal. Those initial failures are not counted
  as passing validation.
- Whitespace checks passed. GitHub CI results are reported by the PR; local
  checks do not imply completion of every platform job.

## #899 / #904: underwater models in watching views

Authority: [WATER-VIS-001/002](PLAYTEST-UNDERWATER-VISIBILITY-FINDINGS.md),
the same identified byte-tactics revision and GOG build. This is a static
reference comparison; no original-game watch session was executed.

The owner clarified that the reported extractors were enemy units and were
also invisible while spectating. An ordinary enemy's submerged model without
sonar is not itself proof of a bug: the original requires the applicable
detection. A native watcher receives sonar contacts for live units before
jammers run.

RWE's full-visibility view admitted the model but then cut its underwater
fragments using the nominal seat's ownership and sonar. Its fogged watching
view combined sensor sets that omit each player's own units, which also loses
underwater units that nobody else detects. The model admission and water pass
now use the same displayed view. Watching sonar grants are presentation-only;
foreign sonar jamming still erases them. The explicit `See everything` choice
also reveals underwater geometry, including through jamming, consistently with
its existing whole-model/cloak policy. That stronger policy and the combined
fog choice are recorded as deliberate viewing divergences in §88.

A fully lit ordinary-player map now retains current sonar contacts and still
uses the underwater model-admission test. Turning fog off no longer discards
detection information or substitutes terrain visibility for sonar eligibility.
Normal players do not receive the watcher grant.

### Validation

- MinGW64 Release game and tests built successfully.
- Presentation/retail selection: **18 cases, 3419 assertions passed**.
  The new controlled-depth case loads actual FBI and 3DO data for `ARMUWMEX`,
  `CORUWMEX`, `ARMSUB` and `ARMTIDE` from `D:/RWE-Data`. It checks ordinary
  owner/nonowner admission, the partly submerged tidal generator, watcher
  admission/tint selection, sonar jamming and unchanged simulation hash/sensors.
- Full suite with `RWE_SCENARIO_DATA=D:/RWE-Data`: **1980 cases passed,
  1 HOME-path-dependent checksum case skipped, 218542 assertions passed**.
- Production view/cache and renderer call sites were reviewed. These are
  executed RWE model/predicate tests, not a native visual capture or proof of
  pixel-identical original rendering.

#904's watcher fault is corrected. The owner subsequently clarified #899's
ordinary-player fault: **bubbles were present from a hidden enemy extractor**,
rather than expecting that extractor's model to be revealed. The existing
effect consumer checks existence and the bubble helper checks terrain-point
visibility, which is insufficient for whole-unit admission. The separate
[WATER-VIS-003 finding](PLAYTEST-UNIT-SFX-VISIBILITY-FINDINGS.md) confirms the
required emission gate and distinguishes new bubbles from legitimate residual
dots. That normal-play emission fix is **not implemented in this pass** and
remains the next confirmed defect under #899.

## #898: water build feedback and queued boxes

Authority: [BUILD-GRID-001/002/003](PLAYTEST-BUILD-GRID-FINDINGS.md), static
comparison with the identified byte-tactics revision and GOG build. An accepted
click carries preview height; queued outlines and their ten-tick sweeps retain
the order's stored Y. RWE instead carried the seabed height and resampled it
again in queued outlines and sweeps.

The click now uses the existing placement-height query. Immediate feedback and
queued outlines retain that position, and the sweep receives the same stored
height. No submarine construction-height or placement-legality formula changed.

### Executed RWE checks

- New `898-water-build-height` scenario loads shipped Coast To Coast, seed 7,
  and ARMTIDE through the real `GameScene`. It prepares explored terrain and a
  legal distant water site, then drives the placement click. These fixture
  setup operations are RWE test machinery, not original-game behavior claims.
- After correcting the fixture's initial unexplored-terrain refusal, the
  pre-fix scenario **failed on immediate feedback height**. The fixed scenario
  passes both the feedback check and the real queued command's position/height.
  Coast To Coast sea level is 85; the shipped ARM tidal draft is 8, so the
  accepted water-only placement plane is 77, rather than the seabed sample.
- All **6 scenarios passed** on the shipped archive directory, including the
  new placement regression, tabs, selection keybindings, camera tracking,
  own-clock orders and paused placement/selection.
- Building/presentation/retail/replay selection: **34 cases, 3697 assertions
  passed**. Release game, unit-test and scenario targets built successfully.
- The renderer's queued outline and sweep call sites were reviewed to use
  stored order/feedback height. The headless scenario checks actual input and
  command transitions; it does not supply a pixel capture. No original-runtime
  placement check was executed.

## Wrap-up state

The owner requested wrapping up before starting further implementation.
Commander border exclusions and watching visibility are merged; #898 is the
final correction in this pass. #895 remains open for order-specific aircraft
recovery. #899 remains open for the normal-play bubble emission gate above.
The rest of the ranked queue remains open. Linux Debug, launcher and class
checks for PRs #918 and #919 subsequently passed; final-PR CI is independent.
