#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
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
#include <vector>

/**
 * A builder does not put its fabricator away between one job and the next.
 *
 * Reported from a play-test: a construction kbot interrupted to build another
 * structure behind it "should keep its fabricator out and just turn around
 * rather than putting it away, turning around and then taking it out", and the
 * same going from one wreck to the next on a reclaiming patrol. The stow now
 * waits half a second, and work taken up inside that window cancels it.
 */
namespace rwe
{
    namespace
    {
        constexpr unsigned int StaticStows = 0;
        constexpr unsigned int StaticDeploys = 1;

        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        void countInto(CobScript& script, const std::string& name, unsigned int index)
        {
            script.functions.push_back(CobFunctionInfo{name, static_cast<unsigned int>(script.instructions.size())});
            push(script, OpCode::PUSH_STATIC);
            push(script, index);
            push(script, OpCode::PUSH_CONSTANT);
            push(script, 1u);
            push(script, OpCode::ADD);
            push(script, OpCode::POP_STATIC);
            push(script, index);
            push(script, OpCode::PUSH_CONSTANT);
            push(script, 0u);
            push(script, OpCode::RETURN);
        }

        /** Counts what the engine asks for: stows in static 0, deploys in static 1. */
        std::shared_ptr<CobScript> makeArmCountingScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 2;
            script->pieces.push_back("base");
            countInto(*script, "StopBuilding", StaticStows);
            countInto(*script, "StartBuilding", StaticDeploys);
            return script;
        }

        MapTerrain makeStowTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerStowModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        int readStatic(GameSimulation& sim, UnitId id, unsigned int index)
        {
            return sim.getUnitState(id).cobEnvironment->_statics.at(index);
        }

        struct StowFixture
        {
            std::shared_ptr<CobScript> script = makeArmCountingScript();
            GameSimulation sim{makeStowTerrain(), 0u, 0, 0};
            PlayerId player;
            UnitId builderId;

            StowFixture() : player(addPlayer(sim)), builderId(UnitId(0))
            {
                UnitDefinition builder{};
                builder.objectName = "model";
                builder.isMobile = true;
                builder.canMove = true;
                builder.builder = true;
                builder.buildDistance = 200_ss;
                builder.workerTimePerTick = 60u;
                builder.maxVelocity = 3_ss;
                builder.acceleration = 1_ss;
                builder.brakeRate = 1_ss;
                // Fast enough to finish any turn within a tick. A builder
                // faces its work before starting on it now, and what these
                // cases measure is when the arm goes away, not how long the
                // unit takes to come round.
                builder.turnRate = 32768_ss;
                builder.maxHitPoints = 100;
                builder.buildTime = 0u;
                // The player's capacity is recomputed from the units that
                // exist, so somebody has to hold the stores.
                builder.metalStorage = Metal(10000.0f);
                builder.energyStorage = Energy(10000.0f);
                builder.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
                sim.unitDefinitions["builder"] = builder;

                UnitDefinition frame{};
                frame.objectName = "model";
                // Mobile, though it never moves: a building would need a
                // yardmap and this is only ever something to lathe at.
                frame.isMobile = true;
                frame.canMove = false;
                frame.maxHitPoints = 100;
                frame.buildTime = 1200u;
                frame.buildCostMetal = Metal(1.0f);
                frame.buildCostEnergy = Energy(1.0f);
                frame.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
                sim.unitDefinitions["frame"] = frame;

                registerStowModel(sim);

                builderId = addUnitOfType(sim, "builder", player, SimVector(200_ss, 0_ss, 200_ss), script);
                sim.getUnitState(builderId).inBuildStance = true;
            }

            /** A half-built structure standing where the builder can reach it. */
            UnitId addFrame(const SimVector& position)
            {
                auto id = addUnitOfType(sim, "frame", player, position, script);
                auto& unit = sim.getUnitState(id);
                unit.buildTimeCompleted = 0u;
                unit.hitPoints = 1;
                return id;
            }

            /**
             * A frame one tick of work short of done. What is under test is the
             * moment a job ends, so the tests need one to end promptly while
             * the next is still going.
             */
            UnitId addNearlyFinishedFrame(const SimVector& position)
            {
                auto id = addFrame(position);
                sim.getUnitState(id).buildTimeCompleted = 1200u - 60u;
                return id;
            }

            int stows() { return readStatic(sim, builderId, StaticStows); }
        };
    }

    TEST_CASE("a builder keeps its arm out between back-to-back jobs", "[armstow]")
    {
        StowFixture f;
        auto first = f.addNearlyFinishedFrame(SimVector(260_ss, 0_ss, 200_ss));
        auto second = f.addFrame(SimVector(200_ss, 0_ss, 260_ss));

        auto& builder = f.sim.getUnitState(f.builderId);
        builder.orders.push_back(CompleteBuildOrder(first));
        builder.orders.push_back(CompleteBuildOrder(second));

        // Long enough for the first to be finished and the second taken up.
        tick(f.sim, 10);

        REQUIRE_FALSE(f.sim.getUnitState(first).isBeingBuilt(f.sim.unitDefinitions.at("frame")));
        auto building = std::get_if<UnitBehaviorStateBuilding>(&f.sim.getUnitState(f.builderId).behaviourState);
        REQUIRE(building != nullptr);
        REQUIRE(building->targetUnit == second);

        // The arm never went away in between.
        REQUIRE(f.stows() == 0);
    }

    TEST_CASE("a builder with nothing left to do puts its arm away", "[armstow]")
    {
        // The other half of the rule: the delay is a grace, not a licence to
        // carry the arm about for ever.
        StowFixture f;
        auto only = f.addNearlyFinishedFrame(SimVector(260_ss, 0_ss, 200_ss));
        f.sim.getUnitState(f.builderId).orders.push_back(CompleteBuildOrder(only));

        tick(f.sim, 5);
        REQUIRE_FALSE(f.sim.getUnitState(only).isBeingBuilt(f.sim.unitDefinitions.at("frame")));

        // Inside the grace, still out.
        REQUIRE(f.stows() == 0);

        // Past it, away.
        tick(f.sim, 20);
        REQUIRE(f.stows() == 1);

        // And it does not go on stowing every tick afterwards.
        tick(f.sim, 30);
        REQUIRE(f.stows() == 1);
    }
}
