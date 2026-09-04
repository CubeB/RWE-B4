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
    const int spacing = argInt(argc, argv, "--spacing", 48);

    // 256 heightmap cells is 4096 world units across -- about the size of a
    // real four-player map, and long enough that a crossing is a real search.
    const int mapCells = 256;
    const auto halfWorld = SimScalar(static_cast<float>(mapCells) * 16.0f / 2.0f);

    GameSimulation sim(makeTerrain(mapCells), 0u, 0, 0);
    if (budget > 0)
    {
        sim.pathFindingService.expansionBudgetPerTick = budget;
    }

    auto script = makeEmptyScript();
    auto player = addPlayer(sim);
    sim.unitDefinitions["walker"] = makeWalkerDef();
    sim.unitDefinitions["wall"] = makeWallDef();
    std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
    sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

    BenchRandom rng(12345u);

    // Three broken bands across the route rather than a scatter, so a
    // crossing has to find a gap instead of drifting round one rock. Each
    // band is a run of blocks with a few missing at random.
    int wallsPlaced = 0;
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

    // The walkers stand along the west edge, a clear footprint apart so that
    // none of them is walled in by its neighbours, and all head east.
    std::vector<UnitId> walkers;
    walkers.reserve(static_cast<std::size_t>(unitCount));
    int placed = 0;
    const int columnHeight = 56;
    for (int i = 0; placed < unitCount && i < unitCount * 8; ++i)
    {
        auto x = -halfWorld + SimScalar(96.0f) + SimScalar(static_cast<float>((i / columnHeight) * spacing));
        auto z = SimScalar(static_cast<float>(((i % columnHeight) - (columnHeight / 2)) * spacing));
        if (auto id = spawn(sim, "walker", player, SimVector(x, 0_ss, z), script))
        {
            walkers.push_back(*id);
            ++placed;
        }
    }

    std::cout << "map " << mapCells << " cells, " << placed << " units, " << wallsPlaced << " obstacles\n";
    std::cout << "budget " << sim.pathFindingService.expansionBudgetPerTick
              << ", per-search cap " << MaxOpenListQueries << "\n";

    // Spread the destinations down the east edge. Sending every unit at one
    // point means the first arrivals block the goal and everyone behind them
    // searches the whole map to conclude it cannot be reached, which is a
    // real behaviour but not the one being measured here.
    for (std::size_t i = 0; i < walkers.size(); ++i)
    {
        auto z = SimScalar(static_cast<float>((static_cast<int>(i % 56) - 28) * 48));
        auto destination = SimVector(halfWorld - SimScalar(96.0f), 0_ss, z);
        sim.getUnitState(walkers[i]).orders.push_back(MoveOrder(destination));
    }

    // Warm up: the first tick issues every request at once, which is the
    // moment the queue is deepest and the one worth watching.
    std::size_t deepestQueue = 0;
    long long totalMicros = 0;
    long long worstMicros = 0;
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
              << "  cut off by the cap " << c.searchesTruncated
              << "  exhausted " << c.searchesExhausted
              << "  expansions " << c.expansions;
    if (c.searches > 0)
    {
        std::cout << "  mean " << (c.expansions / c.searches) << " per search";
    }
    std::cout << "\n";

    return 0;
}
