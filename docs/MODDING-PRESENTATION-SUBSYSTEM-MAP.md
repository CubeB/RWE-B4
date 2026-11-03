# Presentation and subsystem modding: implementation map

Working map for roadmap MOD-10 and MOD-11, produced from a read-only survey of
the tree on 2026-11-02. It is a plan, not an implemented capability. Paths are
relative to the repository root; line numbers are from the survey and must be
re-checked after a rebase.

## MOD-10: presentation modding

### The boundary

Presentation may read the simulation and never write it (the rule already
stated in `src/rwe/sim/GameSimulation.h`). A mod API must expose a **filtered
player view**, not `GameSimulation&`, because several public fields leak hidden
state:

- `simulation.units` and `simulation.projectiles` expose every unit and round
  regardless of fog, cloak, sonar or waterline;
- `simulation.playerVisibility` exposes every player's vision;
- `simulation.explored` is a shared LOS-group bitmask and needs
  `losGroupBitFor`;
- `simulation.rng` must never be reachable from presentation;
- `GameScene::getPlayer` exposes enemy stockpiles.

Reuse the existing gates rather than reimplementing them:

- `unitIsVisibleToLocalPlayer` (`GameScene_commands.cpp`)
- `unitIsDetectableByLocalPlayer` (radar/sonar contacts)
- `positionIsExploredByLocalPlayer`, `positionIsVisibleToLocalPlayer`
- `effectIsVisibleToPlayer` (`GameScene_util.h`)
- the picking filters already in `getUnitUnderCursor` / `getFirstCollidingUnit`

A `PlayerView` should return: own units and fully visible units with id, type,
owner/team, position/heading and health; radar/sonar contacts with no name or
health; explored versus visible features; projectiles only where visible; own
resources, allied resources only as the native UI already shows them; camera
and selection as plain values; and a command sink.

### Where it attaches

| Surface | Existing code | Smallest change |
| --- | --- | --- |
| Panel host | `UiFactory::panelFromGuiFile`, `UiPanel`, `GameScene.cpp` `currentPanel` swap | Add an overlay `UiPanel` beside the native panel, or replace through `setNextPanel` |
| Panel input | `GameScene_input.cpp` `currentPanel->mouseDown/move/up` | Dispatch the mod panel first or after the native panel |
| Render | `GameScene_render.cpp` `renderUi`, `renderOverlay`, `renderWorld` | Consume a `PresentationDrawList` in `renderOverlay`; append world batches after `renderWorld` |
| Commands | `localPlayerIssueUnitOrder` / `localPlayerEnqueueUnitOrder` → `localPlayerCommandBuffer` → `pushLocalCommandSet` / `tryTickGame` | A sink that only appends to the local command buffer; never write the sim |
| Picking | `getUnitUnderCursor`, `getFeatureUnderCursor`, `getMouseTerrainCoordinate` | Wrappers that preserve the visibility/cargo rules |
| Camera | `GameCameraState`, `computeCameraConstraint`, `GameScene::update` | A `CameraController` over camera state only |
| Minimap | `renderMinimap`, `updateMinimapPicture`, the two matrices | A `MinimapController` with hit-test and transforms |
| Audio | `GameScene_audio.cpp` play/notification/weapon/music paths | An `AudioPolicy` whose default returns today's handle or track |
| Reload | panel swap path, `runChatCommand` | Presentation-only developer reload; rebuild off to the side, keep the last good panel, never rebuild the simulation |

Megamap does **not** exist in RWE. Treat it as new presentation built on the
same filtered view; do not describe it as parity work.

### Constraints

- Keep the presentation runtime behind `std::unique_ptr` and a forward
  declaration in `GameScene.h`; no `std::function`, `BehaviorSubject`,
  `nlohmann::json` or container members by value in that header.
- Implement in a new `GameScene_presentation.cpp`; keep the include list tight.
- No new `PlayerCommand` alternative. Commands go through the per-tick feed.
- A component owning a `Subject` calls `releaseSubscriptions()` in its own
  destructor.
- A presentation mod must not change a sync hash merely by drawing. Load,
  reload, draw and input tests compare `RWE_HASH_LOG` with the mod enabled and
  disabled.

### Ordered work

1. Record the unmodded baseline (`tools/hash-oracle.sh record`, a scenario,
   `battle_test --units 200`, section-budget top ten).
2. Add a filtered `PlayerView` in a new header, implemented outside
   `GameScene.h`, using the existing gates.
3. Add the pimpl presentation runtime to `GameScene`.
4. Add the overlay panel host and input dispatch.
5. Add the command sink and filtered picking.
6. Add `PresentationDrawList` and route effects through existing spawn helpers.
7. Add camera/minimap controllers and the audio policy.
8. Add developer reload.
9. Enable `ModKind::Presentation` in the resolver and carry presentation
   options in the resolved set.
