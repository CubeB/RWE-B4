/**
 * path_bench -- what the pathfinder actually costs, and what the queue does.
 *
 * The path budget is the binding constraint on a big fight: the service
 * spends a fixed number of node expansions a tick and every search is capped,
 * so past a certain unit count requests queue up and a unit waits seconds for
 * a route. Changing those numbers changes *when* units get paths, which no
 * pinning test can guard -- so the case for a new number has to be made out
 * of measurements rather than out of taste.
 *
 * This runs the real PathFindingService over a real GameSimulation with no
 * window and no renderer: terrain with obstacles in it, N units at one end,
 * all ordered to the other, ticked for a while. It reports the queue depth,
 * how many searches actually completed, how many of those were truncated by
 * the per-search cap, and the wall time the phase costs.
 *
 *   path_bench [--units N] [--ticks N] [--budget N] [--obstacles N]
 *
 * --budget and --cap override the compiled-in numbers so a sweep can be run
 * without rebuilding. Everything else about the simulation is fixed, so two
 * runs differing only in the budget are comparable.
 */

#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/pathfinding/PathFindingService.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/MovementClassId.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        /** Deliberately not the simulation's generator: this must not disturb it. */
        struct BenchRandom
        {
            unsigned int state;
            explicit BenchRandom(unsigned int seed) : state(seed) {}
            unsigned int next()
            {
                state = (state * 1103515245u) + 12345u;
                return (state >> 16u) & 0x7FFFu;
            }
            int between(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<unsigned int>(hi - lo + 1)); }
        };

        MapTerrain makeTerrain(int size)
        {
            Grid<unsigned char> heights(size, size, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * Flat land either side of a channel too deep for the walker. The two
         * banks are separate terrain components, so a goal across it is
         * unreachable no matter what units are doing -- which is what lets a
         * reachability precheck that ignores units prove it without searching.
         */
        MapTerrain makeChannelTerrain(int size, int channelX, int channelWidth)
        {
            Grid<unsigned char> heights(size, size, static_cast<unsigned char>(60));
            for (int y = 0; y < size; ++y)
            {
                for (int x = channelX; x < channelX + channelWidth; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(0));
                }
            }
            return MapTerrain(std::move(heights), 30_ss);
        }

        std::shared_ptr<CobScript> makeEmptyScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        UnitDefinition makeWalkerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = SimScalar(1.5f);
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.1_ssf;
            d.turnRate = 360_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 100;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * Gives the walker a named movement class, so terrain is a real
         * barrier to it.
         *
         * An ad-hoc movement class has no registered walkable grid, and the
         * search only consults terrain through that grid -- so without this
         * the water in the pressed-water scenario is invisible and the goal
         * is reachable straight across it. Real ground units name a class
         * from MOVEINFO.TDF, so this is the representative case.
         */
        MovementClassId registerWalkerClass(GameSimulation& sim)
        {
            MovementClassDefinition mc{"BENCHWALK", 2u, 2u, 0u, 0u, 255u, 255u};
            auto id = sim.movementClassDatabase.registerMovementClass(mc);
            sim.movementClassCollisionService.registerMovementClass(id, computeWalkableGrid(sim.terrain, mc));
            return id;
        }

        /** Something solid to route around, so the searches are not straight lines. */
        UnitDefinition makeWallDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.maxHitPoints = 10000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            // An immobile unit stamps itself into the occupancy grid through
            // its yard map, and GameSimulation asserts on having one. Four by
            // four of solid, matching the footprint above.
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
            return d;
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("bench"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::optional<UnitId> spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit));
        }

        int argInt(int argc, char** argv, const char* name, int fallback)
        {
            for (int i = 1; i + 1 < argc; ++i)
            {
                if (std::strcmp(argv[i], name) == 0)
                {
                    return std::atoi(argv[i + 1]);
                }
            }
            return fallback;
        }

        const char* argStr(int argc, char** argv, const char* name, const char* fallback)
        {
            for (int i = 1; i + 1 < argc; ++i)
            {
                if (std::strcmp(argv[i], name) == 0)
                {
                    return argv[i + 1];
                }
            }
            return fallback;
        }
    }
}

