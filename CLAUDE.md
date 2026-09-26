# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See also `@CONTEXT.md` — the project's domain glossary and design-decision record. Keep it in step when introducing or renaming project terms; the `.claude/skills/domain-modeling` skill is the discipline that maintains it.

## Project Overview

Robot War Engine (RWE) is an open-source real-time strategy game engine with high compatibility for Total Annihilation data files. It consists of a C++20 core engine and a TypeScript/Electron launcher application.

Most of the current work is not new features but fidelity: taking behaviours the engine already reproduces and pinning them to exactly what the original executable does. That work has its own reference documents and its own hazards — read "Matching Total Annihilation" below before changing anything TA-facing.

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

Run the suite for its current size; it prints the count on the way out, and so
does every CI job's `test rwe` step. The figure is deliberately not written
down here. It goes stale the moment anyone adds a test, and when two branches
both bring it up to date they conflict on the only line either of them
touched -- which happened three times in one day before this sentence
replaced the number.

This machine has two configured trees, both MSYS2/MinGW64 with `Unix Makefiles`: `build/` (Debug) and `build-release/` (Release). Play-testing uses `build-release/rwe.exe`. **Rebuild the `rwe` target, not just `rwe_test`** — a green test suite says nothing about whether the game still links, and several of the executables below share `librwe` with it.

`RWE_ENABLE_SIMPROF` and `RWE_ENABLE_RENDERPROF` compile in the per-phase tick and frame timers (`src/rwe/sim/sim_prof.h`, `src/rwe/render/render_prof.h`). Both are off by default, neither has a CMake option, and both write a breakdown to the log every two seconds: `-DCMAKE_CXX_FLAGS="-DRWE_ENABLE_RENDERPROF -DRWE_ENABLE_SIMPROF"`. **Measure with them before optimising anything** — the obvious guess has been wrong twice. See `docs/PROFILING.md`, which also says how to read the numbers without being caught by the vsync cap or by how much a `battle_test` run drifts.


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

Before a network game starts, the lobby checks that every player has the same
game data, because having the same mods by name does not mean having the same
bytes in them and the difference is a desync. `common/archives.ts` reduces each
mod to its archives and the SHA-256 of each, hashed in the background against a
cache keyed by size and modification time; the room refuses to start while
anyone differs or is still being checked, and the game server applies the same
rule so that skipping the client is no help. **That file mirrors the engine's
archive extension list and load order** from
`src/rwe/vfs/CompositeVirtualFileSystem.cpp`; a change to one belongs in the
other.

## Looking at the game

Reaching for a screenshot is usually not the fastest way to settle a question, and several of these will settle one that a screenshot could only gesture at.

