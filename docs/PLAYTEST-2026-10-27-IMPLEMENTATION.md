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

## #899: the normal-play script-effect emission gate

Authority: [WATER-VIS-003A/003B](PLAYTEST-UNIT-SFX-VISIBILITY-FINDINGS.md),
static comparison with byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73` and the matching GOG v3.1
`TotalA.exe`. The implementation room read only that prose. No original-game
session or demo oracle was executed.

The original admits a script `emit-sfx` request through the whole-unit display
predicate (`0x465AC0`) at the dispatcher (`0x480EB0`), before it prepares any
effect: a hidden unit's request is refused as a unit. RWE's event consumer only
asked whether the unit existed, and each effect helper asked whether the unit's
**origin cell** was lit. For a wholly submerged enemy extractor on watched
ground, the seabed is visible while the model is not, so its 259 bubbles leaked
its position.

`processSimEvents` now applies `unitIsVisibleToLocalPlayer` once, at the
`EmitParticleFromPieceEvent` consumer, before the effect switch. That is the
same predicate the model draw uses -- ownership, cloak, the strictly-below-sea
model top with the sonar-contact veto, then the fog corners -- so bubbles stop
exactly where the model stops. The five per-helper terrain-point checks were
removed; their call sites exist only through this dispatcher. COB execution,
the deterministic event stream and hashed state are untouched, and no
source-unit purge was added: a dot admitted before visibility changed still
expires on its own tick or on the sea-bed test, as WATER-VIS-003B establishes.
Unsupported effect types still throw, as before; visibility alone grants them
nothing.

### Executed RWE checks

Two new scenarios run the real `GameScene` on shipped Coast To Coast (sea level
85), seed 7, with one local human and one computer seat:

- `899-hidden-extractor-bubbles` places an enemy `ARMUWMEX` on the seabed
  beside a local `ARMCS` (sight 224, sonar 0), so the seabed is lit by the
  player's own sight while the model stays hidden with no sonar contact. It
  asserts both facts, then that **no** reverse-ramp wake dot (the bubble marker,
  `0x474A85`) exists at any 8-tick sample through tick 120.
- `899-owned-extractor-bubbles` is the positive control: the player's own
  `ARMUWMEX` alone must produce bubble dots. This proves the shipped script
  really does emit 259 in this fixture -- `ARMUWMEX` is `ActivateWhenBuilt=1`
  and its `Activate` throws three `256|3` requests then sleeps 250 ticks, so
  the fixture activates its spawned copy through the simulation's ordinary
  activation path.

Before the gate was restored, the hidden scenario **failed at tick 8 with
bubbles present**, reproducing the report; with the gate it passes. All 8
scenarios passed on `D:/RWE-Data` (the 6 existing ones included). The full suite
with `RWE_SCENARIO_DATA=D:/RWE-Data` passed **1980 cases with 1 skipped
HOME-path checksum case and 218531 assertions**; Release `rwe`, `rwe_test` and
`scenario` built successfully.

Remaining uncertainty: this is a static reference comparison plus executed RWE
scenarios, not a native visual capture or a pixel check of the original. The
scenarios count presentation dots and do not attribute dots to a source; the
negative is meaningful because the owned twin shows the same script emitting in
the same harness. Allied visibility is not separately executed here: it comes
through the shared sensor picture that the #899/#904 watcher work and its
retail predicate test already cover.

## #897: a clickable accept in the battleroom

The owner's report was a UX complaint about sharing a code and the host's
`/accept`. The transport itself is RWE-original (there is no byte-tactics
counterpart to compare), so the investigation was an executed one: the
simulated-internet harness (`ui_probe --multiplayer-flow`) passed every
invite, reply-code, approval, expiry, version and reachability check on the
merged revision. Nothing in the two-code path was broken.

The friction it left was that admitting a waiting player required knowing and
typing a command. The battleroom now has an **Accept** button beside the
message box, host-only: hidden unless a request is waiting, labelled with the
oldest requester's name, and running the same admission as `/accept` through
one shared helper so the two cannot drift. The typed command is unchanged.
This is an RWE-original control, recorded in §88; the approval model and the
LAN path are untouched.

### Executed RWE checks

- `ui_probe --multiplayer-flow D:/RWE-Data`: the waiting request surfaces the
  button; its label names the request ("Accept Net"); a real click admits the
  oldest pending guest; the button disappears once the queue is empty; the
  by-name command, the empty-queue message, the reply-code fallback and the
  version refusal still pass. `flow done: 0 check(s) failed`.
- The harness drives two real `MainMenuScene`s over LAN discovery with a fake
  STUN server, so this is an executed menu-flow check, not a unit test of the
  handler. It does not emulate two real routers; the direct path is a
  loopback stand-in.
- Release `ui_probe` built successfully; the game targets are unaffected by
  this menu-only change.


## #903: the battleroom message-box baseline

Authority: [BT-GUI-TEXTBOX-BASELINE-FINDINGS](BT-GUI-TEXTBOX-BASELINE-FINDINGS.md),
static comparison at byte-tactics `e89721c46c3fa06e59558419b54fe26061fafb73`
and the identified GOG v3.1 binary. That finding is a static read: it ran no
original game and no RWE render.

A type-3 text input puts its text's pen at **gadget Y + 3**, independent of the
box's height, and the original's font load normalises every glyph hotspot by the
`I` frame's height. RWE's drawer takes a baseline, so it needs the pen plus that
height: for `hattfont12` (I = 12) the row is **Y + 15**. RWE used Y + 12, which
is three rows high at every box height; the LOUNGE2 MESSAGE box (21, 441,
294 x 20) drew its capital ink on rows 442-453 instead of 445-456. The caret is
now the pen through two rows past the `I` bottom, both ends painted (rows
`Y+3 … Y+17`, fifteen rows), replacing a twelve-row line anchored at the old
baseline.

The hitbox, focus, horizontal trimming, plate choice and font are unchanged;
labels already matched and are untouched; the OUTPUT list's 12-row pitch against
the reference's 15 is a separate mismatch and is not changed here.

### Executed RWE checks

- New `[ui]` case pins the arithmetic for the shipped face: a 20-high box at
  y 441 with an I-height of 12 gives drawer row 456 and caret rows 444-458; a
  28-high box gives the same row; a face with I-height 10 gives 454 and a
  thirteen-row caret. The test uses constructed `SpriteSeries` faces, not the
  shipped GAF.
- `[ui]`: 31 cases, 1173 assertions passed. Full suite: **1981 passed, one
  HOME-path checksum skip, 218536 assertions passed**.
- No render capture was made. The production path calls the tested helpers,
  but the visual result is arithmetic plus a clean build, not an observed
  comparison; a `ui_probe --panel LOUNGE2 --render` capture remains the
  outstanding visual check.

## #906: command masks, the developer phrase and the repeat key

Authority: [BT-GAME-COMMAND-MASK-GATING-FINDINGS](BT-GAME-COMMAND-MASK-GATING-FINDINGS.md),
static comparison at byte-tactics `e89721c46c3fa06e59558419b54fe26061fafb73`
and the identified GOG v3.1 binary.

`chatCommandMask` now computes the original's per-line mask: 7 once the local
debug bit is set (the original *replaces* the mask with 7, so debug also grants
the cheat bit), otherwise 1 in a campaign and 3 in a skirmish; multiplayer uses
the host's Cheat Codes option, which RWE has no equivalent of, so it stays 1.
`+Now Film Chris Include Reload Assert` is parsed exactly -- command word
case-insensitive, five arguments case-sensitive, six tokens -- and any other
`Now` clears the unlock. The repeat key is `SDLK_BACKSLASH` (virtual-key `0x5C`
on a US layout), active only while debug is unlocked and the message bar is
open; it reruns the saved `+` line, which is capped at 79 bytes and
re-tokenised, as the original does. The debug bit and the saved line are
presentation: never hashed, saved, replayed or replicated.

Only the eight sharing commands exist, in the ordinary table. The cheat and
debug tables and the mask-4 default handler are not ported -- the last is a
unit spawner and debug-script loader in the original, not an error path -- so
the raised mask bits currently enable only the repeat. RWE keeps its
`Unknown command: <word>` message and its no-echo chat rule; both are recorded
in §88.

### Executed RWE checks

- New scenario `906-developer-phrase-and-repeat` drives the real `GameScene`:
  the exact phrase unlocks debug; an unknown `+` line prints once; backslash
  while the bar is open prints the same line again; a bare `+Now` clears the
  unlock; backslash then does nothing. It passes on `D:/RWE-Data`.
- All 9 scenarios pass. `[chat]`: 16 cases, 88 assertions. Full suite:
  **1983 passed, one HOME-path checksum skip, 218556 assertions**.
- The mask's cheat bit has no handler to exercise, so its end-to-end effect is
  not demonstrated; the pure mask cases and the repeat path are. No original
  runtime was run, and the key is the code's virtual-key number, not a
  physical-key test on other layouts.

## #907: `+NoShake`, `+Gamma` and `+ScrollSpeed`

Authority: `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 4 and
[BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS](BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS.md)
sections 2.2 and 2.3. The remaining display and option commands need further
mapping decisions, so this pass is three bindings, not the whole ticket.

