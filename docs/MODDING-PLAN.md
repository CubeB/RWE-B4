# RWE modding plan

Status: proposal, 2026-10-31. Documentation only; no runtime implementation or
change to an accepted ADR is authorized by this plan.

Updated 2026-11-01: the user requires modded AI to remain in lockstep.
[The implementation roadmap](MODDING-ROADMAP.md) breaks this proposal into
reviewable work items and defines the first release.

## Goal

Make ordinary mods possible with data and readable Lua scripts, without an
engine rebuild. Make every gameplay and presentation subsystem an explicit
extension target, including complete replacement where hooks are insufficient.
Keep existing TA content usable and retain a verified default game.

The author experience is the priority: create a mod from a template, edit a
script, run a scenario, read a useful error, and share a package. Advanced
authors can supply compiled WebAssembly modules through the same contracts.
Writing a new movement solver need not become beginner-level work for changing
weapon damage to be beginner-friendly.

## Sources and current position

Design references, read 2026-10-31:

- [ozgb's original mod-support proposal, issue 1](https://github.com/ozgb/byte-tactics-moddable/issues/1):
  Lua hooks, simulation/presentation separation, data-driven modes, batched
  expensive hooks, and preservation of existing TA content.
- [The subsequent subsystem plan, issue 2](https://github.com/ozgb/byte-tactics-moddable/issues/2):
  replaceable WebAssembly simulation subsystems with Lua above them. Its
  2026-10-24 rescope moves the eventual engine and mod features into later
  clean-room milestones.
- [Lua design, issue 43](https://github.com/ozgb/byte-tactics-moddable/issues/43),
  [hook registry, issue 42](https://github.com/ozgb/byte-tactics-moddable/issues/42),
  [resolved mod sets, issue 39](https://github.com/ozgb/byte-tactics-moddable/issues/39),
  and [UI modules, issue 46](https://github.com/ozgb/byte-tactics-moddable/issues/46).
- [Repository status at revision 334ebe4c](https://github.com/ozgb/byte-tactics-moddable/blob/334ebe4c16488f87801ac896b048d74c5a3ca80e/STATUS.md):
  distinguishes the reference/oracle work from the later modding product.

These are design inputs, not evidence that all described capabilities ship.
No decompiled implementation was opened for this plan, and no implementation,
layout, or source identifier is adopted from it. Future original-behaviour
comparisons must pass through ADR-0004's separate spec room.

RWE currently has TA data/COB support, mod loading and package-management work,
native AI and pathfinding choices, and an opt-in Community Patch ruleset. A
search of the engine build and runtime did not find a Lua or WebAssembly mod
runtime. ADR-0002 is **proposed**, rather than an implemented capability.

ADR-0002 explicitly keeps mechanism native and postpones Lua. This plan
proposes changing those limits: Lua is an early author-facing feature, and
native gameplay mechanisms become replaceable incrementally. Before runtime
implementation, revise ADR-0002 through review rather than quietly treating
this document as its replacement. ADR-0006's Community Patch remains an
explicit opt-in ruleset; do not replace it with arbitrary quirk defaults.

## Architecture

### Comparison with Nanolathe and Open Annihilation

Additional documentation review, 2026-10-31: Nanolathe revision
`e05a79097ec2a7566dab8495e759b01473549a11`, Open Annihilation revision
`fb69ee3b0f025918e275a27363e488d5200bf263`. These are static documentation
checks, not executed engine or mod tests. The findings below describe the
reviewed contracts and their stated implementation status.

**Nanolathe separates content packages from compiled gameplay extensions.**
Its [mods and mutators design](https://github.com/nanolathe-gg/nanolathe/blob/e05a79097ec2a7566dab8495e759b01473549a11/docs/DESIGN_MODS_MUTATORS.md)
marks the library, drop-to-install, catalogue, in-process content switching,
save sidecar and global mutators as implemented, with listed follow-ups.
Each content mod carries `nanolathe-mod.json` for layout, limits, rules and
recommended settings. It selects one content mod with separately supported
add-ons, rather than arbitrary content-mod stacking. Its mutators transform
a per-battle content clone; they are a bounded set of engine-supported
adjustments, not general Lua programs. The content-mod design explicitly
excludes running package code other than data and COB bytecode.

Its [gameplay rules design](https://github.com/nanolathe-gg/nanolathe/blob/e05a79097ec2a7566dab8495e759b01473549a11/docs/DESIGN_GAMEPLAY_RULES.md)
documents native interfaces for combat, construction, visibility, orders,
movement, pathfinding and other policy. A build registers composed rulesets
and binds one whole set to a session. These are compiled Go extension points,
not a downloadable Lua or WebAssembly subsystem SDK.

RWE should adopt the product lessons: a visible library, per-mod settings,
reversible content switching, a complete match identity and transforms that
leave installed content untouched. Use one-time binding of chosen providers
where practical. Retain RWE's planned explicit composition of compatible
packages rather than adopting Nanolathe's single-content-mod limit.

**Open Annihilation has three distinct modding surfaces.** Its
[OAMOD standard](https://github.com/open-annihilation/open-annihilation/blob/fb69ee3b0f025918e275a27363e488d5200bf263/docs/mods/oamod-standard.md)
describes `oamod.yaml` profiles: content layout, limits, data keys, COB
extensions and named engine-supported behaviour changes. The engine validates
the resolved profile and distinguishes simulation identity from the full
profile identity. This is a declarative compatibility surface, not arbitrary
replacement of every engine system.

Its [mutator standard](https://github.com/open-annihilation/open-annihilation/blob/fb69ee3b0f025918e275a27363e488d5200bf263/docs/mutators/oamut-standard.md)
describes implemented sandboxed Lua packages that run **before the match**.
They generate a temporary content layer, modifying definitions, menus, map
settings and permitted profile parameters. The scripts do not run during the
battle. Ordered stacking, dependency checks, a dry run and a journal explain
which package changed each field. The
[multiplayer contract](https://github.com/open-annihilation/open-annihilation/blob/fb69ee3b0f025918e275a27363e488d5200bf263/docs/multiplayer/mutators.md)
compares the selected mutators and their generated result; these matches
exclude original 3.1c clients.

Its [brain standard](https://github.com/open-annihilation/open-annihilation/blob/fb69ee3b0f025918e275a27363e488d5200bf263/docs/brains/oabrain-standard.md)
describes implemented Lua AI packages that do run during play. They control
one opted-in computer seat through ordinary orders, with explicit budgets,
information access and saved data. A readable Classic example accompanies
the native default AI. Only the machine hosting that AI executes its script;
remote peers and demo playback consume the resulting unit traffic. That
networking rule is specific to OA's ownership model and must not be copied
into RWE's lockstep execution or used to omit relevant RWE mod/AI state from
verification.

RWE should therefore distinguish pre-match Lua transforms, runtime gameplay
rules, AI scripts and presentation scripts. They can share runtime machinery
but need different capabilities and lifetimes. Add OAMOD import with a
versioned support map: report unsupported semantics rather than silently
ignoring them. Do not advertise full OAMOD, OAMUT or OABrain compatibility
merely because RWE can parse their metadata or execute Lua.

### Three authoring levels

| Level | Author changes | Intended use |
| --- | --- | --- |
| Data | Definitions, assets, settings and validated profiles | Unit packs, balance, maps, existing TA mods |
| Lua | Pre-match data transforms, events, decisions, commands, scenarios, AI and presentation | Balance mutators, new gameplay rules and interface mods |
| Subsystem modules | A versioned implementation of an engine contract, optionally compiled to WebAssembly | New pathfinders, movement, projectile models or other total conversions |

Keep RWE's native host, renderer and build usable while introducing these
levels. Porting the entire engine to wasm32, building a browser host, rewriting
RWE in Rust, and adding another network model are separate projects. None is
a prerequisite for useful Lua modding.

### One contract system

Define language-independent APIs around stable object IDs, typed values,
queries, effects and documented update phases. Do not expose RWE's C++ object
layouts or unrestricted pointers to the game state. A subsystem contract must
say which state it owns, when writes become visible, what events it emits,
what its replacement must implement, and how it saves and restores state.

Resolve data-only transforms before constructing the simulation. Apply them
transactionally to a temporary content layer or compiled-content clone,
validate the result and preserve the original installed files. Record both
the selected sources/options and a canonical identity of the effective
simulation content. Expose a dry run and field provenance so an author can
answer why a unit has its current cost or build menu. Exact generated text
need not be RWE's canonical form, but every peer must agree on the effective
content. Version the transform rules and reject an unsupported result.

Use three explicit composition rules:

- Permission hooks combine by veto: any denial rejects the action.
- Value hooks transform the previous result in a declared order.
- Complete subsystem replacements have one selected owner; competing owners
  require an explicit choice or declared replacement, otherwise loading fails.

Events run in a fixed order, with re-entry controlled by the contract. Effects
are validated and applied at documented points. Do not blanket-defer existing
native updates or introduce whole-phase snapshots: either can change the
default game's ordering and same-tick observations.

The default implementation must use each public extension contract as it is
introduced. Avoid a default path with privileged access that replacement mods
cannot express. Keep native defaults initially; migrate selected policy to a
base package when the runtime and verification are ready. A total conversion
can eventually replace the base package and subsystem providers.

### Lua first, with a measured runtime choice

Ship Lua source as the normal script format. Authors should not need a
compiler, C headers, or knowledge of WebAssembly to write a rules mod.

Prototype embedded Lua and Lua hosted inside WebAssembly against the same
small API. Select one supported simulation execution model before freezing
the SDK. Measure portability, deterministic execution, save/load, memory
limits, debugging, startup and real game cost. Do not require mod authors to
choose a backend, or maintain two simulation paths in the first release.

Use a pinned Lua/runtime configuration, deterministic engine RNG and math,
ordered entity queries and table traversal, and fixed simulation call order.
Block host clocks, unrestricted filesystem/network access, native-library
loading and other nondeterministic operations. WebAssembly alone does not
solve Lua table ordering, host imports, memory allocation or state management.
The [Lua manual](https://www.lua.org/manual/5.4/manual.html#pdf-next) leaves
table enumeration order unspecified; [Wasmtime's determinism guidance](https://docs.wasmtime.dev/examples-deterministic-wasm-execution.html)
also requires explicit decisions about imports, arithmetic, growth and fuel.

Separate simulation and presentation instances. Presentation receives only
the player's permitted view and submits ordinary validated commands. It cannot
mutate the simulation, consume its RNG or inspect hidden enemy state through
an unrestricted query API.

### AI remains in lockstep

User requirement, 2026-11-01: scriptable AI keeps RWE's lockstep model. Every
peer runs each selected AI script on the same committed simulation ticks, in
the same seat order, from matching observations, script state and random
streams. The host selects the AI package in setup; it does not become the
only machine that thinks for that seat. Do not adopt OA's single-machine
brain execution or exclude AI identity/state from RWE's agreement checks.

Give AI scripts an explicit seat, observation policy and ordinary command
interface. Keep the existing Classic AI as the default and provide a readable
example AI as part of the SDK. Any additional awareness must be a declared,
agreed capability. Integrate script-produced commands with `AiController`
and RWE's existing tick-gated AI feed, preserving `aiCommandBufferDepth()`
and `onlyComputerPlayersAreNotReady()`. An attempted tick that stalls must
not think again or enqueue duplicate commands on each rendered frame.

The resolved match includes the AI package bytes, API/runtime version,
options and seat assignment. Authoritative AI memory, RNG positions, timers,
fault state and future command work need deterministic serialization and
diagnostics; the future-state hash must cover them under the new mod protocol.
Do not rely on identical visible unit state to hide different AI plans.

Preserve command-stream replay semantics: playback consumes recorded commands
without a second AI producer. Save/resume and rejoin must restore the same AI
future, not restart its strategy. A returning peer can suppress AI execution
while consuming recorded commands, so command-only catch-up does not by
itself reconstruct private script state. Define a verified state-transfer or
deterministic reconstruction path before enabling runtime AI in games that
support rejoining.

Acceptance includes mixed native/scripted AI seats, repeated same-seed runs,
different frame rates and network stalls, Windows/Linux comparisons,
save/resume and drop/rejoin. Compare script state, RNG and scheduled commands
as well as simulation hashes. This requirement does not redesign the existing
original-game own-clock mode. Scripted AI support there is deferred until its
separate ownership contract is documented; refuse that unsupported combination
explicitly in the initial release.

### Persistent state, multiplayer and failures

Provide engine-owned, namespaced state with global, player and entity scopes.
Save, hash and diagnose all authoritative mod state, including RNG positions,
timers, pending actions and fault status. Initially, require persistent Lua
state to use this supported store; rebuild script registrations on load.
Do not promise automatic serialization of arbitrary closures, coroutines or
native userdata. If a later runtime supports VM snapshots, prove that they
restore every relevant VM component before replacing this contract.

A resolved mod set records exact simulation content, dependency versions,
ordering, selected subsystem providers, API/runtime versions and settings.
Every entry path, including direct engine launches, must agree on its identity
before starting multiplayer. Saves and replays record it and refuse an
incompatible set with a useful explanation. Local presentation mods can differ
when their capabilities and the game's policy permit it.

Use deterministic instruction budgets for simulation faults; wall-clock
profiling is diagnostic only. Bound memory and loading as well as execution.
Specify whether a hook failure discards its pending writes/effects and disables
the optional mod, or ends the match. A fault in a required ruleset or subsystem
must end the match consistently rather than silently substitute stock rules.
Host resource exhaustion cannot be assumed to occur identically on every peer;
reserve resources at load where practical and define a coordinated failure path.

## System coverage

Maintain a tracked inventory with status, contract, owner and a working example
for each row. A row is complete only when a mod can change it through the
documented API and the engine verifies that use.

| Area | Initial Lua/data surface | Eventual deeper replacement |
| --- | --- | --- |
| Definitions and assets | Custom unit/weapon keys, build menus, resource mappings | Versioned definition transforms and loaders for additional mod assets |
| Weapons and combat | Damage, armour, shields, targeting rules, projectile lifecycle | Target selection, projectile integration and collision response |
| Economy and construction | Income, costs, build permission, repair/reclaim rates | Economy settlement and construction models |
| Units and orders | Lifecycle events, abilities, order registration and validation | Mission/order execution, locomotion and transport models |
| Movement and terrain | Movement modes, placement rules, path costs | Pathfinding, steering, terrain interaction and collision systems |
| Sensors | Vision/radar rules and sharing settings | Visibility, detection and jamming providers |
| AI | Existing profile data, decision hooks and command APIs | A complete AI provider per seat, with explicit information access |
| Missions and game rules | Start units, scripted events, teams, victory and defeat | Scenario/ruleset providers and declared simulation-phase extensions |
| Unit animation | Existing COB plus a documented script bridge | Optional Lua unit behaviours/animation providers alongside COB |
| UI and controls | Panels, selection tools, tooltips, input and draw primitives | Replaceable HUD, minimap, megamap and camera controllers |
| Graphics and audio | Effects, materials, shaders, sound/music policy | Supported render/effect passes and audio policy providers |

Network message extensions, save-state registration and phase scheduling are
also explicit contracts. The host still validates commands, manages transport,
frames saves and enforces runtime boundaries. Moddability does not grant a mod
permission to bypass those guarantees.

## Delivery order and acceptance checks

1. **Agree the architecture and establish baselines.** Review ADR-0002's
   proposed changes; inventory extension targets; capture same-seed RWE hashes,
   saves and performance scenarios. Identify which native choices already have
   usable interfaces. No gameplay change is needed for this milestone.
2. **Build the authoring foundation.** Specify one canonical manifest and
   resolved-set format, validate dependencies and conflicts, preserve unknown
   definition keys, and add create/validate/run/package developer commands.
   Integrate existing package/library work rather than introducing a parallel
   installer. Reconcile ADR-0002's archive manifest and the existing launcher
   metadata explicitly. Pass: a legacy data mod and a new empty script mod
   both load with reproducible identities.
3. **Deliver pre-match Lua mutators.** Complete the runtime spike and sandbox;
   let scripts adjust definitions through transactional transforms. Include
   a cost/build-speed example, a dry run, field provenance and ordered
   composition. Include saves, replays and multiplayer immediately. Pass:
   Windows and Linux resolve identical effective content, disagreeing results
   refuse launch, and installed content is unchanged after either success or
   failure. No mutator script needs to remain active during the battle.
4. **Deliver runtime gameplay and AI scripting.** Add the hook dispatcher,
   supported persistent state, fault handling, game rules, economy/construction
   hooks, custom orders, abilities, AI decisions and the COB bridge. Ship a
   shield/damage or scenario mod and a small opt-in AI. Pass: composed examples
   produce matching lockstep hashes; save/resume matches uninterrupted play;
   drop/rejoin restores the same AI future; stalls do not cause extra AI work;
   conflicts are explained; and existing TA data/COB content continues to work.
5. **Deliver presentation modding.** Add filtered queries, panels, input,
   draw lists, camera, effects and audio APIs. Support developer hot reload for
   presentation; simulation edits restart a reproducible scenario initially.
   Pass: a HUD/minimap mod cannot reveal hidden state or change a sync hash
   merely by drawing, and its actions use the ordinary command path.
6. **Prove subsystem replacement.** Introduce one small, bounded subsystem
   provider before attempting movement or collision. Require default and
   replacement implementations to emit the same required hooks. Pass: replace
   the provider, save/load it, diagnose its state and benchmark the real game;
   the default provider preserves the pre-extraction behaviour.
7. **Expand to full coverage and stabilize the SDK.** Work through the system
   inventory, including pathfinding, sensors, movement and projectiles. Batch
   expensive calls and register selective hooks by definition. Publish examples,
   API versions, deprecation rules, profiling and a total-conversion template.
   Pass: every claimed extension target has a demonstrable replacement or
   customization, rather than an undocumented promise to add a hook later.

The first useful release is milestones 1-3, accompanied by a small Lua
transform SDK, a template, a working example and multiplayer/save/replay checks.
Milestone 4 is the first runtime gameplay scripting release. Complete subsystem
coverage follows in later releases; it must not hold either usable scripting
release hostage.

## Verification and compatibility boundaries

Default behaviour migrations need two separate forms of evidence: comparison
with the documented original result under ADR-0004, and regression checks that
the RWE extraction preserves its existing behaviour. Byte-identical RWE hash
logs establish the latter, not original parity. Use shipped TA data and executed
original-game checks or suitable demo oracles where available; do not treat TA
demos as an original lockstep command stream.

Record new modding infrastructure as RWE-original, and record deliberate
departures from decoded default behaviour in the existing divergence register.
Preserve existing replay/save support under a stated migration policy. Mods
requiring new synchronized rules cannot be assumed compatible with an
unmodified TotalA.exe peer or representable in a TA demo; reject unsupported
combinations explicitly. Performance acceptance comes from RWE's real scenarios
on supported builds, not another project's microbenchmark figures.