- **`battle_test`** — a standing battle. Spawns a fixed number of units for each of two players at opposite start positions, walks them at each other, and replaces them as they die, with a live unit-count slider in the F10 debug panel so the fight can be pushed until something gives without restarting. It goes through `GameLaunch::run`, so it is the same renderer, simulation and scene loop as `rwe.exe`, which is the only way its numbers mean anything. `battle_test --map "Coast To Coast" --units 200 --unit-type CORAK`; `--list-maps` and `--list-units` say what is available. Writes `battle_test.log` in the local data directory, flushed a line at a time, because these runs normally end under `taskkill`.
- **`scenario`** — the tick-indexed scenario runner. Drives the *real* `GameScene` -- the same input handlers, selection, cursor modes, panels and simulation as `rwe.exe` -- at chosen ticks by calling its handlers directly, then asserts on the result. It is for the interface faults a pure-function unit test cannot reach: a fault in a sequence of real handler calls, or in a panel's lifecycle across a rebuild, so a sim question still belongs in `sim_test_util` and Catch2. Scenarios are C++ functions registered in `src/scenario.cpp` against `rwe/game/ScenarioDriver.h`; the driver parks a mouse override and refuses to run unless the scene is headless, which is what keeps one update equal to one tick. `scenario --list`; `scenario --run 342-active-tab-stays-pressed --data-path <dir>`; `--all` runs every one in a single process. It exits non-zero on a failed assertion. A scenario run twice at the same seed writes byte-identical `RWE_HASH_LOG` files -- against itself, not against another run: a scenario spawns units and sends commands, so its stream is its own. A step whose tick is never reached is a failure too, not a silent pass, and the driver gives up on a run that cannot reach its end tick (a paused game stops the clock) rather than hanging. Needs game data and a GL context, so under Xvfb on a machine without a display. The two that ship pin #342 (the active BUILD/ORDERS tab staying pressed) and the ROADMAP round 10 crash (placing a unit while paused, then selecting it); the first fails without the fix in `attachOrdersMenuEventHandlers`, which is the shape a scenario is for.
- **`ai_arena`** — `rwe --ai-arena` with no window: no SDL, no GL context, no ImGui. It builds the simulation through the same `loadGameSimulation` the loading scene uses and runs the same tick loop, so on seed N it writes the same `AI-ARENA-RESULT`, the same `ai-arena.csv`/`ai-arena-events.csv` and the same `RWE_HASH_LOG` lines as the windowed run. This is the arena for CI and any machine without a display, and the way to hunt a desync where Xvfb is not available. `ai_arena --map "Coast To Coast" --ai-arena 900 --seed 7 --player "A;Computer;ARM;0" --player "B;Computer;ARM;1" --start-location random`; `--out <dir>` chooses where the CSVs land.
- **`--record-demo <file>`** — `rwe --map ...` and `ai_arena` both write the game they run as a TA Demo Recorder compatible `.tad`; `rwe --replay <file> --record-demo <out>` records one offline, re-running the recording through the real simulation. A demo is state and effects where RWE's replay is a command stream, so `tad_probe` and the reference scorers read it. `docs/TA-DEMOS.md`, "Writing one", says what is written faithfully and what is a recorded divergence.
- **`ui_probe`** — builds the real UI panels from the real game data headlessly, dumps every gadget's hitbox, and delivers clicks the way the scenes deliver them, printing which gadget takes each event and what message comes out. It diagnoses layout and dispatch faults without a window.
- **`solar_probe <file.3do>`** — replays the engine's own shading pipeline over a model and prints the shade row each polygon would get. This is how the vertex-normal convention was settled, and it takes one command where a play-test took a round trip.
- **`tad_probe --file <demo.ted>`** — reads Total Annihilation demo recordings (`.tad`/`.ted`) and prints the header, players, extra sectors, tick range and a histogram of subpacket codes, plus a count of anything it could not account for. `--dir` walks a corpus and exits non-zero if any file desynchronises, which is what makes it a check rather than a listing; `--dump-unknown` explains what it could not size. Offline — no SDL, no GL, no VFS. Demos are not checked in; `tools/fetch-demos.py` fetches a small corpus, and read the warning at the top of it before running it.
- **`tad_episodes`** — mines the demo corpus for short bounded episodes with real numbers in them, and writes the four checked-in fixtures (`tad_economy_episodes.h`, `tad_build_episodes.h`, `tad_weapon_episodes.h`, `tad_stall_episodes.h`). Do not hand-edit those; regenerate them. Every mode, and the filters that make an episode mean anything, are in `docs/TA-DEMOS.md`, "The tools, mode by mode".
- **`tools/tad-buildtime.py`**, **`tad-weapontime.py`**, **`tad-stalltime.py`**, **`tad-storagecapacity.py`** — the reference scorers for build timing, the weapon oracle, the stall oracle and storage capacity. Each exits non-zero if a scored cell moves, so they are checks rather than listings — and also when there is *nothing* to score, so read the message and not just the status. The ported versions inside `tad_episodes` must keep agreeing with them; the scripts are the reference. `docs/TA-DEMOS.md` for what each scores and what it deliberately does not.
- **`tools/visual-test.ps1`** — when only the renderer will do. It launches `build-release/rwe.exe`, finds the window, and then *drives* it: real clicks at client-relative coordinates, screenshots cropped and nearest-neighbour magnified around the thing under test. `-phase build|air|ship` are the scripted sequences already written; adding one is a few lines. Prefer this to ad-hoc screenshotting — a scripted click sequence is repeatable and an eyeballed one is not.
- **`tools/crash-catch.cmd`** runs the Debug build under gdb and writes a backtrace to `crash.txt`. Play normally, reproduce the crash, close the window.
- **`tools/net-test.py`** -- a network game on one machine, run with `uv run tools/net-test.py`. It starts N peers on loopback, optionally kills one part way through, and always diffs the survivors' `RWE_HASH_LOG` files tick for tick, because two peers that stayed in step have identical files and two that did not have a first differing line -- the same tick the game's own desync report names. `--peers 3 --kill 2` for drop handling, `--ai` to put a computer player in the game, `--desync-at 200` to make one peer report a wrong hash so the desync report can be read without waiting for a real fault, `--chat` to have every peer say a line so chat can be seen crossing the wire, `--rejoin` to kill a peer and bring it back into the same game, and `--rejoin --bridge` to do that the way a launcher does it -- over the game's own stdin and stdout (`rwe --bridge`) rather than the test environment variable. Reach for it after any change to the simulation that two machines have to agree about; it wraps each peer in `xvfb-run -a` when there is no display, so it runs on Linux and in CI, and CI runs the basic and `--kill` modes when `RWE_SCENARIO_DATA` supplies game data.
- **Reading an arena run:** `tools/arena-analyse.py <outDir>` turns a run's CSVs, event files and logs into milestones, build orders, a five-minute checkpoint table and efficiency figures (stalled, capped, wasted, idle builders, idle factories) per side, and an aggregate that splits tuned from control; `--brief` for the aggregate alone, `--game 3` for one seed, `--json` to keep the figures. `tools/arena-paired.py <outDir>...` compares each tuned seed with its own control, side by side -- use it, not the arena's tuned/untuned table, whenever `-sideA` and `-sideB` differ, because `-tune` alternates which player is tuned and that table then mixes the knob with the faction. `tools/arena-deaths.py <outDir>` says where each side's extractors and army died -- how far along the line from its own base to the enemy's, how many armed enemies stood within 600 and of what type, and whether any of its own army or towers were there -- from the death columns of the events file (`--before 15` for the early game). `tools/ai-profile.py <game log>` totals the `RWE_AI_PROFILE=1` summaries per pass and names the heaviest windows. Reach for these before reading a log by eye: the wasted-energy figure that led to `solarOnDemand` is not visible in any one line of any log. Two traps: **`MapIntel::shipyardSites` is every cell a shipyard fits on, hundreds of thousands on a water map** -- anything that walks it belongs behind a memo or a cheap prefilter, never in a per-pass loop -- and **never edit a source file while a `make` is running in either tree**, a background one included: objects compiled from the old header finish after the edit, make believes them current, and the binary has two layouts of one struct. The signature is nonsense incomes in the AI status line, every arena game ending at once, and exit code 0xC0000005; `touch` the header and rebuild both trees.
- **The playtest harness:** `tools/playtest/run.py` expands a TOML scenario matrix into headless arena runs -- each with a control twin, a watchdog and its own directory -- and writes `index.jsonl` and `findings.jsonl`; the checkers under `tools/playtest/checkers/` read those runs for stalls, production, unit deaths, invariants, pathfinding, AI cost and same-seed divergence. `tools/playtest/q.py` queries one run's `event-log.jsonl` by event name, player, window or why-tag. `nightly.sh` runs the whole matrix against fresh `revival`, and `baselines.py` says whether a scenario has drifted from what it used to look like. The `playtest-scan` skill is what triages the findings into issues.
- **The event log:** `event-log.jsonl` sits beside the arena CSVs, one JSON object a line, carrying what the AI decided and why, with the sim tick and seconds derived from it and nothing at all from the wall clock -- so two same-seed runs write byte-identical logs, which is a stronger determinism check than the CSVs. **It records only when something exists that will write it**: the arena report, in `ai_arena` and in `GameScene` when the run asked for `--ai-arena`. An ordinary game keeps nothing, because nothing would ever read it and a two-hour match would otherwise hold thousands of events an hour for the length of it. `SimEventLog` is a pure observer either way -- never hashed, never saved, never dumped, and the simulation never reads it back.
- **Hunting a determinism fault:** `RWE_HASH_LOG=<file>` writes the tick and the sync hash, one line a tick, and unlike the hash exchange it stays on while a replay is played back. `RWE_STATE_DUMP=<first>:<last>:<prefix>` (with `RWE_STATE_DUMP_STEP=<n>`) writes the full saved state as JSON for each tick in the range. Play one replay headlessly several times -- `rwe.exe --replay <file> --ai-arena <seconds>` -- and diff the hash logs to find the tick, then bisect the state dumps to find the field. The saved state usually differs *before* the hash does, because there is state that is saved and not hashed (weapon aim state is), and that is the lead worth having. This is how the freed-thread fault below was found, in an afternoon, after being invisible to every test.
- **The network overlay (F9):** a see-through panel showing the lockstep live -- tick rate, stalls and whom each waited for, and per peer the round trip, silence, tick lead, unacked sets and commands buffered, as thirty seconds of graphs. It records whether or not it is showing, so F9 after a freeze still shows the freeze, and it is a pure observer. `docs/NETWORK-OVERLAY.md` says how to read it, including which pattern means a jittery connection, a frozen peer, one-way packet loss or a machine that cannot keep up.
- **Reading a desync report:** a `GameHash` mismatch names the first tick the peers disagreed on -- not the tick it was noticed on, which is a round trip later -- and every peer names the same one, because every peer compares the same two ordered hash streams. Each writes `rwe-desync-tick<n>-player<p>.json` beside the log: the mismatch, then the hashed state as `dump_util` writes it, so a diff between two peers' dumps is a list of candidates for the field that moved. A bug report wants every peer's file. To fire the report without a real desync, `RWE_DESYNC_AT=<tick>` on **one** peer makes it report a wrong sync hash from that tick onwards; the simulation is untouched, so it counterfeits a desync rather than causing one. Two peers on loopback, off-screen, is how it was checked: `rwe --map "Coast To Coast" --port 15337 --player "A;Human;ARM;0" --player "B;Network,[::1]:15338;CORE;1"` against its mirror on 15338. Only peers that report a hash are compared -- this machine and the machines on the other end of the network -- so a computer player has a command buffer and no hash buffer; giving it one would stall the comparison for everybody, which is the state desync detection was quietly in until 2026-09-22.
- Environment switches, all pure observers but the desync one above: `RWE_AI_PROFILE=1` times each AI pass and logs anything over 2 ms; `RWE_PATH_PROFILE=1` logs the pathfinder's counters every ten seconds as deltas -- searches, expansions, suspensions, exhausted searches, and how deep the request queue is -- which is the only place to see that a fight has saturated the expansion budget and left units walking their straight-line stand-ins into walls (issue #155 is what it found); `RWE_SIM_LAG=<ms>[@<fromTick>]` sleeps that long after every simulation tick from that tick onwards, so a peer whose machine cannot keep up can be reproduced on one that can -- it changes when ticks run and never what they contain, and `tools/net-test.py --lag <peer>:<ms>` sets it on one peer (a large lag on a long `ai_arena` run can trip its `--watchdog`, which bounds wall time); `RWE_DEBUG_SPAWN=ARMPW*12@0:8:1` spawns units on a timer (`<type>*<count>@<owner>:<seconds>[:<near player>]`); `RWE_CHAT_TEST=<tick>:<text>` says that line of chat once at that tick, there being nobody at the keyboard in a headless run; `RWE_DEBUG_SELF_DESTRUCT[=_PLAYER]`, `RWE_TRACE_BOMBER`, `RWE_TRACE_GUNSHIP`, `RWE_TRACE_MISSILE`.

