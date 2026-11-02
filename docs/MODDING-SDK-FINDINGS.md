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
file are part of what peers must agree on. Entry script bytes are not yet
hashed here: MOD-05 must extend the identity to the script and content files
before any mutator ships, and MOD-SDK-004 will record that when it lands. Until
then this identity is complete only for manifests without scripts, and callers
must not treat it as the full simulation identity.

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
bytes) on the multiplayer wire, launcher-side re-resolution of a recorded set,
and automatic regeneration under a recorded runtime version. These must not be
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

## Verification

Executed 2026-11-02: `rwe_test "[modsdk]"` and the full `rwe_test` suite; the
`rwe` target and `rwe_setup` were rebuilt. These are RWE tests of RWE-original
behaviour, not original-game execution. `rwe_setup --resolve-mods` is the
offline dry-run entry point; it prints the resolved set and its identity as
JSON, and exits non-zero with the specific error when a set cannot load.

## Deferred, and explicitly not claimed

- Archive-embedded manifests, TDF adapters and ADR-0002 D4 reconciliation.
- Content hashing of entry scripts, loose files and archives into the identity
  (MOD-03/MOD-05).
- Lua or wasm execution, sandboxing, determinism and fault policy (MOD-04).
- Save, replay and multiplayer carrying the identity (MOD-06).
- Launcher Library presentation and author commands (MOD-07).
- Any claim of compatibility with OAMOD, OAMUT, OABrain or another engine's
  manifest semantics.
