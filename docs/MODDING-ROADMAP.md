# RWE modding implementation roadmap

Status: proposed, 2026-11-01. This decomposes [the modding plan](MODDING-PLAN.md)
into implementation-sized work. The source comparisons and their evidence
limits remain in that plan. No runtime work below is marked implemented.

User requirement: every gameplay and presentation system is an eventual
extension target, with Lua as the approachable authoring layer. Scripted AI
stays in lockstep. Existing TA data and COB content remain usable.

## Release boundaries

| Release | Usable result | Required completion |
| --- | --- | --- |
| First mod SDK | Install and compose pre-match Lua balance mutators; preview their changes; play, save and replay the result | MOD-01 through MOD-07 |
| Runtime scripts | Abilities, missions and opt-in Lua AI; AI executes in lockstep on every peer | MOD-08 and MOD-09, including state restoration and rejoin |
| Presentation SDK | Filtered queries, panels, input, camera and effects with developer reload | MOD-10 |
| Subsystem SDK | A proven replacement-provider contract, then coverage of all listed systems | MOD-11 and the coverage inventory |

Release names and task IDs here are planning labels, not existing commands,
GitHub issue numbers or public API versions. The first SDK is useful without
keeping a Lua VM active in the battle, but is not advertised as runtime gameplay
scripting or complete engine moddability.

## Work items

### MOD-01: Settle the architecture and evidence boundary

Dependencies: none. Deliver a reviewed revision of proposed ADR-0002 that
allows early Lua and eventual mechanism replacement, while retaining accepted
ADR-0004, ADR-0005 and ADR-0006. Classify the new loader, scripting and authoring
facilities as RWE-original through the required documentation. An original
default moved behind an interface still needs its own reference finding.

Capture deterministic baseline scenarios using shipped TA data, recording the
engine build, content, launch parameters, hash logs and existing save/replay
behaviour. These are RWE regression baselines, not new original-game checks.

Done when: the ADR conflict is resolved explicitly, evidence needed for the
first code changes is available, and the unmodded baseline is reproducible.

Progress 2026-11-02: `MODDING-SDK-FINDINGS.md` MOD-SDK-001 records the
RWE-original boundary for the authoring foundation, inheriting MOD-ARCHIVE-01
and PACKAGE-001 rather than re-deriving them. The first slice therefore has a
documented evidence status. ADR-0002 now carries a reviewed revision that
keeps its status `proposed`, accepts early Lua for pre-match transforms and
declines to authorize mechanism replacement yet. Still open: newly captured
same-seed baselines for the modding work. No gameplay or
default behaviour changed.

### MOD-02: Specify the package and resolved match

Dependencies: MOD-01. Draft a versioned author manifest, compatibility rules
and canonical resolved-match encoding. Prefer extending existing JSON metadata
over introducing another parser just for the first SDK; reconcile this with
ADR-0002's proposed archive manifest before implementation.

Represent package identity/version, kind, dependency constraints, required API,
entry script, supported options, content files and explicit ordering. Support
legacy archive-only content through an implicit descriptor. Include relevant
loose files and manifest/script bytes in verification; existing archive hashes
alone are insufficient. Give simulation and presentation identities separate
roles. Do not let a presentation label exempt files that influence gameplay.

Done when: hand-worked fixtures resolve identically, missing dependencies and
conflicts have specific errors, and unknown required features refuse loading.

Progress 2026-11-02: the format-1 author manifest, dependency/ordering
resolver, keyed-exclusive composition, option resolution and canonical
resolved-set identity are implemented in `src/rwe/setup/ModManifest.{h,cpp}`
with `src/rwe/setup/ModManifest.test.cpp`, and `rwe_setup --resolve-mods`
is the offline dry run. The rules and the RWE-original evidence boundary are
recorded in `MODDING-SDK-FINDINGS.md` MOD-SDK-002 and MOD-SDK-003. Still open
for this item: an archive-embedded manifest or TDF adapter (the ADR-0002 D4
reconciliation), content hashing of entry scripts and loose files, and
launcher consumption of a resolved set.

