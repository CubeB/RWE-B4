# Modding SDK findings

Working register for the modding plan (`MODDING-PLAN.md`) and roadmap
(`MODDING-ROADMAP.md`). Entries here are the implementation room's citation for
new RWE code under ADR-0004 and the standing verification instruction in
`AGENTS.md`.

## MOD-SDK-001: reference boundary for the authoring foundation

Status: recorded 2026-11-02, before the first code change (MOD-02).

The manifest parser, dependency resolver, canonical resolved-set encoding and
its identity are **RWE-original**. They have no counterpart in `TotalA.exe`:
the original has no author manifest, no dependency resolution, no load-order
composition and no resolved-content identity, because a TA mod is archives
placed in a data directory and the engine registers them in a fixed extension
order (`src/rwe/vfs/CompositeVirtualFileSystem.cpp`, `CONTEXT.md` **Archive**).

The evidence boundary is inherited, not re-derived:

- `TA-MOD-COMPATIBILITY-FINDINGS.md` MOD-ARCHIVE-01 records the static
  byte-tactics comparison at revision
  `e89721c46c3fa06e59558419b54fe26061fafb73`, GOG/v3.1 `TotalA.exe` MD5
  `8e74a1dffa1f5988624c52048f5b20cd`. It establishes original archive
  discovery and registration. It does not establish a manifest, a package
  registry, a resolver or a composed mod set.
- `PACKAGE-LIBRARY-FINDINGS.md` PACKAGE-001 carries the same boundary for the
  existing package library, and PACKAGE-002 through PACKAGE-009 define its
  RWE-original registry, inspection and path policy.
- `MODDING-PLAN.md` and `MODDING-ROADMAP.md` are design inputs. They state
  explicitly that their source comparisons are not evidence that another
  project's capabilities ship, and that MOD-02's format must be reconciled
  with ADR-0002 before implementation.

No decompilation was opened for this work. No original runtime was executed.
No original-game parity is claimed, and none of this changes simulation
behaviour. The default game, its hashes, saves and replays are untouched by
MOD-SDK-002 and MOD-SDK-003.

Remaining uncertainty: ADR-0002 D4 proposes an archive-embedded `modinfo.tdf`.
The first SDK reads `rwe_mod.json`, the existing JSON metadata, as MOD-02
prefers. MOD-SDK-002 records how the two reconcile; the archive-embedded
adapter is not implemented here.

## MOD-SDK-002: author manifest, format version 1

Status: implemented 2026-11-02 in `src/rwe/setup/ModManifest.{h,cpp}` and
`src/rwe/setup/ModManifest.test.cpp`.

The author manifest is the existing `rwe_mod.json`, extended rather than
replaced. A manifest with no `manifestVersion` is a **legacy descriptor**: the
display metadata PACKAGE-003 already recognises. It carries no resolution
semantics, and the caller supplies an implicit stable id derived from content
(MOD-02's legacy archive-only path; content derivation belongs to MOD-03).

Format version 1 adds:

| Field | Meaning |
| --- | --- |
| `manifestVersion` | `1`. Absent means a legacy descriptor. |
| `id` | Stable identity, `[a-z0-9][a-z0-9._-]*`, at most 128 characters. Not a display name. |
| `version` | `major.minor.patch`. Pre-release and build metadata are refused in format 1 rather than half-understood. |
| `kind` | `mutator`, `content`, `ai`, `maps`, `presentation`, `base` or `assets`. |
| `api` | The authoring API the package requires. Format 1 accepts exactly API 1. |
| `entry` | Normalized relative path to the package's script. Required for `mutator`; `.lua` in format 1. |
| `requires` | Array of `{ "id", "version" }`. The constraint defaults to any version. |
| `after` / `before` | Arrays of package ids constraining load order. Unknown ids are errors, not ignored. |
| `features` | Required API features, `name@version`, such as `transform.definitions@1`. A feature the environment does not support refuses the set. |
| `exclusive` | Keyed-exclusive keys this package claims, such as `hook.damage.v1`. |
| `replaces` | `"<packageId>:<key>"` entries. Each must name a key the package itself claims as exclusive. |
| `options` | Player-selectable settings: `key`, `type` (`number`, `boolean`, `string`), `default`, optional `min`/`max` for numbers and `choices` for strings. |

Validation is strict and naming is specific. A missing dependency, an
unsatisfied version, a duplicate id, an ordering cycle, an unknown ordering
reference, an unsupported API, feature or kind, an unknown option and an
invalid option value each produce their own error code and message. Nothing is
silently ignored, because a mod that half-loads is a desync waiting for a
report.

Version constraints in format 1 are deliberately small: `*`, an exact
`1.2.3`, `^1.2.3`, `~1.2.3`, and whitespace-separated comparators
(`>=1.2.0 <2.0.0`). Ranges are conjunctions. This covers the composition the
first SDK needs without inventing a second full constraint grammar.

Load order is computed from the constraints, never from the caller's list
order:

- a dependency loads before its dependent;
- `after` and `before` add the same kind of edge;
- the topological order is tie-broken by package id, so the same manifests
  resolve identically whatever order they are read in.

Keyed-exclusive composition follows ADR-0002 D5. A second claim on a key is an
error unless the later package's `replaces` names the current owner and that
key, in which case it takes ownership. The resolved owner of every key is
recorded in the set.

### Reconciliation with ADR-0002 D4

ADR-0002 proposes `modinfo.tdf` inside a mod archive, so the existing archive
fingerprint covers it. The first SDK keeps `rwe_mod.json` as the author file
for three reasons: it already exists in shipped community packages
(PACKAGE-003), MOD-02 prefers extending it, and the first SDK is pre-match and
does not yet load wasm or Lua at runtime. The resolved-set identity in
MOD-SDK-003 covers the manifest bytes and the entry script, so two peers
cannot compose differently while agreeing on archive fingerprints. Moving the
canonical manifest inside the archive, or adding a TDF adapter, remains an
ADR-0002 review item; this entry does not treat the JSON choice as a revision
of the ADR.

## MOD-SDK-003: canonical resolved set and identity

Status: implemented 2026-11-02, same files.

A resolved set is a versioned JSON object:

```json
{
  "api": 1,
  "format": "rwe.resolved-mods",
  "formatVersion": 1,
  "packages": [
    {
      "api": 1,
      "entry": "scripts/main.lua",
      "exclusive": ["hook.damage.v1"],
      "features": ["transform.definitions@1"],
      "id": "com.example.armcost",
      "kind": "mutator",
      "manifestSha256": "<64 hex>",
      "options": {},
      "version": "1.0.0"
    }
  ]
}
```

`packages` is in load order. Keys inside each object are sorted (nlohmann's
JSON object is map-backed) and the encoding is compact with no insignificant
whitespace. The identity is the lowercase hex SHA-256 of those exact bytes.

Deliberately excluded from identity: the package's source path, its folder
name and its download provenance. MOD-03 requires that paths cannot change
content identity, and a renamed folder must not refuse a save or a multiplayer
launch. Deliberately included: the stable `id`, version, kind, required API,
entry path, the SHA-256 of the author's manifest bytes, resolved option values,
required features, the exclusive keys the package owns, and the load order
itself.

The manifest hash covers the author file as written. A change to its display
name, its author field, its whitespace or any unknown key it carries therefore
changes the identity, because those bytes are part of what one peer must have
and another must not lack. That is deliberately stronger than "semantic fields
only": an unknown key may be meaningful to a future SDK or to another tool, and
silently ignoring it is how two peers compose differently while agreeing on a
hash. The display name is not separately exempted; only the filesystem
location is.

The manifest hash is supplied by the caller and computed over the raw manifest
bytes, not over a re-serialization, so whitespace and key order in the author
file are part of what peers must agree on. Entry script bytes were not
hashed when this entry was written; MOD-SDK-014 adds them (resolved-set format
2). Loose content files beyond the entry remain outside the identity.

`schemaVersion` of the package registry is unrelated and unchanged. A resolved
set is derived data; the registry remains the installed-content record.

## MOD-SDK-004: the first-SDK Lua runtime

Status: implemented 2026-11-02 in `libs/lua`, `src/rwe/mod/ModTransform.{h,cpp}`
and `src/rwe/mod/ModTransform.test.cpp`.

Lua 5.4.7 is vendored under `libs/lua`, pinned to the 2024-06-13 release from
`https://www.lua.org/ftp/lua-5.4.7.tar.gz`, under Lua's MIT licence (its README
is carried with the source). The interpreter and standard library are built as
`rwe_lua`; the standalone `lua`/`luac` programs are not.