## Determinism

The simulation is lockstep: peers exchange commands, not state, and a `GameHash` mismatch is a desync. Two rules keep biting.

**Never draw from `std::uniform_int_distribution` in the sim.** Its bias correction is implementation-defined, so two builds of the engine can draw different numbers from the same seeded generator and fall out of step. Take a modulo of the generator's raw output instead — `UnitBehaviorService.cpp` and `GameSimulation::dealStartPositions` do, and say why at the call site; `SimRandom.h`'s `randomBelow` is the wrapper to reach for.

**That cleanup is finished, as of 2026-09-08.** This paragraph used to say ten such draws remained in `GameSimulation.cpp` and `cob.cpp` and two more in `ai/BuildManager.cpp`; audited, there are none left anywhere in the simulation. Every surviving mention of `uniform_int_distribution` under `src/rwe/` is either a comment warning against it (`SimRandom.h`, `GameSimulation.h`, three sites in `UnitBehaviorService.cpp`) or code outside the sim: `GameNetworkService.h`, and four presentation draws in `GameScene.cpp` for particle lifetime, screen shake and jitter. Those four are safe for a second reason worth knowing — `GameScene` draws from its own `effectsRng`, not `simulation.rng`, so rendering cannot advance the simulation's sequence. **Keep it that way:** presentation code that reaches into `simulation.rng` is a desync even if the value is only ever drawn on screen.