int main(int argc, char** argv)
{
    using namespace rwe;

    // With RWE_ENABLE_SIMPROF compiled in, the phase timers report through
    // the global logger, so there has to be one for them to reach.
    auto logger = std::make_shared<SimpleLogger>("path_bench.log", true);
    setGlobalLogger(logger);

    const int unitCount = argInt(argc, argv, "--units", 400);
    const int tickCount = argInt(argc, argv, "--ticks", 300);
    const int budget = argInt(argc, argv, "--budget", 0);
    const int obstacleCount = argInt(argc, argv, "--obstacles", 90);
    const int noRelax = argInt(argc, argv, "--no-relax", 0);
    const int spacing = argInt(argc, argv, "--spacing", 48);
    const std::string scenario = argStr(argc, argv, "--scenario", "spread");
    // RWE_BENCH_HASH=1 prints the sync hash every tick, so a change to the
    // pathfinder can be checked against a build without it: two builds that
    // agree on every hash ran the same simulation.
    const bool logHashes = std::getenv("RWE_BENCH_HASH") != nullptr;

    // 256 heightmap cells is 4096 world units across -- about the size of a
    // real four-player map, and long enough that a crossing is a real search.
    const int mapCells = 256;
    const auto halfWorld = SimScalar(static_cast<float>(mapCells) * 16.0f / 2.0f);

    // The pressed scenarios are the shape that makes the first pass fail: the
    // unit starts hard against the barrier with the goal a few cells beyond
    // it, so the walk can get no closer than where it stands, the goal is
    // never relaxed, and the A* runs out over the whole reachable component.
    // One barrier is water, which a terrain reachability precheck can prove
    // unreachable without searching; the other is wall units, which it cannot
    // see. Running both says how much of the exhausted-search tail is terrain
    // and how much is units, which is what decides the fix.
    const bool pressedWater = scenario == "pressed-water";
    const bool pressedWall = scenario == "pressed-wall";
    const bool pressed = pressedWater || pressedWall;

    GameSimulation sim(
        pressedWater ? makeChannelTerrain(mapCells, 124, 8) : makeTerrain(mapCells),
        0u,
        0,
        0);
    if (budget > 0)
    {
        sim.pathFindingService.expansionBudgetPerTick = budget;
    }
    if (noRelax != 0)
    {
        sim.pathFindingService.relaxGoalWithFirstPass = false;
    }

    auto script = makeEmptyScript();
    auto player = addPlayer(sim);
    sim.unitDefinitions["walker"] = makeWalkerDef();
    sim.unitDefinitions["walker"].movementCollisionInfo = UnitDefinition::NamedMovementClass{registerWalkerClass(sim)};
    sim.unitDefinitions["wall"] = makeWallDef();
    std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
    sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

    BenchRandom rng(12345u);

    int wallsPlaced = 0;
    if (pressedWall)
    {
        // A continuous north-south wall of wall units at world x 0, placed
        // every 64 world units -- exactly the 4x4 footprint -- so no 2x2
        // walker can squeeze through. It reaches the map edge at both ends,
        // so there is no way round it either.
        for (int z = -2048; z <= 2048; z += 64)
        {
            if (spawn(sim, "wall", player, SimVector(0_ss, 0_ss, SimScalar(static_cast<float>(z))), script))
            {
                ++wallsPlaced;
            }
        }
    }
    else if (!pressed)
    {
        // Three broken bands across the route rather than a scatter, so a
        // crossing has to find a gap instead of drifting round one rock. Each
        // band is a run of blocks with a few missing at random.
        const int bandX[3] = {-700, 0, 700};
        for (int band = 0; band < 3 && wallsPlaced < obstacleCount; ++band)
        {
            for (int z = -1400; z <= 1400 && wallsPlaced < obstacleCount; z += 96)
            {
                // Roughly one block in six missing, which is the gap to find.
                if (rng.between(0, 5) == 0)
                {
                    continue;
                }
                auto x = SimScalar(static_cast<float>(bandX[band] + rng.between(-24, 24)));
                if (spawn(sim, "wall", player, SimVector(x, 0_ss, SimScalar(static_cast<float>(z))), script))
                {
                    ++wallsPlaced;
                }
            }
        }
    }

    // The walkers stand along the west edge, a clear footprint apart so that
    // none of them is walled in by its neighbours, and all head east. The
    // pressed scenarios instead put a single rank hard against the barrier.
    std::vector<UnitId> walkers;
    walkers.reserve(static_cast<std::size_t>(unitCount));
    int placed = 0;
    const int columnHeight = 56;
    const auto pressedX = SimScalar(static_cast<float>(pressedWater ? -80 : -64));
    for (int i = 0; placed < unitCount && i < unitCount * 8; ++i)
    {
        auto x = pressed
            ? pressedX
            : -halfWorld + SimScalar(96.0f) + SimScalar(static_cast<float>((i / columnHeight) * spacing));
        auto z = pressed
            ? SimScalar(static_cast<float>((i - (unitCount / 2)) * spacing))
            : SimScalar(static_cast<float>(((i % columnHeight) - (columnHeight / 2)) * spacing));
        if (auto id = spawn(sim, "walker", player, SimVector(x, 0_ss, z), script))
        {
            walkers.push_back(*id);
            ++placed;
        }
    }

    std::cout << "scenario " << scenario << "\n";
    std::cout << "map " << mapCells << " cells, " << placed << " units, " << wallsPlaced << " obstacles\n";
    std::cout << "budget " << sim.pathFindingService.expansionBudgetPerTick
              << " expansions a tick, no per-search cap\n";

    // Spread the destinations down the east edge. Sending every unit at one
    // point means the first arrivals block the goal and everyone behind them
    // searches the whole map to conclude it cannot be reached, which is a
    // real behaviour but not the one being measured here.
    for (std::size_t i = 0; i < walkers.size(); ++i)
    {
        SimVector destination;
        if (pressed)
        {
            // Directly across the barrier from where the unit stands.
            destination = SimVector(SimScalar(96.0f), 0_ss, sim.getUnitState(walkers[i]).position.z);
        }
        else
        {
            auto z = SimScalar(static_cast<float>((static_cast<int>(i % 56) - 28) * 48));
            destination = SimVector(halfWorld - SimScalar(96.0f), 0_ss, z);
        }
        sim.getUnitState(walkers[i]).orders.push_back(MoveOrder(destination));
    }

    // Warm up: the first tick issues every request at once, which is the
    // moment the queue is deepest and the one worth watching.
    std::size_t deepestQueue = 0;

    // The clock's own representation, not a guess at it: microseconds::rep is
    // `long long` on the Windows toolchains and `long` on Linux, and std::max
    // will not deduce a single type from the two.
    using Micros = std::chrono::microseconds::rep;
    Micros totalMicros = 0;
    Micros worstMicros = 0;
    int ticksWithFullQueue = 0;

    for (int tick = 0; tick < tickCount; ++tick)
    {
        auto start = std::chrono::steady_clock::now();
        sim.tick();
        auto end = std::chrono::steady_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        totalMicros += micros;
        worstMicros = std::max(worstMicros, micros);

        auto queued = sim.pathRequests.size();
        deepestQueue = std::max(deepestQueue, queued);
        if (queued > 0)
        {
            ++ticksWithFullQueue;
        }

        if (logHashes)
        {
            std::cout << "hash " << tick << " " << sim.computeHash().value << "\n";
        }
    }

    // How many actually arrived, and how many are still waiting on a route.
    int arrived = 0;
    int stillMoving = 0;
    for (auto id : walkers)
    {
        auto unit = sim.tryGetUnitState(id);
        if (!unit)
        {
            continue;
        }
        if (unit->get().orders.empty())
        {
            ++arrived;
        }
        else
        {
            ++stillMoving;
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "final hash " << sim.computeHash().value << "\n";
    std::cout << "ticks " << tickCount
              << "  mean tick " << (static_cast<double>(totalMicros) / tickCount / 1000.0) << " ms"
              << "  worst " << (static_cast<double>(worstMicros) / 1000.0) << " ms"
              << "  deepest queue " << deepestQueue
              << "  ticks with a queue " << ticksWithFullQueue
              << "\n";
    std::cout << "arrived " << arrived << "  still going " << stillMoving
              << "  requests left " << sim.pathRequests.size() << "\n";

    const auto& c = sim.pathFindingService.counters;
    std::cout << "searches " << c.searches
              << "  exhausted " << c.searchesExhausted
              << "  suspended " << c.searchesSuspended
              << "  abandoned " << c.searchesAbandoned
              << "  expansions " << c.expansions
              << "  relaxed " << c.searchesRelaxed
              << "  walk steps " << c.bugWalkSteps;
    if (c.searches > 0)
    {
        std::cout << "  mean " << (c.expansions / c.searches) << " per search";
    }
    std::cout << "\n";

    return 0;
}