MOD-04 asked for a measured choice between embedded Lua and Lua inside
WebAssembly. Both were considered:

- Embedded Lua is available now, needs no wasm toolchain, no host ABI for
  engine internals, and no sandbox interface beyond the transform calls.
- Lua inside wasm would inherit a wasm sandbox and fuel accounting, but RWE has
  no wasm runtime vendored, and the pre-match transform surface touches only
  names and three integers, so the extra layer buys nothing yet.

The first SDK therefore embeds Lua. This is an RWE-original first-SDK choice,
not a revision of ADR-0002's eventual policy ABI. Runtime gameplay hooks
remain future work and are not claimed by this entry.

Determinism and bounding, as implemented:

- one Lua state per script, created and closed around one transform; no VM
  survives into the battle;
- a fixed string-hash seed (`libs/lua/lstate.c` patch) so table traversal
  order does not depend on the clock or addresses;
- unit iteration is sorted by name before the script sees it, and the
  transform journal is emitted in unit/key order;
- `os`, `io`, `package` and `debug` are never opened; `dofile`, `loadfile`,
  `load`, `math.random` and `math.randomseed` are removed;
- a count hook charges the budget in 1000-VM-instruction windows and raises a
  Lua error when it is exhausted;
- a custom allocator enforces a byte budget and reports exhaustion as a normal
  Lua error;
- host calls are bounded by finite ranges (`0 .. 4294967295`), a change cap, a
  log cap and a log-line length cap;
- no clock, filesystem, network or native loading is reachable from a script.

Known uncertainty, deliberately not overclaimed: `math.sin`/`cos`/`exp`/`log`
and the `^` operator still call the C library, whose last-bit results are not
pinned across toolchains. Format 1's supported arithmetic is `+ - * / %`,
comparison, and `math.floor`/`abs`/`min`/`max`; a future revision must either
remove the remaining libm calls or route them through `det_math`. No same-seed
hash baseline has been captured for Lua execution yet.

## MOD-SDK-005: the pre-match transform surface

Status: implemented 2026-11-02 in `src/rwe/mod/ModTransform.{h,cpp}` and
wired into `GameSimulationLoader.cpp`.

Format 1 exposes exactly three integer FBI keys:

| Key | FBI field | Engine field |
| --- | --- | --- |
| `MetalCost` | `BuildCostMetal` | `UnitFbi::buildCostMetal` |
| `EnergyCost` | `BuildCostEnergy` | `UnitFbi::buildCostEnergy` |
| `BuildTime` | `BuildTime` | `UnitFbi::buildTime` |

Scripts get `rwe.unit_names()` (sorted array), `rwe.unit_get(name, key)`,
`rwe.unit_set(name, key, value)`, `rwe.log(text)` and `rwe.options` (the
resolved manifest options). Unknown units or keys are errors, never silently
ignored. Keys are case-sensitive and are the FBI spellings.

Rules:

- Values are held as doubles during the script so a multiplier works, then
  rounded once at commit, half away from zero, and required to fit an
  unsigned 32-bit field. Negative, non-finite or too-large values are refused
  during the call.
- A script's writes are atomic. It sees its own writes immediately, so two
  hits in one pass compose; a failure restores every original value and
  returns no journal. The next mutator in the stack sees the committed,
  rounded result.
- The journal records only values that actually moved, with `before` and
  `after`, sorted by unit and key. A dry run can therefore explain every
  changed value.
- Instructions, memory, changes and log size are bounded together; the
  script's own package id and chunk name appear in every diagnostic.

Integration: `GameSimulationLoader` parses every FBI before any definition or
media is built, runs the resolved scripts in order on the disposable parsed
records, writes the rounded values back into `UnitFbi`, and then runs the
ordinary validated loading path. No installed file is modified, so a failed
transform leaves the previous data untouched. A failed transform throws and
refuses the launch.

Evidence, executed 2026-11-02: `rwe_test "[modtransform]"` passes 13 cases.
With the shipped data in `%APPDATA%/RWE/Data`, the generated example resolved
278 unit definitions and changed 270 `MetalCost` values; the eight unchanged
units have a zero cost. This is an RWE test on real shipped data, not an
original-game execution.

## MOD-SDK-006: identity through launch, saves and replays

Status: implemented 2026-11-02 in `GameParameters.h`, `SaveFile.cpp`,
`ReplayFile.{h,cpp}`, `GameSimulationLoader.cpp` and `GameScene_menu.cpp`.
Multiplayer wire comparison remains open.

The launch form is `ModLaunchContent`: the resolved identity, then packages in
load order carrying stable id, version, manifest SHA-256, script name, local
script path and resolved options. It is shared through `GameParameters` and is
null for an unmodded game.

A save writes a `mods` object under its JSON header; a replay writes the same
object in its header, with the replay format version raised from 9 to 10.
Both record identity, order and fingerprints. Neither records the local script
path, because a save moved to another machine must not remember where this one
kept its files. Older saves and replays have no field and read as unmodded.

Verification:

- Loading a save whose recorded set differs from the running one is refused
  in `GameScene::loadSavedGame` with a message naming the first difference
  (`describeModSetMismatch`); it does not load and then diverge at the first
  transform. When the sets agree, the running launch's script paths are kept.
- The loader refuses a recorded set whose scripts have not been resolved on
  this machine, with a message naming the package, rather than quietly
  running the unmodified data.
- A replay rebuilds `GameParameters` with the recorded identity and
  fingerprints. Until the launcher resolves that set into script paths, the
  same explicit refusal applies. This is the intended MOD-06 repair message,
  not a silent fallback.