**Keep sim state in step across four places.** New state on `UnitState`, `MapFeature`, `GamePlayerInfo` or the simulation itself needs adding to `src/rwe/game/save_util.cpp` (serialization), `src/rwe/sim/GameHash_util.cpp` (the sync hash) and `src/rwe/game/dump_util.cpp` (desync diagnostics) as well as to the struct. The save round-trip test (`src/rwe/sim/saveload.test.cpp`) fails if hashed state is missed, but unhashed state needs the discipline: nothing will tell you.

**A member the hash reads is initialised by the time the object exists.**
`UnitState`'s constructor names three of its members and leaves every other
one to its default member initialiser, so a hashed member without one holds
whatever was in the memory the unit was built in. That is zero while the heap
is young -- a page fresh from the operating system arrives zeroed -- and stops
being zero once the process has a history, which is what makes the resulting
desync intermittent and dependent on what ran before it. `nanoPoint`, the
nanolathe nozzle's position, was such a member: meaningless until
`QueryNanoPiece` is first asked, hashed from the moment the unit exists, and
it failed the replay keyframe test about one run in ten and only after the
rest of the suite had run. "The factory always sets it before anything reads
it" is not the property that matters, because the hash reads it first;
`position` and `owner` were in the same state for the same reason.
`GameHash_util.test.cpp`'s "a new unit hashes the same wherever in memory it
was built" builds a unit over storage filled with a byte, twice with
different bytes, and requires the hashes to agree, which is the general form
of the rule.

A unit's **order queue is hashed**, position included, so state that lives on
an order -- capture progress does, because that is where the original keeps it
-- is covered like any other. It was not always: moving that progress off
`UnitState` silently took it out of the hash until the queue was added, which
is the failure mode this rule exists to catch.

Derived state is the exception and should say so. `UnitSpatialIndex` is rebuilt from the unit list every tick, is never saved and never hashed, and returns a deliberate *superset* of each query so that the exact test still runs against live positions — which is what makes it incapable of changing an outcome.

