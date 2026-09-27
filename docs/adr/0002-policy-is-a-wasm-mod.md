# ADR-0002: Game policy runs as WebAssembly mods, and TA's own policy is the first of them

Modders want to change behaviour, not just data. TA: Escalation had to ship a
byte-patched `TotalA.exe` to do it (`docs/TA-PATCHES.md`), and a native plugin
would be unsafe to load from a community archive and non-deterministic across
compilers. **RWE splits the engine into mechanism, which stays native, and
policy, which runs as sandboxed WebAssembly mods behind a narrow, versioned
ABI. TA's own policy is the bottom mod, `ta-base`, and it ships as wasm.** A
native build of the same source is kept as a local debugging option.

**Status**: proposed

## Why this shape

Escalation's own patch is the evidence for where the line goes. `TA-PATCHES.md`
and `TOTALA-EXE-WEAPONS.md` list nine behavioural changes:
- one constant: the AI handicap, `-0.7` to `-2.0`;
- one new FBI flag gating an existing behaviour: repair-pad seeking;
- two flipped conditions: the expired ballistic round, and the clamp at
  `0x41BD90`;
- one swap between existing weapon motor functions;
- three changed decisions: D-gun and `commandfire` completion, the reclaim
  power-switch gate, and the reaction to being attacked;
- one new order type, inferred from the mission-state renumbering.

None of them touches movement, pathfinding, collision, the damage pipeline or
the economy settle; all of those are confirmed unpatched byte for byte.
Modders want to change *decisions*, which fit a small interface, and seldom
the machinery beneath them, which does not. "The v0 surface" below maps each
of the nine changes to what covers it.

## Decisions

**D1 -- Mechanism stays native; policy becomes wasm, and a hook chooses while
the engine acts.** Mechanism is the tick loop, movement integration,
pathfinding, collision, the projectile physics step and its motor functions,
the damage pipeline, the COB VM, the economy settle, save, hash, networking and
rendering. Policy is the choice between the behaviours mechanism offers:
- whether an aircraft seeks a pad;
- which reaction a unit makes to an attack;
- when a D-gun order is done;
- whether an expired round detonates;
- which motor a round flies on;
- the final damage of a hit;
- the computer player.

A hook returns a choice and the engine carries it out, which is why swapping
a motor function or picking a reaction is policy even though the motors and
the reactions themselves are mechanism. Moving mechanism would force the ABI to
expose nearly every engine internal, and then it could not stay stable while
fidelity work changes those internals weekly.

**D2 -- TA's policy is the `ta-base` mod, and it ships as wasm.** It is loaded
through the same path as any other mod, always in the base slot, as the bottom
layer of every fold (D5). A mod such as Escalation layers on top of it; a total
conversion replaces it. **TA's arithmetic for a migrated policy lives in
`ta-base`, not in the engine.** The fold starts with the hook's declared seed
(listed in "The v0 surface"), a neutral value that `ta-base` ignores; the
engine holds no second copy of TA's answer. This keeps the ABI honest: a hook
exists only once TA's own behaviour runs through it, so a gap in the ABI is
found by `ta-base` before it is found by a mod author.

**D3 -- `ta-base` can also be built natively, for debugging.** Debuggers,
sanitizers and profilers work poorly or not at all on wasm, so a CMake option,
off by default, links `ta-base`'s source directly against the host imports.
It is for one situation: something has gone wrong, and the developer rebuilds
locally to reproduce it under gdb. The tests and CI run the wasm build, because
that is what ships. The two builds are not required to hash alike, and a native
build need not load a save made by the wasm one; a replay re-runs from the
start, so it reproduces without either. The native build compiles `ta-base`
with floating-point contraction off, so that a bug which does not reproduce
under it points at the difference between the builds, which is itself the
lead.

**D4 -- One wasm instance per mod, each with its own memory.** Mods do not
share memory and cannot patch one another. They compose through the hook rules
in D5 and communicate through the engine-owned store (D14). A mod may
export hooks from any subsystem. Its manifest declares which, so the launcher
can list what each mod touches, and where two conflict, without running
anything.

The manifest, `modinfo.tdf`, lives **inside one of the mod's archives**, beside
its `.wasm`, so that the mod fingerprint covers it. A loose file in the mod
directory, as `rwe_mod.json` is, is outside the fingerprint: two peers could
agree on every archive and still compose their mods differently. It declares:
- the wasm modules;
- each exported hook with its version;
- `requires`: a mod name with a version range, for a mod that reads another's
  store format (D14);
