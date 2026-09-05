# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Robot War Engine (RWE) is an open-source real-time strategy game engine with high compatibility for Total Annihilation data files. It consists of a C++20 core engine and a TypeScript/Electron launcher application.

Most of the current work is not new features but making the engine behave like the original rather than merely look like it. That work has its own reference documents and its own hazards — read "Matching Total Annihilation" below before changing anything TA-facing.

## Build Commands

### C++ Engine (from repo root)

```bash
# First time setup (submodules + protobuf)
git submodule update --init --recursive
cd libs && ./build-protobuf.sh && cd ..

# Configure and build
mkdir build && cd build
cmake .. -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run unit tests
./build/rwe_test

# Run a single test by name (Catch2 syntax)
./build/rwe_test "test name pattern"
./build/rwe_test "[tag]"
```

The suite passes 364 cases / 54,376 assertions as of 2026-09-04. If a document quotes a different figure, run the suite rather than believing either of them.

This machine has two configured trees, both MSYS2/MinGW64 with `Unix Makefiles`: `build/` (Debug) and `build-release/` (Release). Play-testing uses `build-release/rwe.exe`. **Rebuild the `rwe` target, not just `rwe_test`** — a green test suite says nothing about whether the game still links, and several of the executables below share `librwe` with it.

Defining `RWE_ENABLE_SIMPROF` compiles in the per-phase tick timers declared in `src/rwe/sim/sim_prof.h`. There is no CMake option for it — configure with `-DCMAKE_CXX_FLAGS=-DRWE_ENABLE_SIMPROF`, or define it in the one translation unit you are measuring. They are off by default because the timers cost a clock pair each and their call sites sit inside per-unit work, so leaving them live would tax the very thing they measure. With them on, `GameSimulation::tick` writes a `SIMPROF ticks=…` line to the log every two seconds, broken down by phase. That breakdown is what found both hotspots in the performance pass (unit behaviour and pathfinding, almost exactly equal at 11.9 ms and 11.4 ms a tick with 800 units); measure with it before optimising anything, because the obvious guess there — the renderer — was wrong.

### Launcher (from `launcher/` directory)

```bash
npm ci
npm run tsc          # Type check
npm test             # Jest tests
npm run lint         # ESLint
npm run server       # Webpack dev server (hot reload)
npm start            # Launch Electron app (needs RWE_HOME env var)
npm run master-server # Local multiplayer master server
npm run package      # Package for distribution
```

## Architecture

### Core Engine (`src/rwe/`)

The engine is built as a static library `librwe` linked by multiple executables (`rwe`, `rwe_bridge`, `rwe_test`, the diagnostic harnesses below, and various format test tools).

Key subsystems:

- **sim/** - Deterministic game simulation (units, weapons, projectiles, terrain, resources, visibility). Uses fixed-point-shaped math types (`SimScalar`, `SimVector`, `SimAngle`) for cross-platform determinism.
- **ai/** - The skirmish computer player: `AiPlayerController` plus BuildManager, EconomyManager, ArmyManager, ScoutManager, TransportManager, StrategicManager, PerceptionManager, ThreatMap, ReachabilityMap. It runs inside `GameSimulation::tick` and emits ordinary `PlayerCommand`s, so it is part of the deterministic sim — see `docs/ai-architecture-proposal.md`.
- **scene/** - `Scene` and `SceneManager`. The scenes themselves sit a level up: MainMenuScene → LoadingScene → `game/GameScene`, plus MovieScene for the Smacker films.
- **render/** - OpenGL 3.0+ rendering pipeline with GLSL shaders (in `shaders/`).
- **cob/** - Virtual machine executing Total Annihilation's COB unit behavior scripts. CobThread runs concurrent script coroutines within CobExecutionContext.
- **io/** - Parsers for TA file formats: HPI (archives), GAF (sprites), TDF (config), 3DO (models), COB (scripts), FBI (units), TNT (terrain), PCX (images), OTA (maps), GUI (layouts), SMK (the Smacker movies the GOG release ships as `.ZRB`).
- **vfs/** - Virtual file system abstracting over HPI archives and directories.
- **pathfinding/** - A* pathfinding with octile distance on grids, over flat stamped scratch arrays (`AStarScratch.h`).
- **proto/** - Protocol buffer networking (defined in `proto/network.proto`).
- **geometry/** / **math/** - Linear algebra and spatial primitives.
- **collections/** - Custom data structures (MinHeap, VectorMap).

`src/rwe/GameLaunch.{h,cpp}` holds the shared launch path — SDL, the GL context, the VFS, every service, then the scene loop. `main.cpp` keeps argument handling and nothing else, so a harness can launch the *real* game rather than a reduced imitation of it.

### Launcher (`launcher/src/`)

Electron app with React/Redux for the multiplayer lobby. Communicates with the engine via `rwe_bridge` (JSON IPC). Contains `launcher/`, `master-server/`, `game-server/`, and `common/` modules.

## Looking at the game

Reaching for a screenshot is usually not the fastest way to settle a question, and several of these will settle one that a screenshot could only gesture at.

- **`battle_test`** — a standing battle. Spawns a fixed number of units for each of two players at opposite start positions, walks them at each other, and replaces them as they die, with a live unit-count slider in the F10 debug panel so the fight can be pushed until something gives without restarting. It goes through `GameLaunch::run`, so it is the same renderer, simulation and scene loop as `rwe.exe`, which is the only way its numbers mean anything. `battle_test --map "Coast To Coast" --units 200 --unit-type CORAK`; `--list-maps` and `--list-units` say what is available. Writes `battle_test.log` in the local data directory, flushed a line at a time, because these runs normally end under `taskkill`.
- **`ui_probe`** — builds the real UI panels from the real game data headlessly, dumps every gadget's hitbox, and delivers clicks the way the scenes deliver them, printing which gadget takes each event and what message comes out. It diagnoses layout and dispatch faults without a window.
- **`solar_probe <file.3do>`** — replays the engine's own shading pipeline over a model and prints the shade row each polygon would get. This is how the vertex-normal convention was settled, and it takes one command where a play-test took a round trip.
- **`tools/visual-test.ps1`** — when only the renderer will do. It launches `build-release/rwe.exe`, finds the window, and then *drives* it: real clicks at client-relative coordinates, screenshots cropped and nearest-neighbour magnified around the thing under test. `-phase build|air|ship` are the scripted sequences already written; adding one is a few lines. Prefer this to ad-hoc screenshotting — a scripted click sequence is repeatable and an eyeballed one is not.
- **`tools/crash-catch.cmd`** runs the Debug build under gdb and writes a backtrace to `crash.txt`. Play normally, reproduce the crash, close the window.
- Environment switches, all pure observers: `RWE_AI_PROFILE=1` times each AI pass and logs anything over 2 ms; `RWE_DEBUG_SPAWN=ARMPW*12@0:8:1` spawns units on a timer (`<type>*<count>@<owner>:<seconds>[:<near player>]`); `RWE_DEBUG_SELF_DESTRUCT[=_PLAYER]`, `RWE_TRACE_BOMBER`, `RWE_TRACE_GUNSHIP`, `RWE_TRACE_MISSILE`.

## Determinism

The simulation is lockstep: peers exchange commands, not state, and a `GameHash` mismatch is a desync. Two rules keep biting.

**Never draw from `std::uniform_int_distribution` in the sim.** Its bias correction is implementation-defined, so two builds of the engine can draw different numbers from the same seeded generator and fall out of step. Take a modulo of the generator's raw output instead — `UnitBehaviorService.cpp` and `GameSimulation::dealStartPositions` do, and say why at the call site. Ten such draws still remain in `GameSimulation.cpp` and `cob.cpp`, and two more in `ai/BuildManager.cpp`; they are known and tracked in the roadmap, left alone only because changing a draw shifts every subsequent random sequence and they deserve their own pass with the tests watched.

**Keep sim state in step across four places.** New state on `UnitState`, `MapFeature`, `GamePlayerInfo` or the simulation itself needs adding to `src/rwe/game/save_util.cpp` (serialization), `src/rwe/sim/GameHash_util.cpp` (the sync hash) and `src/rwe/game/dump_util.cpp` (desync diagnostics) as well as to the struct. The save round-trip test (`src/rwe/sim/saveload.test.cpp`) fails if hashed state is missed, but unhashed state needs the discipline: nothing will tell you.

Derived state is the exception and should say so. `UnitSpatialIndex` is rebuilt from the unit list every tick, is never saved and never hashed, and returns a deliberate *superset* of each query so that the exact test still runs against live positions — which is what makes it incapable of changing an outcome.

## Other hazards

**Destroying a `Subscription` handle does not unsubscribe.** A subscriber that dies before the `Subject` it listens to must hand its subscription back by hand — call `unsubscribe()` in the destructor — or the subject will deliver into freed memory. Getting this wrong crashed the second game of any session, which is a slow thing to find. `src/rwe/observable/Subject.test.cpp` pins both halves of the rule.

## Code Conventions

- All C++ code is in the `rwe::` namespace
- Formatting enforced by `.clang-format`: Allman brace style, 4-space indent, no column limit. (It still declares `Standard: c++17`; that governs only how clang-format parses. The project builds as C++20.)
- Test files live alongside source: `src/rwe/[subsystem]/[Component].test.cpp`
- Fixtures the simulation tests share live in `src/rwe/sim/sim_test_util.h` (`makeEmptyCobScript`, `addPlayer`, `addUnitOfType`, `tick`, `everFires`). Use those rather than writing another copy — there were fourteen copies of the first one before they were collected. A test that genuinely needs something different keeps its own, under a name that says what is different: `addPlayerWithNothing` in the reclaim tests, `addWellStockedPlayer` and `addUndamagedUnitOfType` in the repair tests.
- Strong typing via opaque ID types: `UnitId`, `PlayerId`, `ProjectileId` (see `OpaqueId`)
- Variant-based state machines for unit behavior and navigation goals
- Error handling uses `Result<T, E>` types rather than exceptions
- Version derived from git tags (format: `v#.#.#`)

## CI

GitHub Actions (`.github/workflows/build.yml`) runs Linux (gcc-14, clang-18 on ubuntu-24.04) and Windows (MSVC 2026, MinGW64) builds in both Debug and Release configurations.

## Saved games

Full-state save/load lives in `src/rwe/game/save_util.*` (simulation
serialization, hash-validated round trip — see `sim/saveload.test.cpp`) and
`src/rwe/game/SaveFile.*` (the on-disk container with the map/players header
and the skirmish options). Saves are `<name>.rwesave` under the local data
path. See the determinism section above for what a new piece of sim state
obliges you to touch. Two known gaps: the `explored` grid is not serialized —
`loadSimulationFromJson` ends in `updateVisibility()`, so a resumed game
rebuilds visibility from where the units are standing and forgets the rest of
the map — and the `SaveFile` header's `PlayerInfo` carries no `teamId` (the
simulation's own player table does, and is what a load restores, so alliances
do survive; the header is simply thinner than the sim).

## Matching Total Annihilation

Much of the current work is making RWE behave like the original rather than
merely look similar. Where a behaviour is meant to match TA, it has usually been
read out of `TotalA.exe` instead of guessed at.

- `docs/TOTALA-EXE.md` — the findings, now ninety-one sections: the flight
  model, fog of war and line of sight, the damage pipeline, missile flight,
  target selection and eligibility, the economy, the nanolathe and construction
  display, effects and render order, the interface (the minimap detection
  rings, the marching waypoint trail, the building placement box and its
  sweep), transports, the situational music system, the renderer and its quad
  scan-conversion, the interface colours, the in-game menus, the keyboard, and
  the FBI and weapon field offsets, the D-gun and what commandfire costs an
  order (§85), what makes a patrolling unit leave its route (§86), and the
  pathfinder and its scheduler (§87). §88 and §91 are the ones to read first if
  you are about to change something — where RWE **deliberately** differs, so
  those do not get "corrected" back, and what is decoded but not ported.
- `docs/TOTALA-EXE-SHADING.md` — the shaded unit rasterizer in full: the
  16-byte vertex record, the per-vertex shade level and its `& 0x1F`, the
  averaged (and deliberately unnormalised) vertex normals, and the Gouraud
  interpolation of the integer row. Read this before touching the unit
  shaders. It overturns two earlier readings: the original *does* light its
  models (the "no lighting" finding had read the `SHADING=off` path), and the
  sun vector is not normalised, which sets the ramp's width at thirteen rows
  rather than thirty-two. The `& 0x1F` wrap is not the original being crude;
  it is where its contrast comes from, and a face one row below zero really
  does come out at the top of the table.
- `docs/TOTALA-EXE-WRECKS.md` — where a wreck comes from, and why one over
  water sinks: the corpse spawns at the dying unit's exact height and the
  water branch adds a fixed 0.175 units a tick of downward velocity, spent by
  a per-tick sweep. The exemption for `IsFeature=1` — the floating dragon's
  teeth — is what shows the rule is deliberate.
- `docs/TOTALA-EXE-MISSIONS.md` — how aircraft decide *where to go* when
  attacking: the mission name table and its handlers, the bomber attack run,
  the fighter strafing pass, the gunship standoff ring, and what `hoverattack`
  and `maneuverleashlength` actually gate.
- `docs/REVERSE-ENGINEERING-PRIORITIES.md` — what is worth reading out of the
  binary next, ranked, with the evidence that each is a real gap and a string
  or offset to pivot on. Most of it is now done; the head of the file says
  what is left.
- `tools/exe/` — the probe scripts that produced them, and the method.
  `tools/exe/shading/` holds the palette work, including `shdgen.py`, which
  regenerates the shipped `PALETTE.SHD` byte-for-byte and is what pins its
  layout.

Read the findings before reimplementing anything TA-facing. Several plausible
readings of that binary are wrong in ways that only surface when you replay the
arithmetic against real unit data — transcribing a decoded routine into a small
standalone program and comparing it tick by tick against RWE has caught more
than one confident mistake, and so has replaying it against a real `.3do` or a
real FBI. Test fixtures that invent plausible-looking numbers instead of using
the shipped ones have hidden at least one bug outright: the bomber tests claimed
to fly an ARM Thunder with an `acceleration` and `turnRate` it does not have,
and the failure was immediate once the real values went in.