**Derived state that can change an outcome is not exempt, and there is one.** A path search is sliced across ticks now, so at the end of any tick the pathfinder may be holding a half-finished A\*. That is not hashed — every peer suspends at the same point, so there is nothing to disagree about — but it *is* serialized, because when the path lands changes where a unit is. It goes into the save as five integers rather than as a search: the footprint the search began from and how many vertices it had expanded, which is enough to rebuild it exactly (`PathFindingService::suspendedSearchStart`, and `TOTALA-EXE.md` §87 for why dropping it instead would pass the saved-game test and break replays). The rule to take from it: ask whether a piece of derived state can move the simulation's future, not whether it is derived.

**Nothing a frame knows may reach the simulation.** A tick's inputs are queued a set per player per tick and drained the same way, so anything that fills those queues has to be driven by ticks and never by frames -- and the trap is that a tick which *cannot* run is attempted again next frame, so "once per tick" written at the top of the attempt is once per frame the moment anything stalls. The computer players' commands were filled in that way and desynced at tick 44 in a two-human, one-computer game on loopback: the peer whose packet was a little late pushed an extra set, and its AI's orders landed a tick after the other peer's. `feedAiCommands` is now guarded by `GameScene::onlyComputerPlayersAreNotReady`, which is the question "is this tick going to run" asked before anything is queued for it. The general form: frame rate, ping, window focus and how long a stall lasted are properties of one machine, and a simulation that can see any of them is a simulation two machines can disagree about. The AI's command buffer depth is a constant for the same reason -- `aiCommandBufferDepth()` rather than the humans' round-trip-derived one -- and the same seed at depth 14 and depth 18 diverges at tick 44, which is how much that one is worth.

**Two ways the heap has decided a game, both found 2026-09-18.** An uninitialised member that is hashed or acted on -- `UnitState::nanoPoint`, `CobThread::returnValue` -- is a desync that shows only when the allocator hands back dirty memory, which a fresh process rarely does and a long game always does. And **a raw pointer kept across ticks to something that can be freed is a coin toss**: a weapon held the address of its aim thread, the thread was killed and freed, and whether the next thread landed on the same address -- so that the stale handle matched a thread that was not its own -- was the allocator's choice. One replay in four drifted. `CobEnvironment` now keeps dead threads for two passes before freeing them, so nothing live can alias one that is still remembered; anything new that holds a handle across ticks needs the same care, or an id that is never reused.

## Other hazards

**Destroying a `Subscription` handle does not unsubscribe.** A subscriber that dies before the `Subject` it listens to must hand its subscription back by hand — call `unsubscribe()` in the destructor — or the subject will deliver into freed memory. Getting this wrong crashed the second game of any session, which is a slow thing to find. `src/rwe/observable/Subject.test.cpp` pins both halves of the rule.

**And handing it back too late is the same bug wearing the other face.** `UiComponent` keeps a store of subscriptions and empties it in `~UiComponent` — the *base* destructor, which runs after the derived class's own members are already gone. So a component that subscribes to a subject it owns itself, and puts the handle in that store, hands it back to a `Subject` that no longer exists: `Subject::unsubscribe` does a `find_if` and an `erase` on a destroyed vector. It corrupts the heap rather than trapping, which is why the crash handler wrote nothing and the log simply stopped. Any `UiComponent` subclass that declares a `Subject` therefore calls `releaseSubscriptions()` in its own destructor, while its subjects are still alive — `UiPanel`, `UiListBox`, `UiScrollBar` and `UiStagedButton` all do, and a new one owning a subject must. `src/rwe/ui/UiComponent.test.cpp` walks the path for a sanitizer to catch; it cannot assert the fault, undefined behaviour being free to do nothing. This one cost a crash on entering a skirmish, the main menu's panels being destroyed on the way in.


**A translation unit can outgrow what a COFF object can describe.** An object file numbers its sections with a signed 16-bit index, so 32767 is the ceiling. At `-O0` nothing is inlined and every implicitly instantiated template — every `std::vector` member, every `std::variant` visit — is emitted into its own COMDAT carrying four sections: `.text$`, `.xdata$`, `.pdata$` and `.debug_frame$`. Four sections per function means the real budget is about 8000 instantiations, and `GameScene.cpp` had reached 11466 of them: 46124 sections in a 49 MB object. The assembler covers for this silently by switching the object to the `pe-bigobj` format, which is why nothing warned; recent binutils reads that format back, but the MinGW64 CI runner's did not, and dropped every COMDAT definition while keeping the references. The Debug job failed to link four executables with 1124 undefined symbols in one object while Release, where the optimiser folds those instantiations away into 1388 sections, passed — which is the signature to recognise: a link failure in Debug only, all of it from a single `.obj`, every missing symbol a template or a lambda.

