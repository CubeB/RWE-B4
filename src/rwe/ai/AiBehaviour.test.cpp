#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/MapIntel.h>
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

            // Naval: real shipped values (docs/ai-architecture-proposal.md
            // S:13.2). The shipyard floats IN the water it needs rather
            // than standing beside it -- 8x8, MinWaterDepth=30 -- and the
            // scout ship and destroyer are ordinary armed hulls with their
            // own, much shallower draughts.
            auto armsy = makeDef(false, true, false, "", 200u);
            armsy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
            armsy.buildCostMetal = Metal(615.0f);
            sim.unitDefinitions["ARMSY"] = armsy;

            auto armpt = makeDef(false, false, true, "LASER", 300u);
            armpt.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
            armpt.buildCostMetal = Metal(100.0f);
            sim.unitDefinitions["ARMPT"] = armpt;

            auto armroy = makeDef(false, false, true, "LASER", 300u);
            armroy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 12u, 255u};
            armroy.buildCostMetal = Metal(898.0f);
            sim.unitDefinitions["ARMROY"] = armroy;
        }

        /**
         * The real tech tree in miniature: the commander's pages stop at the
         * level-one plants, the construction kbot reaches the tech step and
         * the towers, and only the advanced constructor reaches the rest.
         */
        AiBuildTree makeBuildTree()
        {
            AiBuildTree tree;
            // The water structures are on the COMMANDER's pages and nowhere
            // else that matters here: ARMCOM3 carries the tidal generator and
            // sonar, ARMCOM4 the torpedo launcher. ARMCK's three pages carry
            // none of the three, and neither the v3.1 patch nor the expansion
            // ships an ARMCK menu that adds them -- so the omission from
            // ARMCK below is the shipped data, not an oversight.
            tree.buildableBy["ARMCOM"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR", "ARMSY",
                "ARMTIDE", "ARMSONAR", "ARMTL", "ARMUWMEX", "ARMFMKR"};
            tree.buildableBy["ARMCK"] = {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMLLT", "ARMRAD", "ARMMAKR",
                "ARMALAB", "ARMHLT", "ARMGUARD", "ARMRL", "ARMSY"};
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

        /**
         * Mostly open water 60 deep -- comfortably past NavalShipyardMinWaterDepth
         * -- with a dry strip along the west edge (heightmap x in [0, 10)) for
         * the base to stand on. Heightmap width 64 and HeightTileWidthInWorldUnits
         * 16 puts world x 0 at tile 32, so the shore is at world x -352 and
         * everything east of it is water.
         */
        MapTerrain makeWaterMapTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
                // A shelf too shallow for a shipyard (depth 20, under
                // NavalShipyardMinWaterDepth=30) but still water for
                // waterFraction/character purposes -- so a valid nomination
                // has to have skipped it, not just have skipped the dry land.
                for (int x = 10; x < 14; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(40));
                }
            }
            return MapTerrain(std::move(heights), 60_ss);
        }

        /**
         * Two dry shores with open water between them: heightmap x in
         * [0, 10) to the west, [54, 64) to the east, everything between 60
         * deep. World space is centred, so the west shore is world x
         * -512..-352 and the east island 352..512 -- the water between them
         * straddles zero, which is the whole point of the fixture.
         *
         * makeWaterMapTerrain will not do for this: it has a single dry
         * strip, so both sides stand on the same ground and nothing is ever
         * across water from anything.
         */
        MapTerrain makeTwoShoresTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
                for (int x = 54; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
            }
            return MapTerrain(std::move(heights), 60_ss);
        }

        /**
         * A blank blackboard for the site-search cases below, which call
         * chooseBuildSite directly rather than through a controller. It
         * only has to know of no enemies: the search asks it nothing else.
         */
        const AiBlackboard& siteTestBlackboard()
        {
            static const AiBlackboard bb{};
            return bb;
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

    TEST_CASE("the AI builds no further solar collector while energy is going spare", "[ai]")
    {
        // Measured on Great Divide, a quarter to a third of all the energy a
        // side made was thrown away, because collectors were built out to
        // targetSolarCount whatever the grid was doing. solarOnDemand skips
        // the next one while the store is four-fifths full and income is
        // ahead of demand; the metal goes to whatever is wanted next.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The opening collectors are not the rule's business: a side with
        // nothing built has no grid to read. It governs the ones after.
        auto profile = makeDefaultStandardProfile();
        REQUIRE(profile.targetSolarCount > profile.openingSolarCount);
        for (int i = 0; i < profile.openingSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }

        // What the commander is asked for over its next several jobs, each
        // one stood up finished the moment it is ordered so the plan moves
        // on. Where a collector falls among the other wants is the plan's
        // affair and changes; whether one is asked for at all is the rule.
        //
        // The economy figures are the simulation's own, rewritten each tick,
        // so they are put in place between its tick and the AI's.
        auto collectorsOrdered = [&](const AiTuningProfile& p, float stored, float income, float demand) {
            AiPlayerController controller(ai, p, 42u, MapIntel{});
            int collectors = 0;
            for (int job = 0; job < 14; ++job)
            {
                std::vector<PlayerCommand> commands;
                for (int i = 0; i < 31; ++i)
                {
                    sim.tick();
                    auto& player = sim.getPlayer(ai);
                    player.maxMetal = Metal(5000.0f);
                    player.metal = Metal(5000.0f);
                    player.maxEnergy = Energy(1000.0f);
                    player.energy = Energy(stored);
                    player.previousEnergyProductionBuffer = Energy(income);
                    player.previousDesiredEnergyConsumptionBuffer = Energy(demand);
                    controller.tick(sim, commands);
                }
                auto types = buildOrderTypes(commands);
                if (types.empty())
                {
                    // A pass spent on something other than a building.
                    sim.getUnitState(commanderId).orders.clear();
                    continue;
                }
                UNSCOPED_INFO("job " << job << ": " << types.front());
                if (types.front() == "ARMSOLAR")
                {
                    ++collectors;
                }
                addUnit(sim, types.front(), ai, SimVector(SimScalar(-420.0f + (job % 7) * 130.0f), 0_ss, SimScalar(-400.0f + (job / 7) * 150.0f)), script);
                sim.getUnitState(commanderId).orders.clear();
            }
            return collectors;
        };

        SECTION("a full store and a surplus: none")
        {
            REQUIRE(collectorsOrdered(profile, 950.0f, 60.0f, 20.0f) == 0);
        }

        SECTION("a full store but more drawn than made: collectors")
        {
            REQUIRE(collectorsOrdered(profile, 950.0f, 20.0f, 60.0f) > 0);
        }

        SECTION("a surplus but a store half empty: collectors")
        {
            REQUIRE(collectorsOrdered(profile, 500.0f, 60.0f, 20.0f) > 0);
        }

        SECTION("with the rule off, collectors whatever the grid is doing")
        {
            profile.solarOnDemand = false;
            REQUIRE(collectorsOrdered(profile, 950.0f, 60.0f, 20.0f) > 0);
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

    TEST_CASE("the factories are held while the first moho and reactor are paid for", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
        addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
        // An army, so that holding production is not leaving the base naked.
        for (int i = 0; i < 6; ++i)
        {
            addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-300.0f + i * 40.0f), 0_ss, 300_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("no assault kbots while neither stands")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMPW") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROCK") == 0);
        }

        SECTION("production resumes once both do")
        {
            addUnit(sim, "ARMMOHO", ai, SimVector(400_ss, 0_ss, -300_ss), script);
            addUnit(sim, "ARMFUS", ai, SimVector(400_ss, 0_ss, -400_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("switched off, the factories never stop")
        {
            profile.tierTwoEconomyReserve = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("and an enemy at the gates ends the hold")
        {
            addUnit(sim, "ARMPW", PlayerId(0), SimVector(250_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }
    }

    TEST_CASE("fighters are built to match the raid, and go for the bombers first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;
        auto fighter = makeDef(false, false, true, "LASER", 200u);
        fighter.canFly = true;
        sim.unitDefinitions["ARMFIG"] = fighter;

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMAP", ai, SimVector(-200_ss, 0_ss, 200_ss), script);
        auto fighterId = addUnit(sim, "ARMFIG", ai, SimVector(-100_ss, 60_ss, 100_ss), script);
        addUnit(sim, "ARMFIG", ai, SimVector(-140_ss, 60_ss, 100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetScoutPlaneCount = 0;
        profile.targetAirConstructorCount = 0;
        profile.targetBomberCount = 0;

        SECTION("the standing pair is enough for a scout")
        {
            addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 0);
        }

        SECTION("four bombers want four fighters")
        {
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMTHUND", human, SimVector(SimScalar(100.0f + i * 30.0f), 60_ss, 60_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyArmedAirPeak == 4);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 1);
        }

        SECTION("a fighter takes the bomber further off before the scout beside it")
        {
            addUnit(sim, "ARMPEEP", human, SimVector(-80_ss, 60_ss, 100_ss), script);
            auto bomberId = addUnit(sim, "ARMTHUND", human, SimVector(200_ss, 60_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto attacks = ordersFor<AttackOrder>(commands, fighterId);
            REQUIRE(!attacks.empty());
            auto target = std::get_if<UnitId>(&attacks.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == bomberId);
        }
    }

    TEST_CASE("a metal maker is told to switch once, not once a tick", "[ai]")
    {
        // A command takes half a second to land, and the maker reads as it
        // did until then. Told every pass, it was sent the same switch over
        // and over -- and one of those copies arriving after the maker had
        // been shot is what ended a game with a bad variant access.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto maker = makeDef(false, false, false, "", 50u);
        maker.onOffable = true;
        maker.makesMetal = Metal(1.0f);
        sim.unitDefinitions["ARMMAKR"] = maker;
        auto makerId = addUnit(sim, "ARMMAKR", ai, SimVector(200_ss, 0_ss, 0_ss), script);
        sim.getUnitState(makerId).activated = true;
        // Nothing in the tank: the makers go off.
        sim.getPlayer(ai).energy = Energy(0.0f);
        sim.getPlayer(ai).maxEnergy = Energy(1000.0f);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        // Twenty ticks, in none of which the command is applied.
        runTicks(sim, controller, 20, commands);

        int switches = 0;
        for (const auto& command : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&command);
            if (unitCommand != nullptr && unitCommand->unit == makerId && std::holds_alternative<PlayerUnitCommand::SetOnOff>(unitCommand->command))
            {
                REQUIRE_FALSE(std::get<PlayerUnitCommand::SetOnOff>(unitCommand->command).on);
                ++switches;
            }
        }
        REQUIRE(switches == 1);
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

    TEST_CASE("the commander answers a lone raider when there is nothing else to send", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        // These pin the defend-alone rule, which was written against the old
        // safety rule; under commanderStandsItsGround the commander answers
        // whatever small party comes near it anyway, which is tested below.
        profile.commanderStandsItsGround = false;

        SECTION("with no combat units at all, the commander goes at the intruder itself")
        {
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
            REQUIRE(controller.getBlackboard().combatUnits.empty());

            auto attacks = ordersFor<AttackOrder>(commands, commanderId);
            REQUIRE(!attacks.empty());
            auto target = std::get_if<UnitId>(&attacks.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == raiderId);
        }

        SECTION("switched off, it stands there as it used to")
        {
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            profile.commanderDefendsAloneMaxIntruders = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
        }

        SECTION("with an army to send, the army goes and the commander stays home")
        {
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            auto defenderId = addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(!controller.getBlackboard().combatUnits.empty());
            REQUIRE(!ordersFor<AttackOrder>(commands, defenderId).empty());
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
            (void)raiderId;
        }

        SECTION("a raiding party is not the commander's problem")
        {
            // Two intruders against the default of one. A lone harasser is
            // worth the commander's attention; a party is a game already lost,
            // and walking the commander into it only loses it faster.
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().enemiesNearBase.size() == 2);
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("a commander in danger is got out of it, and what is near goes to it", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("a raiding party beside it: it runs the other way, builds nothing, and the army goes for the nearest of them")
        {
            // More than it takes on: 800 metal of raiders against 600.
            sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(400.0f);
            auto nearId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);
            auto defenderId = addUnit(sim, "ARMPW", ai, SimVector(-200_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderInDanger);
            REQUIRE(controller.getBlackboard().commanderFleeing);
            auto moves = ordersFor<MoveOrder>(commands, commanderId);
            REQUIRE(!moves.empty());
            REQUIRE(moves.front().destination.x.value < -200.0f);
            REQUIRE(ordersFor<BuildOrder>(commands, commanderId).empty());

            auto attacks = ordersFor<AttackOrder>(commands, defenderId);
            REQUIRE(!attacks.empty());
            auto target = std::get_if<UnitId>(&attacks.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == nearId);
        }

        SECTION("a lone raider it can shoot is fought, not fled")
        {
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderInDanger);
            REQUIRE(!controller.getBlackboard().commanderFleeing);
            REQUIRE(ordersFor<MoveOrder>(commands, commanderId).empty());
        }

        SECTION("unless it is losing")
        {
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).hitPoints = 40;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderFleeing);
            REQUIRE(!ordersFor<MoveOrder>(commands, commanderId).empty());
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
        }

        SECTION("switched off, none of it")
        {
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);
            profile.commanderDangerRadius = 0_ss;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(!controller.getBlackboard().commanderInDanger);
            REQUIRE(ordersFor<MoveOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("a commander run from its corner stays where a player could see it", "[ai]")
    {
        // The fixture is 1024 across; a base in its bottom right corner, with
        // the threat coming from the top left, runs straight on past the
        // camera's cut-off -- which on Great Divide is where one commander
        // ran to and died.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(400.0f);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(440_ss, 0_ss, 360_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(300_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(300_ss, 0_ss, 260_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 20, commands);

        REQUIRE(controller.getBlackboard().commanderFleeing);
        REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
        auto moves = ordersFor<MoveOrder>(commands, commanderId);
        REQUIRE(!moves.empty());
        REQUIRE(moves.back().destination.x <= sim.terrain.rightCutoffInWorldUnits() - 64_ss);
        REQUIRE(moves.back().destination.z <= sim.terrain.bottomCutoffInWorldUnits() - 64_ss);
    }

    TEST_CASE("the commander takes on a small party, and the D-gun goes first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(50.0f);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-100_ss, 0_ss, -100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        auto attackTarget = [&](const std::vector<PlayerCommand>& commands) -> std::optional<UnitId> {
            auto attacks = ordersFor<AttackOrder>(commands, commanderId);
            if (attacks.empty())
            {
                return std::nullopt;
            }
            auto target = std::get_if<UnitId>(&attacks.back().target);
            return target == nullptr ? std::nullopt : std::optional<UnitId>(*target);
        };

        SECTION("two raiders: it goes for the nearer, and does not run")
        {
            auto nearId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderInDanger);
            REQUIRE_FALSE(controller.getBlackboard().commanderFleeing);
            REQUIRE(attackTarget(commands) == std::optional<UnitId>(nearId));
            REQUIRE(ordersFor<MoveOrder>(commands, commanderId).empty());
        }

        SECTION("switched off, the same two send it running")
        {
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);
            profile.commanderStandsItsGround = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderFleeing);
            REQUIRE_FALSE(attackTarget(commands).has_value());
        }

        SECTION("with a D-gun and the energy for it")
        {
            auto& commanderDef = sim.unitDefinitions["ARMCOM"];
            commanderDef.canDgun = true;
            commanderDef.weapon3 = "DGUN";
            WeaponDefinition dgun{};
            dgun.maxRange = 240_ss;
            dgun.energyPerShot = Energy(500.0f);
            dgun.commandFire = true;
            sim.weaponDefinitions["DGUN"] = dgun;
            sim.getPlayer(ai).energy = Energy(1000.0f);
            sim.getPlayer(ai).maxEnergy = Energy(1000.0f);

            SECTION("a raider in reach is D-gunned")
            {
                auto raiderId = addUnit(sim, "ARMPW", human, SimVector(200_ss, 0_ss, 0_ss), script);
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);

                auto shots = ordersFor<DgunOrder>(commands, commanderId);
                REQUIRE(!shots.empty());
                REQUIRE(*std::get_if<UnitId>(&shots.front().target) == raiderId);
            }

            SECTION("not without the energy for a shot: it fights with the laser")
            {
                sim.getPlayer(ai).energy = Energy(100.0f);
                auto raiderId = addUnit(sim, "ARMPW", human, SimVector(200_ss, 0_ss, 0_ss), script);
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);

                REQUIRE(ordersFor<DgunOrder>(commands, commanderId).empty());
                REQUIRE(attackTarget(commands) == std::optional<UnitId>(raiderId));
            }

            SECTION("not through something of ours")
            {
                addUnit(sim, "ARMPW", ai, SimVector(100_ss, 0_ss, 10_ss), script);
                addUnit(sim, "ARMPW", human, SimVector(200_ss, 0_ss, 0_ss), script);
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);

                REQUIRE(ordersFor<DgunOrder>(commands, commanderId).empty());
            }

            SECTION("not with the D-gun switched off")
            {
                profile.commanderUsesDgun = false;
                addUnit(sim, "ARMPW", human, SimVector(200_ss, 0_ss, 0_ss), script);
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);

                REQUIRE(ordersFor<DgunOrder>(commands, commanderId).empty());
            }
        }
    }

    TEST_CASE("an idle commander helps finish a frame before it goes looking for rocks", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // A construction kbot at work on a tower, so the frame is nobody's
        // orphan: only the commander's own rule can send it there.
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(200_ss, 0_ss, 100_ss), script);
        auto frameId = addUnit(sim, "ARMLLT", ai, SimVector(250_ss, 0_ss, 100_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 50u;
        sim.getUnitState(kbotId).orders.push_back(RepairOrder(frameId));
        // Nothing on the commander's own menu, so the planner has nothing
        // of its own to hand it.
        auto tree = makeBuildTree();
        tree.buildableBy["ARMCOM"] = {};
        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("it goes to help")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto repairs = ordersFor<RepairOrder>(commands, commanderId);
            REQUIRE(!repairs.empty());
            REQUIRE(repairs.front().target == frameId);
        }

        SECTION("switched off, it does not")
        {
            profile.commanderAssistRadius = 0_ss;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(ordersFor<RepairOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("contacts closing on the base put the army in a line across their path", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        auto defenderId = addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);

        // Brutal sees everything, which stands in for a radar the fixture
        // has no definition for; the rule counts a column in plain view the
        // same way it counts blips.
        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 50;
        // The fixture's map is 1024 across, so the rings are drawn to fit it.
        profile.defendRadius = 200_ss;
        // And short enough sighted that the defender does not simply go for them.
        profile.engageRadius = 150_ss;
        // Nor is this the commander's alarm, which would send him at them too.
        profile.commanderDangerRadius = 0_ss;
        auto outside = 450_ss;

        SECTION("two of them, walking in")
        {
            auto a = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            auto b = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 80_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 35, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());

            // runTicks applies no orders, so they are walked by hand.
            sim.getUnitState(a).position.x -= 60_ss;
            sim.getUnitState(b).position.x -= 60_ss;
            commands.clear();
            // A second for the contacts to be read again, and the army's own pass after it.
            runTicks(sim, controller, 70, commands);

            REQUIRE(controller.getBlackboard().incomingAttackFrom.has_value());
            auto moves = ordersFor<MoveOrder>(commands, defenderId);
            REQUIRE(!moves.empty());
            // On the enemy's side of the base, inside the defended ring.
            REQUIRE(moves.back().destination.x.value > 60.0f);
            REQUIRE(moves.back().destination.x.value < profile.defendRadius.value);
        }

        SECTION("standing still, they are not an attack")
        {
            addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 80_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());
        }

        SECTION("one is a scout")
        {
            auto a = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 35, commands);
            sim.getUnitState(a).position.x -= 60_ss;
            runTicks(sim, controller, 35, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());
        }
    }

    TEST_CASE("an attack is ordered at the factory, not at the frame on its pad", "[ai]")
    {
        // A frame on a factory's pad stands where the factory stands, so a
        // nearest-enemy search picked it ahead of the factory: a raid killed
        // the same cheap frame over and over while the factory that kept
        // making it stood untouched. Reported from play.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Home well away, so this is a raid and not a defence.
        addUnit(sim, "ARMCOM", ai, SimVector(-900_ss, 0_ss, -900_ss), script);

        auto labId = addUnit(sim, "ARMLAB", human, SimVector(500_ss, 0_ss, 0_ss), script);
        // Nearer the raider than the lab is, so a search that took frames
        // would take this one.
        auto frameId = addUnit(sim, "ARMPW", human, SimVector(490_ss, 0_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(420_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 20, commands);

        auto attacks = ordersFor<AttackOrder>(commands, raiderId);
        REQUIRE(!attacks.empty());
        for (const auto& attack : attacks)
        {
            auto target = std::get_if<UnitId>(&attack.target);
            REQUIRE(target != nullptr);
            REQUIRE(*target != frameId);
        }
        REQUIRE(*std::get_if<UnitId>(&attacks.front().target) == labId);
    }

    TEST_CASE("a commander under the sea is not sent after what it cannot shoot", "[ai]")
    {
        // Nothing but a waterweapon fires from under the surface, so where the
        // commander walks the seabed an attack order sends it chasing
        // something it can never hurt -- and takes the only builder off the
        // base to do it. This is the flat-ground scene from the test above
        // that answers with the commander; only the sea level differs.
        auto script = makeEmptyCobScript();
        Grid<unsigned char> seabed(64, 64, static_cast<unsigned char>(0));
        GameSimulation sim(MapTerrain(std::move(seabed), 60_ss), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        // A skeeter on the surface, as on Brain Coral. Not a kbot on the
        // seabed: that is submerged, so nobody without sonar sees it, and the
        // AI would never have gone into Defend to be tested at all.
        addUnit(sim, "ARMPT", human, SimVector(250_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 20, commands);

        REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
        REQUIRE(controller.getBlackboard().combatUnits.empty());
        REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
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

    TEST_CASE("CORE plays its own line of kbots, and a knob set by hand still wins", "[ai]")
    {
        auto plain = makeDefaultStandardProfile();

        auto core = makeDefaultStandardProfile();
        applyFactionDefaults(core, "Core");
        REQUIRE(core.labRaiderShare == 1);
        REQUIRE(core.labRocketKbotShare == 2);
        REQUIRE(core.labArtilleryKbotShare == 1);
        REQUIRE(core.attackArmySize == 14);

        auto arm = makeDefaultStandardProfile();
        applyFactionDefaults(arm, "ARM");
        REQUIRE(arm.labRaiderShare == plain.labRaiderShare);
        REQUIRE(arm.labRocketKbotShare == plain.labRocketKbotShare);
        REQUIRE(arm.labArtilleryKbotShare == plain.labArtilleryKbotShare);
        REQUIRE(arm.attackArmySize == plain.attackArmySize);

        // LoadingScene lays --ai-tune over the faction's defaults, so this
        // is the order a knob set by hand meets them in.
        REQUIRE(applyAiTuning(core, "labRaiderShare", "2"));
        REQUIRE(core.labRaiderShare == 2);
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

            // Mapped: the ground is handed over, and the patch with it. The
            // one shared explored grid is marked with the AI's group bit, which
            // is what isExploredBy reads.
            sim.playerVisibility.at(ai.value).exploreAll(ExploredMark{&sim.explored, sim.losGroupBitFor(ai)});
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
                // Both rules, because there are two now and they ask the
                // same question of different things.
                // mexAvoidsEnemyGunsRadius steers the extractor SEARCH to
                // another patch; noticeProductionHarassment declines any
                // site at all that has a gun on it, at the point the order
                // would be issued, which is the backstop for everything
                // that is not an extractor. Switching off only the first no
                // longer walks into the guns, because the second still
                // refuses the order.
                profile.mexAvoidsEnemyGunsRadius = 0_ss;
                profile.noticeProductionHarassment = false;
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

    TEST_CASE("a laser tower is fortified: teeth across the approach, a missile tower behind", "[ai]")
    {
        // A base built out as far as its own two towers, the commander
        // walking away so the construction kbot is the only builder the
        // planner has, and the enemy's lab in sight to the east so the
        // approach has a direction. The shipped geometry is what the
        // profile holds; the test world's numbers only have to leave room.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-400_ss, 0_ss, 400_ss)));
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-60_ss, 0_ss, 60_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-200.0f + i * 40.0f), 0_ss, 150_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(-200_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMVP", ai, SimVector(-120_ss, 0_ss, 220_ss), script);
        addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
        const SimVector tower(100_ss, 0_ss, -100_ss);
        auto towerId = addUnit(sim, "ARMLLT", ai, tower, script);
        addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMLAB", human, SimVector(450_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.fortifyTowers = true;
        auto tree = makeBuildTree();
        tree.buildableBy["ARMCK"].insert("ARMDRAG");

        // The way from the first tower to the enemy's base, flat.
        auto towardsEnemy = SimVector(450_ss - tower.x, 0_ss, 0_ss - tower.z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        SECTION("the first tooth goes across the approach, in front of the first tower")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());

            BuildManager planner;
            std::minstd_rand rng(42u);
            auto plan = planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMDRAG");
            REQUIRE(plan->tower == towerId);
            // The middle of the line, profile.fortifyTeethDistance out along
            // the approach: inside a tower's reach, outside a raider's.
            auto out = (plan->site - tower).dot(towardsEnemy);
            REQUIRE(out > profile.fortifyTeethDistance - 20_ss);
            REQUIRE(out < profile.fortifyTeethDistance + 20_ss);
            REQUIRE(flatDistanceBetween(plan->site, tower) < profile.fortifyTeethDistance + 20_ss);

            // And the construction kbot, which has the button, is sent to it.
            auto orders = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(std::any_of(orders.begin(), orders.end(), [](const BuildOrder& o) { return o.unitType == "ARMDRAG"; }));
        }

        SECTION("a full line wants a missile tower behind it")
        {
            BuildManager planner;
            std::minstd_rand rng(42u);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            // Stand up every tooth the plan asks for, in turn, until it asks
            // for something else.
            std::optional<BuildManager::FortificationPlan> plan;
            for (int i = 0; i < profile.fortifyTeethPerTower + 1; ++i)
            {
                plan = planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng);
                REQUIRE(plan.has_value());
                if (plan->unitType != "ARMDRAG")
                {
                    break;
                }
                addUnit(sim, "ARMDRAG", ai, plan->site, script);
            }
            REQUIRE(plan->unitType == "ARMRL");
            REQUIRE(plan->tower == towerId);
            REQUIRE((plan->site - tower).dot(towardsEnemy) <= 0_ss);
            REQUIRE(flatDistanceBetween(plan->site, tower) <= profile.fortifyMissileCoverRadius);
        }

        SECTION("a tower short of its teeth gets a constructor of its own")
        {
            // One construction kbot stands, which is the whole of
            // targetConstructorCount; the teeth it owes are what ask for
            // a second.
            REQUIRE(profile.targetConstructorCount == 1);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") >= 1);
        }

        SECTION("off, nothing is fortified")
        {
            profile.fortifyTowers = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 0);

            BuildManager planner;
            std::minstd_rand rng(42u);
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng).has_value());
            auto orders = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(std::none_of(orders.begin(), orders.end(), [](const BuildOrder& o) { return o.unitType == "ARMDRAG"; }));
        }
    }

    TEST_CASE("a lost tower is put back, fortified away from the base, and reinforced if it falls again", "[ai]")
    {
        // Driven directly, as the teeth-where-attacked test is: the losses
        // are written into the blackboard by hand, the way EconomyManager
        // writes them, and the planner is asked what it would do.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        const SimVector site(0_ss, 0_ss, 0_ss);
        const SimVector east(1_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();
        profile.fortifyTowers = false;
        profile.fortifyWhereAttacked = false;

        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(-300_ss, 0_ss, 0_ss);
        REQUIRE(bb.sideUnits.antiAirTower == "ARMRL");

        BuildManager planner;
        std::minstd_rand rng(42u);
        const auto delay = static_cast<unsigned int>(profile.rebuildDelaySeconds) * 30u;

        bb.now = GameTime(300);
        bb.recentLosses.push_back(LostBuilding{"ARMLLT", site, GameTime(300)});
        planner.recordLostDefences(sim, profile, bb);
        REQUIRE(planner.getLostDefenceSites().size() == 1);
        REQUIRE(planner.getLostDefenceSites().front().timesLost == 1);

        SECTION("not at once, not under a gun, and then where it stood")
        {
            bb.now = GameTime(300 + delay - 1);
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            bb.now = GameTime(300 + delay);
            bb.knownEnemies[9999u] = KnownEnemy{UnitId(9999u), "ARMPW", site + SimVector(150_ss, 0_ss, 0_ss), bb.now, false, true, false};
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            bb.knownEnemies.clear();
            auto plan = planner.planDefenceRebuild(sim, ai, profile, bb);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMLLT");
            REQUIRE(plan->site.distanceSquared(site) < 1_ss);
            REQUIRE_FALSE(plan->wreck.has_value());

            // Reading the losses again counts nothing twice.
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().front().timesLost == 1);
        }

        SECTION("put back: teeth on the side away from the base, then nothing more")
        {
            auto towerId = addUnit(sim, "ARMLLT", ai, site, script);
            bb.standingBuildings[towerId.value] = StandingBuilding{"ARMLLT", site};
            bb.now = GameTime(300 + delay);
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            for (int i = 0; i < profile.fortifyReactiveTeeth; ++i)
            {
                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE(next->unitType == "ARMDRAG");
                REQUIRE((next->site - site).dot(east) > profile.fortifyTeethDistance - 30_ss);
                addUnit(sim, "ARMDRAG", ai, next->site, script);
            }
            // Lost once: no missile tower yet.
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());

            SECTION("and lost again with its teeth in front: a missile tower behind it")
            {
                bb.now = GameTime(3000);
                bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMLLT", site, GameTime(3000)});
                planner.recordLostDefences(sim, profile, bb);
                REQUIRE(planner.getLostDefenceSites().front().timesLost == 2);

                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE(next->unitType == "ARMRL");
                REQUIRE((next->site - site).dot(east) <= 0_ss);
                REQUIRE(flatDistanceBetween(next->site, site) <= profile.fortifyMissileCoverRadius);
            }

            SECTION("switched off, nothing")
            {
                profile.fortifyRebuiltDefences = false;
                bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMLLT", site, GameTime(3000)});
                REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
            }
        }

        SECTION("a solar collector lost is nobody's to put back")
        {
            bb.now = GameTime(400);
            bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMSOLAR", SimVector(200_ss, 0_ss, 200_ss), GameTime(400)});
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().size() == 1);
        }

        SECTION("switched off, nothing is remembered")
        {
            profile.rebuildLostDefences = false;
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().empty());
            (void)human;
        }
    }

    TEST_CASE("solar collectors go up in rows behind the base", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        // The enemy to the east, so behind is west.
        bb.enemyBasePosition = SimVector(400_ss, 0_ss, 0_ss);

        const auto& solarDef = sim.unitDefinitions.at("ARMSOLAR");
        auto footprint = sim.getFootprintXZ(solarDef.movementCollisionInfo);
        const auto spacing = SimScalar(static_cast<float>(std::max(footprint.first, footprint.second) + 2) * 16.0f);

        BuildManager planner;
        std::minstd_rand rng(42u);

        SECTION("the first behind the base, on the nearest ring")
        {
            auto site = planner.chooseEnergyRowSite(sim, ai, profile, bb, "ARMSOLAR", *bb.baseAnchor, *bb.baseAnchor, rng, nullptr);
            REQUIRE(site.has_value());
            REQUIRE(site->x < 0_ss);
            REQUIRE(rweAbs(site->x) <= spacing + 1_ss);
            REQUIRE(rweAbs(site->z) <= spacing + 1_ss);
        }

        SECTION("the next beside it in a row, the side nearest the builder")
        {
            const SimVector first(-spacing, 0_ss, 0_ss);
            addUnit(sim, "ARMSOLAR", ai, first, script);
            const SimVector builder(-spacing, 0_ss, 200_ss);
            auto site = planner.chooseEnergyRowSite(sim, ai, profile, bb, "ARMSOLAR", *bb.baseAnchor, builder, rng, nullptr);
            REQUIRE(site.has_value());
            REQUIRE(rweAbs(site->x - first.x) <= 1_ss);
            REQUIRE(rweAbs(site->z - (first.z + spacing)) <= 1_ss);
        }
    }

    TEST_CASE("teeth go where a defence keeps being attacked from, and nowhere else", "[ai]")
    {
        // Driven directly rather than through a controller, so that when an
        // attack happens and from where is set by hand: the watch is asked
        // to look, the tower loses hit points with an armed enemy standing
        // on one side of it, the enemy leaves, and the watch looks again.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        const SimVector tower(0_ss, 0_ss, 0_ss);
        auto towerId = addUnit(sim, "ARMLLT", ai, tower, script);
        sim.getUnitState(towerId).hitPoints = 100;

        auto profile = makeDefaultStandardProfile();
        profile.fortifyTowers = false;
        profile.fortifyWhereAttacked = true;

        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(-300_ss, 0_ss, 0_ss);
        bb.standingBuildings[towerId.value] = StandingBuilding{"ARMLLT", tower};
        REQUIRE(bb.sideUnits.dragonsTeeth == "ARMDRAG");

        const auto gap = static_cast<unsigned int>(profile.fortifyAttackGapSeconds) * 30u;
        const SimVector east(1_ss, 0_ss, 0_ss);
        BuildManager planner;
        std::minstd_rand rng(42u);
        unsigned int now = 0;

        auto look = [&] {
            bb.now = GameTime(now);
            planner.watchDefences(sim, profile, bb);
        };
        auto attackFrom = [&](const SimVector& from) {
            bb.knownEnemies[9999u] = KnownEnemy{UnitId(9999u), "ARMPW", tower + from, GameTime(now), false, true, false};
            now += 30;
            sim.getUnitState(towerId).hitPoints -= 10;
            look();
            // And gone again, as raiders are, for longer than the gap
            // that makes the next hit a separate attack.
            bb.knownEnemies.clear();
            now += gap + 30;
            look();
        };
        look();

        SECTION("twice from the east: a few teeth to the east, and then no more")
        {
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());

            attackFrom(SimVector(300_ss, 0_ss, 40_ss));
            auto plan = planner.planFortification(sim, ai, profile, bb, rng);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMDRAG");
            REQUIRE(plan->tower == towerId);
            REQUIRE((plan->site - tower).dot(east) > profile.fortifyTeethDistance - 30_ss);

            for (int i = 0; i < profile.fortifyReactiveTeeth; ++i)
            {
                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE((next->site - tower).dot(east) > 0_ss);
                addUnit(sim, "ARMDRAG", ai, next->site, script);
            }
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }

        SECTION("once from the east and once from the north: nothing")
        {
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            attackFrom(SimVector(0_ss, 0_ss, -300_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }

        SECTION("off: nothing, however often")
        {
            profile.fortifyWhereAttacked = false;
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }
    }

    namespace
    {
        /**
         * A base built out as far as its two towers, the commander walking
         * off so the construction kbot is the builder the planner has, and
         * every builder able to repair, as every shipped one is.
         */
        struct RepairBase
        {
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            GameSimulation sim{makeFlatTerrain(), 0u, 0, 0};
            PlayerId ai;
            UnitId commanderId{0};
            UnitId kbotId{0};
            UnitId labId{0};
            UnitId towerId{0};
            UnitId solarId{0};

            RepairBase()
                : ai(addPlayer(sim, "ai", GamePlayerType::Computer, "ARM"))
            {
                defineWorld(sim);
                sim.unitDefinitions["ARMCK"].canReclamate = true;
                sim.unitDefinitions["ARMCOM"].canReclamate = true;
                commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
                sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-400_ss, 0_ss, 400_ss)));
                kbotId = addUnit(sim, "ARMCK", ai, SimVector(-60_ss, 0_ss, 60_ss), script);
                for (int i = 0; i < 4; ++i)
                {
                    auto id = addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-200.0f + i * 40.0f), 0_ss, 150_ss), script);
                    if (i == 0)
                    {
                        solarId = id;
                    }
                }
                for (int i = 0; i < 3; ++i)
                {
                    addUnit(sim, "ARMMEX", ai, SimVector(-200_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
                }
                labId = addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 200_ss), script);
                addUnit(sim, "ARMVP", ai, SimVector(-120_ss, 0_ss, 220_ss), script);
                addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
                towerId = addUnit(sim, "ARMLLT", ai, SimVector(100_ss, 0_ss, -100_ss), script);
                addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
                for (auto& [id, unit] : sim.units)
                {
                    unit.hitPoints = sim.unitDefinitions.at(unit.unitType).maxHitPoints;
                }
            }

            /** What the kbot is first told to repair, over the second the planner needs. */
            std::optional<UnitId> firstRepair(const AiTuningProfile& profile)
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto repairs = ordersFor<RepairOrder>(commands, kbotId);
                if (repairs.empty())
                {
                    return std::nullopt;
                }
                return repairs.front().target;
            }
        };
    }

    TEST_CASE("builders mend a damaged defence first, then a damaged factory", "[ai]")
    {
        RepairBase base;
        auto profile = makeDefaultStandardProfile();

        SECTION("the tower before the lab, though the lab is as badly hurt")
        {
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            base.sim.getUnitState(base.labId).hitPoints = 40;
            REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.towerId));
        }

        SECTION("the lab, once the towers are whole")
        {
            base.sim.getUnitState(base.labId).hitPoints = 40;
            REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.labId));
        }

        SECTION("a solar collector is not worth the trip")
        {
            base.sim.getUnitState(base.solarId).hitPoints = 40;
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }

        SECTION("off, nothing is mended")
        {
            profile.repairStructures = false;
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }

        SECTION("with the raider still beside it, repairUnderFire decides")
        {
            auto human = addPlayer(base.sim, "human", GamePlayerType::Human, "ARM");
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            addUnit(base.sim, "ARMPW", human, SimVector(250_ss, 0_ss, -100_ss), base.script);

            SECTION("on: mended all the same")
            {
                profile.repairUnderFire = true;
                REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.towerId));
            }

            SECTION("off: left until it has gone")
            {
                profile.repairUnderFire = false;
                REQUIRE_FALSE(base.firstRepair(profile).has_value());
            }
        }
    }

    TEST_CASE("a damaged commander takes a construction kbot off its job", "[ai]")
    {
        RepairBase base;
        auto profile = makeDefaultStandardProfile();
        // Busy: the planner would never offer it anything, so only the
        // commander's call can move it.
        base.sim.getUnitState(base.kbotId).addOrder(BuildOrder("ARMSOLAR", SimVector(-300_ss, 0_ss, -300_ss)));

        SECTION("hurt: the kbot is sent to it")
        {
            base.sim.getUnitState(base.commanderId).hitPoints = 50;
            REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.commanderId));
        }

        SECTION("whole: it is left to its job")
        {
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }

        SECTION("off: it is left to its job")
        {
            profile.repairCommander = false;
            base.sim.getUnitState(base.commanderId).hitPoints = 50;
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }
    }

    TEST_CASE("a tower faces where losses actually came from, not the enemy's unseen base", "[ai]")
    {
        // threatDirection falls back to the world origin when no enemy base
        // has been seen, which is the whole opening and any later stretch
        // where contact has been lost -- not a real threat direction. A
        // tower lost to the south should steer the next one there instead
        // of towards the map's middle.
        //
        // The casualty is itself a light laser tower rather than, say, a
        // solar collector: recentLosses feeds "replace what was just
        // destroyed" too (buildPriorities' last rule), which reorders the
        // wanted list to put back whatever type was lost first of all --
        // a solar collector would then jump the queue ahead of the very
        // tower this test is about, for a reason that has nothing to do
        // with facing. A lost tower is already wanted first (it is what
        // targetDefenceCount asks for), so the reorder is a no-op here and
        // the test is actually isolating the facing logic.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(0_ss, 0_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        // Standing long enough to be noticed, then destroyed -- the raid.
        auto raidedId = addUnit(sim, "ARMLLT", ai, anchor + SimVector(0_ss, 0_ss, -500_ss), script);

        auto profile = makeDefaultStandardProfile();

        SECTION("on: the tower is posted south, towards the loss")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 30, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);

            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(order.position.z < -50_ss);
        }

        SECTION("off: the knob restores the old facing, towards the map's middle")
        {
            profile.defenceFacesRecentLosses = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 30, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);

            auto order = theBuildOrder(commands, "ARMLLT");
            // Facing (1,0,0): posted east, towards the map's middle. Not
            // asserted on z -- with no other tower yet standing, "forward"
            // is capped and tied for every candidate at the post's ring,
            // so the tie-break between a candidate north and one south of
            // it is the RNG's, not the facing logic's, to answer.
            REQUIRE(order.position.x > 50_ss);
        }
    }

    TEST_CASE("a tower has to be worth its metal", "[ai]")
    {
        // towerCostJustified is a pure function of the profile, the
        // blackboard's income and the tower's own cost, so it is tested
        // directly rather than through a whole game -- the same style as
        // canAfford's "a stockpile at the cap" case above.
        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        UnitDefinition tower{};
        tower.buildCostMetal = Metal(600.0f);

        SECTION("an economy too poor to pay it back in time is refused")
        {
            bb.metalIncome = Metal(1.0f);
            // 90 seconds (the default) of a 1/s economy is 90 metal, nowhere near 600.
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("a healthy economy clears it easily")
        {
            bb.metalIncome = Metal(20.0f);
            // 90 seconds of a 20/s economy is 1800, comfortably over 600.
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("an outpost earns extra allowance for what it covers")
        {
            bb.metalIncome = Metal(1.0f);
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower, 0));
            // 90 + 6*45 = 360, still short; 90 + 12*45 = 630 clears it.
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower, 6));
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower, 12));
        }

        SECTION("a reading of zero income is unmeasured, not poor, and does not block")
        {
            bb.metalIncome = Metal(0.0f);
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("the knob switches the whole test off")
        {
            profile.defenceValueMaxPaybackSeconds = 0;
            bb.metalIncome = Metal(1.0f);
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }
    }

    TEST_CASE("a guard is sent to a builder placed away from the base, and released once it is done", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(-300_ss, 0_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        // A small army to draw the guard from.
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMPW", ai, anchor + SimVector(0_ss, 0_ss, SimScalar(-200.0f - i * 40.0f)), script);
        }

        auto profile = makeDefaultStandardProfile();
        // The ordinary front-of-base tower -- 160 out, per layOutBase's
        // world -- already qualifies as "away from the base" for this test.
        profile.buildSiteGuardMinDistance = 50_ss;
        // The feature ships off, having measured worse than not having it,
        // so this asks for it by name. What it pins -- that a guard is
        // detached to the site and released when the job ends -- is the
        // behaviour anyone switching it back on would be relying on.
        profile.buildSiteGuardSize = 2;

        SECTION("a guard is detached to the site, and released once the tower stands")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(bb.buildSiteGuardRequest.has_value());
            bool guardsExactBuildSite = bb.buildSiteGuardRequest->position == order.position;
            REQUIRE(guardsExactBuildSite);
            REQUIRE(bb.guardGroup.size() == static_cast<std::size_t>(profile.buildSiteGuardSize));

            int guardMoves = 0;
            for (auto id : bb.guardGroup)
            {
                auto moves = ordersFor<MoveOrder>(commands, UnitId(id));
                if (!moves.empty())
                {
                    REQUIRE(flatDistanceBetween(moves.back().destination, order.position) < 1_ss);
                    ++guardMoves;
                }
            }
            REQUIRE(guardMoves > 0);

            // The tower stands, and the builder that placed it is idle
            // again -- the job the guard was sent for is over.
            addUnit(sim, "ARMLLT", ai, order.position, script);
            sim.getUnitState(bb.buildSiteGuardRequest->builderId).orders.clear();
            runTicks(sim, controller, 20, commands);
            REQUIRE_FALSE(controller.getBlackboard().buildSiteGuardRequest.has_value());
            REQUIRE(controller.getBlackboard().guardGroup.empty());
        }

        SECTION("the knob at zero never asks for one")
        {
            profile.buildSiteGuardSize = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            theBuildOrder(commands, "ARMLLT");

            const auto& bb = controller.getBlackboard();
            REQUIRE_FALSE(bb.buildSiteGuardRequest.has_value());
            REQUIRE(bb.guardGroup.empty());
        }
    }

    TEST_CASE("the one guard slot is held by the most threatened site, not the most recent", "[ai]")
    {
        // There is a single buildSiteGuardRequest, and it used to be taken by
        // whichever qualifying build order came last. Ten games at hard
        // difficulty made 346 requests and produced 55 guards, so most were
        // overwritten before anyone stood anywhere -- and the four builders
        // that actually died were at sites reading 14988, 13560, 0 and 0.
        // Recency was throwing away exactly the requests worth keeping.
        //
        // Measured over the same games, a tower is where the danger is: 12 of
        // 21 laser tower sites had an enemy that could reach them against 25
        // of 325 extractor sites, and a tower builder died at four times the
        // rate. So the slot should end up on the tower under fire rather than
        // on the next extractor started in an empty corner.
        const SimVector somewhere(500_ss, 0_ss, 500_ss);

        SECTION("an empty slot takes whatever asks for it")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held;
            REQUIRE(BuildManager::guardRequestDisplaces(held, 0.0f));
            REQUIRE(BuildManager::guardRequestDisplaces(held, 14988.0f));
        }

        SECTION("a more threatened site takes the slot off a quieter one")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 0.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 13560.0f));
        }

        SECTION("a quieter site does not take it off a threatened one")
        {
            // The case the measurement found: a tower going up under fire
            // must not lose its guard to an extractor nothing is near.
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 14988.0f});
            REQUIRE_FALSE(BuildManager::guardRequestDisplaces(held, 0.0f));
            REQUIRE_FALSE(BuildManager::guardRequestDisplaces(held, 13560.0f));
        }

        SECTION("with the threat test switched off every site ties, and recency decides as before")
        {
            // buildSiteGuardThreat at zero leaves every site reading zero.
            // Equal threat has to keep displacing or the slot would freeze on
            // the first request until it timed out -- the kill switch is meant
            // to restore the old behaviour exactly, not a new one.
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 0.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 0.0f));
        }

        SECTION("an equally threatened site also displaces, for the same reason")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 825.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 825.0f));
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

    TEST_CASE("an extractor goes to the heart of a deposit, not its near edge", "[ai]")
    {
        // A metal patch is one CELL, not one deposit, so the nearest ring of
        // the site search to find anything is a deposit's near edge -- and the
        // search used to stop there, putting a 3x3 extractor half off every
        // deposit bigger than about two cells square. Extraction is the metal
        // under the footprint, so that was income lost, not a cosmetic
        // offset. Reported from a replay on Brain Coral.
        GameSimulation sim(makeFlatTerrain(128, 128), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition mexDef;
        mexDef.isMobile = false;
        mexDef.builder = false;
        // ARMMEX and ARMUWMEX are both 3x3.
        mexDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["MEX"] = mexDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto anchorHm = sim.terrain.worldToHeightmapCoordinate(anchor);

        auto paint = [&](int x0, int z0, int width, int height) {
            for (int z = z0; z < z0 + height; ++z)
            {
                for (int x = x0; x < x0 + width; ++x)
                {
                    sim.metalGrid.set(x, z, static_cast<unsigned char>(200));
                }
            }
        };
        auto metalUnder = [&](const SimVector& site) {
            auto rect = sim.computeFootprintRegion(site, mexDef.movementCollisionInfo);
            unsigned int total = 0;
            for (int z = rect.y; z < rect.y + static_cast<int>(rect.height); ++z)
            {
                for (int x = rect.x; x < rect.x + static_cast<int>(rect.width); ++x)
                {
                    total += sim.metalGrid.get(x, z);
                }
            }
            return total;
        };

        SECTION("a deposit found from the side is taken at its heart")
        {
            // 5x5, its near edge six cells east of the anchor. On that edge a
            // 3x3 covers six of the deposit's cells; one ring further in it
            // covers nine.
            paint(anchorHm.x + 6, anchorHm.y - 2, 5, 5);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("but it does not walk past a spot to reach a richer deposit further off")
        {
            // A single cell three out, bare ground, then a 5x5 twelve out. The
            // walk goes INTO the deposit it has found; it is not a hunt for a
            // better one elsewhere, and bare ground is where it stops. Without
            // the gap test the lone cell's ring counts as improving and the
            // walk carries on out to the big deposit.
            paint(anchorHm.x + 3, anchorHm.y, 1, 1);
            paint(anchorHm.x + 12, anchorHm.y - 2, 5, 5);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 200u);
        }

        SECTION("a rule whose edge runs through a deposit still gets its heart")
        {
            // The commander's leash cut a Great Divide rock so that only its
            // near column was inside, and the extractor went on that column,
            // over four of the rock's nine cells. The ground rule is asked of
            // the deposit now: one cell inside is enough.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            const auto nearColumnEdge = sim.terrain.heightmapIndexToWorldCorner(anchorHm.x + 7, anchorHm.y).x;
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}, [&](const SimVector& p) { return p.x < nearColumnEdge; });
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("a deposit whose heart is refused waits, rather than taking the cell beside it")
        {
            // A dropped order is remembered as a site, and the next search
            // used to take the site next to it on the same rock: 101 of the
            // 173 extractors Great Divide put off-centre. With a second
            // deposit further off, that one is taken instead.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            const auto heart = sim.terrain.heightmapIndexToWorldCenter(anchorHm.x + 7, anchorHm.y);
            auto notTheHeart = [&](const SimVector& p) { return p.distanceSquared(heart) > 4_ss; };
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, notTheHeart).has_value());

            // A fresh planner, since the patches are indexed once a game.
            paint(anchorHm.x - 12, anchorHm.y - 1, 3, 3);
            BuildManager later;
            auto site = later.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, notTheHeart);
            REQUIRE(site.has_value());
            REQUIRE(site->x < anchor.x);
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("a unit standing on its heart: it waits, and after a minute takes what is free")
        {
            // A 2x2 kbot over the deposit's corner cell and the ground
            // beyond it: the centred extractor is refused, the placement one
            // row up is not, and it covers six of the nine cells.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            auto kbot = makeDef(false, false, true, "", 100u);
            kbot.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["KBOT"] = kbot;
            auto script = makeEmptyCobScript();
            auto kbotId = addUnit(sim, "KBOT", PlayerId(0), sim.terrain.heightmapIndexToWorldCorner(anchorHm.x + 9, anchorHm.y + 2), script);
            // The fixture's addUnit leaves the occupancy grid alone, so the
            // kbot's cells are stamped by hand, as the simulation's own
            // tryAddUnit stamps them.
            auto kbotRect = sim.computeFootprintRegion(sim.getUnitState(kbotId).position, sim.unitDefinitions.at("KBOT").movementCollisionInfo);
            sim.occupiedGrid.forEach(sim.occupiedGrid.clipRegion(kbotRect), [&](auto& cell) { cell.mobileUnitId = kbotId; });

            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());

            sim.gameTime = GameTime(sim.gameTime.value + (60u * 30u) - 1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());

            sim.gameTime = GameTime(sim.gameTime.value + 1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 6u * 200u);
        }

        SECTION("nothing goes where the camera cannot see")
        {
            // The bottom eight rows of height tiles are past the camera's
            // cut-off. A deposit there is out of a player's reach, and so out
            // of the computer's.
            const auto heights = sim.terrain.getHeightMap().getHeight();
            paint(anchorHm.x - 1, heights - 6, 3, 3);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());
        }
    }

    TEST_CASE("with no free patch left, an extractor is reclaimed for a moho -- one, and only with the metal in hand", "[ai]")
    {
        // The original refuses a building placed over a standing unit
        // (TOTALA-EXE.md, 0x47D547), so an upgrade is a reclaim and then a
        // build, and the patch earns nothing in between.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMLAB", ai, SimVector(400_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-400_ss, 0_ss, -400_ss), script);
        auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-300_ss, 0_ss, 100_ss), script);

        // Well apart, so a 5x5 has room where each 3x3 stands. No metal is
        // painted anywhere: there is no free patch, which is the point.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        std::vector<UnitId> extractors;
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            extractors.push_back(addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-200.0f + i * 160.0f), 0_ss, 200_ss), script));
        }
        sim.unitDefinitions["ARMMOHO"].buildCostMetal = Metal(1508.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        auto reclaimedUnits = [&](const std::vector<PlayerCommand>& commands) {
            std::set<unsigned int> targets;
            for (const auto& order : ordersFor<ReclaimOrder>(commands, advancedId))
            {
                if (auto unit = std::get_if<UnitId>(&order.target))
                {
                    targets.insert(unit->value);
                }
            }
            return targets;
        };

        SECTION("the nearest one, and no other however many passes go by")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);

            auto targets = reclaimedUnits(commands);
            REQUIRE(targets.size() == 1);
            // The builder stands at x -300; the first extractor is at -200.
            REQUIRE(*targets.begin() == extractors.front().value);
            REQUIRE(ordersFor<BuildOrder>(commands, advancedId).empty());
        }

        SECTION("then the moho, where the extractor stood, and the loss is not read as a raid")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            REQUIRE(reclaimedUnits(commands).size() == 1);

            auto where = sim.getUnitState(extractors.front()).position;
            sim.quietlyKillUnit(extractors.front());
            commands.clear();
            runTicks(sim, controller, 91, commands);

            auto builds = ordersFor<BuildOrder>(commands, advancedId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMMOHO");
            REQUIRE(builds.front().position.x.value == where.x.value);
            REQUIRE(builds.front().position.z.value == where.z.value);
            REQUIRE(controller.getBlackboard().recentLosses.empty());
        }

        SECTION("not on an empty store: the patch would stand idle while the moho was saved for")
        {
            sim.getPlayer(ai).metal = Metal(300.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            REQUIRE(reclaimedUnits(commands).empty());
        }

        SECTION("and not at all with the knob off")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            profile.extractorUpgrades = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            REQUIRE(reclaimedUnits(commands).empty());
        }
    }

    TEST_CASE("a geothermal plant goes on a vent, and is not asked for where there is none", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.lineOfSightMode = LineOfSightMode::Circular;
        sim.unitDefinitions["ARMGEO"] = makeDef(false, false, false, "", 50u);

        auto profile = makeDefaultStandardProfile();
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, -100_ss), script);

        // The opening already built, so the plan has got as far as the vent.
        for (int i = 0; i < profile.targetSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-600.0f + i * 100.0f), 0_ss, 500_ss), script);
        }
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-600.0f + i * 100.0f), 0_ss, 650_ss), script);
        }
        // And everything the plan puts ahead of it.
        addUnit(sim, "ARMRAD", ai, SimVector(-600_ss, 0_ss, -500_ss), script);
        addUnit(sim, "ARMRL", ai, SimVector(-500_ss, 0_ss, -500_ss), script);
        for (int i = 0; i < profile.targetDefenceCount; ++i)
        {
            addUnit(sim, "ARMLLT", ai, SimVector(SimScalar(-400.0f + i * 100.0f), 0_ss, -500_ss), script);
        }
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        auto tree = makeBuildTree();
        tree.buildableBy["ARMCK"].insert("ARMGEO");

        SECTION("on the vent")
        {
            FeatureDefinition vent{};
            vent.name = "steamvent";
            vent.footprintX = 2;
            vent.footprintZ = 2;
            vent.geothermal = true;
            auto ventDef = sim.featureDefinitions.insert(vent);
            auto ventId = sim.addFeature(ventDef, 26, 28).value();
            auto where = sim.getFeature(ventId).position;

            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);

            auto builds = ordersFor<BuildOrder>(commands, kbotId);
            auto geo = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& o) { return o.unitType == "ARMGEO"; });
            REQUIRE(geo != builds.end());
            REQUIRE(geo->position.x.value == where.x.value);
            REQUIRE(geo->position.z.value == where.z.value);
        }

        SECTION("and nothing else does")
        {
            // A vent blocks nothing, so the placement test would let a solar
            // collector stand on it. Vents all round the builder, and no
            // button for the plant: whatever is built, none of it may
            // overlap one.
            FeatureDefinition vent{};
            vent.name = "steamvent";
            vent.footprintX = 2;
            vent.footprintZ = 2;
            vent.geothermal = true;
            // As every shipped vent is, and it is what puts one in the sim's vent grid.
            vent.indestructible = true;
            auto ventDef = sim.featureDefinitions.insert(vent);
            std::vector<SimVector> vents;
            for (int z = 14; z <= 34; z += 5)
            {
                for (int x = 10; x <= 34; x += 5)
                {
                    if (auto id = sim.addFeature(ventDef, x, z))
                    {
                        vents.push_back(sim.getFeature(*id).position);
                    }
                }
            }
            REQUIRE(vents.size() >= 6);
            auto hungry = profile;
            hungry.targetSolarCount += 4;
            AiPlayerController controller(ai, hungry, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!builds.empty());
            for (const auto& order : builds)
            {
                for (const auto& v : vents)
                {
                    auto dx = std::abs(order.position.x.value - v.x.value);
                    auto dz = std::abs(order.position.z.value - v.z.value);
                    INFO(order.unitType << " at " << order.position.x.value << "," << order.position.z.value << " vent " << v.x.value << "," << v.z.value);
                    REQUIRE((dx >= 32.0f || dz >= 32.0f));
                }
            }
        }

        SECTION("nothing, with no vent on the map")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMGEO") == types.end());
        }
    }

    TEST_CASE("the site search walks outward past a ring the builder cannot reach", "[ai]")
    {
        // The ring walk stops at the NEAREST ring with room on it, which is
        // right for a solar collector and is exactly what makes filtering
        // its result the wrong way to refuse a site: the ring empties and
        // the search gives up, so a base whose first ring happened to sit
        // across water would refuse to build at all rather than build
        // further out on its own ground.
        //
        // So the predicate is asked inside the walk. This test discriminates
        // between the two: everything within the first two rings is refused,
        // and a search that filtered afterwards would return nothing here.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();

        // Spacing is (max(footprint) + 2) tiles, so 7 tiles or 112 world
        // units for a 5x5: rings 1 and 2 reach x = -112 and -224, and only
        // ring 3 reaches past -300.
        auto farSideOnly = [](const SimVector& p) { return p.x < -300_ss; };

        BuildManager buildManager;
        std::minstd_rand rng(1u);
        auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng, farSideOnly);

        REQUIRE(site.has_value());
        REQUIRE(site->x < -300_ss);
    }

    TEST_CASE("a building is not planted across a metal patch, anywhere under it", "[ai]")
    {
        // The rule existed; it only ever looked at ONE cell. collectBuildableSites
        // tested metalGrid.get(rect.x, rect.y) -- the footprint's top-left corner
        // -- so a building whose corner was clear sat straight down across a
        // patch. ARMSOLAR is 5x5, which left 24 of its 25 cells unchecked, and a
        // 145-metal solar parked on a patch denies a 50-metal extractor that spot
        // for the rest of the game. Observed in play before it was found here.
        GameSimulation sim(makeFlatTerrain(128, 128), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        // ARMSOLAR's own footprint, because the size is the whole point: the
        // bigger it is, the more of it the old check could not see.
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();

        // Where it goes with nothing in the way.
        BuildManager clean;
        std::minstd_rand rngA(1u);
        auto firstSite = clean.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rngA);
        REQUIRE(firstSite.has_value());
        auto firstRect = sim.computeFootprintRegion(*firstSite, solarDef.movementCollisionInfo);

        // A patch in the MIDDLE of that footprint -- deliberately not its
        // top-left cell, which is the only one the old rule consulted. With the
        // same seed and the same anchor, the old code returned this very site
        // again; the corner it checked is still clear.
        sim.metalGrid.set(firstRect.x + 2, firstRect.y + 2, static_cast<unsigned char>(200));

        BuildManager retry;
        std::minstd_rand rngB(1u);
        auto secondSite = retry.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rngB);
        REQUIRE(secondSite.has_value());
        auto secondRect = sim.computeFootprintRegion(*secondSite, solarDef.movementCollisionInfo);

        // Not one cell of the chosen footprint may hold a patch.
        for (int y = secondRect.y; y < secondRect.y + static_cast<int>(secondRect.height); ++y)
        {
            for (int x = secondRect.x; x < secondRect.x + static_cast<int>(secondRect.width); ++x)
            {
                INFO("cell " << x << "," << y);
                REQUIRE(sim.metalGrid.get(x, y) <= sim.surfaceMetal);
            }
        }
    }

    TEST_CASE("a building is not planted across a factory's exit", "[ai]")
    {
        // A unit a factory finishes has to walk off the pad before the next
        // one can be spawned there, so a building hard against the factory
        // stops it producing for the rest of the game.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto script = makeEmptyCobScript();

        UnitDefinition factoryDef;
        factoryDef.isMobile = false;
        factoryDef.builder = true;
        factoryDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["FACTORY"] = factoryDef;

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto factoryId = addUnitOfType(sim, "FACTORY", ai, anchor, script);
        auto factoryRect = sim.computeFootprintRegion(sim.getUnitState(factoryId).position, factoryDef.movementCollisionInfo);

        auto profile = makeDefaultStandardProfile();
        std::minstd_rand rng(1u);
        BuildManager buildManager;

        auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng);
        REQUIRE(site.has_value());

        auto rect = sim.computeFootprintRegion(*site, solarDef.movementCollisionInfo);
        // Three tiles of lane, the factory's own margin, on at least one axis.
        auto clearX = (rect.x >= factoryRect.x + factoryRect.width + 3) || (factoryRect.x >= rect.x + rect.width + 3);
        auto clearZ = (rect.y >= factoryRect.y + factoryRect.height + 3) || (factoryRect.y >= rect.y + rect.height + 3);
        REQUIRE((clearX || clearZ));
    }

    TEST_CASE("the site search widens when nothing fits, and only then", "[ai]")
    {
        // The island start that was costing the AI its entire game. A 6x6
        // lab needs spacing (6+2)*16 = 128 world units a ring, so a 512
        // budget is four rings; on a small island every one of those rings
        // is in the sea and the planner -- which takes the first want it can
        // SITE, not the first it can afford -- silently skipped the lab. No
        // lab meant no factory, no army, and a side that never left Boom,
        // while the shipyard got built anyway because its siting scans
        // MapIntel globally instead of walking rings. It read as an AI that
        // liked boats.
        //
        // Raising maxMexSearchRadius outright fixed that and cost about 5%
        // on land, losing two games of twenty in one slot of a swapped
        // Painted Desert run, so the widening is conditional instead. Both
        // halves of that are pinned here: it must find the far site when the
        // near rings are empty, and the kill switch must genuinely restore
        // the old refusal.
        //
        // The terrain is land only in a 10x10 tile patch at the origin --
        // bounded on BOTH axes, unlike makeWaterMapTerrain's strip, which is
        // narrow in x but runs the full height of the map and so always has
        // room along z inside 512.
        // World coordinates are CENTRED on the map, so heightmap tile and
        // world position relate as tile = (world + 896) / 16 for this
        // 112-tile map (112 * 16 = 1792 world units, half of it 896).
        // Getting that backwards is what made an earlier version of this
        // fixture put the anchor inside the island it was supposed to be
        // unable to reach.
        Grid<unsigned char> heights(112, 112, static_cast<unsigned char>(0));
        // The anchor's island: tiles 50-59, i.e. world -96 to 64.
        for (int y = 50; y < 60; ++y)
        {
            for (int x = 50; x < 60; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(90));
            }
        }
        // The far island: tiles 2-13, i.e. world -864 to -672.
        //
        // A 6x6 building spaces its rings (6+2)*16 = 128 apart, so a 512
        // budget is four rings reaching 512 world units from the anchor at
        // world -16. Rings 1-4 put their whole footprint in open water; the
        // first candidate whose 6x6 lands entirely on this island is ring 6,
        // 768 out, about 1086 away -- past the budget and well inside the
        // 2048 fallback. Up and to the left of the anchor, because the same
        // island down and to the right sits past the camera's cut-off, where
        // no site is offered (footprintInsideVisibleMap).
        for (int y = 2; y < 14; ++y)
        {
            for (int x = 2; x < 14; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(90));
            }
        }
        MapTerrain terrain(std::move(heights), 60_ss);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        // A 6x6 building that cannot stand in water. The field order is
        // {footprintX, footprintZ, maxSlope, maxWaterSlope, minWaterDepth,
        // maxWaterDepth}, so dry-land-only is a maxWaterDepth of 0 with the
        // slopes left open -- the same shape ReachabilityMap.test.cpp uses
        // for its land mover.
        UnitDefinition labDef;
        labDef.isMobile = false;
        labDef.builder = false;
        labDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["LAB"] = labDef;

        // World -16 is tile 55, the middle of the anchor's island.
        auto anchor = SimVector(-16_ss, 90_ss, -16_ss);
        std::minstd_rand rng(1u);
        BuildManager buildManager;

        SECTION("with the fallback off, the search gives up exactly as it used to")
        {
            auto profile = makeDefaultStandardProfile();
            // Equal, not smaller: the guard is a strict >, so this is the
            // documented way to switch the behaviour off.
            profile.buildSiteFallbackRadius = profile.maxMexSearchRadius;

            REQUIRE_FALSE(buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "LAB", anchor, rng).has_value());
        }

        SECTION("with it on, the far patch is found")
        {
            auto profile = makeDefaultStandardProfile();
            REQUIRE(profile.buildSiteFallbackRadius > profile.maxMexSearchRadius);

            auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "LAB", anchor, rng);
            REQUIRE(site.has_value());

            // It really is the far patch, not somewhere the near rings could
            // have reached -- otherwise the section above would have found it
            // too and this would pin nothing.
            REQUIRE(flatDistanceBetween(*site, anchor) > profile.maxMexSearchRadius);

            // And it is dry: a site in the sea would mean the footprint test
            // is not being applied at the wider radius.
            REQUIRE(sim.terrain.getHeightAt(site->x, site->z) >= sim.terrain.getSeaLevel());
        }
    }

    TEST_CASE("naval: a shipyard goes up on a water map, in water deep enough for it", "[ai]")
    {
        // makeWaterMapTerrain's shelf (tiles 10-13) is water but too shallow
        // for ARMSY's own MinWaterDepth=30; only tiles 14 and beyond clear
        // it. Every existing priority ahead of the naval branch in
        // buildPriorities is zeroed out or pre-satisfied below, so the
        // shipyard is the first thing the commander's one planning pass can
        // actually find a site for and afford -- see BuildManager.cpp's
        // buildPriorities for why that ordering matters.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);
        REQUIRE_FALSE(mapIntel.shipyardSites.empty());

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        REQUIRE(!builds.empty());
        REQUIRE(builds.front().unitType == "ARMSY");

        // The site is one MapIntel actually nominated for its depth, and
        // specifically one on the deep side of the shelf -- not a ring
        // search from the (dry) anchor, which chooseBuildSite would have
        // run and which could never satisfy MinWaterDepth=30 at all.
        auto matched = std::find_if(mapIntel.shipyardSites.begin(), mapIntel.shipyardSites.end(), [&](const NavalSite& s) {
            return s.position.distanceSquared(builds.front().position) < (1_ss * 1_ss);
        });
        REQUIRE(matched != mapIntel.shipyardSites.end());
        REQUIRE(matched->tile.x >= 14);
    }

    TEST_CASE("naval: a water map takes its energy out of the water", "[ai]")
    {
        // The AI had no vocabulary for a water structure at all: AiSideUnits
        // carried the shipyard, the hulls and the construction ship, and not
        // one of the things that stand in the water. On a 92% water map that
        // left it competing with its own factories and extractors for the
        // little dry ground there is, to plant 5x5 solar collectors on it.
        //
        // Shipped numbers, which are what make this worth doing: ARMSOLAR is
        // 5x5, 145 metal, MaxWaterDepth=0. ARMTIDE is 3x3, 82 metal,
        // MinWaterDepth=20. Cheaper, nine cells against twenty-five, and it
        // stands where nothing else wants to.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The tidal generator at its shipped shape. Defined here rather than
        // in defineWorld so that the thirty-odd tests which know nothing
        // about water carry on resolving it to empty and skipping the rule.
        auto tide = makeDef(false, false, false, "", 100u);
        tide.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 20u, 255u};
        tide.buildCostMetal = Metal(82.0f);
        sim.unitDefinitions["ARMTIDE"] = tide;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        // The lab pre-placed, because it is wanted above the water block and
        // would otherwise be the only thing this test ever saw ordered.
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        // The yard and the sonar silenced so the tidal generator is what is
        // left in the water block, not what happens to win a race with them.
        profile.targetShipyardCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        auto tidal = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTIDE"; });
        REQUIRE(tidal != builds.end());

        // And it is IN the water, which is the half that proves the siting
        // rather than the wanting. A tidal generator is exempt from the
        // build-site reachability gate on purpose -- every site it has is
        // ground the builder cannot walk to, so the gate that stops the
        // commander crossing to another island would otherwise refuse the
        // lot. canBeBuiltAt is what keeps it honest, through the same
        // MinWaterDepth test the shipyard sites go through.
        REQUIRE(sim.terrain.getHeightAt(tidal->position.x, tidal->position.z) < sim.terrain.getSeaLevel());
    }

    TEST_CASE("naval: the metal under the water is worth taking once the dry patches are gone", "[ai]")
    {
        // Every submerged metal patch on a water map was worth nothing to the
        // AI, because the only extractor it knew about is MaxWaterDepth=0.
        // ARMUWMEX is the same ExtractsMetal=0.001 for 130 metal instead of
        // 50 -- not better metal, metal that was otherwise unreachable.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The ordinary extractor at its REAL draught. makeDef leaves
        // maxWaterDepth at 255, which would let a 50-metal ARMMEX sit on the
        // seabed and take the patch before the underwater one was ever
        // reached -- the test would pass with none of this code running.
        // ARMMEX is MaxWaterDepth=0 in the shipped data.
        sim.unitDefinitions.at("ARMMEX").movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 0u};

        auto uwmex = makeDef(false, false, false, "", 50u);
        uwmex.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 19u, 255u};
        uwmex.buildCostMetal = Metal(130.0f);
        sim.unitDefinitions["ARMUWMEX"] = uwmex;

        // A patch out in the deep, east of the shelf. Heightmap x >= 14 is
        // height 0 against sea level 60, so the water there is 60 deep --
        // comfortably past ARMUWMEX's MinWaterDepth of 19 and impossible for
        // anything with MaxWaterDepth=0.
        for (int y = 30; y <= 33; ++y)
        {
            for (int x = 20; x <= 23; ++x)
            {
                sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
            }
        }

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingSolarCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.targetShipyardCount = 0;
        profile.targetTidalCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        auto submerged = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMUWMEX"; });
        REQUIRE(submerged != builds.end());

        // On the patch, and under the sea. Both halves matter: the first says
        // it went where the metal is, the second that the reachability gate
        // was skipped rather than the site being dry after all.
        auto tile = sim.terrain.worldToHeightmapCoordinate(submerged->position);
        REQUIRE(sim.metalGrid.get(tile.x, tile.y) > sim.surfaceMetal);
        REQUIRE(sim.terrain.getHeightAt(submerged->position.x, submerged->position.z) < sim.terrain.getSeaLevel());
    }

    TEST_CASE("naval: the torpedo launcher waits for something to shoot at", "[ai]")
    {
        // ARMTL is 804 metal and CORTL 831 -- a destroyer's price for
        // something that cannot move. A wet map is not on its own a reason to
        // spend it; an enemy hull in the water is. This is the difference
        // between a defence worth what it costs and one built because the map
        // looked dangerous, and it is the half of this feature that is a
        // judgement rather than a fact, so it gets pinned.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The torpedo launcher at its shipped shape. MinWaterDepth=1, so it
        // sits in the shallows off a shore rather than out where the shipyard
        // goes.
        auto tl = makeDef(false, false, false, "LASER", 200u);
        tl.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 1u, 255u};
        tl.buildCostMetal = Metal(804.0f);
        sim.unitDefinitions["ARMTL"] = tl;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.targetShipyardCount = 0;
        profile.targetTidalCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 1;
        // knownEnemies is what the gate reads, and omniscience is what fills
        // it without waiting for a scout to fly. makeDefaultStandardProfile
        // leaves it off -- only the brutal profile sets it -- so it is set
        // here by hand.
        profile.cheatModeOmniscient = true;

        SECTION("a wet map on its own does not earn one")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::none_of(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTL"; }));
        }

        SECTION("an enemy hull in the water does")
        {
            // A destroyer, out in the deep. ARMROY is mobile, does not fly,
            // and its movement class has MinWaterDepth=12 -- which is the
            // whole test: a movement class with a minimum water depth can
            // only float.
            addUnit(sim, "ARMROY", human, SimVector(200_ss, 0_ss, 0_ss), script);

            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::any_of(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTL"; }));
        }
    }

    TEST_CASE("naval: the shipyard comes before the rest of the base on a water map", "[ai]")
    {
        // The ordering test that the siting test above deliberately is not.
        // That one zeroes every competing priority so the yard is the only
        // thing the planner can reach, which answers "can it find water deep
        // enough" and says nothing at all about WHEN a yard is wanted. This
        // one leaves the competition standing -- radar, the towers, the air
        // plant, the vehicle plant, the advanced lab, and the run up to ten
        // solars -- and asserts the yard beats all of it.
        //
        // That matters because the yard used to sit thirteenth in
        // buildPriorities, which is why a fleet of nine was still a fleet of
        // five when the game ended: on a map where navalFleetTarget is
        // non-zero the yard is what the lab is on land, the factory that
        // makes the only units able to reach the enemy at all.
        //
        // The opening economy is pre-built rather than waited for. The
        // opening extractors and solars are listed ABOVE the first lab, and
        // the yard sits directly below it, so with the opening unmet the
        // first want is an extractor and the ordering under test is never
        // reached.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The dry strip is heightmap x in [0, 10), which is world x -512 to
        // -352, so everything below stands on land.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, 48_ss), script);
        }
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, -48_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        // The premise of the whole test: a zero here is the naval kill switch
        // and every assertion below would pass or fail for the wrong reason.
        REQUIRE(profile.navalFleetSize > 0);
        // The three owned extractors already meet this. Left at its default of
        // eight, the "don't sink the commander into a factory while metal is
        // short" rule sits ABOVE the lab and would ask for a fourth extractor
        // first -- pre-empting the ordering under test and failing the test
        // for a reason that has nothing to do with it. The siting test above
        // never met this only because it set the target to zero.
        profile.targetMetalExtractorCount = 3;

        SECTION("the yard is what the commander lays down next")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMSY");
        }

        SECTION("earlyShipyard=0 restores the old ordering exactly")
        {
            // The knob exists for this: a code change cannot be measured by
            // the arena, whose control arm runs the same binary and would
            // contain the change too. Something else has to come first here,
            // or "-tune earlyShipyard=0" is not a control at all.
            profile.earlyShipyard = false;

            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType != "ARMSY");
        }
    }

    TEST_CASE("naval: no shipyard is ever wanted on a land map", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto mapIntel = analyseMap(makeFlatTerrain(), {});
        REQUIRE(mapIntel.character == MapCharacter::Land);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 61, commands);

        auto types = buildOrderTypes(commands);
        REQUIRE(std::find(types.begin(), types.end(), "ARMSY") == types.end());
    }

    TEST_CASE("naval: navalFleetSize=0 restores today's behaviour exactly", "[ai]")
    {
        // The same water map and the same neutered priority list as "a
        // shipyard goes up on a water map", except for the one knob this
        // is about. Everything else held equal, flipping it back to zero
        // is what has to be the only thing standing between a shipyard and
        // none at all.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.navalFleetSize = 0; // the kill switch

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto types = buildOrderTypes(commands);
        REQUIRE(std::find(types.begin(), types.end(), "ARMSY") == types.end());
    }

    TEST_CASE("naval: a shipyard builds a scout ship, then destroyers", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("eyes first")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMPT") == 1);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 0);
        }

        SECTION("then destroyers, once eyes are covered")
        {
            addUnit(sim, "ARMPT", ai, SimVector(50_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }
    }

    TEST_CASE("naval: a yard whose hulls are shot as they are born stops feeding them in", "[ai]")
    {
        // The all-water soft-lock, in miniature. A nanoframe spawns with
        // zero hit points -- they are trunc(progress * maxdamage) -- so any
        // damage at all kills it, and one enemy scout ship parked off a
        // shipyard therefore destroys every hull the yard makes, for the
        // whole game, at a hundredth of what it costs us. Measured over ten
        // games on Brain Coral: 1910 quiet deaths, every one a weapon kill,
        // 1408 of them ARMROY frames killed by a single CORPT. See ROADMAP
        // Phase 2 and commit 42160d59 for the diagnosis.
        //
        // Nothing could notice, which is what made it a soft-lock rather
        // than a loss: recentLosses is diffed from standingBuildings and so
        // holds buildings only, so a whole production run could be
        // destroyed without a word, and enemiesNearBase was measured from
        // the base anchor rather than from the thing being shot.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The base stands on the dry strip in the west and the yard is off
        // the far corner: 1160 units between the commander and the gun,
        // against a defendRadius of 900. That gap is the point of the
        // fixture -- it is what makes the base anchor the wrong place to
        // measure this from, which is where the old code measured from.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, -400_ss), script);
        auto yardId = addUnit(sim, "ARMSY", ai, SimVector(400_ss, 60_ss, 400_ss), script);
        // Eyes already covered, so the yard's next job is a destroyer.
        addUnit(sim, "ARMPT", ai, SimVector(360_ss, 60_ss, 400_ss), script);
        auto harasserId = addUnit(sim, "ARMPT", human, SimVector(440_ss, 60_ss, 380_ss), script);

        // The hull on the slipway. A frame, which is to say no hit points.
        auto frameId = addUnit(sim, "ARMROY", ai, SimVector(430_ss, 60_ss, 400_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        SECTION("the loss is remembered, the siege is seen, and the queue is left alone")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            const auto& bb = controller.getBlackboard();
            std::vector<PlayerCommand> commands;

            // The first pass has nothing to diff against, exactly as the
            // building side does not.
            runTicks(sim, controller, 2, commands);
            REQUIRE(bb.standingUnits.count(frameId.value) == 1);
            REQUIRE(bb.recentUnitLosses.empty());

            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 2, commands);

            REQUIRE(bb.recentUnitLosses.size() == 1);
            REQUIRE(bb.recentUnitLosses.front().unitType == "ARMROY");
            REQUIRE(bb.recentUnitLosses.front().underConstruction);
            REQUIRE(bb.harassedFactories == std::vector<UnitId>{yardId});
            REQUIRE(bb.besiegedFactories == std::vector<UnitId>{yardId});

            // And the gun counts as an enemy at the base although the base
            // anchor is nowhere near it, which is what lets everything
            // hanging off enemiesNearBase answer at all.
            REQUIRE(std::find(bb.enemiesNearBase.begin(), bb.enemiesNearBase.end(), harasserId) != bb.enemiesNearBase.end());

            // Nothing more is handed to the gun. The yard is not told to
            // stop -- it is simply not topped up while the gun is there.
            std::vector<PlayerCommand> later;
            runTicks(sim, controller, 90, later);
            REQUIRE(countQueueCommands(later, "ARMROY") == 0);
            REQUIRE(countQueueCommands(later, "ARMPT") == 0);
        }

        SECTION("with the knob off, the old behaviour exactly")
        {
            profile.noticeProductionHarassment = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            const auto& bb = controller.getBlackboard();
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 2, commands);

            // The memory itself is unconditional -- it costs a map diff and
            // nothing reads it unless the knob is on -- but nothing is done
            // with it, and a gun 1160 units from the anchor is invisible to
            // every defensive rule the AI has.
            REQUIRE(bb.recentUnitLosses.size() == 1);
            REQUIRE(bb.besiegedFactories.empty());
            REQUIRE(bb.enemiesNearBase.empty());

            std::vector<PlayerCommand> later;
            runTicks(sim, controller, 90, later);
            REQUIRE(countQueueCommands(later, "ARMROY") >= 1);
        }

    }

    TEST_CASE("naval: a yard goes back to work once the gun has gone", "[ai]")
    {
        // The pause is not a shutdown. The memory of the frames lost there
        // lasts UnitLossMemoryTicks, but the live half -- an armed enemy
        // actually sitting on the place -- does not wait for that to age
        // out, so the yard resumes the moment the gun dies or leaves.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMPT", ai, SimVector(-60_ss, 60_ss, 0_ss), script);
        auto harasserId = addUnit(sim, "ARMPT", human, SimVector(60_ss, 60_ss, 0_ss), script);
        auto frameId = addUnit(sim, "ARMROY", ai, SimVector(30_ss, 60_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        const auto& bb = controller.getBlackboard();
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);
        sim.getUnitState(frameId).markAsDead();
        runTicks(sim, controller, 2, commands);
        REQUIRE(bb.besiegedFactories.size() == 1);

        sim.getUnitState(harasserId).markAsDead();
        std::vector<PlayerCommand> after;
        runTicks(sim, controller, 90, after);
        REQUIRE(bb.besiegedFactories.empty());
        // The memory is still there; it is the live half that cleared.
        REQUIRE(!bb.recentUnitLosses.empty());
        REQUIRE(countQueueCommands(after, "ARMROY") >= 1);
    }

    TEST_CASE("the commander answers a besieged production site, but only when asked", "[ai]")
    {
        // The third candidate from the diagnosis, and the one that is off
        // by default: the commander is the base's whole build capacity and
        // the game's loss condition, and on the map this is for it is as
        // likely to end up walking at water it cannot cross as at the gun.
        // It fires only with nothing else to send -- which on a besieged
        // water map is the usual state of affairs, since everything that
        // would otherwise go is dying as a frame before it can move.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        auto harasserId = addUnit(sim, "ARMPT", human, SimVector(-200_ss, 60_ss, 0_ss), script);
        auto frameId = addUnit(sim, "ARMROY", ai, SimVector(-260_ss, 60_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        // There is a second way to send the commander at an intruder --
        // commanderDefendsAloneMaxIntruders, which is on by default -- and the
        // harasser here answers it too. Off, so the sortie this test is about
        // is the only one that can appear.
        profile.commanderDefendsAloneMaxIntruders = 0;

        SECTION("asked")
        {
            profile.commanderAnswersHarassment = true;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 60, commands);
            REQUIRE(controller.getBlackboard().besiegedFactories.size() == 1);
            auto attacks = ordersFor<AttackOrder>(commands, commanderId);
            REQUIRE(!attacks.empty());
            REQUIRE(std::get<UnitId>(attacks.front().target) == harasserId);
        }

        SECTION("not asked")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 60, commands);
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("nothing at all is built under an enemy gun, not just an extractor", "[ai]")
    {
        // What the all-water games actually lost, which is not what the
        // shipyard loses: two commanders replacing tidal generators under
        // an enemy scout ship, 286 and 232 units in one game. The extractor
        // search has refused a patch under guns since the Crystal Maze
        // measurement, but a frame has no hit points whatever it is going
        // to become, so the rule was never really about extractors.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        auto gunId = addUnit(sim, "ARMPT", human, SimVector(-330_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        // Off, so that only the general rule can rule a site out: an
        // extractor refused by the extractor search would prove nothing
        // about anything else.
        profile.mexAvoidsEnemyGunsRadius = 0_ss;
        // The gun is an intruder, and commanderDefendsAloneMaxIntruders is on
        // by default, so the commander would be sent at it instead of
        // building. What this test measures is where the building goes.
        profile.commanderDefendsAloneMaxIntruders = 0;
        // And the gun is inside commanderDangerRadius, so the commander would
        // run rather than build; this is the same switch the tests above use.
        profile.commanderDangerRadius = 0_ss;
        // builderAvoidsContestedRadius refuses any site near an armed enemy
        // too, which would leave the control below with nothing to find.
        profile.builderAvoidsContestedRadius = 0_ss;

        const auto& gunPosition = sim.getUnitState(gunId).position;
        auto radiusSquared = profile.productionHarassRadius * profile.productionHarassRadius;

        SECTION("every site the planner issues is out of its reach")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!orders.empty());
            for (const auto& order : orders)
            {
                REQUIRE(gunPosition.distanceSquared(order.position) > radiusSquared);
            }
        }

        SECTION("and with the knob off, at least one is not")
        {
            profile.noticeProductionHarassment = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!orders.empty());
            auto underGuns = std::count_if(orders.begin(), orders.end(), [&](const BuildOrder& o) {
                return gunPosition.distanceSquared(o.position) <= radiusSquared;
            });
            REQUIRE(underGuns > 0);
        }
    }

    TEST_CASE("naval: a yard on a map with metal under the sea makes construction ships", "[ai]")
    {
        // Nothing ever queued one: the slot resolved, and its only reader was
        // a movement-class lookup. On a map with no dry ground there is no lab
        // either, so the commander was the only builder the AI would ever own.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto armcs = makeDef(false, true, true, "", 200u);
        armcs.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
        sim.unitDefinitions["ARMCS"] = armcs;
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
        // Eyes already covered, so the yard's next job is what comes after them.
        addUnit(sim, "ARMPT", ai, SimVector(50_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("with metal under the water, a builder comes before the warships")
        {
            // Open water east of the shelf, sixty deep.
            sim.metalGrid.set(40, 32, static_cast<unsigned char>(200));
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 0);
        }

        SECTION("with no metal under the water, the yard goes to warships as it always did")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }

        SECTION("switched off, it does not, even with metal to take")
        {
            sim.metalGrid.set(40, 32, static_cast<unsigned char>(200));
            profile.targetConstructionShipCount = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }
    }

    TEST_CASE("the lab stops making kbots for ground they cannot reach", "[ai]")
    {
        // makeChannelTerrain is the fixture that actually produces
        // hasUnreachableGround -- the ferry test above asserts it on this
        // same terrain -- but only once the kbots cannot wade the channel,
        // which is what the movement class override below is for. Without
        // it the far bank is reachable on foot and the cap never engages.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        // Three of them, so armySize clears a cap of two.
        for (auto z : {0_ss, 30_ss, 60_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(-200_ss, 60_ss, z), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        // Straight to the raider branch: the lab makes its constructors first
        // and would otherwise never reach the units under test.
        profile.targetConstructorCount = 0;

        // The branch picks rocket kbots here, not raiders -- with three
        // raiders owned and no rocket kbots, "raiders <= rockets * 2" is
        // false -- so both are counted, or the test would pass for the wrong
        // reason.
        auto kbotsQueued = [](const std::vector<PlayerCommand>& commands) {
            return countQueueCommands(commands, "ARMPW") + countQueueCommands(commands, "ARMROCK");
        };

        // The cap asks how much of the map is water as well as whether some
        // ground is out of reach, so the map has to say so. The channel
        // fixture supplies the unreachable ground and this supplies the
        // water: they are separate inputs, because MapIntel is computed from
        // the map at load and handed to the AI rather than derived from the
        // terrain here.
        //
        // A default-constructed MapIntel is not merely low on water, it is
        // invalid, and that keeps the cap off outright -- the same
        // conservative reading navalFleetTarget already takes. Without the
        // map's own analysis the AI does not know it is on an island map, so
        // it does not behave as though it were.
        MapIntel islandMap;
        islandMap.valid = true;
        islandMap.character = MapCharacter::Water;
        islandMap.waterFraction = 0.92f;

        SECTION("an army below the cap keeps being made")
        {
            // Off by default, so the lab keeps working. The default was
            // briefly 12 above isolatedLandArmyCapMinWaterFraction and was
            // measured back to zero the same day -- the cap halved the land
            // army and bought no hulls with the savings. What this pins holds
            // either way: an uncapped lab, or an army under the cap, goes on
            // being made.
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().hasUnreachableGround);
            REQUIRE(kbotsQueued(commands) >= 1);
        }

        SECTION("a map that is merely half water does not earn the cap")
        {
            // The gate, and the reason it is deliberately not
            // MapCharacter::Water: that threshold is 0.40, which would take
            // in Coast To Coast at 54% -- the one map where capping was
            // measurably a regression, army 29.5 against 87.1 and income 9.8
            // against 11.2 over twenty games. So a half-water map keeps its
            // land army even with the cap set and the army over it.
            MapIntel halfWater;
            halfWater.valid = true;
            halfWater.character = MapCharacter::Water;
            halfWater.waterFraction = 0.54f;

            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, halfWater);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.hasUnreachableGround);
            REQUIRE(bb.armySize >= 3);
            REQUIRE(kbotsQueued(commands) >= 1);
        }

        SECTION("with the cap set, the lab goes quiet and the income is freed")
        {
            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.hasUnreachableGround);
            REQUIRE(bb.armySize >= 3);
            REQUIRE(kbotsQueued(commands) == 0);
        }

        SECTION("the vehicle plant is capped too, not just the kbot lab")
        {
            // The cap guarded one of the three branches that make land combat
            // units, and a knob that stops the lab while the plant beside it
            // goes on making tanks has not capped anything. Pinned here
            // because every other assertion in this test would still pass
            // with the plant's guard removed.
            addUnit(sim, "ARMVP", ai, SimVector(-250_ss, 60_ss, 60_ss), script);
            profile.targetScoutVehicleCount = 0;

            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().hasUnreachableGround);
            REQUIRE(countQueueCommands(commands, "ARMFLASH") == 0);
            REQUIRE(kbotsQueued(commands) == 0);
        }
    }

    TEST_CASE("naval: an advanced shipyard builds cruisers, then a battleship behind them", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Shipped footprints, draughts and prices.
        auto armasy = makeDef(false, true, false, "", 200u);
        armasy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        armasy.buildCostMetal = Metal(2524.0f);
        sim.unitDefinitions["ARMASY"] = armasy;
        auto armcrus = makeDef(false, false, true, "LASER", 300u);
        armcrus.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 30u, 255u};
        armcrus.buildCostMetal = Metal(1719.0f);
        sim.unitDefinitions["ARMCRUS"] = armcrus;
        auto armbats = armcrus;
        armbats.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 30u, 255u};
        armbats.buildCostMetal = Metal(4404.0f);
        sim.unitDefinitions["ARMBATS"] = armbats;
        auto armaas = armcrus;
        armaas.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 30u, 255u};
        armaas.buildCostMetal = Metal(1358.0f);
        sim.unitDefinitions["ARMAAS"] = armaas;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetAdvancedShipyardCount = 1;

        SECTION("the hull with the depth charge first")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMBATS") == 0);
        }

        SECTION("a battleship once two cruisers stand to escort it")
        {
            addUnit(sim, "ARMCRUS", ai, SimVector(120_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(120_ss, 60_ss, 120_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMBATS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 0);
        }

        SECTION("and cover before either, once something of theirs is flying")
        {
            auto bomber = makeDef(false, false, true, "LASER", 200u);
            bomber.canFly = true;
            sim.unitDefinitions["ARMTHUND"] = bomber;
            addUnit(sim, "ARMTHUND", human, SimVector(60_ss, 140_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(countQueueCommands(commands, "ARMAAS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 0);
        }

        SECTION("no anti-air ship against an empty sky")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMAAS") == 0);
        }
    }

    TEST_CASE("naval: with no ground for an air plant, the yard builds the sub that builds the seaplane platform", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto yard = makeDef(false, true, false, "", 200u);
        yard.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMASY"] = yard;
        auto platform = yard;
        platform.movementCollisionInfo = UnitDefinition::AdHocMovementClass{7u, 7u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMPLAT"] = platform;
        auto hull = makeDef(false, false, true, "LASER", 300u);
        hull.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMCRUS"] = hull;
        sim.unitDefinitions["ARMBATS"] = hull;
        auto sub = makeDef(false, true, true, "", 200u);
        sub.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 20u, 255u};
        sim.unitDefinitions["ARMACSUB"] = sub;
        auto plane = makeDef(false, false, true, "LASER", 300u);
        plane.canFly = true;
        sim.unitDefinitions["ARMSEAP"] = plane;
        sim.unitDefinitions["ARMSFIG"] = plane;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("the sub, once two cruisers stand")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 160_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACSUB") == 1);
        }

        SECTION("not where an air plant already stands")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 160_ss), script);
            addUnit(sim, "ARMAP", ai, SimVector(-420_ss, 90_ss, 200_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACSUB") == 0);
        }

        SECTION("and the platform builds torpedo seaplanes against an empty sky")
        {
            addUnit(sim, "ARMPLAT", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMSEAP") == 1);
            REQUIRE(countQueueCommands(commands, "ARMSFIG") == 0);
        }
    }

    TEST_CASE("naval: storage and the reactor go under the water", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // The fixture's kbot wades any depth, which makes the whole sea part of
        // the base and every ship a builder standing at home. A real one stops
        // at the shore, and then a ship plans as the outpost builder it is.
        sim.unitDefinitions["ARMCK"].movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 10u};
        auto afloat = makeDef(false, true, true, "", 200u);
        afloat.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
        sim.unitDefinitions["ARMCS"] = afloat;
        sim.unitDefinitions["ARMACSUB"] = afloat;
        auto sunk = makeDef(false, false, false, "", 50u);
        sunk.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMUWMS"] = sunk;
        sim.unitDefinitions["ARMUWES"] = sunk;
        sim.unitDefinitions["ARMUWFUS"] = sunk;
        auto yard = makeDef(false, true, false, "", 200u);
        yard.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMASY"] = yard;

        // The commander is kept walking, so the one job a planning pass hands
        // out goes to the builder under test.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-420_ss, 90_ss, 400_ss)));
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.navalTechMinMetalIncome = 0;
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(1000.0f);

        auto tree = makeBuildTree();

        SECTION("a construction ship with a full store builds underwater metal storage")
        {
            auto shipId = addUnit(sim, "ARMCS", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMCS"] = {"ARMUWMS", "ARMUWES"};
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, shipId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMUWMS");
        }

        SECTION("not with storage switched off")
        {
            auto shipId = addUnit(sim, "ARMCS", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMCS"] = {"ARMUWMS", "ARMUWES"};
            profile.targetMetalStorageCount = 0;
            profile.targetEnergyStorageCount = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            REQUIRE(ordersFor<BuildOrder>(commands, shipId).empty());
        }

        SECTION("the advanced construction sub builds the underwater fusion plant")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 300_ss), script);
            auto subId = addUnit(sim, "ARMACSUB", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMACSUB"] = {"ARMUWFUS"};
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, subId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMUWFUS");
        }
    }

    TEST_CASE("naval: a warship is never sent inland", "[ai]")
    {
        // Without MapIntel::sameWaterBody filtering the target, the land
        // army's plain "anything within engageRadius gets shot at" rule
        // would fire on this enemy: it is the only known contact and well
        // inside the default 450-unit range. With it, a hull afloat and an
        // enemy standing on dry land are never on the same body of water,
        // so nothing here ever asks the destroyer to fire on it or walk to
        // it.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-450_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(std::find(bb.navalCombatUnits.begin(), bb.navalCombatUnits.end(), destroyerId) != bb.navalCombatUnits.end());
        REQUIRE(std::find(bb.combatUnits.begin(), bb.combatUnits.end(), destroyerId) == bb.combatUnits.end());
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);

        REQUIRE(ordersFor<AttackOrder>(commands, destroyerId).empty());
        for (const auto& order : ordersFor<MoveOrder>(commands, destroyerId))
        {
            // Never further landward than the shore itself (tile 10, world
            // x -352): a ship allowed to walk to bb.rallyPoint or
            // bb.attackTarget -- both on the dry strip -- would fail this.
            REQUIRE(order.destination.x.value > -352.0f);
        }
    }

    TEST_CASE("naval: a hull whose shots are not landing moves round to deep water with a clear run in", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Shots that arrive and do nothing, which is what a torpedo into a
        // shelf amounts to: the target's hit points never move.
        sim.weaponDefinitions["LASER"].damage["DEFAULT"] = 0u;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 300_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(200_ss, 0_ss, 300_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // At the foot of the shelf, with the shallows and the shore right behind it:
        // half the bearings round it are no place for a hull.
        auto targetId = addUnit(sim, "ARMSOLAR", human, SimVector(-270_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.commanderDangerRadius = 0_ss;

        SECTION("after navalStalledAttackSeconds of nothing")
        {
            profile.navalStalledAttackSeconds = 2;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 30, commands);
            REQUIRE(ordersFor<MoveOrder>(commands, destroyerId).empty());

            runTicks(sim, controller, 3 * static_cast<int>(SimTicksPerSecond), commands);
            auto moves = ordersFor<MoveOrder>(commands, destroyerId);
            REQUIRE(!moves.empty());
            const auto& spot = moves.front().destination;
            // Deep water, not the shelf (tiles 10-14, world x below -288) and not the shore.
            REQUIRE(spot.x.value > -288.0f);
            auto ground = sim.terrain.tryGetHeightAt(spot.x, spot.z);
            REQUIRE(ground.has_value());
            REQUIRE(ground->value <= sim.terrain.getSeaLevel().value - 25.0f);
            // And within a torpedo's reach of the target.
            const auto& target = sim.getUnitState(targetId);
            auto dx = spot.x.value - target.position.x.value;
            auto dz = spot.z.value - target.position.z.value;
            REQUIRE((dx * dx) + (dz * dz) <= 345.0f * 345.0f);
        }

        SECTION("switched off, it fires on from where it is")
        {
            profile.navalStalledAttackSeconds = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4 * static_cast<int>(SimTicksPerSecond), commands);
            REQUIRE(ordersFor<MoveOrder>(commands, destroyerId).empty());
        }
    }

    TEST_CASE("a warship goes looking for an enemy the AI has not found")
    {
        // The counterpart of the section above. That one pins that a hull is
        // never sent inland; this one pins that a hull is sent out at all.
        //
        // Nothing that walks can find an enemy across water, so on an island
        // map knownEnemies stays empty for the whole game -- and the Attack
        // phase wants (enemyBasePosition || !knownEnemies.empty()) as well as
        // the army size, so it never fires and every army ferry stays
        // switched off with it. Measured on Hundred Isles before this: a side
        // holding a scout ship, three destroyers and a transport never saw
        // the enemy once in nine hundred seconds.
        //
        // scoutCount is left at its default ON PURPOSE. The naval sections
        // above set it to zero, and ScoutManager::update returns immediately
        // when it is -- before any scout is chosen -- so a test that copied
        // that line would pass without executing one line of what it claims
        // to pin.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // No enemy unit anywhere: knownEnemies stays empty, which is the
        // condition the borrow is gated on.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        profile.scoutTickInterval = 1;

        SECTION("the hull is borrowed, and sent somewhere it can actually float")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.knownEnemies.empty());
            REQUIRE(bb.navalScoutUnitId);
            REQUIRE(*bb.navalScoutUnitId == destroyerId);

            auto moves = ordersFor<MoveOrder>(commands, destroyerId);
            REQUIRE(!moves.empty());
            for (const auto& order : moves)
            {
                // East of the shore at world x -352, so on water. A scout
                // target chosen off the ground layer would sit on the dry
                // strip and the ship would never arrive -- the same
                // wrong-mover mistake the commander's own layer exists for.
                REQUIRE(order.destination.x.value > -352.0f);
            }
        }

        SECTION("navalScouting=false leaves the fleet alone")
        {
            profile.navalScouting = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            // No borrow. Deliberately NOT asserting the destroyer receives no
            // move at all: with the knob off it is an ordinary hull again and
            // updateNavy is free to station it, which is the point of the
            // switch.
            REQUIRE_FALSE(controller.getBlackboard().navalScoutUnitId);
        }
    }

    TEST_CASE("a sea transport finds the landing on the far shore", "[ai]")
    {
        // The army ferry is the only caller of the landing search, and on
        // this map the walk-back from the target runs west, through negative
        // world x. That is ordinary map space -- the world is centred, so it
        // spans -512..512 here -- but the search used to test its candidates
        // against 0..width and read the whole western half as off the map.
        // The walk-back gave up at its first step, and the fallback's water
        // probes were skipped for the same reason, so the ferry was refused
        // outright: "army ferry blocked, no landing near ... (sea)".
        auto script = makeEmptyCobScript();
        auto terrain = makeTwoShoresTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // Nothing that walks may cross 60-deep water. makeDef leaves
        // maxWaterDepth at 255, which would wade the whole ocean and leave
        // the enemy shore perfectly reachable -- no ferry would ever be
        // wanted and this test would pin nothing.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }
        // The side's sea transport at its shipped shape -- 6x6,
        // MinWaterDepth=12, twenty slots, transport size 3 (AiSideUnits.h).
        // Defined here rather than in defineWorld deliberately: resolving
        // seaTransport promotes it ahead of the destroyer as the mover the
        // naval layer is flooded for, which would quietly relabel the map
        // under the two naval cases above.
        auto tship = makeDef(false, false, true, "", 200u);
        tship.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 12u, 255u};
        tship.transportCapacity = 20;
        tship.transportSize = 3;
        tship.buildCostMetal = Metal(919.0f);
        sim.unitDefinitions["ARMTSHIP"] = tship;

        addUnit(sim, "ARMCOM", ai, SimVector(400_ss, 90_ss, 0_ss), script);
        // Both hulls sit where their whole FOOTPRINT is afloat. Components
        // are labelled by a footprint top-left tile, so an 8x8 shipyard at
        // tile 50 spans 50-57 and runs onto the island at 54; anchoring the
        // naval layer there homed it on a single tile and left every real
        // water tile unreachable. Water is tiles 10-53, so the shipyard
        // starts at 44 (world 192) and the transport at 46 (world 224).
        addUnit(sim, "ARMSY", ai, SimVector(192_ss, 0_ss, 0_ss), script);
        auto transportId = addUnit(sim, "ARMTSHIP", ai, SimVector(224_ss, 0_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(400_ss, 90_ss, z), script);
        }
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        // Keep the phase in Attack for the length of the test: with waves on
        // it flips back to Boom the moment the wave is spent, and the ferry
        // is only considered in Attack.
        profile.attackInWaves = false;
        // Never fall back to Boom. EconomyManager drops ferry passengers
        // from combatUnits, so the moment the army is aboard armySize is 0
        // and the phase would leave Attack -- taking the ferry branch with
        // it -- before the assertions below ever run. The drop is correct
        // behaviour; it just must not happen inside the measurement window.
        profile.retreatArmySize = 0;
        // The enemy is far across the water, and defendRadius is the only gate
        // on enemiesNearBase (PerceptionManager) -- which puts the AI in
        // Defend ahead of the whole phase switch, where the ferry is
        // never considered at all.
        profile.defendRadius = 200_ss;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 40, commands);

        const auto& bb = controller.getBlackboard();
        // Guards first, so a refusal for some unrelated reason cannot pass
        // itself off as the behaviour under test. navalLandingNear returns
        // nothing immediately when the naval layer was never built, and the
        // ferry is not considered at all outside the Attack phase.
        REQUIRE(controller.getReachabilityMap().isNavalValid());
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);
        REQUIRE(bb.phase == GamePhase::Attack);
        REQUIRE(bb.enemyAcrossWater);
        REQUIRE(std::find(bb.transports.begin(), bb.transports.end(), transportId) != bb.transports.end());

        auto unloads = ordersFor<UnloadOrder>(commands, transportId);
        REQUIRE(!unloads.empty());
        // Set down on the enemy's own shore, west of the water's edge.
        REQUIRE(unloads.front().destination.x < -352_ss);
        REQUIRE(!ordersFor<LoadOrder>(commands, transportId).empty());
        REQUIRE(controller.getTransportManager().getFerries().count(transportId.value) == 1);
    }

    TEST_CASE("an air ferry sets the army down short of the target", "[ai]")
    {
        // The other half of the same fault, on the other side of the
        // canFly branch. Here the walk-back has somewhere to stop -- the
        // near bank of the channel -- and the point of walking back at all
        // is to land the army short of the enemy rather than on top of it.
        // Reading negative world x as off the map broke the walk at its
        // first step and left the unguarded fallback to set the army down
        // on the target itself.
        auto script = makeEmptyCobScript();
        auto terrain = makeChannelTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The channel is 30 deep; a kbot stopping at 20 cannot wade it.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // Base on the east bank, enemy on the west bank, channel between at
        // world x -64..64.
        addUnit(sim, "ARMCOM", ai, SimVector(300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(280_ss, 60_ss, 40_ss), script);
        auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(260_ss, 120_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(300_ss, 60_ss, z), script);
        }
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-300_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        profile.attackInWaves = false;
        // Never fall back to Boom. EconomyManager drops ferry passengers
        // from combatUnits, so the moment the army is aboard armySize is 0
        // and the phase would leave Attack -- taking the ferry branch with
        // it -- before the assertions below ever run. The drop is correct
        // behaviour; it just must not happen inside the measurement window.
        profile.retreatArmySize = 0;
        // The enemy is far across the water, and defendRadius is the only gate
        // on enemiesNearBase (PerceptionManager) -- which puts the AI in
        // Defend ahead of the whole phase switch, where the ferry is
        // never considered at all.
        profile.defendRadius = 200_ss;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 40, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);
        REQUIRE(bb.phase == GamePhase::Attack);
        REQUIRE(bb.enemyAcrossWater);
        REQUIRE(std::find(bb.transports.begin(), bb.transports.end(), atlasId) != bb.transports.end());

        auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
        REQUIRE(!unloads.empty());
        // Short of the enemy, on the west bank but well back from the
        // target at -300: the first step of the walk-back lands near -108.
        REQUIRE(unloads.front().destination.x > -200_ss);
        REQUIRE(unloads.front().destination.x < -64_ss);
    }

    TEST_CASE("a fleet can call the attack, but only when asked to", "[ai]")
    {
        // The phase machine counted armySize, which counts combatUnits, and
        // hulls are deliberately not in those -- so a side whose whole
        // strength was afloat stayed in Boom for ever. The phase gates the
        // army ferry, so on an island map that alone stopped the ferry.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        // A shipyard, so the opening is over and the phase reaches Boom.
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        // Three hulls and not one land unit that fights.
        for (auto z : {0_ss, 40_ss, 80_ss})
        {
            addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, z), script);
        }
        // Something to attack, so the target half of the gate is satisfied
        // either way and only the strength half is under test. Far enough
        // out not to count as an enemy at the door.
        addUnit(sim, "ARMPW", human, SimVector(-450_ss, 0_ss, 300_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.defendRadius = 200_ss;

        SECTION("off by default, so a fleet alone does not call one")
        {
            profile.navalScouting = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.navalCombatUnits.size() == 3);
            REQUIRE(bb.armySize == 0);
            REQUIRE(bb.phase != GamePhase::Attack);
        }

        SECTION("with the knob set, the fleet is enough on its own")
        {
            profile.navalScouting = false;
            profile.attackNavalSize = 3;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.armySize == 0);
            REQUIRE(bb.phase == GamePhase::Attack);
        }
    }
}
