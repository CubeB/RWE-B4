#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <rwe/ai/AiBuildTree.h>
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
            // "DEFAULT" upper case, as the loader stores it and the
            // simulation looks it up (Projectile.cpp).
            laser.damage["DEFAULT"] = 30u;
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

            // Level two. The advanced lab is a factory, its constructor is a
            // mobile builder, and the rest are what that constructor puts up.
            sim.unitDefinitions["ARMALAB"] = makeDef(false, true, false, "", 100u);
            sim.unitDefinitions["ARMACK"] = makeDef(false, true, true, "", 100u);
            // Worth teching for: the same price as a raider and four times
            // the hit points, which is the shape of Core's Can against an
            // A.K. and the reason teching is a per-side question at all.
            auto zeus = makeDef(false, false, true, "LASER", 200u);
            zeus.maxHitPoints = 400;
            sim.unitDefinitions["ARMZEUS"] = zeus;
            sim.unitDefinitions["ARMHLT"] = makeDef(false, false, false, "LASER", 200u);
            sim.unitDefinitions["ARMGUARD"] = makeDef(false, false, false, "LASER", 200u);
            sim.unitDefinitions["ARMARAD"] = makeDef(false, false, false, "", 200u);
            sim.unitDefinitions["ARMMOHO"] = makeDef(false, false, false, "", 50u);
            sim.unitDefinitions["ARMFUS"] = makeDef(false, false, false, "", 50u);
        }

        /**
         * The real tech tree in miniature: the commander's pages stop at the
         * level-one plants, the construction kbot reaches the tech step and
         * the towers, and only the advanced constructor reaches the rest.
         */
        AiBuildTree makeBuildTree()
        {
            AiBuildTree tree;
            tree.buildableBy["ARMCOM"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR"};
            tree.buildableBy["ARMCK"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR",
                "ARMALAB", "ARMHLT", "ARMGUARD", "ARMRL"};
            tree.buildableBy["ARMACK"] = {"ARMLAB", "ARMARAD", "ARMFUS", "ARMMOHO"};
            return tree;
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

    TEST_CASE("a builder is only asked for what it has a button for", "[ai]")
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
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);

        // An enemy aircraft in view, so anti-air is wanted. The commander has
        // no Defender button; the construction kbot does.
        auto human = PlayerId(0);
        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;
        addUnit(sim, "ARMTHUND", human, SimVector(60_ss, 80_ss, 60_ss), script);

        SECTION("without a tree the commander builds anti-air it could never have ordered")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMRL") != types.end());
        }

        SECTION("with the tree it does not, and the construction kbot does instead")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto commanderTypes = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::none_of(commanderTypes.begin(), commanderTypes.end(), [](const BuildOrder& o) { return o.unitType == "ARMRL"; }));

            // One builder is planned per pass, the lowest-numbered idle one,
            // so the commander is sent away to leave the kbot at the front.
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
            sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType == "ARMRL");
        }
    }

    TEST_CASE("a rich base techs to level two, and the advanced lab makes its constructor first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        // The level-one plan satisfied, so nothing above the tech rule wants
        // anything: the opening quotas and both expansion targets are met.
        auto profile = makeDefaultStandardProfile();
        // Teching ships off, because measured it loses (see the knob's own
        // comment). The rules are still meant to work when it is asked for.
        profile.techLevelTwo = true;
        for (int i = 0; i < profile.targetSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMRAD", ai, SimVector(-200_ss, 0_ss, 200_ss), script);
        // The rest of the level-one plan, all of which outranks teching and
        // rightly so: a couple of towers and an air plant are cheap, and a
        // player has them long before an advanced lab.
        for (int i = 0; i < profile.targetDefenceCount; ++i)
        {
            addUnit(sim, "ARMLLT", ai, SimVector(SimScalar(-300.0f - i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < profile.baseAntiAirTowerCount; ++i)
        {
            addUnit(sim, "ARMRL", ai, SimVector(SimScalar(-300.0f - i * 40.0f), 0_ss, 80_ss), script);
        }
        addUnit(sim, "ARMAP", ai, SimVector(300_ss, 0_ss, -200_ss), script);
        addUnit(sim, "ARMVP", ai, SimVector(300_ss, 0_ss, -280_ss), script);
        // Teching is judged on income, not on a full store, so the
        // extractors have to actually earn: two a second each, which over
        // the target count clears techMinMetalIncome comfortably.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        // Rich as well, so nothing is waiting on the stockpile.
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(1000.0f);

        // One builder is planned per pass, the lowest-numbered idle one, so
        // the commander is sent walking to leave the kbot at the front.
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));

        SECTION("the construction kbot puts up the advanced lab")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType == "ARMALAB");
        }

        SECTION("a level two that is no better than level one is not worth its factory")
        {
            // Same hit points as the raider now, so the lab buys nothing.
            sim.unitDefinitions["ARMZEUS"].maxHitPoints = sim.unitDefinitions["ARMPW"].maxHitPoints;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().advancedArmyValueRatio == 1.0f);
            // Nothing else is left wanting on this base, so the honest test
            // is that no advanced lab was ordered by anyone -- not that
            // something cheaper was ordered instead.
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMALAB") == types.end());
        }

        SECTION("with teching switched off it builds a level-one tower instead")
        {
            profile.techLevelTwo = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType != "ARMALAB");
        }

        SECTION("the advanced lab makes the advanced constructor, then assault kbots")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACK") == 1);

            addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("and the advanced constructor is the one that reaches the advanced radar")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
            auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
            sim.getUnitState(kbotId).addOrder(MoveOrder(SimVector(-600_ss, 0_ss, -600_ss)));
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto builds = ordersFor<BuildOrder>(commands, advancedId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMARAD");
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

    TEST_CASE("the army attacks in waves and holds when outnumbered", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        // Omniscient, so the enemy base is known without a scout.
        auto profile = makeDefaultBrutalProfile();
        profile.attackArmySize = 3;
        profile.retreatArmySize = 2;
        profile.scoutCount = 0;

        SECTION("a unit built during the attack gathers for the next wave instead of walking to the front")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 200_ss), script);
            std::vector<UnitId> wave;
            for (int i = 0; i < 3; ++i)
            {
                wave.push_back(addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script));
            }
            runTicks(sim, controller, 20, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.attackGroup.size() == 3);
            REQUIRE(bb.attackTarget.has_value());

            auto lateId = addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
            commands.clear();
            runTicks(sim, controller, 20, commands);
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.attackGroup.count(lateId.value) == 0);
            auto legs = ordersFor<MoveOrder>(commands, lateId);
            REQUIRE(!legs.empty());
            REQUIRE(bb.rallyPoint.has_value());
            REQUIRE(legs.front().destination.distanceSquared(*bb.rallyPoint) < (64_ss * 64_ss));
            REQUIRE(legs.front().destination.distanceSquared(*bb.attackTarget) > (200_ss * 200_ss));

            SECTION("and the attack ends when the wave that set out is spent, not when the army is")
            {
                sim.getUnitState(wave[0]).markAsDeadNoCorpse();
                sim.getUnitState(wave[1]).markAsDeadNoCorpse();
                runTicks(sim, controller, 20, commands);
                REQUIRE(bb.phase == GamePhase::Boom);
                REQUIRE(bb.attackGroup.empty());
                REQUIRE_FALSE(bb.waveSpent);
            }

            SECTION("and one intruder at home is met by the reserve, not by recalling the wave")
            {
                // A raider inside the defend radius, out of everyone's
                // engage radius. Before, this put the AI in Defend and the
                // wave turned round.
                auto raiderId = addUnit(sim, "ARMPW", human, SimVector(-600_ss, 0_ss, 300_ss), script);
                commands.clear();
                runTicks(sim, controller, 20, commands);
                REQUIRE(bb.phase == GamePhase::Attack);
                REQUIRE(bb.enemiesNearBase == std::vector<UnitId>{raiderId});
                auto answered = ordersFor<AttackOrder>(commands, lateId);
                REQUIRE(answered.size() >= 1);
                REQUIRE(std::get<UnitId>(answered.front().target) == raiderId);
                for (auto waveUnit : wave)
                {
                    REQUIRE(ordersFor<AttackOrder>(commands, waveUnit).empty());
                }
            }
        }

        SECTION("outnumbered at home, the army holds at the rally point rather than charging")
        {
            // One of ours against three of theirs, inside the defend radius
            // but beyond anything's engage radius.
            auto ownId = addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", human, SimVector(650_ss, 0_ss, SimScalar(i * 40.0f)), script);
            }

            SECTION("held")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);
                REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
                REQUIRE(controller.getBlackboard().enemiesNearBase.size() == 3);
                REQUIRE(ordersFor<AttackOrder>(commands, ownId).empty());
                REQUIRE(!ordersFor<MoveOrder>(commands, ownId).empty());
            }

            SECTION("or, with the rule off, thrown at them one at a time")
            {
                profile.holdWhenOutnumbered = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);
                REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
                REQUIRE(!ordersFor<AttackOrder>(commands, ownId).empty());
            }
        }
    }

    TEST_CASE("an AI knob can be set by name", "[ai]")
    {
        auto p = makeDefaultStandardProfile();
        REQUIRE(applyAiTuning(p, "attackArmySize", "12"));
        REQUIRE(p.attackArmySize == 12);
        REQUIRE(applyAiTuning(p, "attackInWaves", "0"));
        REQUIRE_FALSE(p.attackInWaves);
        REQUIRE(applyAiTuning(p, "engageRadius", "300"));
        REQUIRE(p.engageRadius == 300_ss);
        REQUIRE_FALSE(applyAiTuning(p, "noSuchKnob", "1"));
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

    TEST_CASE("a building one builder has been sent for is not planned again for another", "[ai]")
    {
        // The planner counts what stands and what is going up, and a frame
        // only appears when the builder reaches the site. With two builders
        // that left a gap: the commander was sent 1200 units to put up a
        // radar, the construction kbot fell idle a second later, and the
        // counts said there was no radar, so it built one too. Both stood
        // to the end of the game, 128 units apart.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // A base past its opening, so the plan has reached the radar.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
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

        SECTION("with nobody sent for a radar, the idle kbot is")
        {
            sim.getUnitState(commanderId).orders.push_back(MoveOrder(SimVector(-400_ss, 0_ss, 0_ss)));
            runTicks(sim, controller, 31, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(types == std::vector<std::string>{"ARMRAD"});
        }

        SECTION("with the commander on its way to build one, the kbot is given something else")
        {
            // Far enough that no frame goes down inside the planning pass.
            sim.getUnitState(commanderId).orders.push_back(BuildOrder("ARMRAD", SimVector(-400_ss, 0_ss, 0_ss)));
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
            REQUIRE(bb.ownedTotalCounts.at("ARMRAD") == 1);
            REQUIRE(bb.ownedCompletedCounts.count("ARMRAD") == 0);
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() != "ARMRAD");
        }
    }

    namespace
    {
        SimScalar flatDistanceBetween(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }

        /** The one build order in the commands, and where it was for. */
        BuildOrder theBuildOrder(const std::vector<PlayerCommand>& commands, const std::string& unitType)
        {
            std::vector<BuildOrder> found;
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
                        found.push_back(*build);
                    }
                }
            }
            REQUIRE(found.size() == 1);
            REQUIRE(found.front().unitType == unitType);
            return found.front();
        }

        /**
         * A base past its opening, with its radar, standing off-centre at
         * x = -300 so that "towards the map centre" is +x. Everything is
         * within a hundred of the anchor, inside one tower's reach.
         */
        void layOutBase(GameSimulation& sim, PlayerId ai, const std::shared_ptr<CobScript>& script, const SimVector& anchor)
        {
            addUnit(sim, "ARMCOM", ai, anchor, script);
            addUnit(sim, "ARMLAB", ai, anchor + SimVector(0_ss, 0_ss, -80_ss), script);
            addUnit(sim, "ARMRAD", ai, anchor + SimVector(-80_ss, 0_ss, -80_ss), script);
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, anchor + SimVector(SimScalar(-90.0f + i * 30.0f), 0_ss, 80_ss), script);
            }
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMMEX", ai, anchor + SimVector(-90_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
            }
        }
    }

    TEST_CASE("defences are spaced by their range, and the radar stands at the edge of the base", "[ai]")
    {
        // The nearest-free-ring rule put every tower on the ring around the
        // same post: three laser towers within 64 units of each other and
        // two Defenders side by side, each covering the ground the last one
        // did. And the radar went wherever the next free ring was, which is
        // the middle of the base, where it sees what the buildings already
        // see.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(-300_ss, 0_ss, 0_ss);
        // The test world's one weapon reaches 200; the front post is 160
        // out from the anchor, towards the map centre.
        const SimScalar range = sim.weaponDefinitions.at("LASER").maxRange;
        auto profile = makeDefaultStandardProfile();
        const SimVector post = anchor + SimVector(profile.defenceDistanceFromBase, 0_ss, 0_ss);

        SECTION("the second laser tower goes a range from the first, along the front, not behind it")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMLLT");

            REQUIRE(flatDistanceBetween(order.position, post) >= range);
            // No further back than the post, and not out in front alone.
            REQUIRE(order.position.x >= post.x);
            REQUIRE(order.position.x < post.x + range);
            REQUIRE(flatDistanceBetween(order.position, anchor) > range);
        }

        SECTION("with the rule off, the second tower goes on the nearest free ring as before")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            profile.spreadDefences = false;

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(flatDistanceBetween(order.position, anchor) < range);
        }

        SECTION("the first anti-air tower goes to the middle of the base, where it covers all of it")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            addUnit(sim, "ARMLLT", ai, post + SimVector(0_ss, 0_ss, 200_ss), script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMRL");
            REQUIRE(flatDistanceBetween(order.position, anchor) < 100_ss);
        }

        SECTION("the next anti-air tower goes where the first one's umbrella runs out")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            addUnit(sim, "ARMLLT", ai, post + SimVector(0_ss, 0_ss, 200_ss), script);
            addUnit(sim, "ARMRL", ai, anchor, script);
            // A cluster beyond the first tower's reach, and an enemy plane
            // in sight so that more anti-air is wanted at all.
            const SimVector cluster(50_ss, 0_ss, 0_ss);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, cluster + SimVector(0_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
            }
            addUnit(sim, "ARMPEEP", human, anchor + SimVector(0_ss, 0_ss, 150_ss), script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto order = theBuildOrder(commands, "ARMRL");

            REQUIRE(flatDistanceBetween(order.position, cluster) <= range);
            REQUIRE(flatDistanceBetween(order.position, anchor) >= range);
        }

        SECTION("the radar goes to the edge of the base facing the map centre")
        {
            layOutBase(sim, ai, script, anchor);
            // Take the base's radar away again; this section is about where it goes.
            for (auto& [unitId, unit] : sim.units)
            {
                if (unit.unitType == "ARMRAD")
                {
                    unit.markAsDeadNoCorpse();
                }
            }
            const SimVector radarPost = anchor + SimVector(profile.radarDistanceFromBase, 0_ss, 0_ss);

            SECTION("on")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto order = theBuildOrder(commands, "ARMRAD");
                REQUIRE(flatDistanceBetween(order.position, radarPost) < 100_ss);
            }

            SECTION("off, it goes on the nearest free ring, in the middle of everything")
            {
                profile.spreadDefences = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto order = theBuildOrder(commands, "ARMRAD");
                REQUIRE(flatDistanceBetween(order.position, anchor) < 200_ss);
            }
        }
    }

    TEST_CASE("a tower is not sited on ground the base cannot walk to", "[ai]")
    {
        // Scoring the whole base radius, rather than the nearest ring, let
        // the coverage rule pick a Defender site on a mesa the builder could
        // not climb. The order died on the spot, the same site won again
        // next pass, and a commander spent four minutes of one arena game
        // re-issuing it. Here the far bank plays the mesa.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // The base on the west bank with its towers, so the Defender is
        // next, and on the east bank a solar farm bigger than the base --
        // eleven buildings against sixteen -- so that coverage alone would
        // send the tower across the water.
        (void)human;
        const SimVector anchor(-300_ss, 60_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        addUnit(sim, "ARMLLT", ai, anchor + SimVector(160_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLLT", ai, anchor + SimVector(160_ss, 0_ss, 200_ss), script);
        for (int column = 0; column < 2; ++column)
        {
            for (int row = 0; row < 8; ++row)
            {
                addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(250.0f + column * 40.0f), 60_ss, SimScalar(-140.0f + row * 40.0f)), script);
            }
        }

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);
        REQUIRE(controller.getBlackboard().groundReachabilityValid);
        auto order = theBuildOrder(commands, "ARMRL");

        // West of the channel, which runs down x -64..64.
        REQUIRE(order.position.x < -64_ss);
        REQUIRE(controller.getReachabilityMap().isReachable(sim, order.position));
    }

    TEST_CASE("the AI expands to the metal it is shown, and stays on its own side of the map", "[ai]")
    {
        // The metal spots are on a player's map from the start, and the
        // expansion search used to wait for the AI to have explored them,
        // which with no scout until the eighth extractor meant a plateau
        // from the seventh minute to the sixteenth. Two patches, both well
        // outside the commander's sight of 300: one at (328, 328) and one
        // at (-440, -440) in world units (cells 52,52 and 4,4 of the
        // 64-wide map, which is centred on the middle).
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto cell : {52, 4})
        {
            for (int y = cell; y < cell + 2; ++y)
            {
                for (int x = cell; x < cell + 2; ++x)
                {
                    sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
                }
            }
        }
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The near search is what fills in around the base; shrink it so
        // that only the expansion search can reach either patch.
        auto profile = makeDefaultStandardProfile();
        profile.nearMexSearchRadius = 100_ss;

        SECTION("an unexplored patch is claimed, nearest first")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(orders.size() == 1);
            REQUIRE(orders.front().unitType == "ARMMEX");
            REQUIRE(orders.front().position.x > 300_ss);
            REQUIRE(orders.front().position.z > 300_ss);
        }

        SECTION("with the exploration gate on, it waits to have seen the ground")
        {
            profile.expansionNeedsExploredGround = true;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 0);

            // Mapped: the ground is handed over, and the patch with it.
            sim.playerVisibility.at(ai.value).exploreAll();
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 1);
        }

        SECTION("a patch nearer the enemy's base than ours is left to them")
        {
            // An enemy building beside the nearer patch, and an omniscient
            // AI so that it counts as found.
            addUnit(sim, "ARMSOLAR", human, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.cheatModeOmniscient = true;

            SECTION("so the further patch, on our side, is taken instead")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x < -300_ss);
            }

            SECTION("unless the knob says the whole map is fair game")
            {
                profile.expansionStaysOnOurSide = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x > 300_ss);
            }
        }

        SECTION("a patch under a known enemy's guns is left alone")
        {
            // An enemy tower beside the nearer patch, the side rule off so
            // that only the guns can rule the patch out.
            addUnit(sim, "ARMLLT", human, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.cheatModeOmniscient = true;
            profile.expansionStaysOnOurSide = false;

            SECTION("the builder takes the further patch")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x < -300_ss);
            }

            SECTION("with the rule off it walks into them")
            {
                profile.mexAvoidsEnemyGunsRadius = 0_ss;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().position.x > 300_ss);
            }
        }

        SECTION("a site whose order was dropped is left alone for a while")
        {
            profile.failedSiteMemorySeconds = 2;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto first = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(first.size() == 1);
            REQUIRE(first.front().unitType == "ARMMEX");

            // The order is dropped -- unreachable, say -- and the commander
            // is idle again with nothing standing at the site. The next
            // pass must not send it straight back.
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto second = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(second.size() == 1);
            bool sameSite = second.front().position.distanceSquared(first.front().position) < (1_ss * 1_ss);
            REQUIRE_FALSE(sameSite);

            // The memory expires, and the patch is fair game again.
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 91, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") >= 1);
        }

        SECTION("the commander stays within its leash of home, whichever search finds the patch")
        {
            // Both patches lie 460 and 620 from home; the near search would
            // reach them from where it stands, and must not.
            profile.nearMexSearchRadius = 1200_ss;
            profile.commanderMexSearchRadius = 400_ss;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 0);
        }
    }

    TEST_CASE("an extractor cluster beyond the base's cover gets a tower of its own", "[ai]")
    {
        // The base is built out: opening quotas, lab, radar and its two own
        // towers, so that the first thing the plan has left to want is the
        // outpost tower. The map is only 1024 across, so the base's own
        // defence radius is brought in to 200 to leave room for an outpost.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
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
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.defendRadius = 200_ss;

        // Two extractors 566 from the base, 40 apart: one tower's reach
        // (the test laser's 200) covers both.
        const SimVector outpostA(-400_ss, 0_ss, -400_ss);
        const SimVector outpostB(-440_ss, 0_ss, -400_ss);

        SECTION("two undefended extractors together are a cluster, and the tower goes to them")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            BuildManager planner;
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->extractors == 2);
            REQUIRE_FALSE(plan->raided);
            REQUIRE(plan->anchor.x == -420_ss);
            REQUIRE(plan->anchor.z == -400_ss);

            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(orders.size() == 1);
            REQUIRE(orders.front().unitType == "ARMLLT");
            REQUIRE(orders.front().position.distanceSquared(plan->anchor) < (200_ss * 200_ss));
        }

        SECTION("a cluster a tower already reaches wants nothing")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            addUnit(sim, "ARMLLT", ai, SimVector(-420_ss, 0_ss, -380_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
        }

        SECTION("below the cluster minimum an extractor is not worth a tower, until it is raided")
        {
            profile.outpostDefenceMinExtractors = 2;
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            auto raidedId = addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            BuildManager planner;

            // Both standing: a cluster of two. Kill one before the AI has
            // ever seen it, so its memory holds one lone extractor and no loss.
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.empty());
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());

            // Now the same loss, seen to happen.
            auto lostId = addUnit(sim, "ARMMEX", ai, outpostB, script);
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(lostId).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->raided);
            REQUIRE(plan->extractors == 1);
        }

        SECTION("a site the raid wiped out gets the tower at the loss, ready for the rebuild")
        {
            auto lostA = addUnit(sim, "ARMMEX", ai, outpostA, script);
            auto lostB = addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(lostA).markAsDead();
            sim.getUnitState(lostB).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 2);

            BuildManager planner;
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->raided);
            REQUIRE(plan->extractors == 0);
            REQUIRE(plan->anchor.distanceSquared(outpostA) <= (50_ss * 50_ss));
        }

        SECTION("the outpost towers are bounded by the knob")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            // One outpost tower already standing, elsewhere.
            addUnit(sim, "ARMLLT", ai, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.outpostDefenceCount = 1;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
            profile.outpostDefenceCount = 2;
            REQUIRE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
        }
    }

    TEST_CASE("a builder assisting a factory is offered to the planner again", "[ai]")
    {
        // The bug this pins: a builder sent to lend a hand at a factory
        // carries a GuardOrder that nothing ever takes off, and idleBuilders
        // used to require an empty order queue -- so a builder that once ran
        // out of work was gone from the planning pool for the rest of the
        // game. It should still be offered to the planner, just behind the
        // genuinely idle ones, and it should not be double-counted as idle.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto factoryId = addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        sim.getUnitState(kbotId).orders.push_back(GuardOrder(factoryId));

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
        REQUIRE(bb.idleBuilderCount == 0);
    }

    TEST_CASE("the planner takes each available builder in turn", "[ai]")
    {
        // Before plannerCursor the planner always read idleBuilders.front(),
        // which in id order is whichever builder exists longest -- so with
        // more than one idle builder, only the first was ever served and the
        // rest sat unplanned for no matter how long the game went on.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Only a commander sets bb.homePosition/baseAnchor; without one
        // BuildManager::update has nowhere to anchor the plan and returns
        // before looking at either builder. Sent walking so it does not
        // itself become the one idle builder served.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        addUnit(sim, "ARMCK", ai, SimVector(-40_ss, 0_ss, -40_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});

        std::vector<PlayerCommand> firstPass;
        runTicks(sim, controller, 30, firstPass);
        std::optional<UnitId> firstBuilder;
        for (const auto& c : firstPass)
        {
            if (auto unitCommand = std::get_if<PlayerUnitCommand>(&c))
            {
                firstBuilder = unitCommand->unit;
                break;
            }
        }
        REQUIRE(firstBuilder.has_value());

        std::vector<PlayerCommand> secondPass;
        runTicks(sim, controller, 30, secondPass);
        std::optional<UnitId> secondBuilder;
        for (const auto& c : secondPass)
        {
            if (auto unitCommand = std::get_if<PlayerUnitCommand>(&c))
            {
                secondBuilder = unitCommand->unit;
                break;
            }
        }
        REQUIRE(secondBuilder.has_value());

        REQUIRE(*firstBuilder != *secondBuilder);
    }

    TEST_CASE("an advanced constructor spends the tier even with teching off", "[ai]")
    {
        // The level-two spending rules ask only whether an advanced lab
        // stands, not whether the tech knob is on: deciding to tech is one
        // question and spending a tier already bought is another. Before
        // this, gating both on the same knob meant a finished advanced
        // constructor was left with an empty list -- and sent to nanolathe
        // Peewees instead -- the moment teching was switched off, even with
        // the lab already paid for.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();
        profile.techLevelTwo = false;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // Out of the way, so the idle advanced constructor is the one the
        // planner reaches.
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));

        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
        auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);

        // Income clears techMinMetalIncome comfortably. The stockpile is
        // plentiful but kept well under the cap, so a fusion plant is not
        // also in the running -- this is about the moho and the radar.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        sim.getPlayer(ai).metal = Metal(500.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, advancedId);
        REQUIRE(!builds.empty());
        REQUIRE((builds.front().unitType == "ARMMOHO" || builds.front().unitType == "ARMARAD"));
    }
}