The fix was to split the file, and the rule it leaves behind is a size one: no translation unit should need bigobj. `objdump -f` names the format and `objdump -h <obj> | grep -cE '^ *[0-9]+ '` counts the sections, so a suspect object takes one command to check. `-g1` does not help — `.debug_frame$` is emitted at every debug level — and neither does splitting off code that is merely long: moving the renderers out, 21% of the lines, removed 4% of the sections, because roughly 9500 of them are the standard library's variant, string and vector machinery, re-instantiated in any translation unit that touches the game's types. Only splitting off code that is *dense* helps. `GameScene` is eight files now (`_audio`, `_commands`, `_debug`, `_input`, `_menu`, `_render`, `_replay` and what remains), beside the older `_util`. Measured 2026-09-09 after the last of those cuts: `_commands` 21981 sections, `_replay` 18805, `GameScene.cpp` 15644, `_menu` 13105, `_input` 10810, `_debug` 4839, `_render` 4818, `_util` 3655, `_audio` 3420 — the largest now 67% of the ceiling, where before these two cuts it was 82%.

**`GameSimulation.cpp` is the file to watch now, not `GameScene`.** `tools/section-budget.sh build 75` is a CI step on the MinGW64 Debug job, and measured 2026-09-24 the worst object in the tree is `GameSimulation.cpp` at 24331 sections -- 74%, and **244 below** the 24575 the check allows. It was 23651 on the morning of the 23rd and 24115 that evening; one alternative added to `PlayerCommand`'s variant, for the rejoin, cost 464, and merging the demo recorder's event coverage cost the 216 after that. **That margin is now smaller than either of those two changes**, so the next one of their kind fails the check rather than approaching it, and the work to reach for is below rather than a measurement afterwards. **An include added to `GameSimulation.h` is an include added to most of the engine**, and that is where the headroom goes. `SimEventLog` cost 580 sections the day it arrived, purely by declaring a `std::variant` of eleven alternatives as a member in its header; putting the buffer behind a pimpl gave every one of them back, with no call site changed. That is the move to reach for: **a type stored by value in a widely included header should keep its containers in its own `.cpp`**, either behind a pimpl or, as `GameSimulation` does for `AiPlayerController`, behind a `unique_ptr` and a forward declaration.

Two things that pass measurement teaches, both worth knowing before making any of these bigger. **The section count is a budget, not a free win**: the standard library's floor is paid once per translation unit, so a split adds to the total even as it takes the peak down. Taking the debug harness out of `GameScene.cpp` cost 2216 sections across the pair to take 2623 off the larger; taking the menu out of `_commands` cost 8365 to take 4740 off. And **that floor is not a constant** — `_debug` came out at 4839 against the 9500 quoted above, because its include list is what it uses rather than what it would inherit, while `_menu` at 13105 pays for `MainMenuScene.h`, `LoadingScene.h` and `SaveFile.h`, which it genuinely needs. Keeping a new file's includes tight is most of what decides where it lands.

**Anything read from a file or a socket is someone else's.** Maps, mods,
models, scripts and films come from community sites, peers send packets, and
the lobby relays other players' strings. `docs/SECURITY-AUDIT.md` (2026-09-25)
lists what that has cost -- a heap overflow from an archive sitting in the data
folder, a network game frozen by an honest fifty-unit order -- and the rules
that came out of it. The short version: every count, size, offset and index
from outside is checked before it sizes or indexes anything, an `assert` is not
a check because release builds drop it, every walk over a structure the input
describes is bounded, a fault in content costs that content and never every
peer, and a new parser gets a case in `src/rwe/io/malformed_input.test.cpp`.

**A forward declaration that says `class` where the definition says `struct`
breaks MSVC and nothing else.** MSVC mangles the class-key into the symbol
name and keeps whichever tag the translation unit saw first, so a header that
forward-declares `class GameSimulation;` makes every object compiled through it
mangle `class` while `GameSimulation.obj` mangles `struct`, and the two never
meet. gcc, clang and MinGW ignore the tag when mangling, so the only sign is a
Windows-MSVC link failure while every other job is green.

`DemoRecorder.h` did this and took `revival` red on 2026-09-24: four unresolved
externals on `DemoRecorder`'s own methods, in a file that had plainly compiled.
Clang had said so in the same run and the warning scrolled past --
`-Wmismatched-tags`, "may result in linker errors under the Microsoft C++
ABI". **That warning is the check**, and it is worth reading Linux clang's
output for it whenever MSVC alone fails to link. The engine's aggregates are
`struct` far more often than `class`, so `struct` is the safer guess when
adding a forward declaration, and matching the definition is the rule.

## Code Conventions

