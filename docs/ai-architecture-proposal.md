# RWE Skirmish AI Architecture

**Status: built. This was a design proposal (rts-ai-architect agent, 2026-04-26); it is kept because the architecture it describes is the architecture that exists.** The AI lives in `src/rwe/ai/` and plays a skirmish game. Read the header below for what was built and where it diverged, then the body for *why* each decision was made. **Section 14 is the current plan** and is the one to read if you are about to work on the AI: it is written against the Wikibooks *Total Annihilation Tactics and Strategy Guide*, taken as a specification for what a competent human does, and it orders the work by what would most stop the AI looking like a beginner. Section 13 is the gap list it grew out of and still holds the file-by-file plumbing traces: it was written after the September 2026 map/fog/loss pass and says what is still missing, what each piece would take, and which files a per-slot AI setting has to pass through — that reasoning is not written down anywhere else, and §12's answered questions in particular are the record of decisions that would otherwise have to be re-argued.

The text from §1 onwards is the proposal as written, in the future tense it was written in, annotated where reality departed from it. Its RWE `file:line` citations are as of April 2026 and most have moved; the file paths are still right, and the architectural claims still hold. The paragraph this header replaces asserted that there was no computer player at all, citing a `// TODO: implement computer AI logic` in `GameScene` that no longer exists.

## What was built

The shape is as proposed. `GameSimulation` owns `aiControllers` (a `PlayerId` → `AiPlayerController` map) and `aiPendingCommands`; the controller runs inside `GameSimulation::tick`, writes ordinary `PlayerCommand`s into the pending buffer, and `GameScene` drains them through `takeAiCommandsForPlayer` into `playerCommandService` exactly as it pushes a human's input. That is §4's placement B with the delayed-emit indirection, unchanged. The managers are there too: `StrategicManager`, `EconomyManager`, `BuildManager`, `ArmyManager`, `ThreatMap`, `AiBlackboard`, `AiTuningProfile`.

Four managers the proposal did not anticipate were added as the work went on: `PerceptionManager` (what the AI knows through fog, since fog of war landed after this was written), `ScoutManager`, `TransportManager` and `ReachabilityMap`. Each has an entry in `docs/ROADMAP.md` Phase 2.

Three things were designed and not built:

- **Platoons.** There is one army, not a set of platoons with composition templates. `AiIds.h` held `PlatoonId` as a declared-but-unused tag, and was deleted in the September 2026 cleanup along with `AiTaskId` and `BuildJobId`, none of which anything included. §7 is therefore a design that has not been tested against reality.
- **Data-driven profiles.** §6 and Q3 call for build orders and tuning in TDF. `AiTuningProfile` is hard-coded C++ with a profile per difficulty tier. Nothing loads from `data/ai/profiles/`.
- **A separate `TacticalLayer`.** Target selection and retreat live inside `ArmyManager` rather than in a layer of their own.

And one difficulty tier was renamed: the proposal's **Normal** is **Standard** in `AiDifficulty`. Selected with `--ai-difficulty`.

The known gap the proposal did not foresee is **multiplayer**: each peer generates its AI's commands locally and never sends them, so an AI player in a network game desyncs. Single-player is unaffected. Fixing it means either running the AI on one host and transmitting its commands, or making AI command timing deterministic across peers.

---

## 1. Goals and non-goals

### Goals (Must Have)

- **Functional opponent.** From a single Commander, the AI builds a viable economy, expands metal extraction, builds factories, defends its base, scouts, and launches attacks against the human. A human at the lowest difficulty can lose a five-minute game if they idle.
- **Same-channel parity with humans.** The AI emits `PlayerCommand` values into the same `PlayerCommandService` pipeline humans use, so multiplayer (replays, netcode, desync detection) keeps working with zero new plumbing.
- **Deterministic across clients.** AI decisions for a given `PlayerId` produce identical command streams given identical sim state and seeded RNG.
- **Tunable, data-driven.** Build orders, threat weights, retreat thresholds, platoon compositions, and tier transitions are expressed in a config (TDF or JSON), not hard-coded.
- **Layered, Sorian-style.** Strategic / Economy / Build / Platoon / Threat / Tactical separation, so a future contributor can swap, say, the Build Manager without touching threat code.
- **Difficulty scaling without hard cheats by default.** Scale via parameter tuning (build aggressiveness, scouting radius, reaction time). A separable, off-by-default "resource cheat" knob is provided for the highest tiers.

### Non-goals (Won't Do, This Pass)

- **Adaptive learning across games / PvP-grade play.** No reinforcement learning, no opponent modelling beyond a few rolling counters. We aim to be the *most fun* punching bag for one to four humans, not to win SC2-tier tournaments.
- **Perfect micro.** No sub-tick reaction, no frame-perfect kiting, no APM throttle theatrics. Tactical layer optimises for "doesn't look stupid" not "world champion".
- **Naval or amphibious mastery.** First-class targets are land + air. Naval support is scoped only as much as a few TA stock maps demand (T1 boatyard + cons-ship parity); deep naval doctrine is deferred.
- **Cooperative ally micro (multi-AI teamwork).** AIs on the same team will not collude beyond "don't shoot each other" until a follow-up.
- **TA `.AI` profile binary parity.** See §10. We will not parse the original `.AI` weighted-list files; we will accept the OTA `aiProfile` *string* as a name to look up our own RWE-native config.

### Difficulty tiers (proposed)

Four tiers, picked at lobby time. All tiers run the same code; they differ only in `AiTuningProfile` parameters.

| Tier   | Build aggression | Scout cadence | Cheating | Notes                                                |
|--------|------------------|---------------|----------|------------------------------------------------------|
| Easy   | Low              | Sparse        | None     | Builds slowly, attacks late, holds back army.        |
| Normal | Medium           | Regular       | None     | The reference experience.                            |
| Hard   | High             | Aggressive    | None     | Tighter build orders, earlier raids.                 |
| Brutal | High             | Aggressive    | Resource bonus (×1.25 metal/energy income, off by default) | Optional, opt-in cheat. |

*As built:* the tiers are `AiDifficulty::Easy / Standard / Hard / Brutal` — **Standard**, not Normal — selected with `--ai-difficulty`. Brutal is omniscient and takes the ×1.25 income, applied in the simulation; it is on at that tier rather than off by default, which is what Q2 decided. The profiles are hard-coded rather than loaded from a file.

---

## 2. High-level architecture

Sorian-style hierarchy. Each "manager" has a single responsibility and a defined update cadence. Managers communicate by writing into a shared `AiBlackboard` value type (a struct of small POD blackboard slices); no hidden global state.

```
                                 +--------------------------+
                                 |   StrategicManager       |     (every 1 s of sim time)
                                 |  - GamePhase: Opening,   |
                                 |    Boom, Attack, Defend, |
                                 |    Tech, Endgame         |
                                 |  - AttackTriggerScore    |
                                 +-----------+--------------+
                                             |
                       +---------------------+---------------------+
                       |                     |                     |
              +--------v-------+   +---------v---------+   +-------v-------+
              | EconomyManager |   |  BuildManager     |   | ArmyManager   |
              | (per 0.5 s)    |   |  (per tick)       |   |  (per 0.25 s) |
              | - target M/E   |   |  - active build   |   | - platoons    |
              |   surplus      |   |    queues per     |   | - missions    |
              | - BP allocation|   |    builder        |   |               |
              +--------+-------+   +-------+-----------+   +-------+-------+
                       |                   |                       |
                       |             +-----v------+         +------v------+
                       |             | BuildJob   |         | Platoon SM  |
                       |             | scheduler  |         | (Forming,   |
                       |             +-----+------+         | Moving, etc)|
                       |                   |                +------+------+
                       |                   |                       |
                       +-------------------v-----------------------+
                                           |
                                +----------v-----------+
                                |  TacticalLayer       | (per tick, per platoon/unit)
                                |  - target select     |
                                |  - retreat triggers  |
                                |  - issues UnitOrder  |
                                +----------+-----------+
                                           |
                                +----------v-----------+
                                |  CommandEmitter      | (per tick)
                                |  serialises into     |
                                |  PlayerCommand vec   |
                                +----------+-----------+
                                           |
                                  pushCommands(...)
                                           |
                                           v
                                   PlayerCommandService
```

Cross-cutting providers (read-mostly, always on):

- **PerceivedWorldModel** — what the AI knows about the map and enemies (mirrors sim today; will degrade with fog of war later — §8).
- **ThreatMap** — multi-layer influence grid; rebuilt every 1 s (§5).
- **ScoutManager** — drives intel gathering, owned by ArmyManager but its outputs flow into PerceivedWorldModel.
- **AiTuningProfile** — the immutable parameter bundle (loaded once per match).

### Update cadence summary

| Component             | Cadence                  | Why                                  |
|-----------------------|--------------------------|--------------------------------------|
| StrategicManager      | every 30 ticks (~1 s)    | Slow, high-level                     |
| EconomyManager        | every 15 ticks (~0.5 s)  | React to stalls quickly              |
| BuildManager          | every tick               | Issues build commands as BP frees up |
| ThreatMap rebuild     | every 30 ticks           | Expensive; periodic                  |
| ArmyManager           | every 8 ticks (~0.25 s)  | Reasonable mission cadence           |
| Platoon update        | every 8 ticks            | Movement / retreat decisions         |
| TacticalLayer         | every tick               | Targeting, micro                     |
| CommandEmitter        | every tick               | Drains queued intents                |

`SceneTickInterval` is 1/30 s (deduced from `GameScene::SecondsPerTick`); cadences expressed in ticks are exact and deterministic.

---

## 3. Data structures and types

A new subsystem at `src/rwe/ai/`. Files live alongside tests using the existing `[Component].test.cpp` convention.

Proposed file layout (no implementation yet, just shape):

```
src/rwe/ai/
    AiPlayerController.h / .cpp    -- one per AI player; owns the managers
    AiBlackboard.h
    AiTuningProfile.h / .cpp       -- loaded from RWE-native config; see §6
    AiIds.h                        -- PlatoonId, TaskId, BuildJobId
    PerceivedWorldModel.h / .cpp
    ThreatMap.h / .cpp             -- §5
    StrategicManager.h / .cpp
    EconomyManager.h / .cpp
    BuildManager.h / .cpp
    BuildOrder.h / .cpp            -- conditional build templates
    ArmyManager.h / .cpp
    Platoon.h / .cpp               -- state-machine platoon
    PlatoonComposition.h           -- "tier-1 raider squad" templates
    TacticalLayer.h / .cpp
    ScoutManager.h / .cpp
    CommandEmitter.h / .cpp
    UnitClassifier.h / .cpp        -- bucket UnitDefinition into roles/categories
    AiPlayerController.test.cpp
    ThreatMap.test.cpp
    BuildOrder.test.cpp
    Platoon.test.cpp
```

### Opaque IDs (new)

Pattern follows `src/rwe/sim/UnitId.h` exactly:

```cpp
namespace rwe
{
    struct PlatoonIdTag;
    using PlatoonId = OpaqueId<unsigned int, PlatoonIdTag>;

    struct BuildJobIdTag;
    using BuildJobId = OpaqueId<unsigned int, BuildJobIdTag>;

    struct AiTaskIdTag;
    using AiTaskId = OpaqueId<unsigned int, AiTaskIdTag>;
}
```

### Top-level controller

One per AI player. Lives at scene level (see §4 for which parts ride in `sim/`).

```cpp
class AiPlayerController
{
public:
    AiPlayerController(PlayerId playerId, AiTuningProfile profile, std::uint64_t seed);

    // Called from GameScene::update where the existing TODO sits (line 2080).
    // Returns the commands the AI wants to push this scene tick.
    std::vector<PlayerCommand> tick(const GameSimulation& sim, SceneTime now);

private:
    PlayerId playerId;
    AiTuningProfile profile;
    std::minstd_rand rng;       // sub-seeded from sim-side rng for AI-only choices
    AiBlackboard blackboard;
    PerceivedWorldModel world;
    ThreatMap threat;
    StrategicManager strategic;
    EconomyManager economy;
    BuildManager build;
    ArmyManager army;
    ScoutManager scout;
    TacticalLayer tactical;
    CommandEmitter emitter;
};
```

### Platoon (state-machine variant)

Follows the variant-based unit-state pattern already used for `CursorMode`, `UnitOrder`, etc.

```cpp
struct PlatoonStateForming    { SimVector rallyPoint; };
struct PlatoonStateMoving     { SimVector destination; };
struct PlatoonStateEngaging   { UnitId primaryTarget; };
struct PlatoonStateRetreating { SimVector retreatPoint; };
struct PlatoonStateDisbanding {};

using PlatoonState = std::variant<
    PlatoonStateForming,
    PlatoonStateMoving,
    PlatoonStateEngaging,
    PlatoonStateRetreating,
    PlatoonStateDisbanding>;

struct Platoon
{
    PlatoonId id;
    PlatoonState state;
    std::vector<UnitId> members;
    std::optional<AiTaskId> assignedMission;
    PlatoonCompositionTemplate template_;   // see §7
    SceneTime stateEnteredAt;
};
```

