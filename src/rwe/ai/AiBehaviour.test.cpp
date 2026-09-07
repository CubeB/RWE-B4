#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

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
            // Anti-air: a missile tower a constructor puts up, and the
            // level-1 kbot the first lab can already build.
            sim.unitDefinitions["ARMRL"] = makeDef(false, false, false, "LASER", 200u);
            sim.unitDefinitions["ARMJETH"] = makeDef(false, false, true, "LASER", 200u);
            sim.unitDefinitions["CORCOM"] = makeDef(true, true, true, "", 300u);
            sim.unitDefinitions["CORSOLAR"] = makeDef(false, false, false, "", 50u);

            // Eyes and lift: a scout plane, an air transport and the plants that make them.
            auto peeper = makeDef(false, false, true, "", 400u);
            peeper.canFly = true;
            peeper.maxVelocity = 6_ss;
            sim.unitDefinitions["ARMPEEP"] = peeper;
            auto atlas = makeDef(false, false, true, "", 100u);
            atlas.canFly = true;
            atlas.transportCapacity = 1;
            atlas.transportSize = 3;
            sim.unitDefinitions["ARMATLAS"] = atlas;
            sim.unitDefinitions["ARMAP"] = makeDef(false, true, false, "", 100u);
            sim.unitDefinitions["ARMVP"] = makeDef(false, true, false, "", 100u);
            sim.unitDefinitions["ARMFAV"] = makeDef(false, false, true, "LASER", 300u);
            sim.unitDefinitions["ARMFLASH"] = makeDef(false, false, true, "LASER", 200u);
        }

        /** Two banks of land either side of a channel too deep for a kbot, running north to south. */
        MapTerrain makeChannelTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 28; x < 36; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(0));
                }
            }
            return MapTerrain(std::move(heights), 30_ss);
        }

        template <typename Order>
        std::vector<Order> ordersFor(const std::vector<PlayerCommand>& commands, UnitId unit)
        {
            std::vector<Order> found;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand || unitCommand->unit != unit)
                {
                    continue;
                }
                if (auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command))
                {
                    if (auto order = std::get_if<Order>(&issue->order))
                    {
                        found.push_back(*order);
                    }
                }
            }
            return found;
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
            if (sim.unitDefinitions.at(type).canFly)
            {
                UnitPhysicsInfoAir air;
                air.movementState = AirMovementStateFlying();
                unit.physics = air;
            }
            return unitId;
        }

        /** Build orders naming a particular unit type. */
        int countOrdersFor(const std::vector<PlayerCommand>& commands, const std::string& unitType)
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
                if (!issue)
                {
                    continue;
                }
                if (auto build = std::get_if<BuildOrder>(&issue->order); build && build->unitType == unitType)
                {
                    ++n;
                }
            }
            return n;
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

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
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

    TEST_CASE("the AI does not start what it cannot pay for", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The opening quotas are met, so the lab is what the plan wants next.
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("with the metal in hand the lab goes up")
        {
            runTicks(sim, controller, 31, commands);
            REQUIRE(buildOrderTypes(commands) == std::vector<std::string>{"ARMLAB"});
        }

        SECTION("with no metal and no income, nothing is started")
        {
            sim.getPlayer(ai).metal = Metal(0.0f);
            runTicks(sim, controller, 31, commands);
            REQUIRE(buildOrderTypes(commands).empty());
        }

        SECTION("a stockpile at the cap is spent whatever the rate, because income over the cap is lost")
        {
            sim.getPlayer(ai).metal = Metal(1000.0f);
            sim.getPlayer(ai).maxMetal = Metal(1000.0f);
            AiBlackboard bb;
            bb.currentMetal = Metal(1000.0f);
            bb.metalStorage = Metal(1000.0f);
            bb.metalIncome = Metal(0.0f);
            bb.metalDemand = Metal(50.0f);
            BuildManager::BuildEstimate expensive{5000.0f, 100.0f};
            REQUIRE(BuildManager::canAfford(bb, expensive));
        }

        SECTION("the estimate is the unbuilt share of the price at the builder's own rate")
        {
            UnitDefinition target{};
            target.buildTime = 3000u;
            target.buildCostMetal = Metal(600.0f);
            UnitDefinition builder{};
            builder.workerTimePerTick = 5u;
            auto fresh = BuildManager::estimateBuild(target, builder);
            REQUIRE(fresh.metal == 600.0f);
            REQUIRE(fresh.seconds == 20.0f);
            auto halfDone = BuildManager::estimateBuild(target, builder, 1500u);
            REQUIRE(halfDone.metal == 300.0f);
            REQUIRE(halfDone.seconds == 10.0f);
        }

        SECTION("while it saves, the builder reclaims rather than spending on something cheaper")
        {
            // A little income, so the lab is within reach of waiting but not
            // of the stockpile: the rule is to wait for it, not to build the
            // radar instead and never get there.
            sim.getPlayer(ai).metal = Metal(0.0f);
            sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
            // Reclaiming needs the rock to have been seen. True line of
            // sight walks ray tables this bare simulation does not carry,
            // so it sees nothing past its own cell; circular sight is plain
            // geometry and does.
            sim.lineOfSightMode = LineOfSightMode::Circular;
            FeatureDefinition rock{};
            rock.name = "rock";
            rock.footprintX = 1;
            rock.footprintZ = 1;
            rock.reclaimable = true;
            rock.metal = 30;
            auto rockDef = sim.featureDefinitions.insert(rock);
            // Heightmap (34, 34) is a few tiles from the commander: world
            // coordinates run from the middle of the map, not its corner.
            auto rockId = sim.addFeature(rockDef, 34, 34).value();

            runTicks(sim, controller, 61, commands);
            REQUIRE(buildOrderTypes(commands).empty());
            auto reclaims = ordersFor<ReclaimOrder>(commands, commanderId);
            REQUIRE(!reclaims.empty());
            REQUIRE(std::get<FeatureId>(reclaims.front().target) == rockId);
        }

        SECTION("what a builder is already on counts against what the next one can afford")
        {
            // The commander is on a lab frame: 100 metal over the 3.3
            // seconds a worker of rate 1 takes, thirty a second. A second
            // builder falls idle with income of six and nothing in the
            // bank, and the radar it wants next is out of reach.
            sim.getPlayer(ai).metal = Metal(0.0f);
            sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
            auto frameId = addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            sim.getUnitState(frameId).buildTimeCompleted = 0u;
            sim.getUnitState(commanderId).orders.push_back(BuildOrder("ARMLAB", SimVector(200_ss, 0_ss, 200_ss)));
            sim.getUnitState(commanderId).buildOrderUnitId = frameId;
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);

            runTicks(sim, controller, 61, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.metalCommitted.value > 29.0f);
            REQUIRE(bb.metalCommitted.value < 31.0f);
            REQUIRE(bb.orphanedFrames.empty());
            REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
            REQUIRE(buildOrderTypes(commands).empty());
        }
    }

    TEST_CASE("a frame nobody is working on is finished before anything new is started", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }

        // Half a radar, as a raid that killed the constructor would leave it.
        auto frameId = addUnit(sim, "ARMRAD", ai, SimVector(150_ss, 0_ss, 150_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 50u;

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        REQUIRE(controller.getBlackboard().orphanedFrames == std::vector<UnitId>{frameId});
        REQUIRE(buildOrderTypes(commands).empty());
        auto repairs = ordersFor<RepairOrder>(commands, commanderId);
        REQUIRE(repairs.size() == 1);
        REQUIRE(repairs.front().target == frameId);

        SECTION("once a builder is on its way it is nobody's orphan")
        {
            sim.getUnitState(commanderId).orders.push_back(RepairOrder(frameId));
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().orphanedFrames.empty());
            REQUIRE(ordersFor<RepairOrder>(commands, commanderId).empty());
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
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.empty());
            REQUIRE_FALSE(controller.getBlackboard().enemyBasePosition.has_value());
        }

        SECTION("an omniscient AI knows where the enemy base is")
        {
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.size() == 1);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
            REQUIRE(controller.getThreatMap().economicAt(SimVector(450_ss, 0_ss, 0_ss)) > 0.0f);
        }

        SECTION("an enemy that walks into view is remembered, and forgotten once seen to be gone")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
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
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
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
            AiPlayerController cheat(ai, brutal, 42u, MapIntel{});
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

    TEST_CASE("scouts explore the ground the AI has not seen", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(100_ss, 0_ss, 100_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("a scout plane is sent along a string of legs and the army keeps its raiders")
        {
            auto peeperId = addUnit(sim, "ARMPEEP", ai, SimVector(120_ss, 80_ss, 120_ss), script);
            auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(140_ss, 0_ss, 100_ss), script);
            runTicks(sim, controller, 61, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.scoutUnits == std::vector<UnitId>{peeperId});
            REQUIRE_FALSE(bb.scoutUnitId.has_value());
            REQUIRE(bb.combatUnits == std::vector<UnitId>{raiderId});

            auto legs = ordersFor<MoveOrder>(commands, peeperId);
            REQUIRE(legs.size() == 3);
            // Each leg heads for different ground.
            REQUIRE(legs[0].destination.distanceSquared(legs[1].destination) > (300_ss * 300_ss));
            REQUIRE(legs[1].destination.distanceSquared(legs[2].destination) > (300_ss * 300_ss));
            REQUIRE(bb.scoutTargets.count(peeperId.value) == 1);
        }

        SECTION("without a dedicated scout a combat unit stands in, one leg at a time")
        {
            auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(140_ss, 0_ss, 100_ss), script);
            // Until it is made the scout, the army sends it to the rally point; only the scouting pass counts here.
            runTicks(sim, controller, 59, commands);
            commands.clear();
            runTicks(sim, controller, 2, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.scoutUnitId == raiderId);
            REQUIRE(ordersFor<MoveOrder>(commands, raiderId).size() == 1);
        }

        SECTION("the air plant builds a scout plane before anything else")
        {
            auto plantId = addUnit(sim, "ARMAP", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMPEEP") == 1);
            REQUIRE(countQueueCommands(commands, "ARMATLAS") == 0);
            (void)plantId;
        }
    }

    TEST_CASE("the AI ferries a builder across water it cannot walk", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Kbots cannot wade the channel.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }
        // World space is centred on the map: the channel at tiles 28-36 runs
        // down world x -64..64, and the base sits on the west bank.
        // A rich patch on the east bank, at tile (50, 32): world (296, 8).
        for (int y = 32; y < 34; ++y)
        {
            for (int x = 50; x < 52; ++x)
            {
                sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
            }
        }

        auto commanderPosition = SimVector(-300_ss, 60_ss, 0_ss);
        addUnit(sim, "ARMCOM", ai, commanderPosition, script);
        auto builderId = addUnit(sim, "ARMCK", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        const SimVector farBankPatch(296_ss, 60_ss, 8_ss);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("the base knows the far bank is out of reach")
        {
            runTicks(sim, controller, 2, commands);
            const auto& reach = controller.getReachabilityMap();
            REQUIRE(reach.isValid());
            REQUIRE(reach.isReachable(sim, commanderPosition));
            REQUIRE_FALSE(reach.isReachable(sim, farBankPatch));
            REQUIRE(reach.isWalkable(sim, farBankPatch));
            REQUIRE_FALSE(reach.isWalkable(sim, SimVector(0_ss, 0_ss, 0_ss)));
            REQUIRE(controller.getBlackboard().hasUnreachableGround);
        }

        SECTION("an idle transport is sent to carry the builder to the patch")
        {
            auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(-280_ss, 120_ss, 20_ss), script);
            runTicks(sim, controller, 16, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.wantsTransport);
            auto loads = ordersFor<LoadOrder>(commands, atlasId);
            REQUIRE(loads.size() == 1);
            REQUIRE(loads.front().target == builderId);
            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(unloads.size() == 1);
            // Set down on the east bank, on the patch.
            REQUIRE(unloads.front().destination.x > 64_ss);
            REQUIRE(unloads.front().destination.distanceSquared(farBankPatch) < (32_ss * 32_ss));
            REQUIRE(bb.ferryPassengers.count(builderId.value) == 1);
            REQUIRE(controller.getTransportManager().getFerries().count(atlasId.value) == 1);

            // Booked passengers are left alone by the builder planner.
            commands.clear();
            runTicks(sim, controller, 30, commands);
            REQUIRE(ordersFor<BuildOrder>(commands, builderId).empty());
        }

        SECTION("the air plant queues a transport once a scout plane exists and there is ground to reach")
        {
            addUnit(sim, "ARMAP", ai, SimVector(-200_ss, 60_ss, -100_ss), script);
            addUnit(sim, "ARMPEEP", ai, SimVector(-200_ss, 120_ss, -40_ss), script);
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().wantsTransport);
            REQUIRE(controller.getBlackboard().factories.size() == 1);
            REQUIRE(countQueueCommands(commands, "ARMPEEP") == 0);
            REQUIRE(countQueueCommands(commands, "ARMATLAS") == 1);
        }
    }

    TEST_CASE("difficulty profiles differ in aggression", "[ai]")
    {
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).attackArmySize > makeProfileForDifficulty(AiDifficulty::Hard).attackArmySize);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Brutal).cheatModeOmniscient);
        REQUIRE_FALSE(makeProfileForDifficulty(AiDifficulty::Hard).cheatModeOmniscient);
    }
    TEST_CASE("The AI notices a building it has lost", "[ai]")
    {
        // Counting units cannot tell a building that blew up from one that
        // was never built, so the AI keeps the ids of what was standing and
        // diffs them. Without that, a razed base is rebuilt in the order a
        // base is built from nothing, and whatever the raid actually took out
        // is replaced whenever the generic list happens to reach it.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 5u, 0, 0);
        defineWorld(sim);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto solarId = addUnit(sim, "ARMSOLAR", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        // The first pass has nothing to diff against, so it must not report
        // every building we own as a fresh loss.
        runTicks(sim, controller, 2, commands);
        REQUIRE(controller.getBlackboard().recentLosses.empty());
        REQUIRE(controller.getBlackboard().standingBuildings.size() == 1);

        sim.getUnitState(solarId).markAsDead();
        runTicks(sim, controller, 2, commands);

        const auto& losses = controller.getBlackboard().recentLosses;
        REQUIRE(losses.size() == 1);
        REQUIRE(losses.front().unitType == "ARMSOLAR");
        REQUIRE(losses.front().position.x == 100_ss);
        REQUIRE(controller.getBlackboard().standingBuildings.empty());
    }

    TEST_CASE("A building that is still standing is not reported lost", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 5u, 0, 0);
        defineWorld(sim);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSOLAR", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 90, commands);

        REQUIRE(controller.getBlackboard().recentLosses.empty());
    }

    TEST_CASE("the AI answers aircraft, and only once it has seen one", "[ai]")
    {
        // Before this the AI had no answer to air at all: nothing it built was
        // chosen for it and nothing it owned was held back for it, so a single
        // bomber could work through a base unopposed.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        // Past the opening solar count, so the planner is not still busy with
        // power when it reaches the question under test.
        for (int i = 0; i < 5; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script);
        }

        SECTION("nothing airborne seen: no air threat")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE_FALSE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 0);
        }

        SECTION("an aircraft in sight raises the threat and buys a Defender")
        {
            // Inside the commander's 300-unit sight, so it is genuinely seen
            // rather than assumed.
            addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 0_ss, 0_ss), script);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 1);
            REQUIRE(countOrdersFor(commands, "ARMRL") > 0);
        }

        SECTION("the threat outlives the sighting, because aircraft do not sit still")
        {
            auto peeperId = addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);

            // Gone, and seen to be gone, so it leaves knownEnemies. The threat
            // has to survive that: otherwise the AI puts up a tower, loses
            // sight of the bomber, and drops the tower off its wanted list
            // before the bomber comes back.
            sim.getUnitState(peeperId).markAsDeadNoCorpse();
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 0);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().lastEnemyAirSeenAt.has_value());
        }
    }

    TEST_CASE("mobile anti-air covers the base instead of joining the army", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMPW", ai, SimVector(50_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMJETH", ai, SimVector(60_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.antiAirUnits.size() == 1);
        REQUIRE(bb.combatUnits.size() == 1);

        // And it is not counted towards the attack threshold, or the AI would
        // attack sooner for having built defences.
        REQUIRE(bb.armySize == 1);
    }

    TEST_CASE("difficulty decides how seriously aircraft are taken", "[ai]")
    {
        // Easy forgets about air until it is overhead and then under-builds.
        // Being slow to answer a bomber is a more convincing weakness than
        // owning fewer solar collectors.
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).baseAntiAirTowerCount == 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).antiAirMobileCount == 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Standard).baseAntiAirTowerCount > 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Hard).reactiveAntiAirTowerCount
            > makeProfileForDifficulty(AiDifficulty::Standard).reactiveAntiAirTowerCount);
    }

}