- All C++ code is in the `rwe::` namespace
- Formatting enforced by `.clang-format`: Allman brace style, 4-space indent, no column limit. (It still declares `Standard: c++17`; that governs only how clang-format parses. The project builds as C++20.)
- Test files live alongside source: `src/rwe/[subsystem]/[Component].test.cpp`
- Shared sim test helpers live in `src/rwe/sim/sim_test_util.h` (`makeFlatTerrain`, `makeEmptyCobScript`, `addPlayer`, `addUnitOfType`, `tick`, `everFires`). Use those rather than writing another copy — there were fourteen copies of `makeEmptyCobScript` and forty of `makeFlatTerrain` before they were collected. A test that genuinely needs something different keeps its own, under a name that says what is different: `addPlayerWithNothing` in the reclaim tests, `addUndamagedUnitOfType` in the repair tests, `addPlayerWithEmptyStores` in the wreckage tests, `addPlayerWithLargeStores` in the save/load tests, `addFiringUnitOfType` in the patrol tests, `makeDryTerrain` in the transport tests.

  Two rules the `makeFlatTerrain` collection leaves behind. **`makeFlatTerrain` has no default arguments on purpose** — the forty copies carried four different defaults and six different baked-in sizes, so the same bare call meant a 16-square map in one file and a 1024-square map in another, and a shared default would only have hidden that again. Say the size. And **an anonymous namespace inside `namespace rwe` does not shadow the enclosing namespace** — it joins the same overload set, so a file-local `addPlayer` beside the shared one is an ambiguous call, not an override. That is what a name saying the difference is for.
- Strong typing via opaque ID types: `UnitId`, `PlayerId`, `ProjectileId` (see `OpaqueId`)
- Variant-based state machines for unit behavior and navigation goals
- Error handling uses `Result<T, E>` types rather than exceptions
- Version derived from git tags (format: `v#.#.#`)

## CI

GitHub Actions (`.github/workflows/build.yml`) runs Linux (gcc-14, clang-18 on ubuntu-24.04) and Windows (MSVC 2026, MinGW64) builds in both Debug and Release configurations.

## Contributing

`revival` is the working branch and the default. **Only the maintainer pushes to it directly.** Everyone else — human or agent — raises a pull request against it: one branch per issue, named `<issue>-<slug>` like the existing branches, labels applied as you open it. CI runs on the PR the same as it would on a push.

## Labels

Every issue and pull request carries **two** labels, one from each axis: where
the change lands, and why it exists. Purpose takes exactly one, scope one where
one fits, so the pair reads as "sim, conformance" or "presentation, maintenance".
Apply them as you open the thing: `gh issue edit <n> --add-label scope:sim
--add-label purpose:conformance`, or the same two `--label` flags on create.

The axes and every label on them are in `docs/agents/labels.md`. The pair to keep apart is `purpose:conformance` and `purpose:rwe-original`: the first means a decoded routine or a fixture is the standard and a mismatch is a bug, the second means TA has nothing to say here. A departure from a decoded behaviour on purpose is neither — it is `purpose:divergence`, and `TOTALA-EXE.md` §88 has to say so before the label is honest.

## Saved games

Full-state save/load lives in `src/rwe/game/save_util.*` (simulation
serialization, hash-validated round trip — see `sim/saveload.test.cpp`) and
`src/rwe/game/SaveFile.*` (the on-disk container with the map/players header
and the skirmish options). Saves are `<name>.rwesave` under the local data
path. See the determinism section above for what a new piece of sim state
obliges you to touch. The `SaveFile` header's `PlayerInfo` now carries `teamId`
alongside the simulation's own player table, which is what a load actually
restores.

## Matching Total Annihilation

Much of the current work is matching the original's behaviour down to the
arithmetic. Where a behaviour is meant to match TA, it has usually been read out
of `TotalA.exe` instead of guessed at.

- `docs/TOTALA-EXE.md` — **the index to the findings**, and the two sections
  everyone is told to read first: §88, where RWE deliberately differs, and
  §91, what is decoded but not ported. 114 findings numbered to 116 (§83 and
  §84 do not exist). **The numbers never move**, so a §n written anywhere in
  the tree names the same finding for ever; the index says which file holds
  it. The subjects are `-MOVEMENT`, `-VISION`, `-ECONOMY`, `-WEAPONS`,
  `-RENDER`, `-INTERFACE`, `-KEYBOARD`, `-TRANSPORTS`, `-MUSIC` and `-DATA`,
  plus the older `-SHADING`, `-WRECKS` and `-MISSIONS`. Start at the index
  rather than grepping: §53 and §54 are marked there as superseded by
  `-SHADING`, and reading either without that marker gets unit lighting wrong.
