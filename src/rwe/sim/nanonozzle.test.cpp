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
#include <set>
#include <string>
#include <vector>

/**
 * A builder with two nozzles sprays from both of them.
 *
 * `QueryNanoPiece` is a question with a side effect: the shipped scripts for
 * the units that have two answer with a different piece each time they are
 * asked -- ARMACK alternates `rnanospray` and `lnanospray`, the air repair pad
 * `beam1` and `beam2`. RWE asked twice on every tick, once where the work is
 * done and once where a running spray follows its nozzle, so the answer
 * advanced twice and the spray came out of the same side for ever. Reported
 * from a play-test against both of those units.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeNozzleTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        /**
         * The shipped shape in miniature: piece 1, then piece 2, then piece 1
         * again. Written as `piece = 1 + flag` and `flag = 1 - flag` so that
         * the alternation is the script's, exactly as it is in the real ones.
         */
        std::shared_ptr<CobScript> makeTwoNozzleScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 1;
            script->pieces.push_back("base");
            script->pieces.push_back("nano1");
            script->pieces.push_back("nano2");

            script->functions.push_back(CobFunctionInfo{"QueryNanoPiece", static_cast<unsigned int>(script->instructions.size())});
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 1u);
            push(*script, OpCode::PUSH_STATIC);
            push(*script, 0u);
            push(*script, OpCode::ADD);
            push(*script, OpCode::POP_LOCAL_VAR);
            push(*script, 0u);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 1u);
            push(*script, OpCode::PUSH_STATIC);
            push(*script, 0u);
            push(*script, OpCode::SUB);
            push(*script, OpCode::POP_STATIC);
            push(*script, 0u);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u);
            push(*script, OpCode::RETURN);

            return script;
        }

        void registerNozzleModel(GameSimulation& sim)
        {
            // The two nozzles stand well apart, so which one is spraying is a
            // matter of arithmetic rather than eyesight.
            std::vector<UnitPieceDefinition> pieces{
                UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt},
                UnitPieceDefinition{"nano1", SimVector(-10_ss, 0_ss, 0_ss), std::string("base")},
                UnitPieceDefinition{"nano2", SimVector(10_ss, 0_ss, 0_ss), std::string("base")}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitId spawnNozzleUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& position, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> meshes;
            for (const auto& piece : sim.unitModelDefinitions.at(sim.unitDefinitions.at(unitType).objectName).pieces)
            {
                UnitMesh mesh;
                mesh.name = piece.name;
                meshes.push_back(mesh);
            }
            UnitState unit(meshes, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.inBuildStance = true;
            return sim.tryAddUnit(std::move(unit)).value();
        }
    }

    TEST_CASE("a builder with two nozzles sprays from both of them", "[nanolathe]")
    {
        auto script = makeTwoNozzleScript();
        GameSimulation sim(makeNozzleTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerNozzleModel(sim);

        UnitDefinition builder{};
        builder.objectName = "model";
        builder.isMobile = true;
        builder.canMove = false;
        builder.builder = true;
        builder.buildDistance = 200_ss;
        builder.workerTimePerTick = 2u;
        builder.maxHitPoints = 100;
        builder.buildTime = 0u;
        builder.metalStorage = Metal(10000.0f);
        builder.energyStorage = Energy(10000.0f);
        builder.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["builder"] = builder;

        UnitDefinition frameDef{};
        frameDef.objectName = "model";
        // Mobile, though it never moves: a building needs a yardmap and
        // this one is only ever a thing to lathe at.
        frameDef.isMobile = true;
        frameDef.canMove = false;
        frameDef.maxHitPoints = 100;
        frameDef.buildTime = 10000u;
        frameDef.buildCostMetal = Metal(1.0f);
        frameDef.buildCostEnergy = Energy(1.0f);
        frameDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["frame"] = frameDef;

        auto builderId = spawnNozzleUnit(sim, "builder", player, SimVector(200_ss, 0_ss, 200_ss), script);
        auto frameId = spawnNozzleUnit(sim, "frame", player, SimVector(260_ss, 0_ss, 200_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0u;

        sim.getUnitState(builderId).orders.push_back(CompleteBuildOrder(frameId));

        // Where the spray came from on each of several ticks of one job.
        std::set<float> nozzles;
        for (int i = 0; i < 8; ++i)
        {
            sim.tick();
            const auto& state = sim.getUnitState(builderId);
            if (auto building = std::get_if<UnitBehaviorStateBuilding>(&state.behaviourState))
            {
                if (building->nanoParticleOrigin)
                {
                    nozzles.insert(simScalarToFloat(building->nanoParticleOrigin->x));
                }
            }
        }

        // Both of them, not one of them twice.
        REQUIRE(nozzles.size() == 2);
    }
}