### Threat map

```cpp
class ThreatMap
{
public:
    enum class Layer
    {
        AntiGround,
        AntiAir,
        AntiNaval,
        Economic,
        Intel,
    };

    struct Cell { std::array<SimScalar, 5> layers{}; };

    explicit ThreatMap(int widthCells, int heightCells, SimScalar cellSizeWorld);

    void rebuild(const PerceivedWorldModel& world, PlayerId aiOwner);
    void decay(SimScalar factor);     // 0..1

    SimScalar sample(Layer layer, const SimVector& worldPos) const;
    SimScalar sampleRadius(Layer layer, const SimVector& center, SimScalar radius) const;

private:
    int width, height;
    SimScalar cellSize;
    std::vector<Cell> cells;          // row-major; deterministic iteration order
};
```

### Build order

```cpp
struct BuildCondition  // conjunction of these checked vs blackboard
{
    std::optional<SimScalar> minMetalIncome;
    std::optional<SimScalar> minEnergyIncome;
    std::optional<int>       minOwnedOfType;     // pair: unitType, count
    std::optional<int>       maxOwnedOfType;
    std::optional<SimScalar> enemyThreatBelow;
    std::optional<GamePhase> requiredPhase;
    std::vector<std::string> allOf;              // string predicates ("commander_alive")
};

struct BuildStep
{
    std::string unitType;   // e.g. "ARMSOLAR"
    int          count;
    int          priority;   // 0..1000
    BuildCondition when;
};

struct BuildOrderTemplate
{
    std::string name;       // "ARM_LAND_OPENING_NORMAL"
    std::vector<BuildStep> steps;
};
```

### Blackboard

A small POD struct, mostly counters. Read-mostly, refilled at the top of each `tick()`. This is what managers gossip through instead of reaching into each other's internals.

```cpp
struct AiBlackboard
{
    GamePhase phase{GamePhase::Opening};
    SimScalar metalIncome{0_ss};
    SimScalar energyIncome{0_ss};
    SimScalar metalDrain{0_ss};
    SimScalar energyDrain{0_ss};
    SimScalar storedMetal{0_ss};
    SimScalar storedEnergy{0_ss};
    int ownedBuildersIdle{0};
    std::unordered_map<std::string, int> ownedUnitCounts;  // by unitType
    SimScalar totalEnemyThreat{0_ss};
    SimScalar baseThreatNearCommander{0_ss};
    std::optional<UnitId> commanderUnitId;
    SimVector centerOfMass{0_ss, 0_ss, 0_ss};
    SceneTime lastScoutedAt{0};
};
```

---

## 4. Determinism plan

The most important design constraint. Decisions must produce identical outputs on every client.

### What lives in `sim/` (deterministic)

Anything that mutates simulation state, or whose output feeds simulation state, must be compiled into the deterministic sim. That means:

- All AI manager update logic that produces `PlayerCommand`s.
- ThreatMap construction and sampling.
- BuildManager scheduling.
- TacticalLayer target selection.
- Platoon state-machine transitions.

Everything in the list above is allowed only the same primitives the existing `sim/` code uses: `SimScalar`, `SimVector`, `SimAngle`, integer math, `simulation.rng` (a `std::minstd_rand`, see `GameSimulation.h:242`), and ordered containers (`std::map`, sorted vectors). **Important caveat:** `SimScalar` is currently `OpaqueField<float, ...>` (`src/rwe/sim/SimScalar.h:7-8`) — a float wrapper, not literal fixed-point. The codebase relies on every client running the same arch + binary for determinism. AI code does not need to make this *worse*; it just needs to use the same primitives so that when the eventual fixed-point migration happens, AI follows for free.

### What may live outside `sim/` (non-deterministic-safe)

- Debug overlays (threat map heatmap renderer, platoon labels).
- Logging of AI decisions to disk for diagnostics.
- Tuning profile loading from disk (done once at game start, before sim begins).

### Where the AI hooks in

Two reasonable placements; we propose **B**.

**A. Pure scene-level**: AiPlayerController owned by `GameScene`, called from `GameScene::update` at the existing TODO (`src/rwe/game/GameScene.cpp:2080`). Pros: no surgery to the simulation type. Cons: future replays that don't replay the network would lose the AI; trying to "save mid-game and rejoin" requires AI state to be reconstructable.

**B. Sim-owned controller** (recommended): Add a `std::unordered_map<PlayerId, AiPlayerController> aiControllers` to `GameSimulation`. Call `aiControllers[id].tick(...)` from inside `GameSimulation::tick()` (`src/rwe/sim/GameSimulation.cpp:1651`) before unit behaviour runs. The controller writes commands into a per-player out-buffer that `GameScene::update` reads and pushes into `playerCommandService` next frame. Pros: AI state is part of the sim so checksums/dumps include it; replays work without separate handling. Cons: GameSimulation gains a dependency on `PlayerCommand`/`UnitOrder` (it already has `UnitOrder` as those flow into unit state).

We propose **B with a delayed-emit indirection**: AI runs inside the sim and writes its commands into `aiPendingCommands[playerId]` on the simulation. Each frame, `GameScene::update` drains those into `playerCommandService->pushCommands(id, ...)` *exactly the same way human input is pushed*. Net traffic still uses the existing path (host or each peer can push its own AIs; see §8 cheating-mode for the host-authoritative variant).

Why "AI mirrors human input" is load-bearing:

- `processPlayerCommand` (`src/rwe/game/GameScene.cpp:3663`) and `processUnitCommand` (3697) do not care who issued a command. AI reuses every existing validation, animation, audio cue, and side effect for free.
- Network proto (`proto/network.proto:53-115`) round-trips `PlayerCommand` already; if we ever want AI runs that share decisions across peers (host-authoritative AI) the wire format requires no change.
- The same desync detector (`src/rwe/game/GameScene.cpp:2402`) will catch any AI nondeterminism on the day it ships.

### Determinism rules of engagement (short list)

1. Use only `simulation.rng` for any random AI decision. Never `std::random_device`, `std::mt19937` seeded by clock, etc.
2. No `std::chrono`, no `SDL_GetTicks` in deterministic paths. Use `simulation.gameTime` and `SceneTime`.
3. Iterate `std::unordered_*` only when the order is irrelevant. When it is relevant (it usually is), copy keys to a vector and `std::sort` by `OpaqueId.value`.
4. No threading. Managers run in series in `tick()` order.
5. Use `SimScalar` arithmetic; no implicit-or-explicit `float`/`double` math in scoring functions or threat rebuilds.
6. Tests must seed with a fixed seed and assert exact command output.

---

## 5. Threat / influence map

### Storage

A 2-D grid over the playable map. We propose **two cells per heightmap tile** of width 16 (i.e. 32 world units per cell). On a 12×12-tile map (192×192 world units) that is 6×6 cells; on a 64×64-tile map that is 32×32 cells. Five layers:

- `AntiGround` — sum of enemy DPS that can hit ground units within range, weighted by uptime.
- `AntiAir` — same, against air.
- `AntiNaval` — same, against ships.
- `Economic` — value of enemy economic structures (mex, fusion, factory) seen in the cell. Higher = juicier raid target.
- `Intel` — staleness/uncertainty (decays toward 1.0 = "no idea what's there"; 0.0 = "scouted this tick").

Each `Cell` is `std::array<SimScalar, 5>`. Storage:

```
sizeof(Cell) = 5 * 4 bytes = 20 bytes.
On a 32x32 cell map: 32*32*20 = 20480 bytes  (~20 KB)
On a 64x64 cell map: 64*64*20 = 81920 bytes  (~80 KB)
```

Per AI player. Cheap.

### Cadence

Full rebuild every 30 ticks (~1 s). Between rebuilds we apply a multiplicative decay (`factor = 0.95`) per rebuild on the Intel and Economic layers — older information evaporates. AntiGround/AntiAir/AntiNaval are recomputed from scratch each rebuild based on currently-perceived units, so they don't decay.

A rebuild on a 32×32 grid with ~50 enemy units is O(grid_cells + units * avgRange²/cellSize²) — well under a millisecond.

### Cell lookup

```cpp
SimScalar threat = threatMap.sample(ThreatMap::Layer::AntiGround, candidatePos);
SimScalar areaThreat = threatMap.sampleRadius(
    ThreatMap::Layer::AntiAir, platoonCenter, 256_ss);
```

### How decisions use it

- ArmyManager scores attack candidates: `score(target) = economic - antiGround*w_ag - antiAir*w_aa`. Sorian's "threat-weighted attack condition" verbatim.
- BuildManager picks build sites that minimise enemy threat + maximise economic adjacency.
- ScoutManager picks scouting destinations by maximising the Intel layer (i.e. visit stale cells).
- TacticalLayer's retreat trigger: if `sampleRadius(AntiGround, platoon, X)` > my-platoon-DPS × `retreatThresholdRatio`, retreat.

---

## 6. Build order system

A hybrid of conditional steps + opening books + tech transitions. Configuration is RWE-native, parsed at game start, *not* shared with TA's `.AI` format (see §10).

### Config sources

Two layers, both data-driven:

1. **`AiTuningProfile`** — a small TDF or JSON loaded from `data/ai/profiles/<name>.tdf` (where `<name>` is what OTA `aiProfile` produces, defaulting to `DEFAULT`). Holds knobs: `attackTriggerScore`, `expansionMexCount`, `defensiveBuildShare`, `tier2TransitionAt`, etc.
2. **`BuildOrderTemplate` library** — a list of named build orders shipped under `data/ai/build-orders/`. Examples: `ARM_LAND_OPENING_NORMAL.tdf`, `CORE_AIR_RUSH.tdf`. Each entry is a list of `BuildStep`s with `BuildCondition`s (§3 sketch).

`BuildStep::when` predicates that need *runtime* logic beyond what the data can express are kept to a tiny set of named C++ predicates (e.g. `"commander_alive"`, `"under_attack_for_30s"`) — rare, and added only when needed. This is a pragmatic cap on data complexity: most conditions are simple thresholds (income / counts / phase / threat).

### How conditions evaluate

Every BuildManager tick, the active opening book yields its highest-priority `BuildStep` whose `when.match(blackboard)` is true and whose target unit isn't already at-or-above `count`. That step is converted into a `BuildJob` and assigned to an idle builder by EconomyManager.

Tech transitions are not special-cased: they're just BuildSteps gated on `phase == Tech` plus a count of T2 lab built > 0.

### Opening books

Selectable. The StrategicManager picks one at game start based on `AiTuningProfile.openingPreferences` + map size + side. Switching books mid-game is allowed but rare (e.g. on commander loss → "rebuild from base" book).

A representative sketch for ARM normal opening:

```
1.  ARMSOLAR x4    (priority 900, when: phase=Opening)
2.  ARMMEX x3      (priority 950, when: ownedOf(ARMMEX)<3 && metalIncome<3)
3.  ARMLAB x1      (priority 800, when: ownedOf(ARMSOLAR)>=3)
4.  ARMCK x2       (priority 700, when: ownedOf(ARMLAB)>=1)
5.  ARMVP x1       (priority 700, when: phase=Boom)
6.  ARMPW x6       (priority 500, when: ownedOf(ARMVP)>=1, allOf=["under_no_attack"])
...
```

(Real values to be playtested in Phase 3.)

### Tech transitions

`StrategicManager` advances `GamePhase` based on counters: opening → boom (after first lab + N mexes), boom → attack (after attack trigger score), attack ↔ defend (toggled on local threat). Each phase change can swap to a different opening book or modify weights.

---

## 7. Platoon abstraction

A platoon is a logical grouping of N units with a shared mission and a state machine. Tactical layer micros within the platoon; platoons exist for the lifetime of the mission.

### Composition templates

```cpp
struct PlatoonCompositionSlot
{
    UnitClass roleClass;        // see UnitClassifier
    int       minCount;
    int       targetCount;
};

struct PlatoonCompositionTemplate
{
    std::string name;          // "T1_LAND_RAID"
    std::vector<PlatoonCompositionSlot> slots;
    SimScalar  retreatHpRatio;  // 0.4 -> retreat when avg HP < 40%
    SimScalar  threatRatioThreshold; // engage only if my-DPS > enemy-DPS * 0.7
};
```

`UnitClass` is a closed enum over the *roles* the AI cares about: `RaiderLight`, `RaiderHeavy`, `FrontlineKbot`, `FrontlineTank`, `RangedArtillery`, `Aa`, `Air`, `Builder`, `Scout`. A `UnitClassifier` derives this enum from `UnitDefinition` (using `canFly`, `floater`, weapon ranges, weapon target categories) at game-start time and caches the mapping in `unitTypeName -> UnitClass`. This is necessary because TA `Category` is not currently parsed (`src/rwe/io/fbi/io.cpp` does not read `Category`/`TEDClass`).

