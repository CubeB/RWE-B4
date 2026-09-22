#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
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
                  sim(makeFlatTerrain(64, 64), 0u, 0, 0),
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

    TEST_CASE("a queue emptied after its frame was shot does not reach for the frame", "[guardassist]")
    {
        // The order of events at a factory under fire: the frame on the pad
        // is shot, the dead are swept off the unit list at the end of that
        // tick, and the order that empties the queue lands on the next. The
        // plant still holds the frame's id, and clearBuild asked the
        // simulation for it without asking whether it was still there, so
        // getUnitState threw -- in the arena, on the tick the AI first
        // cancelled a besieged factory's queue.
        Yard yard;
        yard.queue(1);

        SECTION("while the plant is building the frame")
        {
            tick(yard.sim, 40);
            REQUIRE(std::holds_alternative<FactoryBehaviorStateBuilding>(yard.sim.getUnitState(yard.factoryId).factoryState));
            auto frame = findFrame(yard.sim, "TANK");
            REQUIRE(frame.has_value());

            yard.sim.quietlyKillUnit(*frame);
            yard.sim.deleteDeadUnits();
            REQUIRE_FALSE(yard.sim.tryGetUnitState(*frame).has_value());

            yard.sim.getUnitState(yard.factoryId).buildQueue.clear();
            REQUIRE_NOTHROW(tick(yard.sim, 2));
            REQUIRE(std::holds_alternative<FactoryBehaviorStateIdle>(yard.sim.getUnitState(yard.factoryId).factoryState));
        }

        SECTION("on the tick the frame was created, before the plant has taken it up")
        {
            // Step until the pad has just produced the frame and the plant
            // is still holding it as a creation, which it turns into
            // building on its next update.
            std::optional<UnitId> frame;
            for (int i = 0; i < 40 && !frame; ++i)
            {
                tick(yard.sim, 1);
                auto creating = std::get_if<FactoryBehaviorStateCreatingUnit>(&yard.sim.getUnitState(yard.factoryId).factoryState);
                if (creating != nullptr)
                {
                    if (auto done = std::get_if<UnitCreationStatusDone>(&creating->status); done != nullptr)
                    {
                        frame = done->unitId;
                    }
                }
            }
            REQUIRE(frame.has_value());

            yard.sim.quietlyKillUnit(*frame);
            yard.sim.deleteDeadUnits();
            yard.sim.getUnitState(yard.factoryId).buildQueue.clear();
            REQUIRE_NOTHROW(tick(yard.sim, 2));
            REQUIRE(std::holds_alternative<FactoryBehaviorStateIdle>(yard.sim.getUnitState(yard.factoryId).factoryState));
        }
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

    // The cases above give the builder a reach of 200 and the plant a 4x4
    // footprint, which is why they passed while the game did not: at those
    // numbers everything is in range of everything. The shipped ones are not
    // so kind. ARMCK reaches 40, a vehicle plant is 8x6 tiles -- 128 by 96
    // world units -- and the frame it is building stands at its middle, some
    // 48 units inside the nearest cell a builder can stand on. Measuring the
    // reach to the frame therefore put the assister permanently out of range:
    // it walked up to the yard, found itself short and stood there, which is
    // what the play-test reported. It measures to the factory now.
    namespace
    {
        UnitDefinition makeBigPlantDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.workerTimePerTick = 2u;
            d.metalStorage = Metal(10000.0f);
            d.energyStorage = Energy(10000.0f);
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            // ARMVP: FootprintX=8, FootprintZ=6.
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 6u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(8, 6, YardMapCell::Ground);
            return d;
        }

        /**
         * A pad in the middle of the yard and impassable everywhere else,
         * which is the shape of a real plant: the unit it is making stands on
         * ground the yard keeps everything else off. Without the open cells
         * the frame cannot be placed at all once the plant occupies its own
         * footprint, and with the whole yard open the builder walks in and
         * the reach rules are never asked.
         */
        void openTheYardPad(UnitDefinition& plant)
        {
            auto& yard = *plant.yardMap;
            for (int z = 2; z <= 3; ++z)
            {
                for (int x = 3; x <= 4; ++x)
                {
                    yard.set(x, z, YardMapCell::GroundPassable);
                }
            }
        }

        UnitDefinition makeShortArmedBuilderDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            // ARMCK: Builddistance=40.
            d.buildDistance = 40_ss;
            d.workerTimePerTick = 3u;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }
    }

    TEST_CASE("a short-armed builder reaches a frame in the middle of a full-sized plant", "[guardassist]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerModel(sim);

        // Upper case and spawned the real way, both for the reasons the case
        // below gives: a lower-case type is invisible to the spawn path, and
        // addUnitOfType does not stamp the occupancy grid. Without the stamp
        // the builder walks straight through the plant to the frame, and this
        // case goes green against a simulation with no reach rule in it at
        // all -- which is what it was doing until the case below was written.
        sim.unitDefinitions["PLANT"] = makeBigPlantDef();
        openTheYardPad(sim.unitDefinitions["PLANT"]);
        sim.unitDefinitions["BUILDER"] = makeShortArmedBuilderDef();
        sim.unitDefinitions["TANK"] = makeTankDef();
        // Long enough to still be under construction once the builder has
        // walked in. The plant on its own finishes three hundred points well
        // inside that walk, and a finished tank would leave the builder idle
        // for honest reasons, saying nothing about its reach.
        sim.unitDefinitions["TANK"].buildTime = 3000u;
        sim.unitScriptDefinitions["TANK"] = *script;
        sim.unitScriptDefinitions["PLANT"] = *script;
        sim.unitScriptDefinitions["BUILDER"] = *script;

        auto plantId = sim.trySpawnUnit("PLANT", player, SimVector(200_ss, 0_ss, 200_ss), std::nullopt).value();
        sim.getUnitState(plantId).inBuildStance = true;
        sim.getUnitState(plantId).buildQueue.push_back(std::make_pair(std::string("TANK"), 1));

        // Well outside the yard, so it has to walk in.
        auto builderId = sim.trySpawnUnit("BUILDER", player, SimVector(360_ss, 0_ss, 200_ss), std::nullopt).value();
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(GuardOrder(plantId));

        tick(sim, 150);

        auto frame = findFrame(sim, "TANK");
        REQUIRE(frame.has_value());

        // In range and lathing, rather than parked outside doing nothing.
        auto building = std::get_if<UnitBehaviorStateBuilding>(&sim.getUnitState(builderId).behaviourState);
        REQUIRE(building != nullptr);
        REQUIRE(building->targetUnit == *frame);

        // Two from the plant and three from the guard.
        auto before = progressOf(sim, "TANK");
        tick(sim, 20);
        REQUIRE(progressOf(sim, "TANK") - before == 100u);
    }

    TEST_CASE("a builder told to finish the frame itself reaches it too", "[guardassist]")
    {
        // Reported from play: a commander set to guard or repair a unit being
        // built in a factory "would never move close enough to actually begin
        // assisting in its fabrication".
        //
        // The guard-the-factory path has measured its reach to the factory
        // since the case above, because a frame on a pad stands at the middle
        // of a building nothing can walk into. Two paths did not: a repair
        // order on the frame, and a guard order aimed at the frame rather
        // than at the plant. Both are the same job asked for in the words a
        // player happens to use, and both left the builder parked outside --
        // the repair one permanently out of arm's length, the guard one
        // falling through to "stay close" and simply watching.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerModel(sim);

        // Upper case, because createUnit upper-cases the type before
        // tryAddUnit looks it up again: a lower-case key is invisible to the
        // spawn path and throws rather than failing the assertion you were
        // looking at.
        sim.unitDefinitions["PLANT"] = makeBigPlantDef();
        openTheYardPad(sim.unitDefinitions["PLANT"]);
        sim.unitDefinitions["BUILDER"] = makeShortArmedBuilderDef();
        sim.unitDefinitions["TANK"] = makeTankDef();
        sim.unitDefinitions["TANK"].buildTime = 3000u;
        sim.unitScriptDefinitions["TANK"] = *script;

        // Spawned the real way, so the plant stamps the occupancy grid.
        // This is the whole point of the case: addUnitOfType puts a unit in
        // the list without occupying anything, so a builder simply walks
        // through the plant to the frame and every reach test passes for the
        // wrong reason. YardMapCell::Ground is impassable, and a yard the
        // builder cannot enter is what puts the frame out of arm's length.
        sim.unitScriptDefinitions["PLANT"] = *script;
        sim.unitScriptDefinitions["BUILDER"] = *script;
        auto plantId = sim.trySpawnUnit("PLANT", player, SimVector(200_ss, 0_ss, 200_ss), std::nullopt).value();
        sim.getUnitState(plantId).inBuildStance = true;
        sim.getUnitState(plantId).buildQueue.push_back(std::make_pair(std::string("TANK"), 1));

        auto builderId = sim.trySpawnUnit("BUILDER", player, SimVector(360_ss, 0_ss, 200_ss), std::nullopt).value();
        sim.getUnitState(builderId).inBuildStance = true;

        // Let the plant stand the frame up, so there is something to name.
        tick(sim, 30);
        auto frame = findFrame(sim, "TANK");
        REQUIRE(frame.has_value());

        auto worksOnTheFrame = [&]() {
            tick(sim, 120);
            auto building = std::get_if<UnitBehaviorStateBuilding>(&sim.getUnitState(builderId).behaviourState);
            REQUIRE(building != nullptr);
            REQUIRE(building->targetUnit == *frame);
            // Two from the plant and three from the builder.
            auto before = progressOf(sim, "TANK");
            tick(sim, 20);
            CHECK(progressOf(sim, "TANK") - before == 100u);
        };

        SECTION("told to repair it")
        {
            sim.getUnitState(builderId).orders.push_back(RepairOrder(*frame));
            worksOnTheFrame();
        }

        SECTION("told to guard it")
        {
            sim.getUnitState(builderId).orders.push_back(GuardOrder(*frame));
            worksOnTheFrame();
            // Guarding is not finishing: the order stands.
            CHECK(sim.getUnitState(builderId).orders.size() == 1);
        }
    }
}
