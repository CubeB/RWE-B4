#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <algorithm>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeMoveRateTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addMoveRatePlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("arm"),
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

        /**
         * A script defining StartMoving, StopMoving and the three MoveRates.
         * Each body just sleeps for a long time, so a thread that was started
         * is still sitting in the environment under its own name when the test
         * looks -- which is how we tell which callback the engine ran.
         */
        std::shared_ptr<CobScript> makeMoveRateScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");

            const std::vector<std::string> names{"StartMoving", "StopMoving", "MoveRate1", "MoveRate2", "MoveRate3"};
            for (const auto& name : names)
            {
                script->functions.push_back(CobFunctionInfo{name, static_cast<unsigned int>(script->instructions.size())});
                script->instructions.push_back(static_cast<uint32_t>(OpCode::PUSH_CONSTANT));
                script->instructions.push_back(600000u); // ten minutes of sleep
                script->instructions.push_back(static_cast<uint32_t>(OpCode::SLEEP));
                script->instructions.push_back(static_cast<uint32_t>(OpCode::PUSH_CONSTANT));
                script->instructions.push_back(0u);
                script->instructions.push_back(static_cast<uint32_t>(OpCode::RETURN));
            }

            return script;
        }

        /** A script with none of the move callbacks at all, like most of the shipped ones. */
        std::shared_ptr<CobScript> makeSilentScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerMoveRateModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * A fast aircraft-like mover. `rate1` and `rate2` of zero stand for a
         * unit that names neither key, which the loader turns into twice the
         * unit's top speed.
         */
        void defineMover(GameSimulation& sim, const std::string& type, float maxVelocity, float rate1, float rate2)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = false;
            d.maxVelocity = SimScalar(maxVelocity);
            d.acceleration = SimScalar(maxVelocity);
            d.brakeRate = SimScalar(maxVelocity);
            d.turnRate = 3000_ss;
            d.moveRate1 = rate1 > 0.0f ? SimScalar(rate1) : SimScalar(maxVelocity) * 2_ss;
            d.moveRate2 = rate2 > 0.0f ? SimScalar(rate2) : SimScalar(maxVelocity) * 2_ss;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        UnitId spawnMover(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        bool hasThread(const GameSimulation& sim, UnitId id, const std::string& name)
        {
            const auto& threads = sim.getUnitState(id).cobEnvironment->threads;
            return std::any_of(threads.begin(), threads.end(), [&](const auto& t) { return t->name == name; });
        }
    }

    // Priority 15. The band machine is 0x43DA70: a unit's current speed against
    // MoveRate1 (`def+0x1AE`) and MoveRate2 (`def+0x1B2`), remembered in bits 2-3
    // of `unit+0x110`, calling StopMoving / StartMoving / MoveRate1..3 only when
    // the band changes. Both thresholds default to twice MaxVelocity (0x42C1E6),
    // which is why a unit that names neither always calls MoveRate1.
    TEST_CASE("move rate callbacks", "[moverate]")
    {
        GameSimulation sim(makeMoveRateTerrain(), 0u, 0, 0);
        registerMoveRateModel(sim);
        auto player = addMoveRatePlayer(sim);
        auto script = makeMoveRateScript();

        SECTION("a unit that names no MoveRate keys calls MoveRate1 when it starts moving")
        {
            // The Atlas's case exactly: no MoveRate keys in its FBI, and its
            // thruster flames are started from MoveRate1.
            defineMover(sim, "atlas", 7.0f, 0.0f, 0.0f);
            auto atlas = spawnMover(sim, "atlas", player, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(atlas).addOrder(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));

            for (int i = 0; i < 20; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(atlas).moveRateBand == 1);
            REQUIRE(hasThread(sim, atlas, "StartMoving"));
            REQUIRE(hasThread(sim, atlas, "MoveRate1"));
            REQUIRE_FALSE(hasThread(sim, atlas, "MoveRate2"));
            REQUIRE_FALSE(hasThread(sim, atlas, "MoveRate3"));
        }

        SECTION("a unit above its MoveRate1 threshold calls MoveRate2 instead")
        {
            // The Fighter: MaxVelocity 10, MoveRate1 8. At full speed it is in
            // the second band, and its MoveRate2 is the barrel roll.
            defineMover(sim, "fighter", 10.0f, 8.0f, 0.0f);
            auto fighter = spawnMover(sim, "fighter", player, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(fighter).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 0_ss)));

            for (int i = 0; i < 20; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(fighter).moveRateBand == 2);
            REQUIRE(hasThread(sim, fighter, "MoveRate2"));
        }

        SECTION("a unit above both thresholds calls MoveRate3")
        {
            // The Valkyrie's shape: MaxVelocity 7 with thresholds of 1 and 2.
            defineMover(sim, "valkyrie", 7.0f, 1.0f, 2.0f);
            auto valkyrie = spawnMover(sim, "valkyrie", player, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(valkyrie).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 0_ss)));

            for (int i = 0; i < 20; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(valkyrie).moveRateBand == 3);
            REQUIRE(hasThread(sim, valkyrie, "MoveRate3"));
        }

        SECTION("stopping drops the unit back to band zero and calls StopMoving")
        {
            defineMover(sim, "atlas", 7.0f, 0.0f, 0.0f);
            auto atlas = spawnMover(sim, "atlas", player, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(atlas).addOrder(MoveOrder(SimVector(100_ss, 0_ss, 0_ss)));

            for (int i = 0; i < 200; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(atlas).moveRateBand == 0);
            REQUIRE(hasThread(sim, atlas, "StopMoving"));
        }

        SECTION("a unit standing still never enters a band at all")
        {
            defineMover(sim, "atlas", 7.0f, 0.0f, 0.0f);
            auto atlas = spawnMover(sim, "atlas", player, SimVector(0_ss, 0_ss, 0_ss), script);

            for (int i = 0; i < 20; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(atlas).moveRateBand == 0);
            REQUIRE_FALSE(hasThread(sim, atlas, "StartMoving"));
            REQUIRE_FALSE(hasThread(sim, atlas, "MoveRate1"));
        }

        SECTION("a script without the functions is simply left alone")
        {
            // Which is nearly all of them: only five of the two hundred shipped
            // scripts define any MoveRate function.
            auto silent = makeSilentScript();
            defineMover(sim, "peewee", 1.3f, 0.0f, 0.0f);
            auto peewee = spawnMover(sim, "peewee", player, SimVector(0_ss, 0_ss, 0_ss), silent);
            sim.getUnitState(peewee).addOrder(MoveOrder(SimVector(200_ss, 0_ss, 0_ss)));

            for (int i = 0; i < 20; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(peewee).moveRateBand == 1);
            REQUIRE(sim.getUnitState(peewee).cobEnvironment->threads.empty());
        }
    }
}