### State machine (variant)

Already sketched in §3. Transitions:

- `Forming` → `Moving` when `members.size() >= template.minCounts.sum()`.
- `Moving` → `Engaging` when within combat range of mission target and threat ratio favours engaging.
- `Engaging` → `Retreating` when `avgHpRatio < template.retreatHpRatio` *or* enemy reinforcements push threat ratio below cutoff.
- Any → `Disbanding` if commander recall / mission cancellation / fewer than half min counts alive.
- `Retreating` → `Forming` once at retreat point (or `Disbanding` if too few members).

Mission assignment is done by ArmyManager: it consults StrategicManager's intent (attack/defend/scout/expand-protection), scans ThreatMap for candidates, and assigns. A `Platoon` only knows its current mission and acts on it.

### Forming a platoon

Two routes:

1. **Pre-build** — ArmyManager declares "I want a `T1_LAND_RAID` platoon" and inserts a high-priority BuildStep series into BuildManager (or rather into a shadow queue). When enough members exist (newly built, by query of `ownedUnitCounts`), they are assigned to a fresh PlatoonId.
2. **Pull-from-pool** — Idle units not assigned to any platoon ("unassigned reserve") are drawn into a forming platoon by best-fit slot match.

### Mission types

A `Mission` is a value type: `MissionAttack(SimVector target)`, `MissionDefend(SimVector point)`, `MissionScout(SimVector waypoint)`, `MissionExpand(SimVector mexLocation)`, `MissionGuardCommander`. ArmyManager owns mission generation and assignment.

---

## 8. Fog of war

We have **no LOS / radar / fog implementation in RWE today**: grepping `src/rwe/` for `losGrid|fogOfWar|sightDistance|radarDistance` finds only weapon `LineOfSight` projectile physics (which is unrelated). So in practice the AI's PerceivedWorldModel and the simulation truth are identical. This is fine for v1 because human players also see everything currently.

We design the *structure* now so it is correct when LOS lands later:

```cpp
class PerceivedWorldModel
{
public:
    void refresh(const GameSimulation& sim, PlayerId aiOwner);

    // Visible: currently within sight or radar of any unit owned by aiOwner.
    // Remembered: previously visible, may now be stale.
    struct PerceivedUnit
    {
        UnitId id;
        std::string unitType;
        SimVector position;
        SimScalar hpRatio;
        SceneTime lastSeen;
    };

    const std::vector<PerceivedUnit>& visibleEnemies() const;
    const std::vector<PerceivedUnit>& rememberedEnemies() const;

private:
    bool cheatModeOmniscient{false};   // see below
    // ...
};
```

In v1 (no LOS), `visibleEnemies()` simply returns every non-allied unit and `rememberedEnemies()` is empty. When LOS arrives, refresh() degrades correctly.

### Cheating-mode toggle

Single boolean per AI player, `cheatModeOmniscient`, on `AiTuningProfile`. When true, PerceivedWorldModel ignores fog and always returns sim truth (which is current behaviour anyway). When false, it filters by LOS once LOS exists. The Brutal tier in §1 sets this to true *and* applies the resource bonus.

We deliberately put the cheat toggle on the perception layer (not as a parallel "cheating brain" elsewhere) so the rest of the AI is unchanged across difficulties.

---

## 9. Integration points with existing code

Concrete file:line references. The AI plugs in cleanly because the project already separated player input from simulation behaviour.

> *As built: every hook in this section was taken, and the line numbers in it are all stale — the files have grown a great deal since April 2026. The names are what to search for. The one substantive difference is the drain call, which is `simulation.takeAiCommandsForPlayer(id)` rather than `takeAiCommands(id)`; and the read-only sim queries listed further down are all still there, plus the visibility ones the last bullet anticipated, which arrived with fog of war.*

**Primary hook — replaces the current TODO:**
- `src/rwe/game/GameScene.cpp:2071-2084` (the `for (Index i = 0; i < getSize(simulation.players); ++i)` loop). The empty `pushCommands` here becomes `pushCommands(id, simulation.takeAiCommands(id))`.

**Sim integration (recommended placement B from §4):**
- `src/rwe/sim/GameSimulation.h:240` (the `struct GameSimulation`) gains `std::unordered_map<PlayerId, AiPlayerController> aiControllers;` and `std::unordered_map<PlayerId, std::vector<PlayerCommand>> aiPendingCommands;`.
- `src/rwe/sim/GameSimulation.cpp:1651` (`GameSimulation::tick()`) gets a new first call: `runAiControllers();`. That iterates AI controllers in deterministic `PlayerId` order and writes commands into `aiPendingCommands`.

**Lifecycle:**
- `src/rwe/LoadingScene.cpp:202-228` constructs `GamePlayerInfo` for each player. After this loop, instantiate `AiPlayerController` for every player whose `playerType == GamePlayerType::Computer`. The AI's tuning profile name comes from `OtaSchema.aiProfile` (already parsed at `src/rwe/io/ota/ota.cpp:74`); the AI's resource starts come from `OtaSchema.computerMetal/computerEnergy` (already parsed but currently ignored — `LoadingScene.cpp:208` uses launcher params for both human and computer).
- The AI's RNG is sub-seeded from `simulation.rng` (`src/rwe/sim/GameSimulation.h:242`) so its sequence is part of the seeded sim and survives replays.

**Commands the AI emits — already exist:**
- `PlayerUnitCommand::IssueOrder(MoveOrder | AttackOrder | BuildOrder | GuardOrder, Immediate|Queued)` via `src/rwe/game/PlayerCommand.h` and `src/rwe/sim/UnitOrder.h:51`.
- `PlayerUnitCommand::ModifyBuildQueue` (for queueing units in factories).
- `PlayerUnitCommand::SetFireOrders` and `SetOnOff` (for toggling fabricators / airbase landing pads etc).

The AI has no need to add new `PlayerCommand` variants in v1.

**Sim queries the AI needs (read-only):**
- `simulation.units` (`GameSimulation.h:275`) — a `VectorMap<UnitState, UnitIdTag>`. Iteration is deterministic.
- `simulation.unitDefinitions` (line 248) — keyed by upper-case unit type name (see e.g. `GameSimulation.cpp:268`).
- `simulation.players` (line 271) — for resource state per player.
- `simulation.terrain` (`MapTerrain` defined `src/rwe/sim/MapTerrain.h`) — for build site validation, ground heights.
- `simulation.occupiedGrid` (`OccupiedGrid.h`) — for "is this cell free".
- `simulation.canBeBuiltAt(...)` (`GameSimulation.h:316`) — exact predicate the AI uses to check build placements.
- `simulation.metalGrid` (line 267) — to find metal patches for mex placement.

All of these exist *today* and are deterministic-safe.

**Where the AI will eventually need new sim API:**
- A pathfinding query `tryFindPathFor(UnitId, SimVector)` that the AI can call without committing to movement, for e.g. "is my retreat path blocked?" The current `pathFindingService` is command-driven; a non-mutating preview query would be added under `src/rwe/pathfinding/` with the same A* core.
- LOS queries when fog of war lands.

---

## 10. Migration from TA's original AI

**Recommendation: do not parse `.AI` files.**

TA shipped `.AI` profile files that listed weighted unit-build preferences in a fixed format. They drove a hard-coded build engine no longer present in RWE. The Sorian-grade architecture proposed here (build orders with conditions, threat maps, platoon templates) is a strict superset; encoding our build logic into the original `.AI` weighted-list format would force us to flatten conditional structure into weights, which is the *exact* behaviour we are trying to leave behind.

**What we keep from TA / OTA:**

- Honour `OtaSchema.aiProfile` (`src/rwe/io/ota/ota.cpp:74`) as the *name* of the RWE-native profile to load. Defaults to `"DEFAULT"` already — perfect mapping.
- Honour `computerMetal`/`computerEnergy` overrides for computer-controlled players when configured (currently ignored in `LoadingScene.cpp:208`).

**What we discard:**

- The literal `.AI` weighted-list file format. We will not write a parser for it.
- The original AI's "give the AI extra resources every N seconds" baked-in cheating. We replicate that behaviour optionally as a toggle on `AiTuningProfile`, off by default below Brutal tier.

This is greenfield. RWE itself currently has no AI at all, so there is no backward compatibility to preserve except in mod compatibility. Mods that ship `.AI` files alongside their units will simply have those files ignored; if a mod author wants AI behaviour they author an RWE-native profile. We can ship a small migration note in docs at release.

---

## 11. Phased delivery plan

*As built: all four phases shipped, and `docs/ROADMAP.md` Phase 2 is now the live tracker for what is left. Phase 1 landed upstream of this branch (tgunnoe, April 2026); Phases 2, 3 and 4 landed on `revival` in August 2026, Phase 3 in a single-army form rather than with platoons. Two Phase-2 items did not land as written — `AiTuningProfile` is hard-coded rather than parsed from a TDF, and there is no `data/ai/profiles/` — and Phase 4's per-run JSON state dump became a 30-second status line in `rwe.log` plus an F10 debug window instead. Phase 4's `RWE_AI_PROFILE=1` timing harness was added later and is not in this plan at all; it is what found the ferry planner rescanning the whole map three times a second.*

Each phase is an independently shippable agent task with a working AI at the end of it. Phases are additive — no phase requires throwing away prior work.

### Phase 1 — "Commander idles, builders build"  (≈ 1–2 weeks)

Goal: the AI builds a viable opening base and stops there. Validates the entire integration path.