- `after` and `before` load-order constraints;
- for each keyed-exclusive key it takes over, `replaces = <mod>:<key>`. That
  names one key of one mod, never everything that mod claims.

**D5 -- Every hook has one composition rule, fixed when the hook is defined.**

| Kind | Rule | Examples |
|---|---|---|
| Fold | Starts from the hook's seed; `ta-base` is the first layer. Each mod in load order receives the layer below's result as `prev` and returns the new one. | Seek a repair pad, attacked reaction, final damage, round detonates |
| Event | Every mod is called in load order. They observe only; no return value. | Unit created, finished, killed; tick |
| Keyed exclusive | One owner per key. A second claim is a load error unless its manifest says `replaces`. | Mod orders (D16) and the computer player, chosen per player in the lobby (D17); neither is in v0 |
| Presentation | Its own instance outside the sim, with read-only imports. | Interface and overlays; not in v0 |

A fold hook cannot call into the layer below and skip the rest. `prev` covers
overriding, vetoing and adjusting, and a single pass in load order is simple to
reason about.

**D6 -- Load order belongs to the launcher, and it is compared, not
implied.** The launcher works the order out from the player's mod list and the
manifests' `requires`, `after` and `before`, and refuses a list those cannot
satisfy. The host sends the resulting ordered list with the game. The lobby
compares it as an ordered list of fingerprints, so two peers with the same
mods in different orders do not start. Today's lobby compares archive bytes,
not order, so this is new. Wasm modules and manifests ship inside a mod's
archives, so the existing fingerprint covers their bytes. Which mods a game
runs, including in TA-compatible network games, is also the launcher's
decision; the engine loads what it is told.

**D7 -- The ABI is plain core wasm and hides engine internals.** It uses a
C-style calling convention with `i32`, `i64` and `f32` only. Host functions are
imported from the `rwe` namespace; hooks are exports named `hook_*`, `on_*`,
`order_*` and `ai_*`. Every engine object is an opaque `u32` id. No struct
layout crosses the boundary, so a change to `UnitState` never breaks a mod.
`SimScalar` is `f32`, angles are `i32` in TA's 65,536ths, and vectors are
written to a guest pointer.

- **Bytes and strings out.** Any import that returns bytes or a string takes a
  guest `(out, cap)` and returns the full length, or -1 when there is nothing
  to return. It writes at most `cap` bytes, so a mod whose buffer was too small
  calls again with a bigger one. The host holds no buffer between calls, so
  there is nothing stale to read.
- **Strings in** appear only in name lookups, never on a per-tick path.
- **Versioning is per hook, in the export name.** A hook is exported as
  `hook_damage_v1`, `hook_damage_v2`, and so on. The engine documents the
  versions it accepts for each hook, calls the newest one a mod exports, and
  refuses to load a mod that exports only versions it no longer accepts,
  naming the hook, as D15 does. A signature change is therefore a new version
  and a decision about how long to accept the old one, never a silent change
  in arity. `rwe_abi_version()` versions only the imports.

The WebAssembly component model is not used.

**D8 -- A mod's linear memory is saved and dumped but not hashed; the store is
hashed.** A mod needs no save or load callbacks; its memory is its state.
Memory is saved-but-unhashed state, as weapon aim state already is. What a mod
decides lands in hashed state (unit state, the store, commands), so a
divergence in its memory reaches the hash the moment it matters, and hashing
megabytes of memory on every tick buys nothing more. Each mod has a
linear-memory quota in pages. It is checked twice: against the module's
declared initial memory when it is loaded, and on every `memory.grow`, which
returns failure past the quota. A mod that traps on that failure faults under
D12. `ta-base` prefers engine-owned
state, in `UnitState` and the store, over its own memory, because state held
there is hashed, appears in desync dumps and can be read by other mods. It is
a preference, not a rule: the computer player will need real state of its own.

**D9 -- Inside a hook, decisions are return values and side effects are
deferred.**
- **Writes.** A mod reads its own namespace live, and sees its own writes at
  once. That is deterministic, because only that mod writes there and its
  calls come in a fixed order. It is also what a mod needs for anything that
  accumulates: a shield taking two hits in one tick must see the first hit's
  deduction when the second arrives. Other mods see a namespace as it stood
  at the start of the phase, and its writes become visible to them at the end
  of the phase. So no mod's view of another depends on call order within the
  phase.
- **No recursion.** No host import calls into another hook, so mods cannot
  recurse into each other, and their effects never depend on who ran first.
