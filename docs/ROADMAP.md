# Robot War Engine — Revival Roadmap

_Last updated: 2026-08-27. Status of the codebase is as of the `revival` integration branch (upstream `master` @ b2d8a31 + merged fork work, see Phase 0)._

## Where the project stands

- **Engine:** ~49k lines of C++20, deterministic fixed-point sim at 30 ticks/s, OpenGL 3 renderer, COB VM, A* pathfinding, lockstep multiplayer over the internet. Loads original TA data (HPI/GAF/3DO/COB/TDF/TNT/OTA).
- **Launcher:** Electron 22 / React 16 / Redux lobby + master server + `rwe_bridge` IPC.
- **History:** Michael Heasell (2017–2023, ~1,950 commits). Kevin Hake modernised the build in March 2026 (Boost removed, C++20, SDL3, CI green). Taylor Gunnoe added hotkeys / speed / pause / Phase‑1 AI in April 2026 (unmerged upstream until now).
- **Never had a stable release.** Only tag is `v0.1.0` (2017); the CMake version is derived from git tags.

## Guiding principles

1. **Determinism is load-bearing.** Anything that influences the sim lives in `sim/`, uses `SimScalar`/`simulation.rng`, no wall-clock or unordered iteration. Every merged feature must keep `rwe_test` and the `GameHash` sync checks green.
2. **Data-driven first.** TA data dictates UI screens and unit behaviour; prefer implementing what the FBI/TDF/GUI files already describe over inventing new config.
3. **Small, mergeable PRs against a green CI.** The March‑2026 CI matrix (gcc‑14, clang‑18, MSVC 2026, MinGW64, Debug+Release) is the gate.
4. **Personal project, for now.** No upstream coordination or outreach is planned. The forks by Kevin Hake, Taylor Gunnoe and Oskar Pedersen are reference material — check them before starting a feature so their work isn't redone.

---

## Phase 0 — Foundation (now → 2 weeks)

Goal: a fork with green CI, a reproducible local build, and a tagged pre-release.