- `docs/TOTALA-EXE-SHADING.md` — the shaded unit rasterizer in full, in two
  halves. Part one is the geometry: the 16-byte vertex record, the per-vertex
  shade level and its `& 0x1F`, the averaged (and deliberately unnormalised)
  vertex normals, and the Gouraud interpolation of the integer row. Part two
  (sections 17-27) is the span filler: the row is truncated per pixel with
  `sar 16` and indexes `PALETTE.SHD[row * 256 + texel]` with **no** second
  mask, clamp, ambient, fog or blend anywhere in the loop, the height test
  sorts by model-space Y as an unsigned byte, and a unit whose FBI says
  `ZBuffer=0` is drawn through a path that does not shade at all (CORFAV and
  CORTRUCK are the only two in the shipped set). Read this before touching the
  unit shaders. It overturns two earlier readings: the original *does* light its
  models (the "no lighting" finding had read the `SHADING=off` path), and the
  sun vector is not normalised, which sets the ramp's width at thirteen rows
  rather than thirty-two. The `& 0x1F` wrap is not the original being crude;
  it is where its contrast comes from, and a face one row below zero really
  does come out at the top of the table.
- `docs/TOTALA-EXE-WRECKS.md` — where a wreck comes from, and why one over
  water sinks: the corpse spawns at the dying unit's exact height and the
  water branch adds a fixed 0.175 units a tick of downward velocity, spent by
  a per-tick sweep. The exemption for `IsFeature=1` — the floating dragon's
  teeth — is what shows the rule is deliberate. It also names all eleven
  death causes, and the one that mattered is cause 7: `IsFeature=1` again,
  read from the same bit, which is why such a unit always leaves the intact
  wreck whatever its `Killed` ladder asked for.
- `docs/TOTALA-EXE-MISSIONS.md` — how aircraft decide *where to go* when
  attacking: the mission name table and its handlers, the bomber attack run,
  the fighter strafing pass, the gunship standoff ring, and what `hoverattack`
  and `maneuverleashlength` actually gate.
- `docs/compatibility.md` -- the plain-language account of where RWE and the
  original part company and where they deliberately do not: §88 regrouped by
  why the difference exists, plus the original's quirks that are reproduced on
  purpose. Written for a player or a modder; §88 stays the record for anyone
  changing the code.
- `docs/TA-PATCHES.md` — what the official v3.1 patch and the 2013 unofficial
  patch each changed, and what of it RWE needs. The short answer is that the
  GOG executable is already v3.1, so every engine fix the official patch made
  is already in the binary the findings were read out of; the unofficial patch
  changes the simulation in exactly one place (the pathfinding budget, raised
  fifty-fold), which RWE is already past. What is left is a handful of v3.1
  interface features, listed there and in the roadmap.
- `docs/TOTALA-EXE-EXTERNAL.md` and `docs/TOTALA-EXE-AI.md` — **not read by
  this project.** The first holds findings from the Nanolathe project's
  independent clean-room reading of the same binary, taken in on 2026-09-24 as
  a cross-check; it settled six disagreements, every one against our corpus,
  and closed five questions ours had left open. The second is the retail
  computer player, a subject ours never decoded, whose headline is that the
  original has no transport policy and never gives an aircraft an attack order.
  Read `-EXTERNAL`'s opening sections before relying on either: nothing in them
  is verified here unless it says so at the point of use.
- `docs/TA-COMMUNITY-AI.md` — what the original's AI *modders* learned, from
  Switeck's design guide and nine shipped community AI packs: the profile
  grammar, the benchmark timings an AI was judged by, and what five expert
  profiles independently agreed to build. Corroborates the AI decode from a
  direction that owes nothing to a disassembler. The lists do not transfer to
  RWE — their whole method is subtraction from a catalogue — but the economic
  doctrine and the benchmarks do.
- `docs/TA-DEMOS.md` — the `.tad`/`.ted` demo format, and why a demo is a stream
  of *state and effects* rather than of orders: TA is owner-authoritative, not
  lockstep, so a demo cannot be fed to `GameSimulation` and playback would
  have to puppet the units directly. What they are good for instead is a
  conformance corpus — build timings, economy curves and weapon events pulled
  out as short bounded episodes with real numbers in them — plus the filters
  that make such an episode mean anything. The container and the three
  transforms are ported (`src/rwe/io/tad/`) and verified over thirteen real games,
  so the format sections are no longer the bare transcription they were; the
  *interpretation* of most subpacket payloads still is, and none of it comes
  out of `TotalA.exe`. The correction worth knowing about is that `0x20` is 186
  bytes in the packet stream and 192 in the header's status record — the
  reference has the second number in the table it walks the first with, which
  silently swallows nine tenths of the alliance records.
- `docs/REVERSE-ENGINEERING-PRIORITIES.md` — what is worth reading out of the
  binary next, ranked, with the evidence that each is a real gap and a string
  or offset to pivot on. **The ranked list is empty as of 2026-09-15** -- every
  entry has been read or refuted, and the file is now a record of what each
  turned into and what it got wrong. Append the next one there.
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

## Agent skills

### Issue tracker

Issues live in GitHub Issues for CubeB/RWE-B4, managed with the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical triage roles map to same-named labels (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` at the repo root plus `docs/adr/`. See `docs/agents/domain.md`.