- **`+NoShake`** parses (argument ignored) and toggles `screenShakeEnabled`,
  which starts on and is not saved. While it is off,
  `addScreenShakeFromWeapon` returns before accumulating anything, so no
  offset is ever applied and nothing has to be taken back off the camera. The
  command prints nothing, as the original's option bit does not.
- **`+Gamma n`** maps the original's brightness `n x 0.1` to RWE's percentage
  field (`n x 10`), so `+Gamma 12` is 120% as the console is in the original,
  not the menu's 100% for the same raw 12. The original does not clamp; RWE's
  unsigned field floors at zero. Saved, silently.
- **`+ScrollSpeed n`** converts the original's pixels per simulation tick
  (menu 1..65, default 32) to RWE's percentage of 1000 world units/s
  (`n x 100 / 32`), so 32 is the base speed. Clamped only to the original's
  byte, then saved, silently.

### Executed RWE checks

- The `906-developer-phrase-and-repeat` scenario gained steps: `+noshake`
  turns shake off and `+NOSHAKE` turns it back on; `+gamma 12` reads back 120;
  `+scrollspeed 32` reads back 100, all through the real `GameScene`. They
  pass. The blast path itself is not exercised.
- `[chat]` parses all three commands with and without arguments. Full suite:
  **1985 passed, one HOME-path checksum skip, 218563 assertions**.
