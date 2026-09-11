#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * A builder told to guard a factory helps it build. A play-test once said it
 * did not, code review could not find why, and there was no fixture to ask.
 * This is the fixture: a factory with a queue, a builder guarding it, and the
 * frame's progress counted tick by tick.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * A plant: a fixed builder with a worker time, and nothing else to do.
         * It carries the yard's storage as well, because the simulation
         * recomputes a player's capacity every second from the units that
         * exist, and a yard with no commander and no storage would be clamped
         * to nothing on the first second boundary and stall.
         */
        UnitDefinition makeFactoryDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.workerTimePerTick = workerTimePerTick;
            d.metalStorage = Metal(10000.0f);
            d.energyStorage = Energy(10000.0f);
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
            return d;
        }

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.buildDistance = 200_ss;
            d.workerTimePerTick = workerTimePerTick;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** What the plant turns out: three hundred build points, cheap enough never to stall. */
        UnitDefinition makeTankDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 300u;
            d.buildCostMetal = Metal(1.0f);
            d.buildCostEnergy = Energy(1.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** The frame the factory is working on, once there is one. */
        std::optional<UnitId> findFrame(GameSimulation& sim, const std::string& unitType)
        {
            for (const auto& [id, unit] : sim.units)
            {
                if (unit.unitType == unitType && !unit.isDead())
                {
                    return UnitId(id);
                }
            }
            return std::nullopt;
        }

        unsigned int progressOf(GameSimulation& sim, const std::string& unitType)
        {
            auto id = findFrame(sim, unitType);
            return id ? sim.getUnitState(*id).buildTimeCompleted : 0u;
        }

        struct Yard
        {
            std::shared_ptr<CobScript> script;
            GameSimulation sim;
            PlayerId player;
            UnitId factoryId;

            Yard()
                : script(makeEmptyCobScript({"base"})),
                  sim(makeFlatTerrain(), 0u, 0, 0),
                  player(addPlayer(sim)),
                  factoryId(UnitId(0))
            {
                sim.unitDefinitions["factory"] = makeFactoryDef(2u);
                sim.unitDefinitions["builder"] = makeBuilderDef(3u);
                sim.unitDefinitions["TANK"] = makeTankDef();
                sim.unitScriptDefinitions["TANK"] = *script;
                registerModel(sim);

                factoryId = addUnitOfType(sim, "factory", player, SimVector(200_ss, 0_ss, 200_ss), script);
                // The stance a StartBuilding script would set; there is no script here.
                sim.getUnitState(factoryId).inBuildStance = true;
            }

            void queue(int count)
            {
                sim.getUnitState(factoryId).buildQueue.push_back(std::make_pair(std::string("TANK"), count));
            }

            UnitId addGuardingBuilder()
            {
                auto builderId = addUnitOfType(sim, "builder", player, SimVector(260_ss, 0_ss, 200_ss), script);
                sim.getUnitState(builderId).inBuildStance = true;
                sim.getUnitState(builderId).orders.push_back(GuardOrder(factoryId));
                return builderId;
            }

            /** Ticks until the frame is complete, or gives up. */
            int ticksToFinish(int limit)
            {
                for (int t = 1; t <= limit; ++t)
                {
                    sim.tick();
                    auto id = findFrame(sim, "TANK");
                    if (id && !sim.getUnitState(*id).isBeingBuilt(sim.unitDefinitions.at("TANK")))
                    {
                        return t;
                    }
                }
                return limit;
            }
        };
    }

    TEST_CASE("a factory alone builds at its own worker time", "[guardassist]")
    {
        // The baseline the guard cases are measured against: the plant needs
        // a couple of ticks to open its pad and stand the frame up, then
        // pays two points a tick, its own worker time.
        Yard yard;
        yard.queue(1);
        tick(yard.sim, 40);
        auto before = progressOf(yard.sim, "TANK");
        REQUIRE(before > 0u);
        tick(yard.sim, 20);
        REQUIRE(progressOf(yard.sim, "TANK") - before == 40u);
    }

    TEST_CASE("a builder guarding the factory adds its worker time to the build", "[guardassist]")
    {
        Yard yard;
        yard.queue(1);
        auto builderId = yard.addGuardingBuilder();
        tick(yard.sim, 40);

        // The guard has found the frame and has its arm on it.
        auto frame = findFrame(yard.sim, "TANK");
        REQUIRE(frame.has_value());
        auto building = std::get_if<UnitBehaviorStateBuilding>(&yard.sim.getUnitState(builderId).behaviourState);
        REQUIRE(building != nullptr);
        REQUIRE(building->targetUnit == *frame);

        // Two from the plant and three from the guard, every tick.
        auto before = progressOf(yard.sim, "TANK");
        tick(yard.sim, 20);
        REQUIRE(progressOf(yard.sim, "TANK") - before == 100u);

        // And the guard order is still standing: helping is not finishing.
        REQUIRE(yard.sim.getUnitState(builderId).orders.size() == 1);
    }

    TEST_CASE("a guarded factory finishes sooner", "[guardassist]")
    {
        int alone;
        {
            Yard yard;
            yard.queue(1);
            alone = yard.ticksToFinish(400);
        }
        int guarded;
        {
            Yard yard;
            yard.queue(1);
            yard.addGuardingBuilder();
            guarded = yard.ticksToFinish(400);
        }
        REQUIRE(alone < 400);
        REQUIRE(guarded < alone);
        // Five points a tick against two: well under half the time.
        REQUIRE(guarded * 2 < alone);
    }

    TEST_CASE("the guard follows the factory on to the next unit in its queue", "[guardassist]")
    {
        // A builder that helped with the first frame and then stood idle
        // while the second went up alone would look exactly like the
        // play-test report. It keeps its order, so it keeps helping.
        Yard yard;
        yard.queue(2);
        auto builderId = yard.addGuardingBuilder();

        // Past the first unit: it rolls off and the plant starts the second.
        auto first = yard.ticksToFinish(400);
        REQUIRE(first < 400);
        tick(yard.sim, 40);

        auto frames = 0;
        std::optional<UnitId> second;
        for (const auto& [id, unit] : yard.sim.units)
        {
            if (unit.unitType == "TANK" && unit.isBeingBuilt(yard.sim.unitDefinitions.at("TANK")))
            {
                ++frames;
                second = UnitId(id);
            }
        }
        REQUIRE(frames == 1);

        auto building = std::get_if<UnitBehaviorStateBuilding>(&yard.sim.getUnitState(builderId).behaviourState);
        REQUIRE(building != nullptr);
        REQUIRE(building->targetUnit == *second);

        auto before = yard.sim.getUnitState(*second).buildTimeCompleted;
        tick(yard.sim, 20);
        REQUIRE(yard.sim.getUnitState(*second).buildTimeCompleted - before == 100u);
    }
}
