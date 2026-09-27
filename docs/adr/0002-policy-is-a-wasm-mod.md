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

Escalation's own patch is the evidence for where the line goes. Of its eight
behavioural changes, one is a constant (the AI handicap, `-0.7` to `-2.0`), one
is a new FBI flag gating an existing behaviour (repair-pad seeking), two are a
condition flipped (the expired ballistic round, the clamp at `0x41BD90`), three
change a decision (D-gun completion, the reclaim gate, the attacked reaction)
and one adds an order type. None of them touches movement, pathfinding,
collision or the economy settle. Modders want to change *decisions*, which fit
a small interface, and seldom the machinery beneath them, which does not.

## Decisions

**D1 -- Mechanism stays native; policy becomes wasm.** Mechanism is the tick
loop, movement integration, pathfinding, collision, the projectile physics
step, the COB VM, the economy settle, save, hash, networking and rendering.
Policy is every decision a hook covers: whether an aircraft seeks a pad, how a
unit answers an attack, when a mission completes, whether a round detonates,
the damage a hit does, what an order does each tick, and the computer player.
Moving mechanism would force the ABI to expose nearly every engine internal,
and then it could not stay stable while fidelity work changes those internals
weekly.

**D2 -- TA's policy is the `ta-base` mod, and it ships as wasm.** It is loaded
through the same path as any other mod, as the bottom layer of every fold
(D5). A mod such as Escalation layers on top of it; a total conversion
replaces it. This keeps the ABI honest: a hook exists only once TA's own
behaviour runs through it, so a gap in the ABI is found by `ta-base` before it
is found by a mod author.

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

**D5 -- Every hook has one composition rule, fixed when the hook is defined.**

| Kind | Rule | Examples |
|---|---|---|
| Fold | `ta-base` is the bottom layer. Each mod in load order receives the layer below's result as `prev` and returns the new one. | Seek a repair pad, attacked reaction, damage amount, round detonates |
| Event | Every mod is called in load order. They observe only; no return value. | Unit created, finished, killed; tick |
| Keyed exclusive | One owner per key. A second claim is a load error unless its manifest says `replaces`. | A named order type; the computer player, chosen per player in the lobby |
| Presentation | Its own instance outside the sim, with read-only imports. | Interface and overlays; not in v0 |

A fold hook cannot call into the layer below and skip the rest. `prev` covers
overriding, vetoing and adjusting, and a single pass in load order is simple to
reason about.

**D6 -- Load order belongs to the launcher.** It is the same on every peer and
covered by the lobby's check that every player has the same data, exactly as
archive order is now. Wasm modules ship inside a mod's archives, so each
player's mod fingerprint covers them without a new mechanism. Which mods a game
runs, including in TA-compatible network games, is also the launcher's
decision; the engine loads what it is told.

**D7 -- The ABI is plain core wasm and hides engine internals.** It uses a
C-style calling convention with `i32`, `i64` and `f32` only, and is versioned
by an exported `rwe_abi_version()`. Host functions are imported from the `rwe`
namespace; hooks are exports named `hook_*`, `on_*`, `order_*` and `ai_*`.
Every engine object is an opaque `u32` id. No struct layout crosses the
boundary, so a change to `UnitState` never breaks a mod. `SimScalar` is `f32`,
angles are `i32` in TA's 65,536ths, vectors are written to a guest pointer, and
strings appear only in name lookups, never on a per-tick path. The WebAssembly
component model is not used.

**D8 -- Mod state lives in the mod's linear memory, which is saved and hashed,
and in the engine-owned store (D14).** A mod needs no save or load callbacks;
its memory is its state. The store is how one mod reads what another has set.
`ta-base` prefers engine-owned state, in `UnitState` and the store, over its
own memory, because state held there appears in desync dumps and other mods
can read it. It is a preference, not a rule: the computer player will need
real state of its own.

**D9 -- Inside a hook, decisions are return values and side effects are
deferred.** A write a hook makes (a command, a var) is queued and applied at a
fixed point in the tick. No host import calls into another hook, so mods cannot
recurse into each other and their effects never depend on who ran first.

**D10 -- Each mod draws from its own random stream**, seeded from the game
seed and the mod's hash, through `rwe_rand_below`. Drawing from
`simulation.rng` would mean that adding a mod which only observes shifts
every later roll in the game.

**D11 -- A mod can read any FBI or TDF key.** `rwe_type_key_i32`, `_f32` and
`_str` read a key by name, with a default. This is how a mod adds a unit tag,
as Escalation added its repair-pad flag, with no engine change. It requires
the parser to keep each type's `TdfBlock`; `parseUnitInfoBlock` currently
reads the keys it knows and drops the rest.

**D12 -- A mod that faults is disabled, not the game.** Every hook call runs
under a deterministic fuel budget. A trap or exhausted fuel disables that mod
on every peer at the same tick; its fold layers then pass `prev` through
unchanged and it receives no more events. A fault in content costs that
content, as `docs/SECURITY-AUDIT.md` requires. Mods import nothing that
reaches the clock, the filesystem or the network, and NaNs are canonicalised.

