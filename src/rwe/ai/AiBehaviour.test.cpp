#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim, const std::string& name, GamePlayerType type, const std::string& side)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                type,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                side,
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        UnitDefinition makeDef(bool commander, bool builder, bool mobile, const std::string& weapon, unsigned int sight)
        {
            UnitDefinition d{};
            d.commander = commander;
            d.builder = builder;
            d.isMobile = mobile;
            d.canMove = mobile;
            d.canAttack = !weapon.empty();
            d.weapon1 = weapon;
            d.maxHitPoints = 100;
            d.buildTime = 100u;
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(100.0f);
            d.sightDistance = sight;
            d.maxVelocity = 2_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        void defineWorld(GameSimulation& sim)
        {
            WeaponDefinition laser{};
            laser.maxRange = 200_ss;
            laser.reloadTime = 1_ss;
            laser.burst = 1;
            laser.damage["default"] = 30u;
            sim.weaponDefinitions["LASER"] = laser;

            sim.unitDefinitions["ARMCOM"] = makeDef(true, true, true, "", 300u);
            sim.unitDefinitions["ARMMEX"] = makeDef(false, false, false, "", 50u);
            sim.unitDefinitions["ARMSOLAR"] = makeDef(false, false, false, "", 50u);
            sim.unitDefinitions["ARMLAB"] = makeDef(false, true, false, "", 100u);
            sim.unitDefinitions["ARMCK"] = makeDef(false, true, true, "", 100u);
            sim.unitDefinitions["ARMPW"] = makeDef(false, false, true, "LASER", 200u);
            sim.unitDefinitions["ARMROCK"] = makeDef(false, false, true, "LASER", 200u);
            sim.unitDefinitions["ARMLLT"] = makeDef(false, false, false, "LASER", 200u);
            sim.unitDefinitions["ARMRAD"] = makeDef(false, false, false, "", 100u);
            sim.unitDefinitions["CORCOM"] = makeDef(true, true, true, "", 300u);
            sim.unitDefinitions["CORSOLAR"] = makeDef(false, false, false, "", 50u);
        }

        UnitId addUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            unit.buildTimeCompleted = sim.unitDefinitions.at(type).buildTime;
            return unitId;
        }

        template <typename Order>
        int countOrders(const std::vector<PlayerCommand>& commands)
        {
            int n = 0;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand)
                {
                    continue;
                }
                auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command);
                if (issue && std::holds_alternative<Order>(issue->order))
                {
                    ++n;
                }
            }
            return n;
        }

        std::vector<std::string> buildOrderTypes(const std::vector<PlayerCommand>& commands)
        {
            std::vector<std::string> types;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand)
                {
                    continue;
                }
                if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
                {
                    if (auto build = std::get_if<BuildOrder>(&issue->order))
                    {
                        types.push_back(build->unitType);
                    }
                }
            }
            return types;
        }

        int countQueueCommands(const std::vector<PlayerCommand>& commands, const std::string& type)
        {
            int n = 0;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand)
                {
                    continue;
                }
                if (auto q = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unitCommand->command); q && q->unitType == type)
                {
                    ++n;
                }
            }
            return n;
        }

        void runTicks(GameSimulation& sim, AiPlayerController& ai, int ticks, std::vector<PlayerCommand>& out)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.tick();
                ai.tick(sim, out);
            }
        }
    }

    TEST_CASE("the AI opens with power, metal and a factory, then produces constructors", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u);
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        SECTION("the first job is a solar collector")
        {
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() == "ARMSOLAR");
        }

        SECTION("once the opening quotas exist the commander builds the lab")
        {
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
            }
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
            }
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() == "ARMLAB");
        }

        SECTION("a factory with an empty queue is told to build constructors first")
        {
            addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 1);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Boom);
        }
    }

    TEST_CASE("the AI only knows what it can see, unless it cheats", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // An enemy solar beyond the commander's 300-unit sight, but on the map.
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 0_ss), script);

        SECTION("an honest AI has not seen the enemy")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.empty());
            REQUIRE_FALSE(controller.getBlackboard().enemyBasePosition.has_value());
        }

        SECTION("an omniscient AI knows where the enemy base is")
        {
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.size() == 1);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
            REQUIRE(controller.getThreatMap().economicAt(SimVector(450_ss, 0_ss, 0_ss)) > 0.0f);
        }

        SECTION("an enemy that walks into view is remembered, and forgotten once seen to be gone")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u);
            std::vector<PlayerCommand> commands;
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(100_ss, 0_ss, 0_ss), script);
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.count(raiderId.value) == 1);
            REQUIRE(controller.getThreatMap().antiGroundAt(SimVector(100_ss, 0_ss, 0_ss)) > 0.0f);

            sim.getUnitState(raiderId).markAsDeadNoCorpse();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.count(raiderId.value) == 0);
        }
    }

    TEST_CASE("the army defends, gathers and attacks", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.attackArmySize = 3;
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u);
        std::vector<PlayerCommand> commands;

        SECTION("an enemy at the gates puts the AI on the defensive and its units attack it")
        {
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
            REQUIRE(countOrders<AttackOrder>(commands) >= 1);
            (void)raiderId;
        }

        SECTION("with no enemy in sight the army waits at the rally point")
        {
            addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Boom);
            REQUIRE(countOrders<MoveOrder>(commands) >= 1);
            REQUIRE(countOrders<AttackOrder>(commands) == 0);
        }

        SECTION("a big enough army with a known enemy base goes on the attack")
        {
            // Let the AI see the enemy base via radar-less omniscience for the test.
            auto brutal = makeDefaultBrutalProfile();
            brutal.attackArmySize = 3;
            brutal.scoutCount = 0;
            AiPlayerController cheat(ai, brutal, 42u);
            addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 200_ss), script);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script);
            }
            runTicks(sim, cheat, 20, commands);
            REQUIRE(cheat.getBlackboard().phase == GamePhase::Attack);
            REQUIRE(cheat.getBlackboard().attackTarget.has_value());
            REQUIRE(countOrders<MoveOrder>(commands) >= 3);
        }
    }

    TEST_CASE("difficulty profiles differ in aggression", "[ai]")
    {
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).attackArmySize > makeProfileForDifficulty(AiDifficulty::Hard).attackArmySize);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Brutal).cheatModeOmniscient);
        REQUIRE_FALSE(makeProfileForDifficulty(AiDifficulty::Hard).cheatModeOmniscient);
    }
}
