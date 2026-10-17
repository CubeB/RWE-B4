#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>

/**
 * BT-3, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md (static reading of 0x437840):
 * an extractor's work figure N is the sum, over the footprint cells that lie
 * inside the map, of the cell's metal byte plus one. The settle credits
 * `ExtractsMetal * N` and `SetSpeed` is started with N.
 *
 * ARMMEX's shipped FBI: FootprintX=3, FootprintZ=3, MaxDamage=170,
 * ExtractsMetal=0.001.
 */
namespace rwe
{
    namespace
    {
        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        /** Files the number `SetSpeed` was given in static 0. */
        std::shared_ptr<CobScript> makeSetSpeedRecorder()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 1;
            script->pieces.push_back("base");

            script->functions.push_back(CobFunctionInfo{"SetSpeed", static_cast<unsigned int>(script->instructions.size())});
            push(*script, OpCode::PUSH_LOCAL_VAR);
            push(*script, 0u);
            push(*script, OpCode::POP_STATIC);
            push(*script, 0u);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u);
            push(*script, OpCode::RETURN);

            return script;
        }

        UnitDefinition makeArmMex(unsigned int footprint)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.maxHitPoints = 170;
            d.buildTime = 0u;
            d.extractsMetal = Metal(0.001f);
            d.energyStorage = Energy(10000.0f);
            d.metalStorage = Metal(10000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(footprint, footprint, YardMapCell::Ground);
            return d;
        }

        struct ExtractorFixture
        {
            GameSimulation sim{makeFlatTerrain(32, 32), 0u, 0, 0};
            PlayerId player{addPlayer(sim)};
            std::shared_ptr<CobScript> script{makeSetSpeedRecorder()};

            ExtractorFixture()
            {
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
            }

            UnitId spawn(unsigned int footprint, const SimVector& position)
            {
                sim.unitDefinitions["ARMMEX"] = makeArmMex(footprint);
                sim.unitScriptDefinitions["ARMMEX"] = *script;
                auto id = sim.trySpawnCompletedUnit("ARMMEX", player, position, std::nullopt);
                REQUIRE(id);
                sim.getUnitState(*id).activated = true;
                return *id;
            }

            int setSpeedArgument(UnitId id)
            {
                return sim.getUnitState(id).cobEnvironment->_statics.at(0);
            }

            /** Metal made over one second by whatever stands on the map. */
            float incomeOverOneSecond()
            {
                sim.getPlayer(player).metal = Metal(0.0f);
                for (unsigned int i = 0; i < SimTicksPerSecond; ++i)
                {
                    sim.tick();
                }
                return sim.getPlayer(player).metal.value;
            }
        };
    }

    TEST_CASE("every footprint cell is worth its metal byte plus one to an extractor (BT-3)", "[economy][cob]")
    {
        ExtractorFixture f;
        const SimVector position = f.sim.terrain.heightmapToWorldSpace(SimVector(10_ss, 0_ss, 10_ss));

        SECTION("a 2 by 2 extractor over cells totalling 60 has 64 units of work")
        {
            // The finding's own example.
            auto info = UnitDefinition::MovementCollisionInfo(UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u});
            auto region = f.sim.computeFootprintRegion(position, info);
            f.sim.metalGrid.set(region.x, region.y, static_cast<unsigned char>(10));
            f.sim.metalGrid.set(region.x + 1, region.y, static_cast<unsigned char>(20));
            f.sim.metalGrid.set(region.x, region.y + 1, static_cast<unsigned char>(30));
            f.sim.metalGrid.set(region.x + 1, region.y + 1, static_cast<unsigned char>(0));

            auto id = f.spawn(2u, position);

            REQUIRE(f.setSpeedArgument(id) == 64);
            REQUIRE(f.incomeOverOneSecond() == Catch::Approx(0.064f).margin(1e-6));
        }

        SECTION("a footprint with no metal at all is worth one a cell, not nothing")
        {
            auto id = f.spawn(3u, position);

            REQUIRE(f.setSpeedArgument(id) == 9);
            REQUIRE(f.incomeOverOneSecond() == Catch::Approx(0.009f).margin(1e-6));
        }

        SECTION("ARMMEX over a patch: zero-metal cells among the rich ones")
        {
            auto info = UnitDefinition::MovementCollisionInfo(UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 0u});
            auto region = f.sim.computeFootprintRegion(position, info);
            const unsigned char bytes[3][3] = {{255, 0, 255}, {0, 100, 0}, {255, 0, 255}};
            for (int z = 0; z < 3; ++z)
            {
                for (int x = 0; x < 3; ++x)
                {
                    f.sim.metalGrid.set(region.x + x, region.y + z, bytes[z][x]);
                }
            }

            auto id = f.spawn(3u, position);

            // 4 * 255 + 100 = 1120, plus nine cells.
            REQUIRE(f.setSpeedArgument(id) == 1129);
            REQUIRE(f.incomeOverOneSecond() == Catch::Approx(1.129f).margin(1e-5));
        }

        SECTION("a cell off the map adds nothing, not even the one")
        {
            // A building cannot be placed hanging off the map, so this asks the
            // routine directly. Every cell that does exist holds 5.
            for (int z = 0; z < f.sim.metalGrid.getHeight(); ++z)
            {
                for (int x = 0; x < f.sim.metalGrid.getWidth(); ++x)
                {
                    f.sim.metalGrid.set(x, z, static_cast<unsigned char>(5));
                }
            }
            const int w = f.sim.metalGrid.getWidth();
            const int h = f.sim.metalGrid.getHeight();

            // Over the top-left corner: only the four cells at (0..1, 0..1).
            REQUIRE(f.sim.extractorMetalWork(DiscreteRect(-1, -1, 3, 3)) == 4 * 6);
            // Over the bottom-right corner: the same four.
            REQUIRE(f.sim.extractorMetalWork(DiscreteRect(w - 2, h - 2, 3, 3)) == 4 * 6);
            // Wholly inside, and wholly outside.
            REQUIRE(f.sim.extractorMetalWork(DiscreteRect(4, 4, 3, 3)) == 9 * 6);
            REQUIRE(f.sim.extractorMetalWork(DiscreteRect(-10, -10, 3, 3)) == 0);
        }
    }
}