Launcher integration, implemented 2026-11-02: `launcher/src/common/archives.ts`
extends the mod fingerprint with the SHA-256 of `<mod>/rwe_mod.json` when that
file exists. The hash is over the raw manifest bytes, uses the existing
streaming hasher and cache, and never includes the absolute path. A mod with no
manifest produces exactly the previous fingerprint and digest, so legacy
content is unchanged. The lobby, the game server's wire validation and its
refusal message all compare and name the manifest difference. Jest: 15 suites,
167 tests, plus 4 packaging tests; `tsc` and ESLint clean. Not executed: a real
two-machine session or a shipped community package carrying `rwe_mod.json`.
A peer on an older launcher that reports no manifest is refused when the
active mod has one; that is deliberate.

Still not implemented: the full resolved-set identity (as opposed to manifest
bytes) on the multiplayer wire, launcher-side re-resolution of a recorded set
(the engine's own `--mod` does it from the command line, MOD-SDK-015), and
automatic regeneration under a recorded runtime version. These must not be
reported as done.

## MOD-SDK-007: the author and preview workflow

Status: implemented 2026-11-02 in `src/rwe_setup.cpp`.

New commands, all local and offline:

```
rwe_setup --create-mod <dir> [--id <id>] [--name <name>] [--version <ver>]
rwe_setup --validate-mod <manifest>
rwe_setup --resolve-mods <manifest>... [--option id:key=value]...
rwe_setup --preview-mods <manifest>... [--data-path <dir>] [--option id:key=value]...
```

- `--create-mod` writes `rwe_mod.json` and `scripts/main.lua` and refuses to
  overwrite an existing manifest. The template is a cost multiplier driven by
  the manifest's `multiplier` option.
- `--validate-mod` parses and validates one manifest, printing its stable id,
  version, kind, API, entry, manifest hash, dependencies and features.
- `--resolve-mods` resolves a set and prints the ordered canonical result and
  identity.
- `--preview-mods` resolves the set, reads the real `units/*.fbi` stack
  through the engine's own VFS (`constructVfs`), runs every script in order
  and prints the identity, unit count, per-package instruction count, log and
  every `before`/`after` change. It writes nothing.

Packaging a mod (the archive/zip step) and the launcher's Library/profile
presentation remain open. The preview is the field-by-field provenance the
plan asks for; the launcher does not yet show it.

## MOD-SDK-008: the authoritative mod state store

Status: implemented 2026-11-02 in `src/rwe/mod/ModStateStore.{h,cpp}` and
`src/rwe/mod/ModStateStore.test.cpp`, and now wired into `GameSimulation`'s
save, hash and dump. No runtime hook writes it yet.

`ModStateStore` is the engine-owned state contract from ADR-0002 D14:

- three scopes: global, per player and per unit;
- one namespace per mod for writes, while every namespace is readable by
  naming it;
- typed values `i32`, `f32`, `string` and `bytes` (`bytes` encoded as hex in
  JSON so arbitrary bytes survive);
- entries sorted by scope id, namespace and key, so iteration and hashing do
  not depend on insertion order;
- per-mod quotas on entries and on key-plus-payload bytes; a refused write
  changes nothing;
- per-unit entries are deleted with the unit, per-player entries with the
  player, and both release their quota;
- a versioned canonical JSON form (`rwe.mod-state`, version 1) whose SHA-256
  is the store hash. Parsing is transactional: malformed or over-quota state
  leaves the existing store untouched.

Evidence, executed 2026-11-02: `rwe_test "[modstore]"` passes 6 cases and 69
assertions, including quota refusal, scope isolation, unit deletion, canonical
round-trip and hash-by-content.

Registration, implemented later on 2026-11-02:

- `GameSimulation` holds a `std::unique_ptr<ModStateStore>` behind a forward
  declaration; `setModState` is out of line so the widely included header never
  sees the store's definition.
- `save_util.cpp` writes `modState` and raises the save version to 4 only when
  a store exists; a version-4 save is accepted on load, and a save without the
  field clears the target's store. Versions 1-3 are unchanged.
- `GameHash_util.cpp` folds `computeHashOf(store.hash())` only when a store
  exists; `dump_util.cpp` writes the same JSON only when it exists.
- `clearSimulationForLoad` clears the store before a load.

Evidence, executed 2026-11-02: `rwe_test "[saveload]"` passes 40 cases and
2031 assertions, including a new case that saves and restores a store, checks
the version remains 3 for an unmodded game and 4 for a modded one, and checks
the hash changes by content and is restored exactly. An unmodded game's hash
and save are byte-for-byte what they were.

Remaining for MOD-08: the runtime hook dispatcher, the disabled-package set,
script RNG/timer state, and the engine APIs that let a script create or inspect
runtime state.

## MOD-SDK-009: the runtime hook dispatcher

Status: implemented 2026-11-02 in `src/rwe/mod/ModRuntime.{h,cpp}` and
`src/rwe/mod/ModRuntime.test.cpp`, with simulation integration in
`GameSimulation.cpp`, `GameSimulationLoader.cpp` and the save/hash/dump paths.

A runtime package is one whose manifest requires a `runtime.*` feature (today
`runtime.damage@1`). It is distinguished from a pre-match mutator by that
feature, not by `kind`, and the loader's transform pass skips it.

`ModRuntime` owns one persistent, sandboxed Lua state per package and the
engine-owned store is shared with `GameSimulation`:

- `hook_damage_v1(victim, attacker, amount, paralyzer, prev) -> number` is a
  fold in resolved order, called at the final-damage point in
  `GameSimulation::applyDamage`: after attacker veterancy, armour and victim
  veterancy, before the value is stored. The result is checked finite and
  non-negative and rounded once, half away from zero.
- `on_tick(tick)` runs after `runAutomaticSharing`, i.e. after every native
  phase, so appending it reorders nothing. It sees the tick's settled state.
- `rwe.store_get(key)` / `rwe.store_set(key, value)` use the package's own
  namespace in `ModStateStore`; `rwe.options` carries resolved options;
  `rwe.log` appends to the runtime log.
- `rwe.rand_below(n)` is a deterministic modulo of the package's own
  `minstd_rand`, seeded from the resolved-set identity plus the package id, so
  adding a runtime package does not move the simulation's own RNG.
- The same sandbox as the transform runtime: no `os`/`io`/`package`/`debug`,
  no `load`/`dofile`/`loadfile`, no `math.random`, bounded memory, and a
  deterministic instruction budget per call, checked in 1000-instruction
  windows.
- A fault (Lua error, non-finite or negative damage) disables that package,
  logs it, and passes `prev` through. Disabled packages are recorded in the
  runtime metadata and therefore hashed.

Save: `save_util.cpp` writes the store under `modState` and the runtime
metadata (disabled packages and RNG positions) under `modRuntime`, excluding
the duplicated store. A recorded `modRuntime` with no resolved runtime is
refused rather than silently dropped. Hash and dump include both. The save
version is 4 whenever either is present; an unmodded game stays at 3.

Launch: `GameSimulationLoader` creates the store and runtime for packages with
a `runtime.*` feature, after the simulation exists and before players and units
are added. The seed is the first 32 bits of the resolved identity.

Evidence, executed 2026-11-02: `rwe_test "[modruntime]"` passes 7 cases and
`"[damage]"` 8, including an end-to-end case where a runtime `prev * 2` hook
turns 100 damage into 200 while the same simulation without a runtime takes
100. Full suite: 2111 cases, 2106 passed, 5 skipped, 0 failed.

Remaining and deliberately not claimed: only the damage fold and per-tick event
exist; there are no unit/projectile queries, no deferred effects, no custom
orders, no per-unit store vars, no scripted AI, and no call-site hooks for
creation/completion/death. Limb (feature/reclaim/remote) damage still bypasses
the unit choke point. The libm/`^` determinism caveat in MOD-SDK-004 applies
unchanged.

## MOD-SDK-010: Lua AI through the lockstep controller path

Status: partially implemented 2026-11-02 in `src/rwe/mod/ModAiScript.{h,cpp}`,
`src/rwe/mod/ModAiController.{h,cpp}`, `src/rwe/mod/ModAi.test.cpp`,
`src/rwe/ai/AiEngine.h`, `src/rwe/ai/AiController.cpp`,
`src/rwe/game/AiSeat.{h,cpp}` and `src/rwe/game/GameSimulationLoader.cpp`.
MOD-09 is **not complete**: AI private state is not saved, resumed or rejoined.

This is **RWE-original**. `TotalA.exe` has no scripted AI, no AI package, no
per-seat script provider and no Lua observation API. The evidence boundary is
inherited from MOD-SDK-001 (the authoring foundation) and MOD-SDK-004 (the
pinned Lua runtime); this entry records an implementation against that boundary,
not original-game parity. No decompilation was opened for it and no original
runtime was executed. The native Classic and RWE engines, their personalities,
`--ai-engine rwe`, saves and replays written by them are unchanged; Classic
remains the default.

### Engine selection and save

`AiEngine` gains `Lua`, named `"lua"` by `aiEngineName` and accepted
case-insensitively by `aiEngineFromName`, beside `"classic"` and `"rwe"`.
`PlayerInfo::aiEngine` round-trips it as `"lua"` in the save header, and
`recordAiEngines` records it from the live controller like any other engine. A
save written before the choice still loads as RWE's engine, and an unknown name
still falls back as before; those policies are untouched.

A seat resolves to Lua only through the existing order: its personality's
engine, else the saved engine, else the game's default. A Classic personality
still plays Classic and an RWE personality still plays RWE.

### Launch resolution

`AiSeatRequest` carries `scriptPackageId` and a
`std::shared_ptr<const ModAiScript>`. `AiSeat.h` forward-declares the struct, so
the widely included header sees neither Lua nor nlohmann::json. `ModAiScript`
holds the package id, script name, source text, resolved options and a
deterministic seed.

`GameSimulationLoader`, immediately before `createAiController`, resolves the
seat's engine and, for Lua, calls `resolveModAiScript` against
`GameParameters::modContent`:

- an empty `scriptPackageId` selects the first package whose manifest `kind` is
  `ai`, in resolved load order, so the choice is deterministic;
- a named id must exist and must be an `ai` package;
- the entry script is read with the same 1 MiB cap as every package script;
- the seed is the first 32 bits of the resolved-set identity, or 0 without one,
  and `AiSeat` still draws exactly one value from `simulation.rng` per seat, so
  the start-position deal is unaffected;
- a seat that asks for Lua with no resolved set, no `ai` package, no local
  script path or an unreadable/oversized script **throws** a message naming the
  package. It is never quietly played by Classic.

A Lua seat with `--ai-tune` is refused, as a Classic seat is: neither has knobs.

### The controller and its surface

`ModAiController` is one persistent, sandboxed Lua state per seat behind a
pimpl. Its tick runs inside the ordinary `GameSimulation::runAiControllers`
pass and appends ordinary `PlayerUnitCommand`s to the same buffer as every
other engine, so the fixed command depth and readiness guard are unchanged.

The first-slice API, sorted as implemented:

| Call | Meaning |
| --- | --- |
| `ai.api_version` | `1` |
| `ai.cmd_attack(unit, target)` | Immediate `AttackOrder`; the target may be any unit the simulation holds |
| `ai.cmd_move(unit, x, z)` | Immediate `MoveOrder` at a finite world position |
| `ai.cmd_stop(unit)` | Immediate `Stop` |
| `ai.log(text)` | Bounded controller log |
| `ai.my_units()` | Sorted array of `{id, type, x, z, health}` for this seat's living units only |
| `ai.options` | The package's resolved manifest options |
| `ai.rand_below(n)` | Deterministic modulo of the controller's own `minstd_rand` |
| `ai.tick()` | The simulation tick number being run |

The script defines `on_tick(tick)`; a script without it is disabled with a
diagnostic rather than left silently inert.

Observation policy: `ai.my_units()` iterates only units whose `owner` equals
the seat and sorts them by unit id. There is no enemy, fog, map or projectile
query, so the first slice adds no omniscience. Commands are rejected with a
Lua error when the unit is unknown or belongs to another seat; a target id must
name an existing simulation unit. Emitted commands are stable-sorted by unit
id before they leave the controller, so author order cannot make two peers
differ, and a unit's own sequence keeps its order.

Determinism: no wall clock, frame rate, ping or presentation state is
reachable. Randomness is the controller's own `minstd_rand`, seeded from the
request seed, the package-id hash and the seat's single simulation draw. Unit
iteration is the snapshot above. The Lua string-hash seed is the fixed one from
MOD-SDK-004.

Sandbox and budgets, reused from `ModRuntime`: `os`, `io`, `package` and
`debug` are not opened; `load`, `dofile` and `loadfile` are removed;
`math.random`/`math.randomseed` are removed; a count hook enforces 200,000 VM
instructions per call in 1000-instruction windows; a custom allocator enforces
a 4 MiB state budget; commands per tick, log entries and log-line length are
bounded. A fault (Lua error, budget exhaustion, foreign unit, missing
`on_tick`) disables the seat for the match and emits nothing for that tick.
The disable is sticky and deterministic, as `ModRuntime`'s package disable is.

### What is deliberately not done

- **Save/resume and rejoin of AI private state are not implemented.** The save
  header records only the engine name. Plans, timers, pending actions, the Lua
  heap and the RNG position are not in `ModStateStore`, the save, the hash or
  a checkpoint. A saved game that resumes a Lua seat rebuilds a fresh state
  and therefore does not yet reproduce the AI's future decisions. Rejoin has
  no state transfer or deterministic reconstruction.
- Replay shadow AI still runs the recorded command stream with an idle seat;
  a replay does not start a second AI producer. The recorded-lua replay path
  requires the recorded AI package to resolve locally, like every MOD-06 set.
- No two-peer delayed-packet or differing-frame-rate run, and no
  Windows/Linux cross-build run, has been performed. The determinism claims
  above are test-level, not an executed multi-machine parity check.
- No seat-to-package assignment UI or PlayerInfo field exists yet: the first
  `ai` package in load order is the seat's, or an explicit package id supplied
  by a caller.
- The libm/`^` caveat in MOD-SDK-004 applies unchanged.

### Evidence

Executed 2026-11-02 with the shipped data build tree: `rwe_test "[modai]"`
passes 11 cases and 72 assertions. The cases cover byte-identical command
sets from one seed, sorted emission, own-unit observation and foreign-unit
refusal, idle seats not thinking, the sandbox removals, sticky deterministic
faults, the instruction budget, a missing `on_tick`, and AI-package resolution
including the refused unresolved, missing, non-ai and no-set cases. `"[ai][engine]"`
passes 15 cases and 137 assertions including Lua selection and the
one-draw-per-seat invariant. `"[saveload][ai]"` passes 4 cases and 23
assertions. The full suite passes 2125 cases (2120 passed, 5 skipped) and
216896 assertions in the final run; the assertion total varies by a few between
runs, so the test-case count is the stable figure. These are RWE tests of
RWE-original behaviour, not original-game execution.

## MOD-SDK-011/012: the first subsystem providers

Status: implemented 2026-11-02 in `src/rwe/sim/EconomyProvider.{h,cpp}` and
`src/rwe/sim/WindProvider.{h,cpp}`, routed through `GameSimulation` and the
save/hash/dump paths.

These are the first MOD-11 providers. Both are value-only:

- `EconomyProvider::settle(const ResourceAccount&, float) -> SettledResourceAccount`
  replaces the two once-a-second settle calls. The native provider wraps
  `settleResourceAccount` unchanged.
- `WindProvider::update(const WindInput&, std::minstd_rand&) -> WindState`
  moves the existing `updateWind` arithmetic unchanged and is the first
  provider that draws from the simulation's random stream.

`GameSimulation` holds each provider behind a forward-declared `unique_ptr`;
null means the native implementation, so an unmodded game pays no indirection
and behaves identically. Each provider has a stable id
(`native.economy@1`, `native.wind@1`) that is written to the save, folded into
the sync hash and shown in the desync dump. Loading a save that names a
different provider is refused rather than silently substituting the native
code.

Evidence: `[economyprovider]` 2 cases, `[windprovider]` 3 cases; full suite
2131 cases, 2126 passed, 5 skipped, 0 failed. These are RWE tests of
RWE-original extraction, not original-game execution. The default provider is
not yet benchmarked against `battle_test`, and no deliberately different
provider has been installed for a full hash-oracle comparison.

## MOD-SDK-013: the presentation view and runtime

Status: implemented 2026-11-03 in `src/rwe/presentation/` and
`src/rwe/game/GameScene_presentation.cpp`, with the boundary from
`MODDING-PRESENTATION-SUBSYSTEM-MAP.md`. RWE-original: the original has no
presentation mod surface, so there is no decompilation comparison for the API
itself. The filters it applies are the decoded ones.

### The boundary

`PlayerView` is the only simulation access a presentation package gets. It
borrows a `GameSimulation` and one `PlayerVisibility` picture for the duration
of a call and exposes copies, never references:

- `units()` and `unit(id)` return only units `canSeeUnitInVisibility` admits
  for the viewer, which is the same predicate the world draw and the target
  scan use. Enemy units under fog, cloak or an untreated waterline are absent.
- `sensorContacts()` returns the radar and sonar ids that are *not* in full
  sight, carrying an id, a position and the sonar flag and nothing else. The
  contact has no type, owner, colour or health, matching BT-141's
  "Unidentified object" rule.
- `features()` returns features on ground the viewer has explored, with a
  `visible` flag distinguishing live sight from memory.
- `projectiles()` applies the same live-sight test the renderer applies to a
  round (`effectIsVisibleToPlayer`'s rule).
- `resources()` returns the viewer's own stockpile. No other player's is
  reachable.
- `unitDefinition(type)` returns static definition data (name, description,
  costs, build time, health, builder/mobile flags) and nothing per-unit.

Deliberately absent: the simulation object, `players`, `playerVisibility`,
the shared `explored` grid, `losGroupBitFor`, `rng`, feature names by table
index (only the definition's name string), and every writable handle.

### The runtime

`PresentationRuntime` is one bounded Lua 5.4 state per package, modelled on
`ModRuntime` but owning no simulation store: presentation state is not hashed,
saved or recorded. The sandbox exposes `base`, `table`, `string`, `math`,
`utf8` and `coroutine`; `dofile`, `loadfile`, `load`, `io`, `os`,
`math.random` and `math.randomseed` are removed. Memory is bounded by the same
limited allocator MOD-08 uses and each callback has an instruction budget; a
fault disables only that package.

Hooks: `on_frame(dt)`, `on_mouse_down(x, y, button) -> boolean`,
`on_mouse_up(x, y, button) -> boolean`, `on_mouse_move(x, y)` and
`on_key_down(key) -> boolean`. An input hook returns whether it consumed the
event; the scene delivers a consumed event to nothing else and gives it to the
native panel and world otherwise. `on_frame` runs once a frame, after the
ticks that frame dispatched.

Drawing goes into a plain `PresentationDrawList` (rect, line, text, bar) in UI
pixels. Nothing in it names a texture, model or renderer handle, and the scene
draws it in `renderOverlay` between the native chrome and the menu screens.

Commands are values: `submit_move`, `submit_stop` and `submit_attack` accept
unit ids and are filtered twice -- once in the runtime against the frame's
own visible units, and again in the scene when the commands are turned into
ordinary `PlayerUnitCommand`s. Only the local human's own units are accepted;
a radar contact is not a target; nothing is submitted while watching a replay
or demo. A frame may issue at most 1024 unit commands.

### Reload

`+ReloadPresentation` (RWE-original, presentation-only) re-reads every
presentation package from the path the launch resolved and builds a complete
replacement runtime. If any package fails to parse, fault at load, or exceed
its budget, the replacement is discarded and the running HUD keeps drawing.
The command is parsed by `parseChatCommand` and never reaches the command
stream or a peer.

### Resolution

`ModResolutionEnvironment::supportedKinds` now includes
`ModKind::Presentation`, so a presentation package resolves and is carried in
the existing resolved set. `resolvedPresentationPackages` skips a recorded
package whose machine-local script path is absent (a save or replay moved
here), because presentation is not simulation identity; a path that exists
but cannot be read is an error at launch. Presentation bytes are therefore
not part of the simulation identity, by design.

### Evidence

`[presentation]` tests cover: hidden enemies absent from both `units()` and
`unit(id)`; a radar contact present without identity or health; unexplored
features hidden and remembered ones marked `visible = false`; a round over
unexplored ground hidden; the viewer's own stockpile only; the sandbox's
absent globals; the draw list; the command sink's rejection of a hidden
unit; input consumption; a failed reload leaving the previous HUD running;
and the shipped `examples/mod-presentation-hud` package loading, drawing and
reloading through the real resolver. A dedicated test computes the sync hash
before and after a frame that reads units/resources, draws and submits a
move, and requires it unchanged.

These are RWE tests of RWE-original behaviour, not original-game execution.
The original-game conformance of the *filters* rests on the decoded
`canSeeUnitInVisibility` and fog predicates those tests reuse, not on a new
decompilation reading.

Executed 2026-11-03 on the MinGW64 Debug tree (`build/`, commit `278ae3c6`
plus this change), after a clean rebuild of `librwe`, `rwe_test` and `rwe`:

- `rwe_test "[presentation]"`: 27 cases, 26 passed, 1 skipped, 190
  assertions. The skip is the existing retail-data case that needs
  `RWE_SCENARIO_DATA`.
- Full `rwe_test`: 2142 cases, 2137 passed, 5 skipped, 0 failed; 217023
  assertions passed.
- `tools/hash-oracle.sh record` then `compare`, 120 simulated seconds on
  each of Crystal Maze and Coast To Coast at seeds 42 and 7: all four
  3599-tick logs identical. That run uses `rwe --ai-arena`, which never
  constructs a `GameScene` and therefore never loads a presentation package;
  it pins the arena path around this change rather than providing a
  mod-on/mod-off comparison. The mod-on invariant is the unit test above,
  which hashes the simulation before and after a frame that reads units and
  resources, draws four commands and submits a move.

### Not done

- Panels remain RWE's own `UiComponent`s; a presentation package draws a list
  rather than constructing a native `UiPanel`.
- Filtered picking, the camera and minimap controllers, and the audio policy
  from the subsystem map are not implemented. A mod cannot move the camera,
  ask what is under an arbitrary screen point, or replace a sound.
- Effects spawned through the existing helpers are not exposed.
- The engine command line had no resolved-set launch switch when this entry
  was written, so the example is exercised by `rwe_test`. `--mod`
  (MOD-SDK-015) now exists; the windowed run has not been made.
- Presentation scripts are not part of the resolved identity, which is
  deliberate; a mismatch between peers cannot desync a game, because nothing
  they draw reaches the simulation.

## MOD-SDK-014: entry scripts in the resolved identity

Status: implemented 2026-11-04 on the local branch `978-hash-entry-scripts`,
issue #978. RWE-original; inherits MOD-SDK-001's boundary. There is no
original-game counterpart and no byte-tactics comparison applies.

**Not yet compiled or run.** The machine this was written on had no C++
toolchain, so the engine half -- including the new `[modsdk]`, `[modai]` and
`[modtransform]` cases -- has not been built or executed. The launcher half
was: `tsc` clean, ESLint clean apart from one existing warning in an untouched
file, and Jest 15 suites, 177 tests. Nothing here may be called verified until
`rwe_test`, the `rwe` target and `rwe_setup` have been built and run.

The gap this closes: MOD-SDK-003's identity covered the manifest's bytes but
not the script it names, so a script edited without touching `rwe_mod.json`
kept the same identity, and a save, a replay and a peer check would all agree
while running different Lua.

### What is hashed

- Each package carries `entrySha256`, the lowercase hex SHA-256 of its entry
  script's raw bytes. The resolved-set JSON gains `"entrySha256"` per package
  (`null` when there is none), and the resolved-set `formatVersion` goes from 1
  to 2, because a format-1 identity never covered scripts and the two are not
  comparable.
- The resolver refuses a package whose manifest names an entry but which
  carries no well-formed hash (`MissingField`), and a hash on a package with no
  entry (`InvalidField`). The caller reads the file; `rwe_setup`'s
  `readModPackage` does it, relative to the manifest, so the path still stays
  out of the identity and a renamed folder still resolves the same.
- **Presentation packages are excluded**, as MOD-SDK-013 already decided: their
  scripts do not reach the simulation, and `+ReloadPresentation` exists to
  change them mid-game. A presentation package carrying a hash is refused, so
  the exclusion cannot be got round by accident.

### Where it is checked

- `ModLaunchPackage::entrySha256` goes into saves and replays.
  `describeModSetMismatch` names a script-only difference as such ("has a
  different script") rather than falling back to the generic identity message.
- `readVerifiedModScript` is now the one reader for simulation scripts: the
  pre-match transform, the runtime hooks and the Lua AI. It refuses a package
  with no recorded hash and a file whose bytes no longer match, naming the
  package, so an edit between resolving and loading cannot run unnoticed.
  `rwe_setup --preview-mods` uses it as well, and now previews only the
  packages the loader would transform with (pre-match mutators), where it used
  to run every package's script as a transform. The presentation reader is
  left as it was, on purpose.
- The launcher's mod fingerprint gains `manifest.entry` (`path`, `size`,
  `hash`) for a format-1, non-presentation manifest whose entry passes the
  engine's own path rule (`isValidEntryPath`, mirroring `validEntryPath`). The
  lobby and the game server compare it, the refusal names the script file
  rather than `rwe_mod.json`, and the game server validates the reported path
  and hash. The lobby digest covers it too.

### Compatibility

- A save or replay recorded before this change carries a format-1 identity,
  which no current launch reproduces, so it is refused with the existing
  identity message rather than loaded. Saves without mods are unaffected.
- A peer on an older launcher reports no `entry`; against a scripted mod that
  is a difference, so the game does not start. This matches MOD-SDK-006's
  existing rule for a peer that reports no manifest.

### Still not covered

- Loose content files and archives other than the entry script. Format 1 has
  no `require` and no declared content-file list, so the entry is the only Lua a
  package can run; anything that adds modules or declared files must extend
  this identity before it ships.
- The full resolved-set identity on the multiplayer wire (#979). The engine's
  identity now covers scripts, and the launcher's fingerprint does separately,
  but the two are not yet the same value.

## MOD-SDK-015: the engine's own --mod launch switch

Status: implemented 2026-11-04 on the local branch `mod-launch-switch`, built
on `978-hash-entry-scripts` (PR #982). RWE-original; inherits MOD-SDK-001's
boundary, and no byte-tactics comparison applies. **Not yet compiled or run**:
the authoring machine has no C++ toolchain.

Until this entry, nothing could put a resolved set into a real game.
`GameParameters::modContent` was only set from a save, a replay or a test, so
the windowed acceptance runs MOD-07 and MOD-10 call for had no way to start,
and #979 had no launch-time identity to put on the wire.

### The switch

- `rwe --map <name> --mod <rwe_mod.json> [--mod ...] [--mod-option
  <id>:<key>=<value> ...]` resolves the manifests and starts the game with the
  result. Load order comes from the manifests (MOD-SDK-002), never from the
  argument order. `ai_arena` takes the same switches, so a modded game -- a Lua
  AI seat included -- can be measured headlessly.
- With `--replay` or `--load`, the recording already names its set, and
  `--mod` supplies this machine's copy of it. The locally resolved set must be
  the recorded one (`describeModSetMismatch`), and then the recording runs
  with this machine's script paths. A different set, or mods against a
  recording made without any, is refused rather than played. This closes
  MOD-SDK-006's "re-resolution of a recorded set" gap for the command line.
  Without `--mod`, a modded recording is still refused with the existing
  "not been resolved on this machine" message.
- `--mod` with `--record-demo` is refused, as `--community-patch` is: a TA
  demo is the original's format and cannot carry the set. `--mod` with
  `--tad`, or with no game to start, is refused.
- `ai_arena` now applies each package's `resourceDirectories` to its path
  mapping. Before, it passed the base mapping straight to the loader, so a
  replay of a ProTA-style package would have read the wrong roots.

### One resolver

`src/rwe/mod/ModLaunch.{h,cpp}` now holds what `rwe_setup.cpp` used to keep
in its anonymous namespace: reading a manifest and hashing its entry
(MOD-SDK-014), parsing option selections, the supported-feature list, and
turning a resolved set into the launch form. `rwe_setup`, `rwe`, `ai_arena`
and `LoadingScene` (resource directories) all call it, so the offline preview
and the real launch cannot resolve the same manifests differently.
`--resolve-mods` keeps printing the canonical resolved set; its malformed
`--option` message now reads "a mod option must be ...".

### Known limits

- On Windows, `rwe`'s arguments are narrow strings, as `--data-path` already
  is. `rwe_setup`'s PACKAGE-004 handling of Unicode paths is not reproduced
  for `--mod`.
- `rwe.cfg` is read by the same argument parser, so a `mod=` line there would
  apply to every launch. That is how every other option behaves; it is noted
  here, not guarded against.
- The launcher does not pass `--mod` yet. A network game still compares
  archives, manifest bytes and entry scripts (MOD-SDK-006/014), not the
  resolved identity (#979).

## MOD-SDK-016: per-unit runtime state and the shield example

Status: implemented 2026-11-04 on the local branch `970-shield-example`, built
on `mod-launch-switch` (MOD-SDK-015). Issue #970, partly. RWE-original;
inherits MOD-SDK-001's boundary, and no byte-tactics comparison applies. **Not
yet compiled or run** when written; CI is the build.

### What a runtime script can now do

Behind a new feature, `runtime.unit-state@1`, which a build without it refuses
at resolution rather than failing mid-game:

- `rwe.tick()` returns the simulation tick the call runs in. A script must not
  keep timing in Lua variables, because those are not saved: a resumed game
  would see different values from an uninterrupted one.
- `rwe.unit_get(unit, key)` and `rwe.unit_set(unit, key, value)` read and
  write the package's own namespace in the store's per-unit scope
  (MOD-SDK-008), with the same value rules as `store_set`. A non-integer or
  out-of-range unit id, a value that is not a finite number, string or
  boolean, and a quota overflow each raise a Lua error, and the error disables
  the package as any other fault does.
- `unit_set` refuses a unit the simulation does not have. A unit id is a slot
  that `spawnNewUnits` reuses within the same tick `deleteDeadUnits` frees it,
  so an entry written for a free id would be inherited by the next unit built
  there.

### Engine side

- `deleteDeadUnits` now calls `ModStateStore::removeUnit` before erasing each
  unit. The store has offered `removeUnit` since MOD-SDK-008, but nothing
  called it; that was harmless only while nothing could write per-unit state.
- `ModRuntime::bind(tick, context, unitExists)` replaces any stored reference
  to the simulation. `GameSimulation` binds itself, with a static function
  pointer, immediately before folding damage and before `on_tick`. A stored
  `[this]` callback would dangle: the loader builds a simulation and
  `GameScene` then moves it. The tick is not runtime state, because it is
  re-bound from the simulation's own clock before every dispatch, so it needs
  no saving.
- The new Lua functions keep C++ objects out of scope wherever `luaL_error`
  can be reached. Lua is built as C, so the error longjmps past C++
  destructors. The older `store_get`/`store_set` hold a `std::string` across
  `luaL_error`, which leaks it on that path; they are left unchanged here.

### The example

`examples/mod-shield-runtime` gives every unit a regenerating shield (options
`capacity`, default 200, and `regen_per_tick`, default 1). Damage is taken
from the shield first. Regeneration is worked out at the next hit, from the
tick of the last one, so an unshot unit costs nothing per tick. Paralyzer hits
pass through untouched. Everything a future hit depends on is in the store,
which is why a saved and resumed runtime gives the same next hit as an
uninterrupted one; `[modruntime]` checks exactly that, plus absorption,
regeneration, capping and per-unit isolation. `[damage][modruntime]` checks,
through the real `applyDamage`, that the hook sees the simulation's tick and
that the unit's entries are gone after `deleteDeadUnits`.

The example writes two entries per shielded unit, and the default quota is
4096 entries per package, so about 2048 units can carry a shield at once. The
next write past that is a quota fault, and the fault disables the package,
the same way on every peer.

### Still open for #970

Lifecycle hooks (added in MOD-SDK-017); filtered unit, projectile and
player queries; deferred effects; a decision on reclaim, feature and remote
damage, which still bypass the unit hook; and the two-peer and
`net-test.py` acceptance runs.

## MOD-SDK-017: unit lifecycle events

Status: implemented 2026-11-04 on `970-shield-example`, after MOD-SDK-016.
Issue #970, partly. RWE-original; inherits MOD-SDK-001's boundary, and no
byte-tactics comparison applies. Not compiled locally; CI is the build.

Behind `runtime.lifecycle@1`, a runtime package may export:

| Event | Arguments | Raised |
| --- | --- | --- |
| `on_unit_created` | unit, owner, unitType | in `trySpawnUnit`, beside `UnitSpawnedEvent`: every new unit, a nanoframe included |
| `on_unit_completed` | unit | when a builder finishes a unit (both build paths in `UnitBehaviorService`), when a unit is spawned complete (`trySpawnCompletedUnit`), and when a resurrection raises one |
| `on_unit_killed` | unit, attacker | on the four real death paths -- killed, quietly killed, reclaimed, self-destructed -- beside the mission rules' `unitDying`, before the unit is marked dead |

- `unitType` is the FBI unit name, never a table index, for the same reason
  the sync hash reads names: a definition index depends on what is in each
  machine's data folder.
- `attacker` is `nil` when there is none. `hook_damage_v1` passes 0 for a
  missing attacker, but 0 can be a real unit id, so the new events do not copy
  that.
- A killed unit still exists when its event runs, so the script can read and
  write its per-unit state. That state is then deleted with the unit
  (MOD-SDK-016).
- A capture is not reported as a death. The mission rules hear one, because
  the original raises a cause-4 death of the old unit, but the unit id lives
  on under its new owner, and telling a mod it died would be false.
- Loading a save places units directly (`save_util.cpp`), not through
  `trySpawnUnit`, so no event is raised again on resume. A replay re-runs the
  simulation, so it raises them exactly as the recorded game did.
- Each event binds the runtime first (`ModRuntime::bind`), so `rwe.tick()` and
  the `unit_set` existence check work inside it. A fault disables the
  package, as for `on_tick`.

Events run synchronously at the point where they are raised, inside the tick.
A script can change nothing in the simulation from them except its own store
entries, so their position inside the tick decides only what the script can
read, never what any native phase does.

Still open for #970: filtered queries, deferred effects, the limb-damage
decision and the two-peer acceptance run.

## MOD-SDK-018: the runtime unit query

Status: implemented 2026-11-04 on `970-shield-example`, after MOD-SDK-017.
Issue #970, partly. RWE-original; no byte-tactics comparison applies. Not
compiled locally; CI is the build.

Behind `runtime.unit-query@1`, `rwe.unit_info(unit)` returns a table with
`owner`, `type` (the FBI name), `health`, `max_health`, `complete`, `dead` and
`x`/`y`/`z`, or `nil` when there is no such unit.

- It is not filtered by line of sight. A runtime rule runs on every peer over
  the whole simulation, the way a native rule does, so what one peer could
  read every peer reads, and the answer is the same everywhere. Filtering by
  what a player can see belongs to the presentation view (MOD-SDK-013) and to
  scripted AI's observation policy (MOD-09), not to rules.
- Positions are the simulation's own floats (`SimScalar`) widened to Lua
  numbers. They are the same values the sync hash reads, so reading them
  cannot diverge, although arithmetic a script then does on them is subject to
  MOD-SDK-004's libm/`^` caveat.
- `ModRuntime::bind` now takes a `ModRuntimeWorld`: a context pointer and
  plain function pointers for unit existence and unit info.
  `GameSimulation::bindModRuntime()` builds one before every dispatch, from
  file-local callbacks in `GameSimulation.cpp`, so the widely included
  header gains one declaration and no types. The three-argument `bind`
  remains for tests that supply only an existence check.

`store_get`/`store_set` were rebuilt on the same helpers as `unit_get` and
`unit_set`, so no Lua-callable function holds a C++ object across `luaL_error`
any more. One behaviour changes with them: `store_set` used to test
`lua_isnumber` first, which accepts numeric strings, so `store_set(k, "12")`
stored the number 12. A string now stays a string, as it does in `unit_set`.
No shipped package depended on the old conversion. The quota error no longer
appends the package id; the runtime log already names the package it
disables.

Still open for #970: deferred effects and the two-peer acceptance run. The
limb-damage decision is MOD-SDK-019.

## MOD-SDK-019: which damage `hook_damage_v1` folds

Status: decision recorded 2026-11-04, closing the "limb damage" question
MOD-SDK-009 left open. Documentation only; no behaviour changes. RWE-original
policy, no byte-tactics comparison: every path below keeps the arithmetic its
existing findings give it.

`hook_damage_v1` folds combat damage to a unit at the final-damage point of
`GameSimulation::applyDamage`, after attacker veterancy, armour and victim
veterancy. These are the other places that take hit points off a unit, and
what the hook does with each:

| Path | Where | Folded? | Why |
| --- | --- | --- | --- |
| Remote damage to a unit simulated elsewhere | `applyRemoteDamage`, `SimulationOwnership.cpp` | Never | The owning simulation is authoritative: it runs the real `applyDamage`, fold included, and only its record or death declaration counts. Folding the local copy too would apply the mod twice or disagree with the owner. |
| Mixed ownership: our attack on a peer's unit | `MixedOwnership.cpp` | Never | The same rule from the attacker's side: the figure is sent to the owner, and the local drop is display only, held above zero. |
| Reclaim, cause 5 | `GameSimulation.cpp`, reclaim | Not in v1 | The hit points taken decide how much of the cost is handed back, so changing them changes the economy. That needs its own contract (a future `hook_reclaim_v1`), not a quiet widening of combat damage. |
| A cancelled factory product, cause 9 | `FactoryConstruction.cpp` | No | A fixed rule, not a hit: bypassing armour but keeping victim veterancy is the original's own arithmetic for it. |
| The end-of-game decay | `GameSimulation_endgame.cpp` | No | The same fixed rule, applied to every unit of a defeated player. A shield absorbing it would be wrong. |
| Feature damage | `GameSimulation_fire.cpp`, features | Out of scope | The hook is unit-only by contract. Features would get a hook of their own. |

Two consequences for authors:

- In an RWE lockstep game every unit is simulated on every peer, so every
  combat hit reaches the hook, once, on every peer. The "never" rows apply
  only to own-clock and mixed-ownership play against an original client,
  where a remote unit's health is the other side's to decide.
- A runtime mod that wants to change reclaim, the factory cancel or the decay
  needs a new hook version. `hook_damage_v1`'s meaning does not grow.

## MOD-SDK-020: deferred effects, starting with heal

Status: implemented 2026-11-04 on `970-shield-example`. Issue #970, partly.
RWE-original; no byte-tactics comparison applies. Not compiled locally; CI is
the build.

Until now a runtime script could change the simulation in one way only, by
folding damage. Behind `runtime.effects@1` it can now ask for a change, and
the asking is separate from the doing:

- `rwe.heal(unit, amount)` queues an effect. It does not touch the unit.
  `amount` must be a whole number from 0 to 1000000000; the unit must exist
  when the call is made. Either refusal is a Lua error, which disables the
  package as any other fault does.
- `GameSimulation::applyModEffects` applies the queue once per tick, straight
  after `on_tick`, after every native phase, in the order the effects were
  queued: by hook call, then by load order within a call. Native phases
  therefore never see a half-applied change, and no hook can re-enter
  `applyDamage` or any other native path.
- Each effect is checked again when it is applied, not only when it is
  queued. A unit that has died, or whose slot has been reused, since then is
  skipped, and so is a unit still under construction, whose hit points track
  its build progress. A heal is capped at the unit's maximum.
- The queue is runtime state. `ModRuntime::toJson` writes it under
  `effects` when it is not empty, so it is saved, hashed and restored; an
  empty queue leaves the JSON, and so every existing save and hash, exactly
  as it was. Saves happen between ticks, after the queue has been applied, so
  in a running game it is normally empty, but anything a hook queues outside
  a tick (a unit created during load, say) survives a save as well.
- At most `maxPendingEffects` (10000) effects may wait at once, across all
  packages; the next is refused.

Heal is the first effect because it is bounded and cannot kill, spawn or move
anything. Effects that do those things need their own validation (resources,
ownership, placement) and are not attempted here.

Still open for #970: more effect kinds and the two-peer acceptance run.

## Verification

Executed 2026-11-02: `rwe_test "[modsdk]"` and the full `rwe_test` suite; the
`rwe` target and `rwe_setup` were rebuilt. These are RWE tests of RWE-original
behaviour, not original-game execution. `rwe_setup --resolve-mods` is the
offline dry-run entry point; it prints the resolved set and its identity as
JSON, and exits non-zero with the specific error when a set cannot load.

## Deferred, and explicitly not claimed

- Archive-embedded manifests, TDF adapters and ADR-0002 D4 reconciliation.
- Content hashing of loose files and archives into the identity (MOD-03).
  Entry scripts are covered by MOD-SDK-014.
- Lua or wasm execution, sandboxing, determinism and fault policy (MOD-04).
- Save, replay and multiplayer carrying the identity (MOD-06).
- Launcher Library presentation and author commands (MOD-07).
- Any claim of compatibility with OAMOD, OAMUT, OABrain or another engine's
  manifest semantics.