Progress 2026-11-04 (#978, local branch, not yet compiled): entry scripts are
part of the identity. Each simulation package's `entrySha256` is in the
format-2 resolved set, every simulation script reader refuses bytes that no
longer match, and the launcher fingerprint compares the script. Presentation
scripts stay out, as MOD-SDK-013 decided. Recorded in MOD-SDK-014. Still open:
loose content files beyond the entry, and an engine build and test run.

### MOD-03: Reuse installation and canonical content resolution

Dependencies: MOD-02. Extend the existing package-library/launcher path rather
than building a second registry or installer. Keep source files unchanged,
stage changes transactionally and retain installed versions needed by saves.
The engine's content resolver is authoritative for the effective asset stack;
the launcher consumes its result rather than inventing another precedence law.

Record the base content, ordered packages, resource-directory mappings and
the effective definition identity. Add field/file provenance to inspection.

Done when: a real data mod and an empty mutator package load together, paths
and names cannot change content identity, and failed activation preserves the
last working profile. Existing archive lookup behaviour remains verified.

Progress 2026-11-02: the engine's own VFS (`constructVfs`) is the authority
for the preview path, and the launch path applies resolved content to the
parsed FBI stack before definitions are built. Stable identity excludes paths
and folder names. Still open: registry-driven activation, transactional
staging into the engine content resolver, and field/file provenance beyond the
transform journal.

### MOD-04: Choose and constrain the Lua runtime

Dependencies: MOD-01 and MOD-02. A small spike compares embedded Lua with Lua
inside WebAssembly against the same transform API. Choose one pinned runtime
configuration for the first SDK. Evaluate supported Windows and Linux builds,
deterministic results, bounded work, memory behaviour and useful diagnostics.
Record the decision before shipping a dependency or freezing script semantics.

Permit Lua source and package-local modules only. Provide ordered definition
queries, deterministic numeric operations and a separate seeded random stream
where needed. Exclude clocks, external files/network, native loading and
address-dependent behaviour. Bound host API work as well as Lua instructions.

Done when: the same fixture produces identical output on the supported builds;
infinite loops, expensive host calls and oversized allocations are bounded;
errors identify the package, script and location.

Progress 2026-11-02: Lua 5.4.7 is vendored and pinned; the sandbox, fixed hash
seed, instruction and memory budgets, sorted queries and diagnostics are
implemented and tested. Recorded in MOD-SDK-004. Still open: the embedded
versus wasm-hosted comparison was decided on availability and surface, not a
measured performance run; libm-based math and `^` are not yet pinned; and no
same-seed hash baseline has been captured.

### MOD-05: Implement the first transform surface

Dependencies: MOD-03 and MOD-04. Start with unit metal cost, energy cost and
build time. Use a disposable content representation or temporary VFS layer,
then run the ordinary validated loading path. Preserve unknown definition
keys. Define scaling arithmetic, rounding and bounds explicitly; do not import
another engine's field limits without checking RWE's documented contracts.

Allow selection by stable definition name and supported properties. Apply
mutators in resolved order, report competing writes and keep a change journal.
A failure discards the attempted result and refuses start; never quietly play
an incomplete stack. The initial API cannot edit unrelated host files or
enable arbitrary Community Patch features individually.

Done when: two balance mutators compose reproducibly, a dry run explains each
changed value, invalid results refuse start, and original content is unchanged
after both success and failure. An empty stack preserves the baseline.

Progress 2026-11-02: metal cost, energy cost and build time are transformable
by stable unit name; scripts compose in resolved order; a failed script is
atomic and refuses the launch; the preview prints every before/after value
against real shipped data. Recorded in MOD-SDK-005. Still open: the full
validated-content clone the plan mentions for later definition keys, and a
same-seed baseline proving the empty stack is byte-identical.

### MOD-06: Carry the result through launch, saves and replays

Dependencies: MOD-02, MOD-03 and MOD-05. Extend every supported launch path,
including standalone engine launches, with resolved simulation identity and
effective content identity. Multiplayer verifies both before starting. A
source/options match with a different transform result is a launch error.

Version the save/replay metadata and define the old-format migration policy.
Preserve exact immutable content needed for later loading, or regenerate only
under the recorded transform/runtime version and verify the result. A missing
package produces a repair message; do not silently substitute another version.
Report unsupported original-client/demo combinations explicitly.

Done when: two peers start only with matching content, a modified loose script
is detected, save/resume reproduces the same battle, and replaying does not
apply the mutators twice. Legacy no-mod saves/replays keep their stated policy.

Progress 2026-11-02: `GameParameters` carries the resolved launch set; saves
and replay headers record identity, order and manifest fingerprints; a
mismatch is refused with the first difference named; unresolved recorded sets
are refused rather than run unmodded. The launcher now hashes raw
`rwe_mod.json` bytes into its mod fingerprint and refuses a lobby or game
server when they differ. Recorded in MOD-SDK-006. Still open: the full
resolved-set identity on the wire, launcher re-resolution of a recorded set,
and a same-seed save/resume and replay test on real data.

Progress 2026-11-04 (local branch `mod-launch-switch`, not yet compiled):
`rwe` and `ai_arena` take `--mod <rwe_mod.json>` and `--mod-option`. With
`--replay` or `--load`, the set must match the recording, which is the
command-line half of re-resolving a recorded set. Recorded in MOD-SDK-015.
The launcher does not pass `--mod` yet.

### MOD-07: Deliver the author and player workflow

Dependencies: MOD-03 through MOD-06. Provide a template, create/validate/preview/
run/package operations, script API documentation and editor annotations. Add
a working cost/build-time example and an invalid example explaining failures.
These operations are proposed additions to existing tools, not current CLI
flags. Include the preview, selected options and conflict explanations in the
launcher Library/profile workflow.

Done when: an author can create a package, edit one Lua file, preview the exact
changes, play it and share it without compiling RWE. Verify a real shipped
unit, multiple mutators, an offline reload and a Windows/Linux network game.

Progress 2026-11-02: `--create-mod`, `--validate-mod`, `--resolve-mods` and
`--preview-mods` exist; the template and preview were exercised against the
shipped data (278 units, 270 changed). Recorded in MOD-SDK-007. Still open:
packaging to a shareable archive, launcher Library presentation, and the
Windows/Linux network acceptance run.

Progress 2026-11-04 (local, not yet compiled): "play it" now has a path that
needs no launcher work: `rwe --map <name> --mod <rwe_mod.json>` (MOD-SDK-015).
The launcher still has to pass it.

### MOD-08: Introduce runtime rules and authoritative script state

Dependencies: first SDK complete. Specify hook timing, result composition,
validated effects and namespaced persistent state before adding runtime APIs.
Use a shield/damage or mission example to exercise state, RNG, timers and
fault handling. Keep presentation and runtime rules in separate instances.

Save/hash/dump all state that determines future decisions; initially restrict
persistent data to the supported store rather than promising arbitrary VM
serialization. Preserve original update visibility when extracting defaults.

Done when: the example behaves consistently across peers, supported failures
have deterministic outcomes, and uninterrupted and saved/resumed runs agree.

Progress 2026-11-02: the authoritative state store exists with the three
scopes, per-mod namespaces and quotas, canonical JSON and a content hash, and
is wired into `GameSimulation`'s save (version 4 only when present), hash and
dump (MOD-SDK-008). The integration map is recorded from a read-only survey:
damage choke point, lifecycle events, tick dispatch, save/hash/dump
registration and section-budget hazards. The persistent runtime now exists: `hook_damage_v1` folds at the final-damage
point, `on_tick` runs after the native phases, the store and a seeded random
stream are reachable, a fault disables the package, and save/hash/dump carry
the metadata (MOD-SDK-009). Still open: the shield/mission example, lifecycle
hooks, unit queries and deferred effects; reclaim, feature and remote damage
still bypass the unit hook.

### MOD-09: Add Lua AI through the lockstep controller path

Dependencies: MOD-08. Add a per-seat script provider behind the existing
`AiController` boundary. Retain Classic as the default. Resolve script bytes,
options, API/runtime version, observation policy and seat assignment before
launch. Every lockstep peer runs the same selected scripts in deterministic
seat order. The host has no exclusive AI execution role.

Use the current simulation-tick AI pass and normal command feed. Preserve the
fixed AI buffer depth and readiness guard. Thinking or draining commands twice
because a frame retries a stalled tick is a failure. AI code cannot depend on
ping, frame rate, presentation state, machine-local budgets or wall time.

Store AI plans, timers, RNG positions, pending future actions and fault status
through the authoritative state contract. Design replay, rejoin catch-up and
the handover to live thinking together: recorded commands alone do not restore
a private Lua strategy. Do not execute recorded and regenerated commands for
the same seat/tick. Rejoin needs a verified state checkpoint/transfer or a
deterministic reconstruction that demonstrably produces the same AI future.

Done when: native/scripted mixed seats agree on two peers under delayed packets
and differing frame rates; the same AI state, RNG and future command schedule
survive save/resume and drop/rejoin; repeated runs agree across supported
Windows/Linux builds. Replay uses its recorded command stream without a second
AI producer. Initial script-AI support is for RWE lockstep and local play;
original-client own-clock integration remains a separately specified feature.

Progress 2026-11-02: the first slice exists. `AiEngine::Lua` is selectable and
round-trips through the save header; a seat's AI package is resolved from the
launch set before the controller is built, and a missing or unresolved package
refuses the launch instead of falling back to Classic. `ModAiController` runs
one persistent sandboxed Lua state per seat during the ordinary AI pass, emits
sorted `PlayerUnitCommand`s from `ai.cmd_move`/`cmd_stop`/`cmd_attack`, and
observes only its own units through `ai.my_units()`. Budgets, the fixed hash
seed, a private seeded RNG and sticky fault disabling follow MOD-08's runtime.
Recorded in MOD-SDK-010. Still open: saving and restoring AI plans, timers,
RNG position and the private Lua state; rejoin catch-up and handover; the
mixed-seat delayed-packet and cross-build acceptance runs; replay using a
recorded stream without a second producer; and assigning a specific AI package
to a specific seat. **MOD-09 is not complete.**

### MOD-10: Expose presentation safely

Dependencies: MOD-02 and the runtime boundary; may progress alongside MOD-08.
Provide filtered player-view queries, panels, picking, input, draw primitives,
camera, audio and effects. UI actions submit ordinary validated commands.
Presentation has its own settings and developer reload lifecycle.

Done when: a HUD/minimap example can reload, never exposes hidden state through
the API, and does not change simulation hashes or RNG simply by being enabled.

Progress 2026-11-02: a read-only survey fixed the presentation boundary, the
filtered-query gates, the panel/input/draw/camera/audio attachment points and
the section-budget rules. Recorded in `MODDING-PRESENTATION-SUBSYSTEM-MAP.md`.

Progress 2026-11-03: the first end-to-end slice exists. `PlayerView` is the
filtered query surface (units, sensor contacts without identity, explored
features, live-sight projectiles, own resources, static definitions);
`PresentationRuntime` is a bounded Lua sandbox with `on_frame` and input hooks;
the draw list is rendered in the scene overlay; orders are filtered twice and
become ordinary local commands; `+ReloadPresentation` rebuilds packages and
keeps the running HUD on failure; and `ModKind::Presentation` resolves. The
`examples/mod-presentation-hud` package loads through the real resolver and
is exercised by `[presentation]` tests, including a sync hash taken before
and after a drawing, reading, ordering frame. Recorded in MOD-SDK-013. Still
open for MOD-10: native `UiPanel` hosting, filtered picking, camera/minimap
controllers, the audio policy, effect spawn helpers, and a windowed acceptance
run once MOD-07 supplies a resolved-set launch path. **MOD-10 is not complete.**

Evidence 2026-11-03: `[presentation]` 27 cases (26 passed, 1 retail-data
skip); full suite 2142 cases, 2137 passed, 5 skipped, 0 failed; a 120-second
four-scenario hash-oracle record/compare identical at 3599 ticks each.

### MOD-11: Prove replacement, then expand coverage

Dependencies: MOD-08 and a chosen module execution contract. Start with a
bounded subsystem whose input/output, state and update timing can be specified.
Publish its query/effect contract, required events, state restoration and
compatibility rules. Prove a default provider and a replacement before growing
the API to movement or collision. Avoid exposing native engine memory layouts.

Done when: an independently packaged provider works through the documented
contract, the default preserves the baseline, save/replay/rejoin are supported
and real battle measurements meet the agreed budget. Then add a tracked task
for every remaining row in the plan's system inventory. Each row needs a real
example and an acceptance check before it is called moddable.

Progress 2026-11-02: the survey recommends the economy settle as the first
provider (pure value interface, fixed phase, no RNG or entity creation) and
wind as the second (per-provider RNG and state restoration), with the provider
pimpl and save/hash/dump order fixed in
`MODDING-PRESENTATION-SUBSYSTEM-MAP.md`. The economy and wind providers now exist: value-only contracts, native defaults wrapping the existing arithmetic unchanged, routed through `GameSimulation`, with provider ids saved, hashed and dumped and a mismatch refused (MOD-SDK-011/012 follow-ups). No module runtime or deliberately different provider has been benchmarked yet; the wasm runtime remains a separate spike.

## Repository integration points inspected

Planning inspection only, at RWE commit `b3af2510`:

| Existing area | Planned integration |
| --- | --- |
| `src/rwe/setup/PackageLibrary.*`, launcher mod selection | Extend package inspection and selection toward the planned Library/profile workflow |
| `launcher/src/common/mods.ts`, `archives.ts` | Retain legacy metadata; extend verification to complete resolved content |
| `src/rwe/vfs/`, `src/rwe/io/tdf/`, `GameSimulationLoader.*` | Resolve and validate transformed content before simulation construction |
| `GameParameters.h`, `SaveFile.*`, `ReplayFile.*` | Persist and verify immutable match/content identity |
| `AiController.h`, `GameSimulation::runAiControllers` | Deterministic native/scripted AI dispatch |
| `PlayerCommandApplication.*`, `GameScene_replay.cpp`, `GameScene_peers.cpp` | Fixed AI command delay, readiness gating, playback and rejoin behaviour |
| `save_util`, `GameHash_util`, `dump_util` | Authoritative runtime state and diagnostics |

This table identifies likely work locations, not permission to bypass the
original-behaviour findings or add every subsystem header to `GameSimulation.h`.
Preserve the existing section-budget discipline and keep runtime dependencies
behind private implementation boundaries.

## Tracking issues (opened 2026-11-02)

These are the follow-up issues for the work that is not in the first SDK PRs.
Close each when its PR merges.

| Issue | Scope |
| --- | --- |
| #970 | MOD-08: finish the runtime hook surface and ship a shield example |
| #971 | MOD-09: scripted AI state survives save/resume and rejoin |
| #972 | MOD-10: presentation mod SDK |
| #973 | MOD-11: subsystem provider contract and the first economy provider |
| #974 | MOD-11b: module runtime spike |
| #975 | ProTA P1/P2: content package recognition and loading |
| #976 | ProTA P3: the ProTA rules profile |
| #977 | ProTA P4: presentation, settings and compatibility run |
| #978 | Hash entry scripts and content files into the resolved identity |
| #979 | Carry the resolved-set identity on the multiplayer wire |
| #980 | TA: Escalation content and rules (docs/ESCALATION-INTEGRATION-PLAN.md) |

