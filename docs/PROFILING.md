# Profiling the tick and the frame

Two compile-time profilers, both off by default and both pure observers.
Neither has a CMake option: define the macro instead.

Defining `RWE_ENABLE_SIMPROF` compiles in the per-phase tick timers declared in `src/rwe/sim/sim_prof.h`. There is no CMake option for it — configure with `-DCMAKE_CXX_FLAGS=-DRWE_ENABLE_SIMPROF`, or define it in the one translation unit you are measuring. They are off by default because the timers cost a clock pair each and their call sites sit inside per-unit work, so leaving them live would tax the very thing they measure. With them on, `GameSimulation::tick` writes a `SIMPROF ticks=…` line to the log every two seconds, broken down by phase. That breakdown is what found both hotspots in the performance pass (unit behaviour and pathfinding, almost exactly equal at 11.9 ms and 11.4 ms a tick with 800 units); measure with it before optimising anything, because the obvious guess there — the renderer — was wrong.

Defining `RWE_ENABLE_RENDERPROF` does the same for the frame: `src/rwe/render/render_prof.h` is deliberately the same shape as `sim_prof.h` (same macro pattern, same zero-the-slots reporting, same reason for being off by default), and `GameScene::render` writes a `RENDERPROF frames=… fps=…` line every two seconds. It covers the whole frame, not just the drawing: `loop` is the frame, `update` the scene update with the simulation ticks inside it (`u.simtick`, `u.particles`, `u.events`), `world` the world render broken down by phase (`w.terrain`, `w.unit.build`/`w.unit.draw`, `w.shadow.build`/`w.shadow.draw`, `w.wake`, `w.post`, …), and the `n.*` slots are counts rather than times — draw calls, meshes in each batch, live particles — because a millisecond figure says a phase is expensive while the count says whether it is expensive because of how much it draws or how often it asks. Both profilers can be on at once: `-DCMAKE_CXX_FLAGS="-DRWE_ENABLE_RENDERPROF -DRWE_ENABLE_SIMPROF"`. That breakdown is what found the renderer's costs in the 2026-09-05 pass, and it corrected the roadmap twice over — the sim tick was no longer 25 ms but 4, and on a water map the largest cost in the frame was neither the sim nor the renderer but the wake particles, split across both.

Two things to know when reading its numbers. The frame is vsync-capped at the display's rate, and nothing sets the swap interval, so once `loop` reaches 16.67 ms the wait shows up inside `ui` (the first GL work of the next frame) rather than in `swap` — a `ui` of 8 ms next to a `world` of 5 ms means the frame is finished early and waiting, not that the HUD got slow. And a `battle_test` run drifts: the fight moves, so how much is on screen varies between runs by a third or more. Compare `n.unitmesh` and `n.waketri` before comparing times, and prefer per-item figures when they differ.

## Release diagnostic logging

Release builds (`NDEBUG`) default to Info rather than Debug. Debug builds
retain Debug output. Warnings, errors, informational status and opt-in AI
profiling reports remain enabled and retain immediate flushing. The frequent
frame-buffer, path and AI planner Debug messages are compiled out, including
their argument formatting. This is RWE-original diagnostic policy; see
[PERF-LOG-001](PERFORMANCE-LOGGING-FINDINGS.md), which records the static
byte-tactics comparison and its limits. No original-runtime parity is claimed.

For a diagnostic Release build, set the compiler definition explicitly, for
example `-DCMAKE_CXX_FLAGS=-DRWE_LOG_LEVEL=0`. Combine it with the existing
profiling definitions in the same CMAKE_CXX_FLAGS value when needed. A CMake
cache variable named RWE_LOG_LEVEL alone is not wired to compiler definitions.

On 2026-10-21 the focused logger suite passed in Debug (31 assertions), Release
(30), and Release with the explicit level-0 override (31), including verification
that disabled diagnostics do not evaluate operands or capture an outer else.
A 10,000-iteration diagnostic-only benchmark wrote 20,000 lines / 1,667,784 bytes
and took 376-417 ms with level 0; the default Release build wrote zero bytes and
evaluated zero debug operands. This demonstrates eliminated logging work, not
a measured game-frame improvement or an established explanation of lag spikes.
The benchmark was run while other development work was active; do not treat
its elapsed-time values as an isolated hardware performance measurement.

## Current threading boundaries

The game scene's update, simulation ticks (including AI and pathfinding), and
render submission execute in order on the scene thread. Networking uses worker
threads (`GameNetworkService`, `LoadingNetworkService` and the lobby/NAT
services); SDL's mixer runs audio callbacks on its audio thread. GPU rendering
is also separate from CPU submission. There is no general simulation worker
pool or option that distributes individual AI players across CPU cores.

Any parallel simulation work would need immutable inputs, separate result
buffers, and deterministic application order. In particular, merely starting
one thread per AI would expose shared observers/logging and simulation reads
to concurrency without establishing their safety. The measured saved-match AI
passes averaged approximately 0.5 ms per tick for all three seats combined;
those observations do not justify a simulation-thread refactor as a remedy
for the observed 430 ms gap. Further frame/simulation timing is needed to
attribute that gap. Live observation remains paused at the user's request.

The isolated full Release game and ai_arena builds passed with RWE_WERROR=ON.
Two shipped-data Full Moon games (seed 6202, four Hard RWE AI seats, ARM/ARM
allied against CORE/CORE, 300 simulated seconds each) reached tick 9000, emitted
no Debug records, and produced identical hash logs for ticks 1-8999 (SHA-256
B2C8A60610F7D3680E9DCCACC6F50567FB0F1090A953E3E484A85CEB4A7384D9).
The arena exits at tick 9000 before recording that tick's diagnostic hash.
This is a repeatability check of the changed RWE build, not a comparison with
the original executable or proof that the user's stalls have been eliminated.
The isolated checkout is based on 82a422c3 and excludes the concurrent AI edits.