- Note: repeated full-suite runs on this machine produced intermittent
  `TaHostSession` failures that do not reproduce in isolation (`[net][ta]`
  passes); they are timing/port flakiness under load, not this change.

## #908: `+MusicMode` and `+Sing` (only the words RWE can honour)

Authority: [BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS](BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS.md)
sections 1.4 and 1.5, plus the owner's rule that no command is added without a
real RWE effect.

`+MusicMode n` maps the original's situation category onto RWE's two moods and
its silence: 0 clears silence and selects Building, 1 selects Battle, 4 stops
the music and blocks the chooser until another category is asked for. 2 and 3
(Victory and Defeat) have no RWE state, so the command prints a notice and
changes nothing rather than faking a category. Not saved, as the original does
not save it.

`+Sing` toggles the speech-waveform substitution: after the existing speech
gates and only when the unit's own voice would have played, the sound becomes
`sounds/sing.wav`, or `sounds/honk.wav` during the first second of each
eight-second block. Local and not saved.

**Deliberately not added**: `CDPlay`, `CDStop` and `Sound3D`. RWE has no CD, no
track table and no positional mixer; the finding records why each word has no
honest counterpart (section 4.2), and the owner's rule is that an inert word is
worse than an unknown one. They remain unknown commands.

### Executed RWE checks

- The `906-developer-phrase-and-repeat` scenario forces Battle, silences,
  returns to Building, and toggles Sing on and off, reading the scene state
  back; it passes. The music player and the speech sound itself are not
  captured.
- `[chat]` parses both commands, including `+musicmode`'s default 0. Full
  suite: **1986 passed, one HOME-path checksum skip, 218568 assertions**.

## #907: the render toggles RWE actually has

Authority: [BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS](BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS.md)
section 2.8, read against RWE's settings fields.

- **`+AntiAlias`** toggles `antiAliasEnabled` and saves; the original's visual
  bit 1.
- **`+Shadow`** toggles the master `shadowsEnabled` and saves; visual bit 2.
- **`+Shading`** toggles the shaded renderer between Off and Buildings-only.
  RWE's menu widens the original's one bit to four stages; the command keeps
  the original's two states rather than inventing a cycle. Saved; visual bit 5.

All three are silent and local, as in the original. The words whose RWE
counterpart does not exist are not registered: `TShadow` needs a separate
feature-shadow pass RWE lacks, `FShadow` a feature-shadow field, `Dither` a
dithered fog, `Light` a commandable light vector, `Contour` a contour overlay,
`LOSType` a second LOS algorithm, and `RCache` has no cache to flush.

### Executed RWE checks

- The scenario toggles each through the real `GameScene` and reads the field
  back; it passes. Rendering itself is not captured.
- `[chat]` parses all three, with and without a stray argument. Full suite:
  **1987 passed, one HOME-path checksum skip, 218571 assertions**.
