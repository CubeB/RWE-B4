#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * The commander, the one unit whose loss ends the game, and so the one with
  * a behaviour of its own.
 *
 * Split out of AiBehaviour.test.cpp, which was 6,906 lines over 58 commits.
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    TEST_CASE("the commander answers a lone raider when there is nothing else to send", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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

    TEST_CASE("the commander does not walk off a young frame when the army can take the fight", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(50.0f);
        sim.unitDefinitions["ARMCK"].canReclamate = true;
        // Armed, as a real one is: a commander going out to fight is cover
        // for whoever it leaves on the frame.
        sim.unitDefinitions["ARMCOM"].weapon1 = "LASER";
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
        // A tower frame a twentieth built, the commander on it: left, it
        // would rot in a few ticks.
        auto frameId = addUnit(sim, "ARMLLT", ai, SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 5u;
        sim.getUnitState(commanderId).orders.push_back(RepairOrder(frameId));
        // Two raiders coming, a hundred metal between them.
        addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 60_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("three kbots of ours near: it stays and lets them fight")
        {
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-150.0f + i * 40.0f), 0_ss, -150_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(controller.getBlackboard().commanderInDanger);
            REQUIRE(ordersFor<AttackOrder>(commands, commanderId).empty());
            REQUIRE(ordersFor<DgunOrder>(commands, commanderId).empty());
            REQUIRE(controller.getBlackboard().commanderKeptFrame == std::optional<UnitId>(frameId));
        }

        SECTION("nobody to fight: it goes, hands the frame to an idle builder, and is told to come back to it")
        {
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-120_ss, 0_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE_FALSE(ordersFor<AttackOrder>(commands, commanderId).empty());
            auto back = ordersFor<RepairOrder>(commands, commanderId);
            REQUIRE(std::any_of(back.begin(), back.end(), [&](const RepairOrder& o) { return o.target == frameId; }));
            auto handed = ordersFor<RepairOrder>(commands, kbotId);
            REQUIRE(std::any_of(handed.begin(), handed.end(), [&](const RepairOrder& o) { return o.target == frameId; }));
        }

        SECTION("a frame that would keep a while: it goes, and nobody is sent")
        {
            sim.unitDefinitions["ARMLLT"].buildCostEnergy = Energy(20000.0f);
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-120_ss, 0_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE_FALSE(ordersFor<AttackOrder>(commands, commanderId).empty());
            auto handed = ordersFor<RepairOrder>(commands, kbotId);
            REQUIRE(std::none_of(handed.begin(), handed.end(), [&](const RepairOrder& o) { return o.target == frameId; }));
        }

        SECTION("switched off: it goes whatever is near")
        {
            profile.commanderKeepsFrames = false;
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-150.0f + i * 40.0f), 0_ss, -150_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE_FALSE(ordersFor<AttackOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("an idle commander helps finish a frame before it goes looking for rocks", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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

    TEST_CASE("the D-gun shot goes to what is worth the charge", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMCOM"].canDgun = true;
        sim.unitDefinitions["ARMCOM"].weapon3 = "DGUN";
        WeaponDefinition dgun{};
        dgun.maxRange = 300_ss;
        dgun.energyPerShot = Energy(400.0f);
        sim.weaponDefinitions["DGUN"] = dgun;
        sim.getPlayer(ai).energy = Energy(5000.0f);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // A cheap raider close in, and a costly one further out but in reach.
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(50.0f);
        sim.unitDefinitions["ARMFLASH"] = sim.unitDefinitions["ARMPW"];
        sim.unitDefinitions["ARMFLASH"].buildCostMetal = Metal(300.0f);
        auto cheapId = addUnit(sim, "ARMPW", human, SimVector(120_ss, 0_ss, 0_ss), script);
        auto dearId = addUnit(sim, "ARMFLASH", human, SimVector(0_ss, 0_ss, 250_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("the dearer of the two in reach")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto shots = ordersFor<DgunOrder>(commands, commanderId);
            REQUIRE_FALSE(shots.empty());
            CHECK(*std::get_if<UnitId>(&shots.front().target) == dearId);
        }

        SECTION("switched off, the nearest")
        {
            profile.dgunByValue = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto shots = ordersFor<DgunOrder>(commands, commanderId);
            REQUIRE_FALSE(shots.empty());
            CHECK(*std::get_if<UnitId>(&shots.front().target) == cheapId);
        }
    }
}