10. Add `scenario`/`ui_probe`/`battle_test` acceptance and hash comparison.

## MOD-11: subsystem replacement

### Recommended first provider: economy settle

`ResourceSettler.h/.cpp` already has a pure value interface
(`ResourceAccount` in, `SettledResourceAccount` out), runs once per game second
from `GameSimulation_economy.cpp`, owns no RNG and creates no entities. Its
state is the existing player/unit economy state, already saved and hashed.
Existing tests (`ResourceSettler.test.cpp`, `economy.test.cpp`,
`extractor.test.cpp`) and `tools/hash-oracle.sh` pin the arithmetic.

The first extraction should prove the contract, not the module runtime: the
default provider wraps the existing `settleResourceAccount` calls.

**Wind is the second provider.** It owns four values and draws from
`simulation.rng`, so it exercises per-provider RNG and state restoration, which
the economy provider does not.

Other candidates, later: feature regrowth (creates entities), mission rules
(complex state), cloak suppression/sensor contacts (per-tick visibility).

### Shape

- Forward-declare `class SubsystemProviders;` in `GameSimulation.h`, hold it by
  `std::unique_ptr`, define the setter out of line. Never include provider
  headers or `<nlohmann/json.hpp>` in that header.
- Provider contract (its own header, small value types and opaque ids only):
  id, kind, API and implementation version, input/output, update phase and
  ordering, state ownership, save/hash/dump hooks, fault policy, keyed-exclusive
  ownership, performance budget.
- Register a `"subsystems"` block beside `modState`/`modRuntime` in
  `save_util.cpp`, `GameHash_util.cpp` and `dump_util.cpp`. The default provider
  must contribute zero, so existing hashes do not move. A save naming a
  provider the set does not contain is refused with the first difference.
- Carry provider identity in the resolved set: `ModManifest` `exclusive` /
  `replaces`, `ModLaunchContent`, `GameParameters`, save and replay headers.

### Proof of no behaviour change

1. `tools/hash-oracle.sh record` then `compare`; byte-identical every tick.
2. Same-seed `scenario` and `battle_test` hash logs.
3. `RWE_STATE_DUMP` comparison; provider-only fields absent or identical.
4. Existing economy/ResourceSettler tests unchanged.
5. Save/resume and replay: same future, no double application.
6. `tools/section-budget.sh build 75`; the provider adds no include to
   `GameSimulation.h`.
7. Install a deliberately different provider, show the expected difference,
   restore the default, show the original log again.

### Module runtime

There is no wasm runtime in the tree. The Lua sandbox is not a substitute: it
executes Lua, not wasm. A provider module runtime needs a runtime choice
(WAMR/wasmtime), exact fuel metering, memory quotas and bounds checks, ABI
version dispatch, and linear-memory save/restore. Reuse `ModStateStore` for
state and the `ModRuntime` lifecycle/fault/budget pattern, not its interpreter.
This is a separate spike after the default-provider proof.

### Ordered work

1. Baseline hashes, saves, replays and section budget.
2. Draft the provider contract in `docs/` citing ADR-0002 D5–D15.
3. Add `SubsystemProviders` as a pimpl in `GameSimulation`.
4. Implement the native economy provider over `settleResourceAccount`; switch
   the two call sites in `GameSimulation_economy.cpp`.
5. Register save/hash/dump with a zero default contribution.
6. Carry provider identity in the resolved set and headers.
7. Test default plus a different replacement; run the hash oracle.
8. Benchmark `battle_test --units 200` under `RWE_ENABLE_SIMPROF`.
9. Add wind as the second provider; then regrowth, missions and sensors.
10. Spike the module runtime against the contract.

## Status

MOD-11 has the first economy and wind providers (MOD-SDK-011/012).

MOD-10's first slice landed 2026-11-03: `src/rwe/presentation/` holds the
filtered `PlayerView`, the Lua `PresentationRuntime`, the draw list and the
package resolver, and `src/rwe/game/GameScene_presentation.cpp` is the scene
attachment. The runtime is driven from `on_frame` and the four input hooks,
draws through the overlay, and turns `submit_*` calls into ordinary local
commands. `+ReloadPresentation` reloads from the resolved script paths and
keeps the last good runtime. What remains from the ordered work above is
native panel hosting (item 4's `UiPanel` half), filtered picking (item 5),
effects (item 6's spawn half), the camera/minimap controllers and audio policy
(item 7), and the windowed `ui_probe`/`battle_test` acceptance run (item 10).
MOD-SDK-013 records the boundary and the evidence.