**D13 -- In a TA-compatible network game, fold hooks run only for units this
machine owns.** TA is owner-authoritative: each machine decides for its own
units, and the rest are puppets driven by their owners' packets. A decision
hook running on a puppet would fight its owner's stream. The one exception is
damage, which the attacker computes and sends as `0x0b`. Events still fire for
every unit. The dispatch that picks local units is the same split as phase 4
of the TA network work (#386), and the two should be designed together.

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
- **Snapshot reads.** A read sees the store as it stood at the start of the
  phase, and writes land at D9's fixed point, so no mod sees another's write
  from the same phase. A mod's scratch data belongs in its linear memory.
- **Ordering.** Keys are kept sorted, so every peer iterates the store alike.
- **Quotas.** Bytes and entries per mod; exceeding one is a D12 fault.

The store is saved, hashed, dumped and carried in the rejoin bundle like any
other sim state. It joins `GameHash`'s sum with one term per entry, kept up to
date on each write by subtracting the old term and adding the new, and the
desync report gives one sub-hash per namespace, which names the mod whose state
diverged. Mod state stays small enough that walking all of it on a desync is
cheap, so nothing finer is needed.

## The v0 surface

The initial hooks are chosen so that every Escalation patch is either a data
key or one of them. That is the acceptance test for v0.

| Export | Kind |
|---|---|
| `rwe_abi_version() -> i32`, `rwe_init() -> i32` | lifecycle |
| `hook_seek_repair_pad(unit, prev) -> i32` | fold |
| `hook_attacked_reaction(unit, attacker, prev) -> i32` | fold |
| `hook_mission_complete(unit, mission, prev) -> i32` | fold |
| `hook_reclaim_allowed(builder, target, prev) -> i32` | fold |
| `hook_round_expired_detonates(proj, prev) -> i32` | fold |
| `hook_weapon_motor(proj, prev) -> i32` | fold |
| `hook_damage(weapon_type, attacker, victim, amount, prev) -> f32` | fold |
| `on_tick(tick)`, `on_unit_created/finished(unit)`, `on_unit_killed(unit, killer, cause)` | event |
| `order_step(unit, order_kind, arg_ptr) -> i32` | keyed exclusive |
| `ai_init(player)`, `ai_tick(player, tick)` | keyed exclusive |

Host imports: `rwe_tick`; `rwe_unit_exists/owner/type/health/max_health/build_progress/heading/flags`
and `rwe_unit_position(unit, out)`; `rwe_player_allied`,
`rwe_player_resources(p, out)`; `rwe_type_by_name`, `rwe_type_key_*`;
`rwe_units_in_radius(out, x, z, r, cap)`, sorted by id; `rwe_rand_below`;
`rwe_store_key`, `rwe_store_get`, `rwe_store_set`, `rwe_store_del`,
`rwe_store_scan`; `rwe_var_register`, `rwe_unit_get_var`, `rwe_unit_set_var`;
`rwe_order_register`; `rwe_cmd_*`, the same commands a player can issue; and
`rwe_log`, which is outside the sim.

Constants, such as the AI handicap, are data read through D11, not hooks.

## Migration

1. The runtime, the manifest, the load order and fold dispatch, with no hooks.
2. One policy that runs rarely, repair-pad seeking, moves into `ta-base`. Its
   existing test passes unchanged against the wasm build.
3. One policy that runs for every unit on every tick, damage, moves next, and
   is measured with `battle_test --units 200` under `RWE_ENABLE_SIMPROF`.
   **This is the go/no-go on performance.** If the cost is too high, per-tick
   hooks get a batched form that takes an array of units, or stay native, and
   only rare decisions move.
4. From then on, a new hook is added together with TA's version of it in
   `ta-base`. The computer player is the largest candidate. It already reaches
   the sim only through commands, but it is also expensive, so it waits for
   step 3's numbers.

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
- **A Merkle tree over the store.** It would buy incremental hashing, finding
  the differing key between peers in O(log n), and sending only the parts
  that differ on a rejoin. The first comes free from `GameHash` being a sum;
  the other two answer a state too large to walk, and mod state never will be.

## Consequences

- CI needs a wasm toolchain, such as wasi-sdk, on all four platforms to build
  `ta-base.wasm`.
- The runtime is not chosen here. WAMR is plain C, builds everywhere and has
  interpreter, ahead-of-time and instruction-metering modes; wasmtime is more
  mature on determinism and needs a Rust C-API dependency. The prototype
  decides, and neither may appear in `GameSimulation.h`. It sits behind a
  forward declaration, for the section budget.
- A fidelity fix to a policy is a change to `ta-base`, not to engine C++, and
  a conformance test covers `ta-base` and the engine together. That is what
  ships, so it is the right thing to test.