- **Commands.** Only the computer player's hooks (`ai_*`, D17) issue player
  commands, and they take exactly the native AI's path. Its commands are
  collected in the simulation and handed out by `takeAiCommandsForPlayer`,
  then queued by `feedAiCommands` at the constant `aiCommandBufferDepth()`
  and behind `onlyComputerPlayersAreNotReady`. A command therefore never
  enters the network-derived buffer depth whose frame-dependence caused the
  tick-44 desync.
- **Effects.** No other hook issues commands. Besides its result and its
  store writes, a hook has one effect in v0: `rwe_unit_apply_damage(unit,
  amount, attacker)`. It is queued and applied at the end of the phase through
  the native damage pipeline, so a shield can charge what it absorbed to its
  own health. Further effects, such as spawning a feature, arrive with mod
  orders (D16).

**D10 -- Each mod draws from its own random stream, except `ta-base`, whose
stream is `simulation.rng`.** `rwe_rand_below` reduces the stream's raw output
by modulo, as `SimRandom.h`'s `randomBelow` does. A layered mod's stream is
seeded from the game seed and the mod's hash. It is saved and hashed, and it is
its own stream so that adding a mod which only observes does not shift every
later roll in the game. `ta-base` draws from `simulation.rng` because the
native policy it replaces does. Ten draw sites in `UnitBehaviorService.cpp`
alone would otherwise change the sequence, and with it every replay and hash
baseline. The payoff is that **migrating a policy is hash-neutral**: moving a
decision from C++ into `ta-base` must leave a same-seed `RWE_HASH_LOG`
byte-identical, and that is the check each migration passes.

**D11 -- A mod can read any FBI or TDF key, for units and for weapons.**
`rwe_type_key_*` and `rwe_weapon_key_*` read a key by name, as `i32`, `f32` or
string, with a default. This is how a mod adds a unit tag, as Escalation added
its repair-pad flag, with no engine change. It requires the parsers to keep
each definition's `TdfBlock`; `parseUnitInfoBlock` currently reads the keys it
knows and drops the rest. `TdfBlock` properties are an unordered map, so any
import that iterates keys returns them sorted.

**D12 -- A mod that faults is disabled, and its disabling is sim state.**
- **Fuel.** Every hook call runs under a fuel budget counted in executed wasm
  instructions. That count is a property of the module and its inputs, not of
  the runtime. The sim uses a single execution mode, and a runtime mode that
  cannot count exactly is not used for it. Peers already run the same engine
  build.
- **What disables a mod.** A trap, exhausted fuel, a store or memory quota
  exceeded, or a non-finite `f32` returned across the boundary.
- **What disabling does.** It happens on every peer at the same tick. The
  mod's fold layers then pass `prev` through unchanged, and it receives no
  more events.
- **The disabled set is hashed, saved and dumped** like any other sim state.
- **NaNs never reach the hash.** Returned floats are checked at the
  boundary, and a non-finite one is a fault, so no NaN reaches
  `computeHashOf(float)`, whose cast to `uint32_t` is undefined for one. A
  fault in content costs that content, as `docs/SECURITY-AUDIT.md` requires.
- **`ta-base` is the exception.** No policy lies below it, so a fault in
  `ta-base` ends the game, deterministically, on every peer.
- **No outside world.** Mods import nothing that reaches the clock, the
  filesystem or the network.
- **The boundary is untrusted input**, under `docs/SECURITY-AUDIT.md`'s rules,
  because a mod is content from a community archive:
  - every guest pointer and length an import receives is bounds-checked
    against the instance's memory before the host reads or writes through it;
  - every `cap` is clamped to what fits;
  - no import trusts a length the guest supplies.
- **Loading is bounded too, since fuel only covers execution.** The loader
  caps:
  - a module's size;
  - its function, table and global counts;
  - its declared memory (D8);
  - the time validation and instantiation may take.

  A module that exceeds a cap is refused at load, before the game starts.
  Every peer loads the same bytes, so every peer refuses it alike.