- Scaffold `src/rwe/ai/` and CMake.
- Implement `AiPlayerController`, minimal `AiBlackboard`, hook into `GameSimulation::tick`.
- Drain commands at `GameScene.cpp:2080`.
- Implement `EconomyManager` with simple reactive logic (build a solar if energy stalls; build a mex if metalIncome < 3 and there's an open spot).
- Implement `BuildManager` with one hardcoded `BuildOrderTemplate` per side (ARM, CORE).
- Implement `UnitClassifier` (rough rules, no `Category` parsing).
- Verify deterministic command stream with a fixed-seed test (`AiPlayerController.test.cpp`): same sim, same seed, same commands.

End state: AI starts with a Commander, builds 4 solars + 3 mex + 1 lab + 2 cons-kbots, then idles.

### Phase 2 — "Threat maps, scouting, expansion"  (≈ 2 weeks)

- Implement `ThreatMap` (5 layers), with rebuild + decay.
- Implement `PerceivedWorldModel` with the no-LOS shim.
- Implement `ScoutManager` driving 1–2 Flash/Peewee scouts to maximise Intel-layer staleness.
- Expand `BuildManager` with conditional `BuildStep`s (count gates, phase gates).
- Implement `StrategicManager` with `GamePhase` machine: Opening → Boom → ready-to-Attack.
- Implement secondary expansion: AI builds an extra mex cluster after first lab.
- Add tunable `AiTuningProfile` parsed from a TDF.

End state: AI has a working economy, knows where the enemy roughly is, and has a small army sitting at base.

### Phase 3 — "Platoons attack"  (≈ 2 weeks)

- Implement `Platoon` and `ArmyManager`.
- Implement two `PlatoonCompositionTemplate`s: `T1_LAND_RAID`, `T1_DEFENSIVE_LINE`.
- Implement `TacticalLayer`: target selection, focus-fire helper, retreat trigger keyed on `template.retreatHpRatio` + threat-ratio rule.
- Implement attack triggers: when `army_DPS > best_target_threat * trigger_ratio`, send a `T1_LAND_RAID` platoon at the highest-`Economic`/lowest-`AntiGround` cell.
- Add the four difficulty tiers' default profiles to `data/ai/profiles/`.
- Sorian-style "no all-in if down to one mex": defensive switch when `baseThreatNearCommander` exceeds threshold.

End state: a five-minute AI vs. AI game that actually finishes; Easy is beatable, Hard is hard.

### Phase 4 — "Polish, debug overlays, AI vs AI tuning"  (≈ 1–2 weeks)

- Debug overlays: ImGui windows showing per-AI blackboard, threat heatmap, platoon list with state, current build queue.
- State logger: a `data/ai/logs/<player>-<seed>.json` dump for each AI run, for offline tuning.
- AI vs. AI head-to-head harness for tuning loops (deterministic, scriptable).
- Tune `AiTuningProfile` defaults from playtesting.
- Implement Brutal tier resource cheat (gated behind config).

End state: shippable. AI plays at four difficulty tiers, is observable in-game, and can be improved by anyone editing TDF profiles.

Future (out of scope for this proposal):
- Naval doctrine.
- Air-only build orders.
- Multi-AI team coordination.
- LOS-aware perception once fog of war lands.
- Optional `.AI` file compatibility shim (only if a mod author asks).

---

## 12. Open questions / decisions for the user

These had a recommended default and wanted sign-off because they affect the public surface or the schedule. **All seven are settled now; the answer as built is recorded under each.** They are the reason to keep this document.

### Q1. Do we honour OTA `computerMetal` / `computerEnergy` for the AI?

Recommendation: **yes**, route them through `LoadingScene.cpp:208` so map authors can dial AI starting resources per scenario (this matches TA behaviour and is virtually free). Alternative: ignore the OTA fields and rely solely on `AiTuningProfile`, simpler but loses authoring intent.

**As built: no, not yet.** `computerMetal`, `computerEnergy` and `aiProfile` are still parsed in `src/rwe/io/ota/ota.cpp` and read nowhere else, so both human and computer players take their starting resources from the launcher parameters. The authoring intent is still on the floor; nothing depends on it, and picking it up is a small job.

### Q2. AI cheating: config flag, or difficulty axis?

**DECIDED (2026-04-26): brand it as a Brutal difficulty axis** — option (a) below, with the option-(b) implementation underneath (independent toggles in `AiTuningProfile`) so modders retain flexibility while UX stays simple.

The proposal makes it a difficulty axis (Brutal tier flips both `cheatModeOmniscient` and a 1.25× resource bonus). Alternative views worth signing off on:

- (a) **Difficulty axis** (recommended). Simple for players: "Brutal cheats, the rest don't."
- (b) **Independent toggle** in AiTuningProfile. Players/modders can build `Hard+OmniscientButNoResourceBonus` etc. More expressive, harder to message in UI.
- (c) **Never cheat at all.** Brutal is purely tuning-driven. Cleanest design but means peak difficulty is capped by what good play can do without cheats — usually below where SP players want.

Recommendation: ship (a) as a label, internally implement (b) so we have the flexibility — UI exposes (a), config exposes (b).

**As built: (a) with (b) underneath, as decided.** Brutal flips omniscient perception and a ×1.25 income bonus, both applied in the simulation, and both are independent fields on `AiTuningProfile`. The tier is chosen with `--ai-difficulty`; there is no per-slot difficulty in the lobby yet.

### Q3. Build orders: TDF or JSON?

Recommendation: **TDF**, because the project already has a TDF reader (`src/rwe/io/tdf/`) and TA modders are familiar with it. Alternative: JSON via the existing `nlohmann/json` dep — easier to validate and version. We can support both if asked but starting with TDF is consistent.

**As built: neither.** Build orders and tuning are hard-coded C++ in `AiTuningProfile.cpp` and `BuildManager.cpp`. Nothing reads a profile from disk. This is the largest departure from the proposal and it is deliberate only in the sense that it was never got to — it is still on the roadmap under "TDF-loaded profiles and build orders", and the recommendation above still stands when someone takes it.

### Q4. Where does `AiPlayerController` live — `sim/` or scene-level?

**DECIDED (2026-04-26): inside `GameSimulation` (placement B in §4).** AI state participates in the desync detector's JSON dump and replays just work; Phase 1 implementation must respect determinism rules in §4.

Proposal recommends `sim/` (placement B in §4). Alternative is keeping it in `ai/` but instantiated from `GameScene` only. The sim placement is the right answer if we want replay/dump fidelity (which we get for free with the desync detector's existing JSON dump path), but it tightens the layering between `sim/` and `ai/`. Sign-off needed because once committed it's load-bearing.

**As built: placement B, and it is load-bearing exactly as warned.** The code sits in `src/rwe/ai/` and the controllers are owned by `GameSimulation` through `unique_ptr` behind a forward declaration — which is why `GameSimulation` needs an out-of-line destructor. `LoadingScene` instantiates one per computer player and hands it to `addAiController`. One consequence the proposal did not call: AI blackboard state is *not* serialized, so a saved game reloads with the AI re-planning from scratch. That is a documented divergence rather than a bug, but it is the price of the AI being sim state that the save path does not carry.

### Q5. Should we extend the FBI parser now, or live with what we have?

**DECIDED (2026-04-26): extend the FBI parser as part of Phase 1** — option (a) below. Adds a few days to Phase 1 scope but unlocks a clean `UnitClassifier` and benefits non-AI code paths too.

`src/rwe/io/fbi/io.cpp` does not currently read `Category`, `BMCode`, `SightDistance`, `RadarDistance`, `TEDClass`, etc. Phase 2's `UnitClassifier` will be cleaner with `Category` in particular. Options:

- (a) **Extend the parser as part of Phase 1.** Cleanest. Small bit of work.
- (b) **Defer**, derive categories from existing fields (`canFly`, `floater`, weapon properties). Phase 2 ships sooner, classifier is uglier.
- Recommendation: (a) — extending the parser is mechanical and benefits non-AI code too.

**As built: (a), and the prediction that it would benefit non-AI code proved right several times over.** `src/rwe/io/fbi/io.cpp` now reads `Category`, `SightDistance`, `RadarDistance`, the three `w*_badTargetCategory` keys, `NoChaseCategory` and a great deal more. Target selection, the order-panel gating, the keyboard's data-driven `Ctrl`+letter selections and the transport rules all key off fields added for this.

### Q6. Do we want a per-AI sub-RNG, or share `simulation.rng`?

Sharing is simpler but couples AI command order to sim consumption order; if the sim ever calls `rng()` more or less in unrelated code paths, the AI's choices shift. Sub-seeding gives stability:

```
ai.rng.seed(simulation.rng()); // pulls one value at construction time, then independent
```

Recommendation: **sub-seed once**, keep AI's RNG private.

**As built: sub-seeded once, verbatim.** `LoadingScene` pulls a single value from `simulation.rng()` at construction and hands it to `AiPlayerController`, which keeps a private `std::minstd_rand`. Note the determinism hazard the AI still carries: two of its draws (`BuildManager.cpp`, breaking ties between candidate build sites) go through `std::uniform_int_distribution`, whose bias correction is implementation-defined. That cannot desync a single-player game, and an AI in a network game already desyncs for the reason in the header, but it is the first thing to fix when that is taken on.

### Q7. Scope check: are we sure naval is non-goal?

Some TA stock maps are mostly water (e.g. *Greenhaven*, *Arctic Plains*). On a heavy-water map a non-naval AI is non-functional. Options:

- (a) AI refuses to play on heavy-water maps (UI warning).
- (b) Stub naval support: cons-ship + boatyard + a few basic ships in Phase 3.
- (c) Push naval to Phase 5 / future.

Recommendation: (a) for v1, (b) added in a 4.5 if a tester complains. Sign-off needed because it caps the playable map pool until naval lands.

**As built: neither (a) nor (b), and the question turned out to be the wrong one.** The AI plays water maps without a navy, by air. `ReachabilityMap` floods the heightmap from the base for the constructor's movement class, so the AI knows what it cannot walk to; `TransportManager` then builds an Atlas or Valkyrie and ferries a constructor to the richest patch across the water — where it builds an outpost of extractors and solars around itself — and in the Attack phase carries the army to a landing near an enemy it cannot reach on foot. No warning, no refusal, no boatyard. Naval doctrine proper is still deferred, and sea transports for the AI are still on the roadmap.

---

End of the 2026-04-26 proposal. Total: 12 sections, ~3,400 words. Annotated 2026-09-04 against the code as built.

---

# 13. What is next, and what it will take (2026-09-06)

The sections above are the 2026-04 proposal and its annotations. This one is
written after the map/fog/loss pass and is a plan rather than a record: what is
still missing, in roughly the order it is worth doing, with the plumbing each
piece actually needs. It exists because the AI has reached the point where the
next steps are no longer obvious from reading the code.

## 13.1 What that pass added

- `MapIntel` (`src/rwe/ai/MapIntel.{h,cpp}`): water fraction, a Land/Mixed/Water
  verdict, and the map's declared start positions. Read once at load in
  `LoadingScene::createGameScene` and handed to every controller. Not simulation
  state -- computed from map data that cannot change, so it is neither saved nor
  hashed, on the `UnitSpatialIndex` precedent.
- Scouts open by checking declared start positions, until the enemy base is
  found.
- The air plant comes forward on anything that is not a Land map.
- Three fog leaks closed: remembered enemies are no longer forgotten when they
  die unseen, wrecks are not reclaimed out of unexplored ground, and expansion
  metal has to have been looked at.
- Loss tracking: `AiBlackboard::standingBuildings` is diffed each tick and
  `recentLosses` jumps the build queue.
- `AiDifficulty::Idle`.

## 13.2 Naval, which is the biggest single gap

The AI has no navy at all. `AiSideUnits` has fifteen fields and not one is a
ship, so on a water map it ferries ground units by air and fights for the land.
That works, and it is why naval was deprioritised, but on a map that reads as
Water it leaves the sea uncontested.

What it needs, in order:

1. **Unit table.** `AiSideUnits` gains `shipyard`, `conShip`, `scoutShip`,
   `attackShip`; `AiSideUnits.cpp` gains the ARM and CORE names. Cheap.
2. **A coastal build site finder.** This is the real work. A shipyard needs a
   cell on land, adjacent to water deep enough to float what it builds, and
   reachable by a builder. `BuildManager` searches rings around an anchor; a
   coastal variant wants the same ring walk with a water-adjacency test, and
   `MapIntel` is the natural place to precompute the coastline once rather than
   testing it per candidate.
3. **A naval branch in `buildPriorities`,** gated on
   `mapIntel.character == MapCharacter::Water`, or Mixed plus
   `hasUnreachableGround`.
4. **Naval movement in `ReachabilityMap`.** It labels connected components for
   exactly one movement class today -- the constructor's. A ship needs its own
   labelling, or the map needs to hold a component id per class. This is what
   makes "can my navy get from here to there" answerable at all.
5. **`ArmyManager` needs to know a ship cannot chase a tank inland,** which is
   the same reachability question from the other end.

Steps 1 and 3 are an afternoon. Steps 2 and 4 are the substance, and 4 in
particular changes a data structure several managers read.

## 13.3 Opponent modelling

Nothing models the opponent. `StrategicManager` is a five-state machine driven
entirely by our own army size and whether something armed is near our base;
`GamePhase::Tech` and `GamePhase::Endgame` are declared and never assigned.

The cheapest thing that would deserve the name: a rolling histogram of enemy
unit types seen, kept on the blackboard beside `knownEnemies` and decayed
rather than reset, plus two derived numbers -- what share of what we have seen
is air, and what share is armour. Then:

- ~~**Mostly air seen and we own no anti-air: build anti-air.**~~ Built
  2026-09-07, and it is the model for the rest of this section: a fact about
  the enemy recorded in `PerceptionManager`, decayed rather than reset, read
  by `BuildManager` as a priority rather than a plan.
- **Nothing seen for a long time while our scouts are alive:** the enemy is
  turtling or teching. Expand harder.
- **Repeated losses at one edge of the base:** fortify that side. `recentLosses`
  already carries the positions, and `BuildManager` currently faces its towers
  at `enemyBasePosition` instead, which is the wrong direction whenever the
  attacks are coming from somewhere else.

That last one is a small change and `recentLosses` was built with it in mind.

## 13.4 Personalities and a pre-skirmish screen

The end goal is choosing what kind of opponent you get, not only how good it
is. The clean shape is an `AiPersonality` orthogonal to `AiDifficulty`:
difficulty says how well it plays, personality says what it is trying to do.

```cpp
enum class AiPersonality { Balanced, Aggressive, Turtle, Rush, Economic };
```

applied as an overlay after `makeProfileForDifficulty`, so that the two
compose. Aggressive lowers `attackArmySize` and `retreatArmySize` and raises
`threatAversion`; Turtle raises `targetDefenceCount` and `attackArmySize`; Rush
cuts the opening economy targets and attacks off the first factory; Economic
raises the extractor and solar targets and delays the lab.

Every file a per-slot setting has to pass through, traced 2026-09-06:

| # | File | What changes |
|---|---|---|
| 1 | `ai/AiTuningProfile.h` | the enum, beside `AiDifficulty` |
| 2 | `ai/AiTuningProfile.cpp` | an `applyPersonality` overlay |
| 3 | `MainMenuModel.h` | a subject on **`PlayerSettings`**, not on `skirmishOptions` -- the latter is per-game and is the wrong home |
| 4 | `MainMenuScene.cpp` | `describeSkirmishOption`, `attachSkirmishOptionComponents`, `cycleSkirmishOption`, the stage-to-enum mapper, and `startGame` where `PlayerInfo` is built |
| 5 | `SKIRMISH.GUI` (game data) | there is no per-slot gadget for this and the GUI files are read-only, so it has to be built in code -- `UiFactory::replaceStagedButton` is the mechanism, added for the VISUALS shading switch |
| 6 | `game/GameParameters.h` | a field on `PlayerInfo`, beside the existing per-game `aiDifficulty` |
| 7 | `game/SaveFile.cpp` | the enum-to-string helpers, the write and the read, or it is lost on save |
| 8 | `main.cpp` | usage text; per-slot rides most naturally on `parsePlayerInfoFromArg` |
| 9 | `LoadingScene.cpp` | the controller instantiation loop reads it off `gameParameters.players[i]` |
| 10 | `game/GameScene.cpp` | the F10 debug line, which prints the difficulty today |

Two things worth fixing while in there: **difficulty is per-game, not per-slot**
(`LoadingScene.cpp` says so in a comment), and **the lobby exposes only three of
the tiers** -- Brutal, and now Idle, are reachable from `--ai-difficulty` and
from a save file but not from the UI.

## 13.5 Difficulty should scale judgement, not just cadence

The four tiers differ in nine count thresholds and three tick intervals. The
build order, the engage and retreat radii, the site-search radii, the scouting
policy and the whole of `ArmyManager` are identical at Easy and at Hard. Brutal
is Standard plus omniscience and a 1.25x income, which leaves it economically
*weaker* than Hard except for the cheat.

Things that should vary by tier and do not:

- **Reaction delay.** Cheap and very effective: hold a decision for N ticks
  before acting on it, N falling with difficulty. Easy noticing a raid ten
  seconds late reads as convincingly weak in a way that Easy building six
  solars instead of ten never will.
- **Scout memory.** Easy could decay `knownEnemies` faster. Forgetting is a
  better model of a weak player than never looking.
- **Target choice.** `threatAversion` is a profile constant that varies by
  neither tier nor circumstance.
- **Anti-air, counters and repair.** None of them exist at any tier, so there
  is nothing there to scale yet.

## 13.6 Other ideas, roughly by value per line of code

1. ~~**Anti-air.**~~ **Done, 2026-09-07.** Towers (ARMRL/CORRL) and mobile
   (ARMJETH/CORCRASH), built reactively off a five-minute memory of having
   seen enemy aircraft, held out of the army so they stay over the base, and
   scaled by difficulty. This was the AI's first piece of opponent modelling
   and the hooks it added -- `KnownEnemy::isAir`, `enemyAirThreat` -- are
   what S:13.3 should build the rest on.
2. **Repair.** Damaged units are never sent to a repair pad and idle builders
   never repair anything, although `docs/TOTALA-EXE.md` S:94 has the original's
   repair-pad behaviour decoded and waiting.
3. **Reclaim as a strategy rather than a fallback.** Reclaim fires only when
   every build priority failed to find a site, only for a builder at base, only
   within 1200 units, and it takes the nearest wreck rather than the richest.
   After a battle the wreck field is the best metal on the map and the AI
   mostly walks past it.
4. **Guard and assist.** Idle builders could assist the factory or guard the
   commander instead of standing still.
5. **Staged attacks.** One army, one target. The proposal's platoons (S:7) were
   designed and never built; even two groups, one holding and one attacking,
   would stop the AI throwing everything away at once.
6. **Retreat that works.** Units fall back on an army-size threshold rather than
   on their own damage. A badly hurt unit should leave -- the scouts already do
   exactly this, so the shape is there to copy.
7. **Save the AI's state.** `save_util.h` deliberately drops `aiControllers`, so
   a loaded game re-plans from nothing and forgets everything it had scouted.
   With `MapIntel` and `recentLosses` on the blackboard there is now more to
   lose than there was.
8. **Multiplayer.** Still broken by design: every peer runs its own AI and never
   transmits, so an AI in a network game desyncs. Either run it on one host and
   send its commands, or make its timing peer-independent.

---

# 14. A plan for playing well (2026-09-07)

Section 13 lists gaps. This one is a plan, and it is written against a source:
the Wikibooks *Total Annihilation Tactics and Strategy Guide*, read as a
specification for what a competent human does. Every phase below names the
advice it comes from, what the AI does instead today, and what it would take.

## 14.0 Two constraints to get out of the way

**This data set is base TA v3.1, not Core Contingency.** Checked against
`D:\RWE-extract\totala1\units`: `ARMVULC`, `ARMMMKR`, `ARMFLAK`, `ARMJAMT`,
`ARMTARG` and `ARMSHOT` are all **absent**. So the guide's porcupine-of-Vulcans,
its Moho metal makers, its Flakker anti-air, and every mention of radar jamming
cannot be built at all, and the plan does not pretend otherwise. What *is*
present is the whole of the base tech tree: `ARMFUS` (fusion), `ARMBRTHA` (Big
Bertha), `ARMGUARD` (Guardian), `ARMARAD` (advanced radar), `ARMSILO` (nuke),
`ARMACK`/`ARMACV`/`ARMACA` (advanced constructors), `ARMALAB`/`ARMAVP`/`ARMAAP`
(advanced plants), `ARMESTOR`/`ARMMSTOR` (storage), `ARMSY`/`ARMASY` (shipyards),
`ARMFIG`/`ARMTHUND` (fighter, bomber), `ARMHLT` (heavy laser tower). That is
more than enough to play the guide's game.

**The engine already speaks the vocabulary.** `PlayerUnitCommand::Command` is
`IssueOrder | ModifyBuildQueue | ModifyStockpile | Stop | SetFireOrders |
SetMovementOrders | SetOnOff | SetCloak | SelfDestruct | CancelBuildOrder`.
Metal-maker toggling, nuke stockpiling, hold-fire and hold-position are all
issuable today, by the same path a human uses. Almost nothing below needs
engine work; it needs the AI to decide to use what is there.

## 14.1 Phase 0: be able to tell whether any of this helped

**Built, 2026-09-07.** `--ai-arena <seconds>` runs a computer-versus-computer
game with no human in it, draws nothing, and quits at the time limit having
written `ai-arena.csv` and logged a one-line `AI-ARENA-RESULT`. `--seed <n>`
varies the simulation seed so a batch is a batch rather than the same game ten
times. `tools\ai-arena.ps1` runs the batch and reduces it to averages.

It is the real game: the same `GameLaunch::run`, the same `LoadingScene`, the
same `GameScene`, the same `PlayerCommandService` the AI's commands already
went through. The only differences are that `SceneManager` skips everything
from the imgui frame to the buffer swap and hands the scene exactly one tick's
worth of time per iteration instead of asking the clock, and that a game with
no human borrows the first slot for a point of view. One tick per iteration
rather than many because each tick pops one entry from every player's command
buffer, and a frame asking for more ticks than the buffer holds would stall.

Ten minutes of game time takes about six seconds of wall clock -- roughly a
hundred times real time -- so twenty games take about two minutes.

**The baseline it immediately produced,** six games, Standard, Coast To Coast,
ten minutes each:

| | units | buildings | army | lost | metal income |
|---|---|---|---|---|---|
| ARM | 26.7 | 20.5 | 1.8 | 0.3 | 6.2 |
| CORE | 28.5 | 19.8 | 4.0 | 0.8 | 7.3 |

Two things in that table are worth more than the rest of this document.

**Nothing happens.** After ten minutes each side has lost well under one unit
on average. Two AIs on a map with a water gap between them never meet. The army
column -- two to four units after ten minutes, against twenty buildings -- says
they are not playing a game so much as two solitaires.

**The economy is not stalling, it is dead.** The last rows of every run read
`metal 0`, `energyDemand` at 191 against an `energyIncome` of 138, and a metal
income of six. That is the Phase 1 diagnosis confirmed before Phase 1 has been
written: it is not that the AI occasionally overshoots, it is that it lives
permanently underwater and everything downstream is paced by it.

So the order in S:14.12 stands, and the first number to beat is army 2-4 and
metal income 6 at ten minutes.

## 14.2 Phase 1: an economy that does not leak

**The guide:** "If you are gaining net metal or energy, and the storage for
that resource is full, then the resources you should be getting are simply
lost." Build storage; spend surplus energy on metal makers; toggle makers to
balance. And, specifically: turn metal makers **off** under heavy attack so the
plasma batteries can keep firing.

**Today:** the AI builds `ARMMAKR` when metal is short and energy is rich, and
then never touches it again. It builds no storage of any kind. It has no notion
of waste. Two three-minute runs during the map/fog pass logged
`energy 0(stalled) (+105/-376 per s)` and `metal 0(stalled)` -- it is not
managing an economy, it is riding one.

**Work:**
- `EconomyManager` computes waste: production that had nowhere to go because
  the store was full, per resource, as a rolling figure on the blackboard.
- `BuildManager` wants `ARMESTOR`/`ARMMSTOR` when waste is non-zero and the
  store is small relative to income. This is the cheapest real improvement
  available.
- A new small pass -- call it the maker controller -- issues `SetOnOff` over
  the owned metal makers each planning tick: on while energy is above a high
  water mark, off below a low one. Hysteresis, or they will flap.
- Under attack (`enemiesNearBase` non-empty), force them all off. This is the
  guide's own note and it costs one condition.
- `ARMFUS` once energy demand outgrows what solars can supply, which is the
  guide's stated mid-game shift.

**How we will know:** wasted-resource figure per minute goes to near zero, and
stall time drops, in the Phase 0 harness.

## 14.3 Phase 2: construction that is actually parallel

**The guide, first substantive advice it gives:** "One of the construction units
will initiate the construction with the other units guarding that unit. Having
construction units guard another construction unit will make them automatically
follow and help constructing whatever this unit is building." And the warning
that guarding *structures* blocks placement later, so helpers belong on patrol
near the site rather than parked on it.

**Today:** `bb.idleBuilders` is a list, and a builder for whom no site was found
does nothing at all except the last-resort wreck reclaim. Builds are strictly
one builder, one job. The commander does most of the work alone.

**Work:**
- Idle builders `GuardOrder` the busiest building builder. `GuardOrder` already
  exists and is already matched in the status log.
- Cap the number assisting one job so the rest keep expanding.
- Heed the guide's warning: assist the *builder*, never the structure.

This is a handful of lines against the largest single lever on early tempo.

## 14.4 Phase 3: a commander that survives

**The guide:** the commander "houses almost all of the energy and metal
resources of the player", should assist construction all game, and "should only
engage in battles where it is certain it will not be destroyed... should not be
a unit part of a large defence or attack force". Its D-gun "will destroy almost
everything with a single hit".

**Today:** the commander is just another entry in `idleBuilders`. It walks to
whatever site the planner picked, including toward the enemy, and there is no
rule anywhere that pulls it out of trouble. `EconomyManager` has a
`homePosition` for laying out buildings and nothing that uses it for safety.

**Work:**
- A commander leash: never take a build site whose threat-map anti-ground value
  is non-zero, and never one further from `homePosition` than a profile radius.
- Retreat on damage or on an enemy inside a close radius, back toward the base
  anchor.
- D-gun: when something hostile is within D-gun range and the commander has the
  energy, fire it. High value, and `docs/TOTALA-EXE.md` S:85 already has what
  commandfire costs an order.

## 14.5 Phase 4: attacking like the guide, not trickling

**The guide is emphatic and specific.** New players fail by spreading forces;
instead gather "all to one spot close to the enemy but out of enemy range, and
then send them in", or "have your army guard its slowest unit, then send that
unit on its way". "The attack should not be initiated before all units are in
place." A group must be mixed -- not only attack units, but anti-air where the
air is contested. Attacks should come "from the least expected directions
simultaneously, e.g. from top and rear instead of a frontal assault."

**Today:** one army, one target, and units are commanded individually to the
rally point and then to `attackTarget`. Whoever is fastest arrives first and
dies first. Composition is whatever the lab happened to make: two raiders per
rocket kbot, forever, on every map and at every difficulty.

**Work, in order of value:**
1. **Stage, then commit.** A staging point at a profile distance short of the
   target; hold there until a set fraction of the army has arrived; then move
   as one. This alone converts the AI's attacks from a queue of suicides into
   a wave.
2. **Move at the speed of the slowest.** The guide's own trick -- guard the
   slowest unit -- is one order per unit and needs no new machinery.
3. **Composition templates.** The proposal's platoons (S:7) designed and never
   built. A template is a list of (type, share): raiders, rockets, and now
   anti-air, with the anti-air share rising with `knownEnemyAirCount`. The
   anti-air work of 2026-09-07 already put mobile AA in its own list and out of
   the army; a template is what lets some of it be released to escort.
4. **Two groups, not one.** Even a crude split -- one holding at home, one
   attacking -- stops the AI emptying its base to attack.
5. **Second axis.** Once there are two groups, sending them at different
   approach bearings is a small change to target selection and is the guide's
   "least expected directions".

## 14.6 Phase 5: seeing the map

**The guide:** "Scouting should start early and be a continuous activity
throughout the game", and construction aircraft should place "advanced radars in
various locations, this will give you a nice overview of the map."

**Today:** better than it was -- scouts open on the declared start positions and
then follow map staleness -- but there is exactly one scout of each type, no
re-scouting of a known base to refresh a stale picture, and radar is one
`ARMRAD` at home.

**Work:** scout count scaling with difficulty and map size; a periodic re-look
at the known enemy base so the picture does not rot; and a radar network --
`ARMRAD` forward at chokepoints, `ARMARAD` when level 2 arrives. Radar coverage
is also what makes Phase 4's target selection worth anything.

## 14.7 Phase 6: expanding, and holding what you took

**The guide** sets out the trade squarely: spreading out gives flexibility but
"the player will be mostly unable to effectively defend every structure
scattered across the map, offering the enemy easy targets". The porcupine is
the other pole -- valuable structures grouped, ringed with fixed weapons.

**Today:** the AI expands mexes outward and defends nothing but the base
anchor, with towers pointed at `enemyBasePosition`. It has the worst half of
both strategies.

**Work:** an expansion is a *place*, not a mex -- a cluster with a tower and
eventually a radar, built as a unit. And defences go where attacks actually
come from: `recentLosses` already carries the positions of everything destroyed
and nothing reads them yet.

## 14.8 Phase 7: level two

**The guide's mid-game** is fusion, advanced constructors and better units.
Everything needed is in the data: `ARMACK`/`ARMACV`/`ARMACA`, `ARMALAB`/
`ARMAVP`/`ARMAAP`, `ARMFUS`, `ARMHLT`, `ARMGUARD`.

**Today:** `GamePhase::Tech` exists in the enum and is never assigned. The AI
plays the whole game on level 1.

**Work:** a tech trigger on economy rather than on a clock -- income above a
threshold and the level-1 targets met -- then an advanced constructor, an
advanced plant, and the better units in the composition templates from Phase 4.
This is the phase that makes the AI dangerous in a long game rather than merely
busy.

## 14.9 Phase 8: breaking a base that will not break

**The guide** lists the ways: long-range weapons built outside the enemy's
artillery range; nukes timed against the window when anti-nuke stockpiles are
empty; cloaked strikes on grouped structures.

Cloaked strikes are out -- `ARMSHOT` is not in this data set. The other two are
available: `ARMBRTHA` and `ARMSILO` both exist, and `ModifyStockpile` is
already a player command.

**Work:** only worth doing after Phase 7, and only as a response to a stalled
attack -- an AI that opens with Big Bertha is a worse opponent, not a better
one. The trigger should be "my last two attacks failed against fixed defences",
which needs the attack outcomes Phase 0's harness would already be counting.

## 14.10 Phase 9: naval

Unchanged from S:13.2, and it stays last because it is the most work for the
narrowest gain: `ARMSY`, `ARMROY`, `ARMCRUS`, `ARMSUB` and `CORSY` are all
present, but the substance is a coastal build-site finder and a
`ReachabilityMap` that can label more than one movement class. Until then the
air ferry is a legitimate answer to water, and the guide's own advice about
unexpected approach directions is better served by Phase 4's second axis.

## 14.11 What difficulty and personality mean once this exists

Today the tiers differ in nine thresholds and three tick intervals; Easy and
Hard share a doctrine. With the phases above there is something real to scale:

| | Easy | Standard | Hard | Brutal |
|---|---|---|---|---|
| Economy (14.2) | no storage, no toggling | storage, toggling | + fusion | + fusion |
| Assist (14.3) | commander alone | assist | full assist | full assist |
| Commander (14.4) | wanders | leashed | leashed + D-gun | leashed + D-gun |
| Attack (14.5) | trickles | stages | stages + composition | + second axis |
| Tech (14.8) | never | late | on economy | early |

And the personalities of S:13.4 stop being adjectives: Turtle is Phase 6's
porcupine with Phase 7's Guardians; Rush is Phase 4 with the staging distance
at zero and no Phase 6; Economic is Phase 2 and Phase 7 with the attack
threshold doubled.

## 14.12 The order, and why

1. **Phase 0**, the harness. Nothing else can be judged without it.
2. **Phase 1**, economy leaks. Cheapest real gain; the AI is visibly stalling
   in every log we have.
3. **Phase 2**, assist. A handful of lines against the biggest early lever.
4. **Phase 3**, commander safety. Cheap, and losing the commander is the single
   worst thing that happens to this AI.
5. **Phase 4**, attack discipline. The largest change in how the AI *plays*,
   and the guide is most specific here.
6. **Phases 5-7**, seeing, holding, teching. Each depends on the ones above.
7. **Phases 8-9**, siege and navy. Last, and honestly optional.

The first four are small and independently shippable. They are also the four
that would make the AI stop looking like a beginner, which is what the guide is
a description of.

## 14.13 What the arena then said, and what it corrected (2026-09-07)

The plan above was written before the arena had been pointed at the AI properly.
Once it was, it falsified four of its own claims. They are left standing above
and corrected here rather than quietly edited, because the corrections are the
more useful record.

**The AI is not short of energy. It is drowning in it.** S:14.2 says it "lives
permanently underwater", quoting a demand of 191 against an income of 138. That
was true of the build it was measured on and stopped being true the moment
`MetalMakerManager` landed: measured across three seeds afterwards, energy
income runs 169-208 against a demand of 96-164, and **storage sits at the cap
for 35-68% of every game**. Building storage, S:14.2's headline suggestion,
would have filled a store that never empties. Metal is the binding constraint
and always was -- stock at zero for 31-43% of samples, and 78% on Ashap
Plateau.

**Builders are not idle, so assist is not the top lever.** S:14.3 called
parallel construction "the largest single lever on early tempo", on the premise
that idle builders were doing nothing. The status line reads `idle builders 0`
through the entire stall. What is actually happening is that build power is
committed 8.5 times over: demand 37/s against income 4.4/s, sustained for 150
seconds with the same status line repeated verbatim. Adding builders to that
opens the throttle on nothing, which is exactly what the count experiment
below shows.

**More is not better, and difficulty was scaling the wrong way.** S:14.11's
table assumes turning the knobs up makes a stronger opponent. Measured on one
map and seed, `targetConstructorCount` inverted it:

| | constructors | first fighting unit | final army |
|---|---|---|---|
| Easy | 1 | 344s | 11 |
| Standard | 2 | 507s | 6 |
| Hard | 3 | 586s | 4 |

Easy beat Hard by three to one. Each extra builder costs 120 metal and splits
an oversubscribed budget across one more nanoframe.

**And the real cause of the whole thing was four metal extractors that were
never built.** Income froze at 4.4/s from t=60 to t=380 in every Standard run.
Painted Desert's nearest unclaimed patches are at 724, 944 and 1056 world
units; the near search radius was 512. Brutal isolates it exactly -- its
omniscience is the only difference in the site test, and it takes the fourth
extractor at t=198 instead of t=492 and finishes on 11.5 income instead of 7.6.

### What was changed, and what it bought

- A `nearMexSearchRadius` of 1200, separate from `maxMexSearchRadius`, which
  turned out to be doing two jobs: it is also the ring count every other
  structure is laid out in, so raising the one number would have sprawled the
  base to match.
- `targetConstructorCount` down to 1 at Standard and 2 at Hard.
- The air plant waits, on a land map, until the extractors it competes with
  are up. It is 850 metal for one 40-metal scout, `planFactories` has nothing
  else to give it once that is built, and it was 21% of everything the AI
  committed in ten minutes.

Like for like at ten minutes on Painted Desert, three seeds:

| | before | after |
|---|---|---|
| metal extractors | 5 | 9 |
| metal income | 7.6 | 9 |
| units lost | 0-1 | 8-13 |

The last row is the one that matters: the two AIs now meet and fight inside
ten minutes instead of never. Given thirty minutes, two of three games end in
an elimination -- armies of 32 and 38, and a hundred-odd units lost apiece.

### Still open, in order

1. **No affordability test anywhere.** `buildPriorities` picks on counts and a
   `metalShort` boolean; nothing compares an item's draw against income, and
   nothing counts open nanoframes. `BuildManager::update` returns early unless
   a builder is idle, so a builder pinned on an unaffordable nanoframe never
   re-plans -- an air plant took 183 seconds against a nominal 24. This is the
   deepest remaining fault and the next thing to fix.
2. **The mex expansion search still needs explored ground**, and the only
   scout is a plane that does not exist until t=380. Either send a builder to
   look, which is what a human does, or let radar coverage count.
3. **Anti-air costs 256 metal and the first two lab slots** the moment one
   enemy scout plane is seen. It is the right rule reacting to too little.

## 14.14 Affordability, growth, and waves (2026-09-07, later)

Three more changes, each measured before it was kept. The measurement
runner now keeps every game's economy and event CSVs beside the summary
(`tools/ai-arena.ps1 -outDir`), and a small script over those is what the
tables below come from: the result line says who won, the samples say why.

### The affordability test, and the run it took to get right

Item 1 of S:14.13's list. Nothing in `BuildManager` asked whether the next
item could be paid for; it started whatever came next and the economy
answered by stalling everything in equal measure. Measured: committed at 2.4
times income through the opening, metal-stalled 12-18% of the first ten
minutes, an air plant that takes 24 seconds taking 85.

Now an idle builder estimates the draw of the next item -- the unbuilt share
of its price at the builder's own rate, `BuildManager::estimateBuild` -- and
starts it only if the stockpile lasts the build at income less what the
other builders are already committed to. Within a minute's saving, it waits
and reclaims rocks meanwhile; out of reach altogether, it is skipped.
Extractors and makers are exempt, being the way out of a stall. Frames
nobody is on are resumed with a repair order before anything new, since
half their metal is already in them and they rot (S:93).

The first version of this made the AI **worse**, and the reason is worth
the paragraph. "What the other builders are committed to" was read from
last tick's demand buffer. The planner runs on the tick a builder falls
idle, and that buffer still carries the job it just finished -- so a
705-metal lab was judged unaffordable with 627 in the bank and nothing
drawing, was skipped, and ten solar collectors went up in its place. First
fighting unit at 550 seconds instead of 230. The fix is
`AiBlackboard::metalCommitted`, worked out from the frames our builders
and factories are actually on. With that:

| ten minutes, Painted Desert, 4 games | before | after |
|---|---|---|
| metal-stalled samples | 12-18% | 2-5% (six of eight sides) |
| army | 5-8 | 8-11 |
| metal income | 7.5-9.2 | 8.7-9.6 |
| fourth extractor, lab, first fighter | 175s, 119s, 230s | unchanged |

### Growth: the targets are where the base starts

The thirty-minute samples then showed the ceiling. Income sat at 8-11 from
the seventh minute to the thirtieth in every game, with 7-8 extractors
standing and patches to spare, because `targetMetalExtractorCount` was 8
and nothing asked for a ninth. And game 2 had both sides at a full 1350
metal from minute fifteen to the end -- the commander lending a hand at the
one lab, and the one lab unable to spend it.

So an extractor is wanted whenever the plan has nothing else (fifty metal
that pays for itself in half a minute), and a store four-fifths full asks
for another lab, up to `surplusLabCount` beyond the first.

| thirty minutes, ARM side, 4 games | before | after |
|---|---|---|
| extractors standing | 7-8 | 10-19 |
| labs | 1 | 1-3 |
| income at the end | 8-11 | 9-16 |
| army at the end | 11-39 | 19-52 |

The ten-minute figures did not move, which is the point: it starts where
the old plan ran out.

### ARM beats CORE, from either slot

With both of the above in, ARM won or dominated every game, and swapping
the sides (`-sideA CORE -sideB ARM`) did not change that: CORE at slot 0
was eliminated in three of four. Same code both sides, so it is the unit
set. The lab's "two raiders per rocket kbot" gives CORE an army of A.K.s,
and an A.K. loses to a Peewee. Not fixed yet; it is measurable now, and it
is why a behaviour change has to be judged with both sides on the same
faction.

### Waves, and holding when outnumbered

Game 4's trace showed the other half of why a side collapses. CORE's wave
fell to two units at minute twelve and its army then sat at one to four for
the rest of the game: each kbot the lab finished was sent to the front
alone (`ArmyManager` moved every combat unit to the attack target the
moment one existed), and in Defend every unit charged the nearest intruder
whatever the odds, so a raiding party of nine was answered two kbots at a
time.

Two rules, both knobs: `attackInWaves` -- the units at the rally point when
the attack is called are the wave, anything built after gathers for the
next one, and the attack ends when the wave is spent rather than when the
whole army is; and `holdWhenOutnumbered` -- with more armed intruders near
the base than combat units, hold at the rally point and fight what comes
within reach.

Judging these needed something the arena could not do: a mirror match
gives both sides the change. `--ai-tune <player>:<knob>=<value>` sets one
knob for one player (`applyAiTuning`), and the runner's `-tuneA`/`-tuneB`
pass it through, so the change is played against its absence in the same
game, both sides ARM.

Eight games, ARM against ARM, the new rules on one side and off on the
other, four with each ordering so the slot cancels:

| | new rules | old rules |
|---|---|---|
| eliminated the other side | 2 | 2 |
| ahead at the thirty-minute cap | 4 of 4 | 0 of 4 |
| army at the end, ordering A / B | 29 / 51 | 14 / 21 |
| income at the end, ordering A / B | 15 / 16 | 15.5 / 11 |

Not a rout -- the old rules still took two games outright, both by an
early wave the new side was still gathering for -- but at the cap the new
side stands with two to four times the army every time, which is what
waves were meant to buy: an army that survives to be one.

### Still open, in order

1. **CORE loses to ARM under the same code.** The lab's two-raiders-per-rocket
   ratio gives CORE an army of A.K.s. Per-side composition, or a ratio
   that reads the units' actual strength, is the fix; the swapped-sides run
   is the test.
2. **The whole army is recalled by one intruder.** `enemiesNearBase` puts
   the AI in Defend for a single scout, and the wave on its way to the
   enemy turns round. Defend should need a threat worth answering.
3. **The mex expansion search still needs explored ground**, and radar
   coverage does not count (S:14.13 item 2, unchanged).
4. **Anti-air costs 256 metal and the first two lab slots** the moment one
   enemy scout plane is seen (S:14.13 item 3, unchanged).

# 15. Level two

Everything up to here has been about playing the first ten minutes properly.
This is about the AI ever leaving level one, which at the time of writing it
cannot do: `GamePhase::Tech` exists in the enum and nothing enters it, no
level-two unit name appears anywhere in `AiSideUnits`, and at the thirty-minute
cap the AI fields sixty Peewees against a player who has had a Zeus line and a
Guardian for ten minutes.

## 15.1 A correction: this data set is not base 3.1

S:14 was written on my claim that the installed data was base Total Annihilation
v3.1 and therefore missing the units that make the strategy guide's advice
work. **That was wrong.** `D:/RWE-Data` holds `ccdata.ccx`, `ccmaps.ccx` and
`ccmiss.ccx` (Core Contingency) and `btdata.ccx`, `btmaps.ccx` (Battle
Tactics) beside the base `totala*.hpi` and the `rev31.gp3` patch, and
`battle_test --list-units` returns 278 units including `Armvulc`, `Corbuzz`,
`CORKROG`, `ARMFLAK`, `CORFLAK`, `ARMTARG`, `ARMMMKR` and the whole moho and
fusion line. The earlier note that six of those were "absent" was simply a bad
reading, and it cost something: the user scaled down what they asked for on the
strength of it. The full expansion roster is available and the plan below uses
it.

## 15.2 The tech tree, read out of the data

The engine does **not** enforce a tech tree. `UnitBehaviorService::createNewUnit`
takes any type name and builds it, and nothing between `BuildOrder` and the
nanoframe asks whether this builder is allowed that unit. What constrains a
*player* is the build menu: `<unitname><n>.GUI` in the `guis` directory, whose
gadget `name=` fields are the unit types that page offers. `LoadingScene`
already parses these into `BuilderGuisDatabase` for the human's UI panels, and
a `TODO` there notes that `download.tdf` was never read. So the tree exists in
the data, is already loaded, and the AI has simply never consulted it.

Read out of the archives, the shape is one advanced plant per domain, each
unlocked by that domain's own level-one constructor:

| builder | unlocks |
|---|---|
| `ARMCK` construction kbot | `ARMALAB`, plus `ARMHLT`, `ARMGUARD`, `ARMRL`, `ARMGEO`, `ARMDRAG` |
| `ARMCV` construction vehicle | `ARMAVP` |
| `ARMCA` construction aircraft | `ARMAAP` |
| `ARMALAB` advanced kbot lab | `ARMACK`, `ARMZEUS`, `ARMFIDO`, `ARMVADER`, `ARMASER`, `ARMFAST` |
| `ARMACK` advanced constructor | `ARMARAD`, `ARMFUS`, `ARMMOHO`, `ARMBRTHA`, `ARMSILO`, `ARMANNI`, `ARMAMD`, `ARMASP` |

Core is the same shape: `CORCK` → `CORALAB` → `CORACK`, with `CORPUN` where
ARM has `ARMGUARD`, and `CORCAN`/`CORPYRO` where ARM has `ARMZEUS`/`ARMFIDO`.

**And the commander is not a construction kbot.** `ARMCOM`'s three pages are
the basic economy, the four level-one plants with `ARMLLT` and `ARMRAD`, and
the underwater set. It cannot build `ARMALAB`, `ARMHLT`, `ARMGUARD` or
`ARMRL`. The AI has been having it build `ARMRL` anyway — twice in game 2 of
the `growth1` run, and in every game since anti-air landed — which is a thing
no player can do. That is a fairness fault the tech work has to fix on its way
past, and it is the reason the capability table below is not merely a tech-two
enabler.

## 15.3 Why level two is affordable now, and what it costs

The measured shape of the AI's economy is the argument for teching. S:14.13
found energy storage pegged at the cap for 35-68% of every game and metal the
binding constraint throughout; S:14.14 then removed the extractor ceiling and
income runs 9-16 by the half hour with the store repeatedly full. That full
store is idle capital, and level two is what it buys — note where the prices
sit:

| | metal | energy | buildTime |
|---|---|---|---|
| `ARMALAB` advanced kbot lab | 2007 | 3277 | 13520 |
| `ARMACK` advanced constructor | **300** | **5784** | 13432 |
| `ARMZEUS` (875 hp) | 267 | 2228 | 5478 |
| `CORCAN` (2800 hp) | 420 | 3500 | 7500 |
| `ARMHLT` heavy laser tower | 584 | 5398 | 9575 |
| `ARMGUARD` heavy plasma | 1946 | 7687 | 13377 |
| `ARMARAD` advanced radar | 125 | 1830 | 4800 |
| `ARMMOHO` moho extractor | 1508 | 8700 | 35750 |
| `ARMFUS` fusion plant | 5130 | 36058 | 93768 |

Level two is priced in **energy**, which is the resource the AI already has
too much of. The advanced constructor is 300 metal. The advanced radar is 125
metal for four times the coverage. Even the Zeus, at 267 metal, is barely more
than two Peewees and beats a great many of them.

The trigger therefore needs no new machinery: `BuildManager::canAfford`
already returns true outright when the stockpile is within 90% of storage,
which is the "we are rich" signal S:14.14 used for the surplus lab. Teching is
a better use of a full store than a third level-one lab, so it goes above that
rule in the order.

## 15.4 What gets built

1. **A build-capability table**, `AiBuildTree`, assembled at load from
   `BuilderGuisDatabase` and handed to each `AiPlayerController` the way
   `MapIntel` is. `buildPriorities` is then filtered by what *this* builder can
   actually build, so the commander stops putting up Defenders and the advanced
   constructor is the only thing that reaches fusion. A builder the table does
   not know (a mod with no GUI page) is allowed everything, so nothing regresses
   to a base that builds nothing.
2. **Level-two names in `AiSideUnits`**: advanced lab, advanced constructor,
   advanced assault kbot, heavy laser tower, heavy plasma turret, advanced
   radar, moho extractor, fusion.
3. **The tech rule** in `buildPriorities`: with the level-one plan satisfied
   and the store rich, want the advanced lab; above the surplus-lab rule.
4. **Advanced production** in `planFactories`: the advanced lab makes its
   constructor first, then level-two assault kbots, mirroring what the
   level-one lab does with `ARMCK` and raiders.
5. **What the advanced constructor is for**: advanced radar early (125 metal),
   heavy towers at the front, then fusion once energy actually binds.
6. ~~**`GamePhase::Tech`** entered when the advanced lab stands.~~ Dropped on
   reading the code: the phase machine is the army's posture, not the
   economy's, and `ArmyManager` treats `Tech` as its `default` case -- gather
   at the rally and do not attack. Entering it would have stopped the army to
   celebrate a building. The enum member stays unused.

## 15.5 Deliberately deferred

- **Moho in place of a level-one extractor.** A moho must stand on a metal
  patch, and by the time the AI can afford one it has claimed every patch it
  can reach, so the upgrade is reclaim-then-rebuild: a two-step operation on a
  building the AI currently only ever adds. Until that exists, mohos go on
  unclaimed patches only, which on a busy map means seldom.
- **Fusion**, at 5130 metal, is 300-500 seconds of the AI's whole income. It
  is gated on energy actually binding, which with the current maker count it
  rarely does. Expect it to fire late or never until metal makers scale.
- **Level-two air and naval**, and the big guns (`ARMBRTHA`, `ARMANNI`,
  `CORDOOM`). Siege is S:14.9 and wants the army rework behind it.
- **Per-side composition by unit strength.** The Can has 2800 hit points to
  the Zeus's 875 and Core's level-one line is the weaker of the two; a
  composition rule that reads hit points and damage rather than a fixed ratio
  is what finally answers "ARM beats CORE from either slot", and it belongs
  with this work but not in its first step.

## 15.6 What it measured, and why it ships switched off

All of §15.4 is built and works: the AI reaches the advanced lab, the advanced
constructor, the Zeus or the Can, and the advanced radar, in that order, and
the build tree keeps each of them behind the builder that really unlocks it.
Then it was measured, and **it loses**.

The trigger took two attempts, and the first failure is worth recording
because it was the same mistake §14.13 caught once already. The rule was
written as "tech when the store is full", reusing S:14.14's surplus signal on
the strength of the finding that storage sits pegged at the cap. But that
finding was about **energy**, and it stopped being true of metal the moment
S:14.14 gave expansion somewhere to go: the AI now spends every metal it earns
on extractors, and metal stood at or above four fifths of storage in eleven
samples out of a hundred and eighty. Across eight games not one advanced lab
was ordered, and the A/B looked like a clean null -- both orderings returned
byte-identical results, because the knob was switching nothing. The trigger is
now metal income against `techMinMetalIncome`, and a longer saving window
(`techSaveUpSeconds`, four minutes) because a 2007-metal building judged
against the ordinary one-minute window is unaffordable for ever.

A second rule had to move with it. The surplus-lab rule from S:14.14 fires on
the same full store and the commander is planned first, so the commander --
which has no button for an advanced lab -- spent every full store on another
level-one lab and the one builder that could tech never saw the store full.
The surplus lab now waits until teching is done with or impossible.

With that working, the results, all ARM against ARM on Painted Desert, four
games per slot ordering per variant so that Painted Desert's considerable slot
bias cannot flatter either side:

| how the lab was paid for | tech side army | level-one side army |
|---|---|---|
| save the full price, builder idles | 31.0 | 47.2 |
| save, but keep building extractors meanwhile | 31.0 | 47.2 |
| place it and let the nanoframe draw | 22.1 | 59.2 |

The third variant is the one a player would recognise and it is the worst of
the three, because it pins the only builder that can tech onto a 2007-metal
frame for minutes. Six ninety-minute games, long enough that they end in
elimination rather than at the cap, gave the teching side two wins out of six.

The cause is timing, not the tier. The lab completes at minute 24 to 28 of a
thirty-minute game, and 2007 metal is about thirty Peewees, so the level-one
side simply spends the same metal sooner. Nothing about the Zeus is wrong; it
never gets built in numbers that matter.

So `techLevelTwo` defaults to **false**. The machinery stays, because it is
correct and because the next attempt needs all of it, and because one part of
it -- the build tree -- is a fairness fix that stands on its own.

**What would change the answer.** The lab has to be up by minute twelve to
fifteen, and it cannot be while it competes with expansion for the same
builder and the same metal. Two things to try, in order: set a builder aside
for the tech step so it is not also the one claiming every patch (a second
construction kbot techs three to five minutes earlier, but measured worse
overall -- 19 army against 27.5 -- so the extra builder has to be *dedicated*,
not merely present); and let the planner run more than one builder a pass,
which the counts can now support since a queued build order is counted from
the moment it is ordered. Until one of those lands, teching stays off rather
than shipping an opponent measured to be worse.

## 15.7 Teching is a question about the side's units, not about the plan

S:15.6 concluded that level two loses and left "a builder set aside for the
tech step" as the next thing to try. That was the wrong next thing, and the
reason is a mistake in §15 itself: it treats teching as one decision. It is
two different decisions, and the two sides do not face the same one.

**What a tier is worth.** `BuildManager::unitCombatValuePerMetal` scores a
combat unit as hit points times damage a second, over its metal price. Both
halves are needed and neither alone will do: on hit points alone a Zeus is
*worse* than a Peewee, on damage alone it is better, and the product is what
decides a fight between equal metal, because a unit that lives twice as long
also fires twice as often. Read off the shipped data:

| unit | metal | hp | damage/s | value per metal |
|---|---|---|---|---|
| ARMPW Peewee | 53 | 250 | 60 | 283 |
| ARMHAM Hammer | 151 | 800 | 44 | 231 |
| **ARMZEUS Zeus** | 267 | 875 | 124 | **407** |
| ARMFIDO Fido | 398 | 1000 | 46 | 116 |
| CORAK A.K. | 56 | 265 | 35 | 164 |
| CORTHUD Thud | 147 | 800 | 42 | 229 |
| **CORCAN Can** | 420 | 2800 | 232 | **1544** |

So Arm's level two is worth 1.44 times its level one and Core's is worth
**9.4 times** — Core's level-one line is the weaker of the two and its Can is
extraordinary. The AI computes this itself at load, from the unit and weapon
definitions, and `techMinArmyValueRatio` (1.5) decides on it. Arm declines,
Core techs. The number is calibrated to the measurements below rather than
derived, which is worth remembering before trusting it for a third side.

That also names the cause of the standing "Arm beats Core from either slot"
finding in §14.14. It is not the ground and not the code: Core's level-one
army is worth 164 a metal against Arm's 283, and Core is meant to answer that
by teching.

**Then delivery, which took three goes.** With Core teching, the tier was
still never reached, and each attempt was measured:

| | labs finished | Cans finished | when the lab lands |
|---|---|---|---|
| value gate alone | **0 of 4** | 0 | started min 20-26, still a frame at the cap |
| plus everyone assists the frame | 4 of 4 | 12 | min 23-29 |
| plus the tech step outranks the second factory | 4 of 4 | **21** | min 18-26 |

The first row is the important one. A 2007-metal frame with one construction
kbot on it does not finish inside a game: in three games of four the lab was
still a nanoframe half an hour in, the metal sunk and not one Can ever
fielded. A player does not watch a lone constructor do that, and neither does
this now — every spare builder piles onto the frame, the commander included,
which it can do even though it could never have *started* the lab. That
asymmetry, that the commander may assist what it cannot order, is exactly why
this was so slow and is worth remembering elsewhere.

**And a bug that had nothing to do with teching.** Fixing the test fixture's
damage key exposed that `ThreatMap` looked up its damage under `"default"`
while the loader stores `"DEFAULT"`. Every weapon therefore scored zero, the
whole anti-ground layer of the influence map was flat in every real game, and
`bestAttackTarget` was choosing targets on economic value with the threat term
it is explicitly weighted against permanently absent. The unit test covering
it passed because the fixture spelled the key the same wrong way. This is the
second time a test fixture's invented data has hidden a real fault (see the
bomber tests in CLAUDE.md), and the same lesson applies: fixtures should carry
the shipped convention, not a plausible-looking one.

**Where it stands.** Delivery is transformed and the AI now judges the tier
correctly, but on Painted Desert at thirty minutes the teching side still
finishes with the smaller army. Some of that gap is an artefact of the
measure: the arena counts units, and a side whose units are worth nine times
as much each is undercounted by exactly the thing that makes it good. The
honest decider is who wins a game that ends, which is what the ninety-minute
runs are for.

**Ninety minutes, where the games end.** Four per slot ordering, Core against
Core: the teching side took four, lost three and drew one. It was eliminated
twice and eliminated nobody; where it survived it finished with three times
the army, 355 against 108 and 315 against 76. So the tier is worth what the
arithmetic says once it arrives, and the price is a vulnerable middle game --
a gamble, not an improvement, and the risk falls in the first twenty minutes,
which is most of a game anyone actually plays.

`techLevelTwo` therefore stays off. What is left to try, in order: let the
planner run more than one builder a pass, which the counts have supported
since queued orders started counting and which is the cheapest way to stop
the tech step and the expansion competing for the same kbot; and hold the
army at home while the lab goes up, since the two eliminations were both a
teching side attacked while its metal was in a building. Neither is a large
change, and the second is testable with the knobs that already exist.

## 16 Four faults from one replay (2026-09-08)

A play-test of arena_4, Core against Core on Painted Desert, produced four
complaints at once, and they turned out to be four separate faults with one
thing in common: each is a rule that was right when it was written and whose
exit condition was never supplied.

## 16.1 The builder pool leaked

An advanced constructor was built and did nothing with it for the rest of the
game. S:15 had assumed the difficulty was getting the tier bought; the
difficulty was that the tier, once bought, could not be spent. Three things
were wrong at once.

**A builder that runs out of work never comes back.** When the planner has
nothing for a builder it sends it to lend a hand at a factory, which is a
`GuardOrder`, and nothing anywhere ever takes that order off again.
`idleBuilders` holds only builders whose order queue is empty. So every builder
that ever ran out of work left the planning pool permanently -- and the
advanced constructor, built late, when the base is finished and the planner
most often has nothing to say, went straight into that hole and nanolathed
Peewees until the game ended. `EconomyManager` now offers a builder guarding
one of our own factories back to the planner, behind the genuinely idle ones,
and the factory fallback no longer re-issues an order the builder is already
carrying out.

**Only the front of the list was ever planned for.** `idleBuilders` is in unit
id order, so its front is the commander or the oldest constructor, and with
assisting builders back in the pool that would have been the same low id every
pass for ever. `BuildManager` keeps a cursor now and takes each available
builder in turn. This is the first item S:15.7 left for next time, and it
arrived for a different reason than the one predicted there.

**The level-two rules were gated on the tech knob.** `techLevelTwo` ships off,
so every rule that spends the tier was switched off with it -- including for a
side that already owns an advanced lab. Whether to tech and how to spend a tier
already bought are two different questions and only the first is policy; the
block now asks only whether an advanced lab stands. The same applies to the
rule that puts every spare builder on an advanced lab frame.

The tier is now spent in the order a player spends it: a heavy tower when
something is actually coming, then the moho extractor, then the advanced radar,
then fusion, then the standing defences. The radar sits above fusion
deliberately. An item that is unaffordable but nearly affordable sets `saving`
and suppresses everything cheaper below it, so 5130 metal of reactor placed
above 125 metal of radar means the radar is never built at all.

## 16.2 Reinforcements waited for the army to die

The complaint was that units built during a fight were not released until the
previous army had been destroyed, and that is exactly what the code said.
`attackInWaves` exists because the army used to send each new kbot to the front
by itself; the cure was to freeze the wave's membership at the moment the
attack was called. But a wave is only declared spent once it has fallen below
`retreatArmySize`, and between those two points every unit built stood at the
rally point and watched. It compounds S:16.5 below: two armies deadlocked at a
wreck wall kill each other very slowly, so the wave stays alive, so the reserve
never moves.

Reinforcements now join a wave that is still out, in batches of
`reinforcementGroupSize` (three). A batch and not each unit as it appears,
because the dribble of single units walking to the front is the thing
`attackInWaves` was written to prevent: the fix keeps that and drops only the
part of it that was never intended.

## 16.3 Nobody was ever sent at the expansion

One side mined an entire flank of the map uncontested. The army walks at
whatever `ThreatMap::bestAttackTarget` picks out, which is one place and nearly
always the enemy base, so an extractor field with nothing covering it was never
a target of anything, and the enemy had no reason to keep a single unit at
home.

A raiding party of `raidPartySize` (three) now goes at the enemy's nearest
outlying building that has nothing covering it -- outlying meaning at least
`raidAvoidBaseRadius` from their base, uncovered meaning the threat map reads
zero there, which is a question that only began returning honest answers when
the `"default"`/`"DEFAULT"` damage-key bug was fixed in S:15.7. The raid is
drawn from the units gathering for the next wave and never from the wave
itself, and it will not form until a wave is already out: taken any earlier it
would come out of the first attack the AI ever makes, and `attackArmySize`
would quietly mean three fewer than it says.

## 16.4 The air plant built one scout and stood idle

`planFactories` gave the air plant a scout, a transport when there was water to
cross, and nothing else. That is 850 metal of factory producing one 40-metal
unarmed aeroplane -- the AI's entire air force, for the whole game, against an
enemy whose expansion sat behind a wall of towers that only an aircraft could
pass.

The plant now builds, in order: the scout, the transport when it is wanted, an
air constructor, fighters once something of theirs is actually flying, and then
bombers. `AirManager` commands the two armed types, and it finds them by type
rather than from a list because the economy sweep deliberately excludes
everything that flies from `combatUnits` -- an escort that leaves with the army
is not cover -- which meant fighters and bombers belonged to no manager at all.
Fighters take what flies and nothing else, and stand over the base when the sky
is clear. Bombers go at the dearest thing the enemy has built, and go in pairs:
sent out one at a time, each new bomber flies at the most valuable thing the
enemy owns, which is the thing deepest inside their anti-air, and is traded for
a fraction of a building. That is S:16.2's mistake wearing a different uniform.

The air constructor has the widest reach of the three. It is a builder, so it
was already in the planning pool -- but the site search asks whether a patch is
reachable *on foot*, which for a flier is exactly backwards: it confined the one
builder that can cross water to the patches everything else could already walk
to, and refused the island patch that is the whole reason to own one. A builder
that flies is no longer held to the ground's shape.

## 16.5 The wall of wrecks

The most visible complaint was armies standing and firing into the wrecks of
previous armies, apparently unable to stop. The simulation is behaving
correctly and deliberately: projectiles collide with blocking features, a
corpse is a blocking feature, and there is no line-of-fire test anywhere in the
engine. The original has none either, which is why a unit will happily empty
itself into a rock. So the deadlock is real and it feeds itself -- a wave walks
up to the wall, cannot path through it, stops, and fires into it; so does the
wave on the other side; both die there and thicken the wall for the next pair.

The answer is not a change to the simulation but the one a player uses: reclaim
the field, which takes the wall down and pays for the next wave twice over. The
AI has always reclaimed wreckage -- within 1200 of its own base anchor, which is
precisely where the battlefield is not. A non-commander builder will now work
wrecks within `battlefieldReclaimRadius` of the wave's own centre of mass, and
only where at least `battlefieldReclaimEscortCount` of our units are already
standing, which is what makes the walk survivable. The centre is taken from the
wave rather than from every combat unit we own, because the reserve stands at
the rally point and a centroid dragged halfway home names a place where nothing
is happening.

## 16.6 What it measured

Eight games, Core against Core on Painted Desert, both sides on the default
profile, against the six-game run of the same shape taken before the change:

| | before | after |
|---|---|---|
| games that ended in a result | 1 of 6 | **4 of 8** |
| battlefield reclaims ordered | 0 | 459 |
| raids sent | -- | 68 |
| reinforcement batches joining a wave | -- | 485 |

The first row is the one that matters, and the others explain it. Half the
games now reach a decision instead of running to the clock with two armies
grinding at a wall of corpses.

The averaged unit and army counts are not worth quoting against the earlier
run and are left out deliberately: a game that ends puts one side at zero and
leaves the winner reclaiming the loser's base, so the same change that made
the games decisive also made the averages a measure of how often somebody won
rather than of how well either played. One winner finished on 87 metal a
second. Per game the two sides are close -- three wins to one, and the four
timeouts split two and two.

**A measurement mistake worth recording.** The first attempt at the level-two
question ran `techLevelTwo` on slot 0 and compared it against slot 1, and slot
0 came out with half the income and two eliminations. That looked decisive and
it was not: the both-default run above puts slot 0 at three eliminations with
no teching at all. The A/B had measured the slot. An A/B on a mirror match has
to hold the slot fixed and vary only the knob, which is what
`ai-arena.ps1 -tuneB` is for -- the same trap S:14 walked into from the other
direction when a mirror match was used to judge a change both sides received.

## 16.7 The battlefield reclaim had to go above the priorities, not below

Worth recording because the first version looked right and did nothing. The
rule was written where the AI's other reclaiming lives: at the bottom of the
planner, reached by a builder with nothing to build. Across six games it never
ran once. Every status line in every log said `idle builders 0`, and the reason
is a rule from S:14.14 -- growth has no ceiling, so there is always another
extractor to want, and a builder is never out of work. A fallback below an
infinite list of wants is unreachable code with extra steps.

It sits above the priorities now, gated three ways instead: a wave has to be
out and standing there, which is what makes the walk survivable; there have to
be at least four reclaimable wrecks inside the radius, so it answers a wall and
not a single corpse; and no other builder of ours may already be reclaiming, so
it costs one builder rather than all of them.

## 16.8 Level two, asked again and answered the same way

S:15.7 shipped `techLevelTwo` off because the tier was never reached. S:16.1
removed three separate reasons it could not be spent once reached, so the
question was worth asking again -- properly this time, on the slot that wins
by default, varying nothing but the knob.

Delivery is transformed. The previous attempt managed two Cans in eight games;
this one reaches seven, and the moho extractor now gets built at all, which it
never had.

The outcome is not. Against the same slot's own record without the knob, over
eight games each on Painted Desert:

| slot 1 | without teching | with teching |
|---|---|---|
| games won of those decided | 3 of 4 | 1 of 3 |
| eliminated | 1 | 2 |
| army at the half hour, games it survived | about 35 | about 27 |

So the tier arrives, and the side that buys it is still smaller and still loses
more. That is the same answer S:15.6 and S:15.7 gave, now for a different
reason: it is no longer that the metal is sunk in a frame that never finishes,
it is that 2007 metal for the lab and 300 for the constructor is an army the
other side has and this one does not, for the twenty minutes it takes to turn
into Cans.

`techLevelTwo` stays off, and the honest description of it is a knob a player
can switch on rather than an unfinished feature. What is left to try, and it is
narrower than before: the assist rule puts *every* spare builder on the lab
frame, which is what made the lab finish and is also, now that the planner
serves every builder rather than the front one, a much larger share of the
economy than it was when the rule was written. Capping it -- two builders, or
only once the frame is past halfway -- is the obvious next experiment, and it
is a knob's worth of work.
