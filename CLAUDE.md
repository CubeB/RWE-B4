# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Robot War Engine (RWE) is an open-source real-time strategy game engine with high compatibility for Total Annihilation data files. It consists of a C++17 core engine and a TypeScript/Electron launcher application. Active priority is simplifying dependencies and improving new developer onboarding.

## Build Commands

### C++ Engine (from repo root)

```bash
# First time setup (submodules + protobuf)
git submodule update --init --recursive
cd libs && ./build-protobuf.sh && cd ..

# Build (Linux/macOS)
mkdir build && cd build
cmake .. -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run unit tests
./build/rwe_test

# Run a single test by name (Catch2 syntax)
./build/rwe_test "test name pattern"
./build/rwe_test "[tag]"
```

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

The engine is built as a static library `librwe` linked by multiple executables (`rwe`, `rwe_bridge`, `rwe_test`, and various format test tools).

Key subsystems:

- **sim/** - Deterministic game simulation (units, weapons, projectiles, terrain, resources). Uses fixed-point math types (`SimScalar`, `SimVector`, `SimAngle`) for cross-platform determinism.
- **scene/** - Scene state machine: MainMenuScene → LoadingScene → GameScene.
- **render/** - OpenGL 3.0+ rendering pipeline with GLSL shaders (in `shaders/`).
- **cob/** - Virtual machine executing Total Annihilation's COB unit behavior scripts. CobThread runs concurrent script coroutines within CobExecutionContext.
- **io/** - Parsers for TA file formats: HPI (archives), GAF (sprites), TDF (config), 3DO (models), COB (scripts), FBI (units), TNT (terrain), PCX (images), OTA (maps), GUI (layouts).
- **vfs/** - Virtual file system abstracting over HPI archives and directories.
- **pathfinding/** - A* pathfinding with octile distance on grids.
- **proto/** - Protocol buffer networking (defined in `proto/network.proto`).
- **geometry/** / **math/** - Linear algebra and spatial primitives.
- **collections/** - Custom data structures (MinHeap, VectorMap).

### Launcher (`launcher/src/`)

Electron app with React/Redux for the multiplayer lobby. Communicates with the engine via `rwe_bridge` (JSON IPC). Contains `launcher/`, `master-server/`, `game-server/`, and `common/` modules.

## Code Conventions

- All C++ code is in the `rwe::` namespace
- Formatting enforced by `.clang-format`: Allman brace style, 4-space indent, no column limit, C++17 standard
- Test files live alongside source: `src/rwe/[subsystem]/[Component].test.cpp`
- Strong typing via opaque ID types: `UnitId`, `PlayerId`, `ProjectileId` (see `OpaqueId`)
- Variant-based state machines for unit behavior and navigation goals
- Error handling uses `Result<T, E>` types rather than exceptions
- Version derived from git tags (format: `v#.#.#`)

## CI

GitHub Actions runs Linux (gcc-12, clang-15) and Windows (MSVC 2022, MinGW64) builds in both Debug and Release configurations.

## Matching Total Annihilation

Much of the current work is making RWE behave like the original rather than
merely look similar. Where a behaviour is meant to match TA, it has usually been
read out of `TotalA.exe` instead of guessed at.

- `docs/TOTALA-EXE.md` — the findings, now fifty-two sections: the flight
  model, fog of war and line of sight, the damage pipeline, missile flight,
  target selection and eligibility, the economy, the nanolathe and construction
  display, effects and render order, the interface (the minimap detection
  rings, the marching waypoint trail, the building placement box and its
  sweep), transports, the situational music system, and the FBI and weapon
  field offsets. The last two sections are the
  ones to read first if you are about to change something — where RWE
  **deliberately** differs, so those do not get "corrected" back, and what is
  decoded but not ported.
- `docs/TOTALA-EXE-MISSIONS.md` — how aircraft decide *where to go* when
  attacking: the mission name table and its handlers, the bomber attack run,
  the gunship standoff ring, and what `hoverattack` and `maneuverleashlength`
  actually gate.
- `docs/REVERSE-ENGINEERING-PRIORITIES.md` — what is worth reading out of the
  binary next, ranked, with the evidence that each is a real gap and a string
  or offset to pivot on.
- `tools/exe/` — the probe scripts that produced them, and the method.

Read the findings before reimplementing anything TA-facing. Several plausible
readings of that binary are wrong in ways that only surface when you replay the
arithmetic against real unit data — transcribing a decoded routine into a small
standalone program and comparing it tick by tick against RWE has caught more
than one confident mistake.