**D13 -- Lockstep runs every hook for every unit on every peer. In a
TA-compatible network game, fold hooks run only for units this machine owns.**
In lockstep each peer simulates every unit, so every peer runs every hook, and
nothing here changes that. TA's own network model is owner-authoritative: each
machine decides for its own units, and the rest are puppets driven by their
owners' packets. A decision hook running on a puppet would fight its owner's
stream. The one exception is damage, which the attacker computes and sends on
the live wire as `0x0b` (#386, step 6). Events still fire for every unit. The
dispatch that picks local units is the same split as phase 4 of the TA network
work, and the two should be designed together.

**D14 -- Mods share state through an engine-owned store: the owner writes,
everyone reads.**
- **Namespaces.** Each mod writes only under its own namespace, so load order
  never decides who wins a key. A mod that wants others to change its state
  exposes a hook and makes the change itself.
- **Scopes.** Entries are global, per player or per unit. Per-unit entries are
  deleted with their unit. A per-unit `i32` read every tick has a fast path:
  a var, registered once by name and then addressed by id.
- **Values.** Opaque bytes, with a type tag (`i32`, `f32`, `string`, `bytes`)
  that exists so desync dumps can print them. The engine does not interpret
  them; a mod publishes its own value format for others to read.
- **Reads.** A mod reads its own namespace live and everyone else's as of the
  start of the phase (D9). So state that accumulates within a tick, such as a
  shield's charge, can live in the store, where it is hashed and other mods can
  read it. A mod's scratch data belongs in its linear memory.
- **Ordering.** Keys are kept sorted, so every peer iterates the store alike.
- **Quotas.** Bytes and entries per mod; exceeding one is a D12 fault.

The store is saved, hashed and dumped like any other sim state. A rejoining
peer rebuilds it by re-running the rejoin bundle, which is a replay, as it
rebuilds everything else. The hash walks the store when it is computed, in key
order, hashing each key and value with a real byte hash. `computeHashOf` on a
string is a byte sum, under which `"ab"` and `"ba"` collide. Nothing is
maintained incrementally, so no missed update can leave two identical stores
hashing differently. The desync report gives one sub-hash per namespace, which
names the mod whose state diverged. Mod state stays small enough that walking
all of it is cheap.

**D15 -- The mod set is part of every save and every replay.** A save or
replay records each mod's fingerprint and the load order, and loading one under
a different set is refused with a message naming the difference. Loading it
anyway would succeed and then diverge at the first fold, which is the worst
failure to debug. A mod is never added to or removed from a game in progress:
calling `rwe_init` on a running world is not something every peer can agree
to.

**D16 -- Mod-defined orders are not in v0.** `UnitOrder` is a closed variant
of fifteen native orders. Each one is saved, hashed and dumped alternative by
alternative, and per-order state, such as capture progress, is fields on
those structs. The shape a mod order will take:
- **Storage.** One new `ModOrder` alternative carries the owning order kind and
  an opaque byte payload, saved, hashed and dumped as bytes.
- **Registration.** Kinds are registered by name, and each is keyed exclusive.
- **Queueing.** A new `PlayerCommand` alternative queues it. A player issues
  it from an order button declared in data: a button naming the order kind and
  a cursor mode. The computer player queues it as it queues any other order.
- **Stepping.** `order_step(unit, kind, payload)` runs once per tick while the
  order is at the front of the queue. It returns running, done or failed, and
  may choose among effects the engine offers through deferred imports: a move
  goal, a weapon target, a build, and placing a feature or unit, which a mine
  layer needs.

This adds two variant alternatives, `ModOrder` and its command, which is the
kind of growth the section budget in `CLAUDE.md` warns about: one
`PlayerCommand` alternative cost 464 sections, and `GameSimulation.cpp` had
244 to spare when last measured. So this waits until after v0, and until #396
has recovered room for both alternatives. Escalation's new order type waits
with it.

**D17 -- A wasm computer player is not in v0.** The native AI reads far more
than the v0 imports offer:
- every unit, through `PerceptionManager`;
- visibility, through `canSeeUnit` and `isVisibleTo`;
- unit definitions and build options;
- terrain and reachability;
- player records.

A replacement AI needs perception imports of its own:
- units by owner, filtered by what the player can see;
- map bounds and terrain height;
- passability for a movement class;
- a type's build options.

Those form their own surface, designed with the first wasm AI rather than
guessed at now. `ai_init`, `ai_tick` and `rwe_cmd_*` arrive with it, on the
command path D9 already fixes.

## The v0 surface

**Acceptance test:** every decoded Escalation behaviour is a data key or a v0
hook. Two are named gaps: the new order type (D16), and the `0x41BD90` clamp,
whose routine is not identified. A hook cannot be named for a computation
nobody has decoded, so that one waits for the decode.

| Escalation change | Covered by |
|---|---|
| AI handicap `-0.7` → `-2.0` | A data key, read by the AI |
| Repair-pad seeking gated on a new flag | `hook_seek_repair_pad`, plus the flag through `rwe_type_key_*` |
| Expired ballistic round: `burnblow` to `noautorange`, inverted | `hook_round_expired_detonates`, plus `rwe_proj_weapon` and `rwe_weapon_key_*` |
| Weapon motor-function swap | `hook_weapon_motor` (what the swap selects is located but not decoded) |
| D-gun and `commandfire` completion | `hook_mission_complete` |
| Reclaim power-switch gate | `hook_reclaim_allowed` |
| Attacked and kamikaze reaction cluster | `hook_attacked_reaction` |
| New order type (mission renumbering) | **Gap:** D16 |
| Clamp at `0x41BD90` | **Gap:** routine not identified |

Each fold hook:

| Hook | Seed | What the engine does with the result | How often |
|---|---|---|---|
| `hook_seek_repair_pad(unit, prev) -> i32` | 0 | Nonzero: break off and search for a pad, as now | Every aircraft, every tick |
| `hook_attacked_reaction(unit, attacker, prev) -> i32` | 0, none | Carries out the chosen reaction: none, return fire (sets the weapon target), move away, or detonate if kamikaze | Per damage event |
| `hook_mission_complete(unit, order, prev) -> i32` | 0 | Nonzero: pops the order. v0 calls it only for D-gun orders and `commandfire` attacks, whose completion is a predicate over state the order imports can read. Another order is added to the hook, with imports for its state, when a mod needs it | Every tick, for units with such an order at the front |
| `hook_reclaim_allowed(builder, target, prev) -> i32` | 1 | Zero: the reclaim does not start | Per reclaim attempt |
| `hook_round_expired_detonates(proj, prev) -> i32` | 0 | Nonzero: detonates the round where it is | Per round expiry |
| `hook_weapon_motor(proj, prev) -> i32` | The weapon's declared motor | Runs that native motor this tick | Every guided round, every tick |
| `hook_damage(weapon, attacker, victim, amount, paralyzer, prev) -> f32` | `amount` after the native pipeline | Applies the result; `ta-base` returns `prev` | Per impact, per victim |

`hook_damage` adjusts the output of the native damage pipeline: veterancy,
`DamageModifier` and armour. It does not replace that pipeline, which
Escalation left untouched and is integer arithmetic pinned to the original.
It is in v0 because it is the hook modders want first, for shields and armour
types.

Every hook is exported as its `_v1` form (D7), such as `hook_damage_v1`.

Other exports: `rwe_abi_version() -> i32` and `rwe_init() -> i32` (lifecycle);
`on_tick(tick)`, `on_unit_created(unit)`, `on_unit_finished(unit)` and
`on_unit_killed(unit, killer, cause)` (events). Nothing keyed exclusive is in
v0: mod orders are D16 and the computer player is D17.

Host imports:

- **Game and units:**
  - `rwe_tick`;
  - `rwe_unit_exists`, `_owner`, `_type`, `_health`, `_max_health`,
    `_build_progress`, `_heading`, `_flags`, `_kills` and
    `rwe_unit_position(unit, out)`;
  - `rwe_unit_order_kind`, `rwe_unit_order_target(unit, out)` and
    `rwe_unit_order_fired`, for the order at the front;
  - `rwe_units_in_radius(out, x, z, r, cap)`, sorted by id.
- **Projectiles:** `rwe_proj_weapon`, `_owner_unit`, `_age`, and
  `rwe_proj_position(proj, out)` and `rwe_proj_velocity(proj, out)`.
- **Players:** `rwe_player_allied`, `rwe_player_resources(p, out)`.
- **Definitions:** `rwe_type_by_name`, `rwe_weapon_by_name`,
  `rwe_type_key_*`, `rwe_weapon_key_*`.
- **Randomness:** `rwe_rand_below`.
- **Store:** `rwe_store_key`, `_get`, `_set`, `_del`, `_scan`;
  `rwe_var_register`, `rwe_unit_get_var`, `rwe_unit_set_var`.
- **Effects:** `rwe_unit_apply_damage(unit, amount, attacker)`, deferred (D9).
- **Outside the sim:** `rwe_log`.

Every import that returns bytes or a string, including `rwe_type_key_str`,
`rwe_store_get` and `rwe_store_scan`, uses D7's `(out, cap) -> length`
convention.

## Migration

0. **Choose the runtime.** A spike builds WAMR and wasmtime on the MinGW64 and
   MSVC runners, checks that each counts executed instructions exactly in the
   mode the sim would use (D12), and picks one.
1. **Infrastructure.** The runtime, the manifest, the ordered load list, fold
   dispatch, and D15's refusal on a mismatched mod set, with no hooks yet.
   `rwe_test` links the runtime and loads the CI-built `ta-base.wasm`, and the
   `sim_test_util.h` helpers load it by default, so every existing test runs
   the shipped policy. The mod SDK ships in the same step, because an ABI
   nobody outside the repository can build against is not yet tested by its
   users:
   - a public header of the imports;
   - a stub library for D3's native build;
   - `rwe_test` and `ai_arena` options that load a named mod on top of
     `ta-base`.
2. **A rare policy, `hook_round_expired_detonates`.** It moves into `ta-base`.
   Its tests pass unchanged, and a same-seed `RWE_HASH_LOG` is byte-identical
   before and after the move (D10).
3. **The per-tick policies, `hook_seek_repair_pad` and `hook_weapon_motor`.**
   They move next, measured with `battle_test --units 200` under
   `RWE_ENABLE_SIMPROF`: once with an aircraft type, and once with a type that
   fires guided rounds. **This is the go/no-go on performance.** If the cost is
   too high, per-tick hooks get a batched form that takes an array of units,
   or stay native, and only rare decisions move.
4. **The rest of v0**, each with the same hash-neutral check. After v0, a new
   hook is added together with TA's version of it in `ta-base`. Mod orders
   (D16) and the computer player (D17) are the largest pieces. The computer
   player already reaches the sim only through commands, but it is also
   expensive, so it waits for step 3's numbers.

The findings travel with the code. A routine's `0x` addresses and § numbers
move into `ta-base`'s source as comments at the point of use, as they sit in
engine source now. `TOTALA-EXE.md` §88 and §91 stay the single register; an
entry about a migrated policy names the `ta-base` file that holds it.

## Considered options

- **Everything in wasm, the base game included.** Rejected by D1. Movement,
  pathfinding and projectiles are hot per-unit loops, where wasm and every
  host call cost real time; the whole test suite and the debugger work on
  native code; and the ABI would have to expose everything.
- **Everything stays native, and mods only add to it.** Rejected by D2. The
  engine's default and the modding surface would drift apart, and nothing
  would show that the ABI can express the game it ships with.
- **One wasm blob per game, holding every mod.** Rejected by D4. Players layer
  mods, and a single blob makes each combination a separate build.
- **Lua, or another embedded scripting language.** Layering comes from how
  hooks compose, which D5 fixes, not from the language. Lua's own help with
  layering is patching functions at runtime, which is what makes load order
  fragile. Lockstep engines that embed Lua have had to patch it for
  determinism, starting with table iteration order. A Lua interpreter compiled
  to wasm can ship as a mod later, and scripts run on it inherit the sandbox,
  the fuel budget and the determinism.
- **Native plugins (DLLs).** Unsafe to load from a community archive, and each
  compiler's floating point would be a desync risk.
- **Hashing each mod's linear memory every tick.** Rejected by D8. The cost
  grows with memory, not with state that matters, and a divergence in memory
  reaches the hash through what the mod decides anyway.
- **Maintaining the store's hash incrementally on each write.** Rejected by
  D14. A missed subtraction, such as a per-unit entry dropped with its unit,
  leaves identical stores hashing differently, and the store is small enough
  to walk.
- **A Merkle tree over the store.** It would buy incremental hashing, finding
  the differing key between peers in O(log n), and sending only the parts that
  differ on a rejoin. None is needed: the store is walked, and a rejoin
  replays.
- **A separate random stream for `ta-base`.** Rejected by D10. It would move
  every replay and hash baseline, and lose the hash-neutral check on
  migration.

## Consequences

- CI needs a wasm toolchain, such as wasi-sdk, on all four platforms to build
  `ta-base.wasm`.
- The runtime is not chosen here. WAMR is plain C, builds everywhere and has
  interpreter, ahead-of-time and instruction-metering modes; wasmtime is more
  mature on determinism and needs a Rust C-API dependency. D12 constrains the
  choice: the mode the sim runs in has to count executed instructions exactly.
  The prototype decides, and neither may appear in `GameSimulation.h`. It sits
  behind a forward declaration, for the section budget.
- A fidelity fix to a migrated policy is a change to `ta-base`, not to engine
  C++, and a conformance test covers `ta-base` and the engine together. That is
  what ships, so it is the right thing to test.