- [x] Local toolchain on `D:\` (MSYS2/MinGW64 at `D:\msys64`, Node 24 at `D:\tools\node`).
- [x] Integration branch `revival` = upstream `master` + `KevinHake/master` (+2: tagged release job, Linux AppImage) + `tgunnoe/feats/next` (+11) + `OskarPedersen/energy-per-shot` (PR #176) + upstream `reclaim` (+5).
- [x] Build `revival` locally (MinGW64 Debug) and run `rwe_test` — 133 test cases / 1,487 assertions pass (2026‑08‑27).
- [x] Reclaim merge verified: compiles and tests pass alongside `feats/next`.
- [x] Fix Windows checkout: `libs/asio/asio/include` is a git symlink that materialises as a text file without `core.symlinks`; CMake now uses the real `libs/asio/include`.
- [x] Smoke-test with real TA data (GOG archives at `D:\RWE-Data`, junctioned to `%AppData%\RWE\Data`): main menu loads; `--map "Coast To Coast"` skirmish runs with Human + Computer players, 0 warnings.
- [x] Release build (`build-release/`, 40 MB `rwe.exe` vs 192 MB Debug): tests pass, AI skirmish clean. Use this one to play.
- [ ] Push `revival` to a private GitHub repo (or fork) so the CI matrix runs; confirm it is green.
- [ ] Tag `v0.2.0-pre1` and let Kevin's release job produce Windows zip/installer + Linux AppImage.
- [ ] Update `README.md` download section (AppVeyor link is dead-end) and `CLAUDE.md` (says C++17; it is C++20).
- [ ] Decide on `experimental/sdl-gpu` (Kevin's SDL_gpu + HLSL/SPIR‑V PoC): keep as a branch, do not merge yet.
- [ ] Decide on upstream `update-protobuf` (protobuf 25.1; needs a CMake-based `build-protobuf.sh` since autotools is gone in 22+). Defer to Phase 5.

## Phase 1 — Playable single-player skirmish loop (≈ 2–3 months)

Goal: a full game vs. no opponent feels like TA — every basic order works, UI is complete enough that a player never needs the F10 debug menu.

**Orders & unit behaviour**
- [x] Reclaim features: the merged branch only ran a timer. Now work = metal + energy at `workerTimePerTick` per tick, resources credited progressively, feature deleted and `featureReclamate` spawned on completion; progress lives on the feature so several builders can share it. Tested at sim level and through `tick()`. Closes the "corpses clog the battlefield" note from the 2022 blog post.
- [x] Reclaim units (own or enemy, in range): work = `buildTime`, payout = build cost × fraction actually built, progress on `UnitState` (hashed + dumped), dies without wreck or explosion (`DeathType::Deleted`). Tick-driven test covers a builder reclaiming an enemy solar.
- [ ] Reclaim polish: play the feature's `seqNameReclamate` animation and reclaim sound; show progress in the unit info panel; auto-reclaim (`autoreclaimable`) when a builder is idle with the area-reclaim command.
- [x] Repair: `RepairOrder` heals at the build rate (free, as in TA) or finishes an unfinished unit; REPAIR panel button, repair cursor mode, and left-click-on-damaged-friendly for builders. Also fixed `buildExistingUnit` checking `isBeingBuilt` against the builder's definition instead of the target's.
- [x] Patrol: `PatrolOrder` loops through waypoints (re-queued on arrival), engages enemies in primary-weapon range en route unless on hold fire; PATROL button + `cursorpatrol` sprite; a single click patrols between the point and the unit's position.
- [x] Capture: `CanCapture` now parsed from FBI; `CaptureOrder` works like repair but accumulates `captureProgress` on the target and swaps owner at `buildTime`, clearing its orders/targets; `UnitCapturedEvent` drops it from the old owner's selection. CAPTURE button + `cursorcapture`.
- [ ] Resurrect (turn a corpse feature back into the unit it came from; needs a corpse→unit-type mapping from `featureDead`/`corpse` definitions).
- [ ] Builders on patrol should repair/reclaim along the route as in TA; capture progress should decay when the captor stops.
- [ ] Transports (load/unload, `TransportCapacity`, `TransportSize`) — currently zero references in the sim.
- [x] Self-destruct: `Ctrl‑D` starts a 5 s countdown (toggle again cancels), the sim detonates the unit with its `SelfDestructAs` weapon (new FBI field, falls back to `ExplodeAs`), no wreck; seconds remaining drawn above the unit. Blink/flash effect not yet done.
- [x] COB getters: Health, VeteranLevel, MinId/MaxId/MyId, UnitTeam, UnitBuildPercentLeft, UnitAllied etc. were implemented in the merged `feats/next` work. Remaining FIXMEs (UnitTeam returning player id, UnitAllied = same owner) need a team/alliance model — tracked under multiplayer.
- [x] Pathing: units slow when climbing (top speed → ½ at the slope limit, floor ¼) (#45); ground steeper than half the slope limit is rough terrain for routing (#33); no diagonal squeeze between touching obstacle corners (#30); an unreachable move target ends at the closest reachable point and the order completes instead of re-pathing forever (#28). Tests in `src/rwe/pathfinding/pathing.test.cpp`.
- [x] Units rock when they fire (#40): `RockUnit(x, z)` is now called on every shot with the recoil direction in the unit's frame (Spring-style scaling).
- [ ] Aiming scripts run twice (#42): after `AimPrimary` returns, the engine re-checks the heading against the script's last aim and re-aims if the target drifted beyond tolerance; needs a repro with logging before changing (the check protects ballistic accuracy).

**In-game UI** (stated focus when upstream stalled)
- [ ] F2 in-game menu: save/load placeholders, options, exit (#154).
- [x] Hotkey help overlay (#155): F1 toggles a two-column reference of every binding.
- [ ] Unit info panel completeness (HP, resource make/use, build progress, kills), order buttons wired to every order above.
- [ ] Build menu paging, queue display, `Shift` queue count badge.
- [x] Pause/speed indicators on screen — already present in the merged work ("Paused" and the +N speed readout).
- [x] Resource stall: spending is now gated on what is actually on hand at the moment (stock + income so far this second − spent so far), so an empty stockpile throttles building continuously to the income rate instead of one-second bursts; stalled energy/metal bars flash red as in TA.
- [ ] Focused-control highlight (#5), list-box selected-item brightness (#6).

**Game flow**
- [x] End-of-game flow: result panel (VICTORY/DEFEAT/DRAW, game time, units destroyed/lost) instead of exiting; the sim keeps running for spectating; Escape returns to the main menu. Single-player games are never "decided". Per-player kill/loss tallies added to `GamePlayerInfo`.
- [x] Line of sight / fog of war / radar: per-player explored/visible/radar grids in the sim (32 world units per cell, rebuilt each tick); sight rays are blocked by higher ground so hills cast shadows; unseen enemies hidden, radar contacts drawn as blips, unexplored features hidden, terrain and minimap fogged with a soft ragged edge (4× upsampled noise threshold, linear filtering), unseen units not clickable; F10 toggle. Not yet: radar jammers, sonar, stealth/cloak.
- [ ] Music playback (`AudioService` has no music support) and sound completeness (unit sound types are defined, many unwired).

## Phase 2 — AI opponent (≈ 2 months, overlaps Phase 1)

Follows `docs/ai-architecture-proposal.md` (tgunnoe). All AI code stays in `sim/` and emits ordinary `PlayerCommand`s, so multiplayer and replays stay deterministic.

- [x] Phase 1 (merged): commander builds fixed opening — runs on Coast To Coast.
- [x] Fixed `Blocked waiting for player commands` with a Computer player: the AI buffer was refilled one entry per frame, so any frame dispatching ≥2 sim ticks (catch-up, or game speed > 1×) skipped a tick. AI buffer is now topped up to `targetCommandBufferSize` like the human buffer (`4b0b17a6`). Verified 0 blocked ticks in Debug and Release.
- [ ] AI in multiplayer: each peer would generate AI commands locally and never send them, so an AI player in a network game will desync. Either run the AI on one host and transmit its commands, or make AI command timing deterministic. Not needed for single-player.
- [x] Phase 2: perception through fog of war (Brutal is omniscient), threat map (anti-ground DPS / economic value / staleness), scouting toward unseen ground, expansion build order (solars, mexes, lab, radar, laser towers facing the enemy, stall reactions), factories producing constructors then raiders/rockets 2:1, idle builders assist the factory.
- [x] Phase 3 (single-army version): rally point facing the enemy, Defend on intruders, Attack on the most valuable known enemy ground once the army reaches the profile's size, regroup when depleted; Easy/Standard/Hard/Brutal profiles via `--ai-difficulty` (Brutal: omniscient + 1.25× income, applied in the sim).
- [x] Phase 4 (lite): F10 debug window lists each AI's phase, economy, unit counts, army and target; `rwe.log` gets a 30 s AI status line (phase, income/demand, counts, what the commander is doing) and phase transitions.
- [x] Verified on Coast To Coast (Hard): 3 mexes + 4 solars + lab by 2.6 min; 5 mexes, 2 metal makers, 3 constructors, radar, towers and first combat units by 8 min. Fixed along the way: commander walling itself in (fixed home anchor, two-tile lanes; unreachable build sites are dropped by the sim), extractors on bare ground (surface metal is not a patch), planner stalling when one need had no site.
- [ ] AI polish: several platoons with unit-role templates; TDF-loaded profiles and build orders; per-slot difficulty in the lobby; metal makers / storage; use of radar jammers and air; tuning from AI-vs-AI runs.
- [ ] Deferred (per proposal): naval doctrine, air-only strategies, multi-AI teams.

## Phase 3 — Multiplayer polish & launcher (≈ 2 months)

- [ ] Launcher dependency refresh: Electron 22 → current LTS, React 16 → 18, Redux Toolkit; drop `react-hot-loader`. Re-run `npm audit` (dependabot PRs #166/#167 still open).
- [ ] Host a public master server (currently "connect to localhost" in dev); fix non-recommended port (#60).
- [ ] Desync detection UX: `GameHash` mismatch → show which tick, dump state (`dump_util`) for bug reports.
- [ ] In-game chat (upstream `network-chat` branch is a 1‑commit scaffold; start from it or from scratch).
- [ ] Replays: record the `PlayerCommand` stream + seed; playback through the same sim.
- [ ] Lobby mod management (the stated reason the launcher exists): detect installed `.hpi/.ufo/.ccx`, hash them, require all players match.
- [ ] Reconnect / drop handling instead of hard failure.

## Phase 4 — Compatibility & content breadth (ongoing)

- [ ] Map-pack robustness: zlib failure on large `.ufo` (#93), "Expected property name" parse crash (#38), missing `StartPos2` schema key (#49), non-destructible doodads (#52), extra tree on Show Down (#70).
- [ ] 3DO texture distortion (#7), GAF animation off-by-one (#82), shadows on water (#25).
- [ ] Run the engine against Core Contingency, Battle Tactics, and the big community mods (TA:Escalation, TA:Mayhem) and log incompatibilities as issues.
- [ ] Invalid UTF‑8 resilience (#14).
- [ ] Compile a `docs/compatibility.md` of what TA behaviour is intentionally *not* replicated (bugs vs features).

## Phase 5 — Performance, renderer, platform (ongoing, low priority until Phase 1 done)

- [ ] Profile Abysmal Lake / Canal Crossing (#62, #111); batch unit/feature rendering (#16; the 2019 `renderer` branch is a 618‑commit‑stale reference only).
- [ ] TDF parser speed in MSVC Debug (#66) — Kevin's `optimize-tdfparse` was merged; re-measure.
- [ ] SDL_gpu backend (from `experimental/sdl-gpu`) once the GL renderer is feature-complete; this is the path to Vulkan/Metal/D3D12 and to WebAssembly (#178's motivation).
- [ ] Protobuf upgrade (from `update-protobuf` branch) with a CMake build; or replace protobuf with a hand-rolled binary codec — the schema is ~15 messages.
- [ ] Official Linux (AppImage — job exists) and macOS builds; reproducible builds (#35).
- [ ] Reduce `GameScene.cpp` (3.8k lines) — split input handling, HUD, and command dispatch.

## Release cadence

- Pre-release tag on every merged phase milestone; changelog generated from commit titles.
- Keep `CLAUDE.md` accurate — it is the onboarding doc for AI-assisted work on the codebase.
- Public announcement (TAUniverse thread t=45555, upstream issues) is deliberately deferred until the project is playable.

## Issue-to-phase map (open upstream issues)

| Phase | Issues |
|---|---|
| Closed by merged work | #131–#152 (Ctrl hotkeys), #153 (pause), #175 (speed), #176 (energyPerShot), #178 (SDL3 done upstream) |
| 1 | #5 #6 #28 #30 #33 #40 #42 #44 #45 #154 #155 #173 |
| 3 | #60 #166 #167 |
| 4 | #7 #14 #38 #49 #52 #70 #82 #93 #25 |
| 5 | #11 #16 #35 #62 #66 #111 #24 (obsolete — Boost gone, close) #108 (superseded — close) |
