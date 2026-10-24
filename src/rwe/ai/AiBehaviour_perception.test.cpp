#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/PerceptionManager.h>
#include <rwe/ai/StrategicManager.h>
#include <rwe/ai/ai_test_util.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <algorithm>

/**
 * What the AI knows and when: scouting, losing a building, noticing
 * aircraft, and the knobs that scale how hard it reacts.
 *
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    TEST_CASE("fighters are built to match the raid, and go for the bombers first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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

    TEST_CASE("with three enemies the AI fights one of them", "[ai]")
    {
        // "The AI needs to know where to concentrate its efforts."
        //
        // enemyBasePosition was the centroid of every enemy building known,
        // which is the right answer to "where does the enemy live" only when
        // there is one enemy. With three on a four-corner map it is very
        // nearly the centre of the map, where none of them lives: the wave
        // walks into the open ground between three opponents and fights all
        // of them, attackBaseRadius finds nothing within 900 of a point
        // nobody built on and falls back to ranging the whole map, and the
        // commander that gets hunted is whichever has the lowest unit id.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(256, 256), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto near = addPlayer(sim, "near", GamePlayerType::Computer, "ARM");
        auto far = addPlayer(sim, "far", GamePlayerType::Computer, "ARM");
        auto further = addPlayer(sim, "further", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto nearBase = SimVector(600_ss, 0_ss, 0_ss);
        auto farBase = SimVector(0_ss, 0_ss, -1800_ss);
        auto furtherBase = SimVector(-1800_ss, 0_ss, 1800_ss);

        // The centroid of all three is nowhere near any of them, which is
        // the whole complaint. Worked out here so the test says what it is
        // discriminating against rather than only that it discriminates.
        auto allThree = SimVector(
            (nearBase.x + farBase.x + furtherBase.x) / 3_ss,
            0_ss,
            (nearBase.z + farBase.z + furtherBase.z) / 3_ss);
        REQUIRE(allThree.distance(nearBase) > 900_ss);
        REQUIRE(allThree.distance(farBase) > 900_ss);
        REQUIRE(allThree.distance(furtherBase) > 900_ss);

        auto addBase = [&](PlayerId owner, const SimVector& where) {
            addUnit(sim, "ARMSOLAR", owner, where, script);
            addUnit(sim, "ARMCOM", owner, where + SimVector(60_ss, 0_ss, 0_ss), script);
        };

        SECTION("the nearest of them, and its base is what the wave is given")
        {
            addBase(near, nearBase);
            addBase(far, farBase);
            addBase(further, furtherBase);

            // Omniscient so that all three are seen at once: what is under
            // test is the choice between them, not the finding of them.
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.focusEnemy.has_value());
            CHECK(*bb.focusEnemy == near);
            REQUIRE(bb.enemyBasePosition.has_value());
            CHECK(bb.enemyBasePosition->distance(nearBase) < 200_ss);

            // And the commander it hunts is that enemy's, not another's.
            REQUIRE(bb.enemyCommanderPosition.has_value());
            CHECK(bb.enemyCommanderPosition->distance(nearBase) < 200_ss);
        }

        SECTION("switched off, it averages all three into a place nobody lives")
        {
            addBase(near, nearBase);
            addBase(far, farBase);
            addBase(further, furtherBase);

            auto profile = makeDefaultBrutalProfile();
            profile.focusOneEnemy = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            CHECK_FALSE(bb.focusEnemy.has_value());
            REQUIRE(bb.enemyBasePosition.has_value());
            // Not any of their bases: the fault, pinned.
            CHECK(bb.enemyBasePosition->distance(nearBase) > 900_ss);
            CHECK(bb.enemyBasePosition->distance(farBase) > 900_ss);
            CHECK(bb.enemyBasePosition->distance(furtherBase) > 900_ss);
        }

        SECTION("a nearer one found later takes the war over")
        {
            // Only the far one to start with, so the war begins against it.
            addBase(far, farBase);
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().focusEnemy == far);

            // Then a much nearer one turns up: 600 against 1800, which beats
            // the margin comfortably.
            addBase(near, nearBase);
            runTicks(sim, controller, 4, commands);
            CHECK(controller.getBlackboard().focusEnemy == near);
        }

        SECTION("but one barely nearer does not, or no wave ever arrives")
        {
            addBase(far, farBase);
            auto profile = makeDefaultBrutalProfile();
            REQUIRE(profile.focusSwitchMargin > 0_ss);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().focusEnemy == far);

            // Nearer, but by less than the margin. A focus that changes hands
            // on every redrawn centroid gives the wave a new objective every
            // pass, and it arrives nowhere.
            auto barely = SimVector(0_ss, 0_ss, 0_ss - (1800_ss - (profile.focusSwitchMargin / 2_ss)));
            addBase(further, barely);
            runTicks(sim, controller, 4, commands);
            CHECK(controller.getBlackboard().focusEnemy == far);
        }
    }

    TEST_CASE("their commander is a thing the AI knows it has seen", "[ai]")
    {
        // The unit whose death ends the game. The AI had no way to refer to
        // it: knownEnemies recorded whether a contact was a building, armed
        // or an aircraft, and nothing else, so the army could be sent at a
        // solar collector and not at the one target that wins.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto theirCommanderPos = SimVector(100_ss, 0_ss, 0_ss);

        SECTION("in sight, it is recorded and its place is the objective")
        {
            auto theirs = addUnit(sim, "ARMCOM", human, theirCommanderPos, script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            const auto& bb = controller.getBlackboard();
            auto known = bb.knownEnemies.find(theirs.value);
            REQUIRE(known != bb.knownEnemies.end());
            CHECK(known->second.isCommander);
            REQUIRE(bb.enemyCommanderPosition.has_value());
            CHECK(bb.enemyCommanderPosition->distance(theirCommanderPos) < 20_ss);
        }

        SECTION("anything else of theirs is not their commander")
        {
            // The flag comes off the unit definition rather than off the
            // unit's type name, so a side whose commander is called
            // something else is covered and a kbot never is.
            auto theirs = addUnit(sim, "ARMPW", human, theirCommanderPos, script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            const auto& bb = controller.getBlackboard();
            auto known = bb.knownEnemies.find(theirs.value);
            REQUIRE(known != bb.knownEnemies.end());
            CHECK_FALSE(known->second.isCommander);
            CHECK_FALSE(bb.enemyCommanderPosition.has_value());
        }

        SECTION("out of sight it is not known at all")
        {
            // Beyond the commander's 300-unit sight. An honest AI does not
            // get to hunt what it has never found.
            addUnit(sim, "ARMCOM", human, SimVector(450_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            CHECK_FALSE(controller.getBlackboard().enemyCommanderPosition.has_value());
        }
    }

    TEST_CASE("the AI only knows what it can see, unless it cheats", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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

    TEST_CASE("a contact that dies out of sight stays on the AI's map, unless it cheats", "[ai]")
    {
        // refresh keeps a contact it cannot see, so a unit that dies where
        // nothing of ours is looking does not leave the AI's map the instant
        // it dies. The managers used to ask the live simulation whether it was
        // dead and drop it anyway, which is knowledge a player does not have.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // The commander sees 300; the far contact is beyond that.
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.updateVisibility();

        auto farAway = SimVector(450_ss, 0_ss, 0_ss);
        auto inSight = SimVector(100_ss, 0_ss, 0_ss);
        REQUIRE_FALSE(sim.isVisibleTo(ai, farAway));
        REQUIRE(sim.isVisibleTo(ai, inSight));

        // Unit 999 does not exist, so it is gone wherever it is.
        KnownEnemy goneFar{UnitId(999), "ARMPW", farAway, GameTime(0), false, true, false};

        SECTION("the AI keeps acting on a contact it cannot see")
        {
            REQUIRE(contactStillStanding(sim, ai, false, goneFar).has_value());
        }

        SECTION("an omniscient AI knows the contact is gone")
        {
            REQUIRE_FALSE(contactStillStanding(sim, ai, true, goneFar).has_value());
        }

        SECTION("a contact we are watching and cannot see is gone")
        {
            KnownEnemy goneNear{UnitId(999), "ARMPW", SimVector(100_ss, 0_ss, 0_ss), GameTime(0), false, true, false};
            REQUIRE_FALSE(contactStillStanding(sim, ai, false, goneNear).has_value());
        }

        SECTION("a contact in sight carries the live unit, for the frame test")
        {
            auto live = addUnit(sim, "ARMPW", human, inSight, script);
            sim.updateVisibility();
            KnownEnemy seen{live, "ARMPW", inSight, GameTime(0), false, true, false};
            auto contact = contactStillStanding(sim, ai, false, seen);
            REQUIRE(contact.has_value());
            REQUIRE(contact->unit != nullptr);
            REQUIRE(contact->unit->position.x == inSight.x);
        }
    }

    TEST_CASE("a dead contact out of sight stays remembered without becoming a commander attack target", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // The only thing that can see past the commander's 300.
        auto peeperId = addUnit(sim, "ARMPEEP", ai, SimVector(300_ss, 60_ss, 0_ss), script);
        // Armed, beyond the commander's sight but well inside
        // commanderDangerRadius (450), so only the scout can find it.
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(320_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        // Run the tactical pass every tick, so a handful of ticks settles it.
        profile.tacticalTickInterval = 1;
        // Keep the scout where it is: nothing here is about scouting.
        profile.scoutCount = 0;
        profile.targetScoutPlaneCount = 0;

        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);
        REQUIRE(controller.getBlackboard().knownEnemies.count(enemyId.value) == 1);

        // The scout dies, so nothing of ours watches where the enemy was.
        sim.getUnitState(peeperId).markAsDeadNoCorpse();
        runTicks(sim, controller, 2, commands);
        REQUIRE(controller.getBlackboard().knownEnemies.count(enemyId.value) == 1);

        // The enemy dies out of sight. The honest AI keeps the marker and
        // may still search its last known location, but cannot target it.
        sim.getUnitState(enemyId).markAsDeadNoCorpse();
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);
        REQUIRE_FALSE(bb.commanderThreat.has_value());
    }

    TEST_CASE("each faction plays its own line of kbots, and a knob set by hand still wins", "[ai]")
    {
        auto plain = makeDefaultStandardProfile();

        auto core = makeDefaultStandardProfile();
        applyFactionDefaults(core, "Core");
        // The measured CORE candidate (docs/AI-TUNING.md:76-87): no kbot
        // raiders, the lab on Storms and Thuds, the vehicle plant on
        // Instigators and Raiders.
        REQUIRE(core.labRaiderShare == 0);
        REQUIRE(core.labRocketKbotShare == 4);
        REQUIRE(core.labArtilleryKbotShare == 2);
        REQUIRE(core.vehicleTankShare == 3);
        REQUIRE(core.vehicleMissileTruckShare == 1);
        REQUIRE(core.vehicleMediumTankShare == 2);
        REQUIRE(core.counterShareBonus == 2);
        REQUIRE(core.counterShareTrigger == 0.3606f);
        REQUIRE(core.attackArmySize == 14);

        auto arm = makeDefaultStandardProfile();
        applyFactionDefaults(arm, "ARM");
        REQUIRE(arm.labRaiderShare == 0);
        REQUIRE(arm.labRocketKbotShare == 0);
        REQUIRE(arm.labArtilleryKbotShare == plain.labArtilleryKbotShare);
        REQUIRE(arm.attackArmySize == plain.attackArmySize);
        // ARM's measured change is the lab only; its vehicle shares and
        // counter settings are left where the shared defaults put them.
        REQUIRE(arm.vehicleTankShare == plain.vehicleTankShare);
        REQUIRE(arm.vehicleMissileTruckShare == plain.vehicleMissileTruckShare);
        REQUIRE(arm.vehicleMediumTankShare == plain.vehicleMediumTankShare);
        REQUIRE(arm.counterShareBonus == plain.counterShareBonus);
        REQUIRE(arm.counterShareTrigger == plain.counterShareTrigger);

        // LoadingScene lays --ai-tune over the faction's defaults, so this
        // is the order a knob set by hand meets them in.
        REQUIRE(applyAiTuning(core, "labRaiderShare", "2"));
        REQUIRE(core.labRaiderShare == 2);
    }

    TEST_CASE("CORE's aggression follows the difficulty tier", "[ai]")
    {
        // applyFactionDefaults runs after makeProfileForDifficulty
        // (AiSeat.cpp:137-141), so a flat attackArmySize here would overwrite
        // Hard and Brutal with Standard's number. CORE asks for six more than
        // its tier instead: Standard keeps the measured 14, while Hard and
        // Brutal attack earlier rather than at the same point.
        auto coreFor = [](AiDifficulty difficulty) {
            auto p = makeProfileForDifficulty(difficulty);
            applyFactionDefaults(p, "CORE");
            return p;
        };

        auto standard = coreFor(AiDifficulty::Standard);
        auto hard = coreFor(AiDifficulty::Hard);
        auto brutal = coreFor(AiDifficulty::Brutal);
        auto easy = coreFor(AiDifficulty::Easy);

        REQUIRE(standard.attackArmySize == 14);
        REQUIRE(hard.attackArmySize == 13);
        REQUIRE(brutal.attackArmySize == 12);
        REQUIRE(easy.attackArmySize == 20);
        // Lower threshold, earlier attack.
        REQUIRE(hard.attackArmySize < standard.attackArmySize);
        REQUIRE(brutal.attackArmySize < hard.attackArmySize);

        // And the gate itself, not just the number: at an army of thirteen
        // Hard CORE has already called the attack and Standard CORE is still
        // building up. Both have a target and nothing at home to defend.
        StrategicManager strategic;
        auto awaiting = [](GamePhase phase) {
            AiBlackboard bb;
            bb.phase = phase;
            bb.enemyBasePosition = SimVector(2000_ss, 0_ss, 0_ss);
            bb.armySize = 13;
            return bb;
        };

        auto standardState = awaiting(GamePhase::Boom);
        strategic.update(standard, standardState);
        REQUIRE(standardState.phase == GamePhase::Boom);

        auto hardState = awaiting(GamePhase::Boom);
        strategic.update(hard, hardState);
        REQUIRE(hardState.phase == GamePhase::Attack);
    }

    TEST_CASE("the enemy composition memory counts roles and exposes shares", "[ai]")
    {
        // One contact of every role. The split is what decides which counter
        // is worth building, so each has to land in its own bucket: a
        // builder is a builder even though it can be shot, a tower is a
        // static defence rather than "other", and a commander is folded with
        // the units no other bucket names.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto longGun = sim.weaponDefinitions.at("LASER");
        longGun.maxRange = 400_ss;
        sim.weaponDefinitions["LONGGUN"] = longGun;
        sim.unitDefinitions["ARMROCK"].weapon1 = "LONGGUN";

        auto tank = makeDef(false, false, true, "LASER", 200u);
        tank.buildCostMetal = Metal(300.0f);
        sim.unitDefinitions["ARMTANK"] = tank;

        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMROCK", human, SimVector(120_ss, 0_ss, 20_ss), script);
        addUnit(sim, "ARMTANK", human, SimVector(80_ss, 0_ss, -40_ss), script);
        addUnit(sim, "ARMTHUND", human, SimVector(-60_ss, 60_ss, 60_ss), script);
        addUnit(sim, "ARMLLT", human, SimVector(40_ss, 0_ss, 100_ss), script);
        addUnit(sim, "ARMCK", human, SimVector(-100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMCOM", human, SimVector(0_ss, 0_ss, -100_ss), script);
        // Beyond the AI commander's 300-unit sight: the memory is built from
        // knownEnemies, so this one must not appear in any bucket.
        addUnit(sim, "ARMPW", human, SimVector(800_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.compositionDecaySeconds = 0;
        AiBlackboard bb;
        PerceptionManager perception;
        // The visibility grid is filled at the end of a tick, and the
        // controller normally refreshes perception after one. Do that here so
        // the fixture sees the contacts it placed.
        sim.tick();
        perception.refresh(sim, ai, profile, bb);

        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Raider)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::RocketArtillery)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Tank)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Air)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::StaticDefence)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Builder)] == 1);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::CommanderOrOther)] == 1);
        CHECK(enemyRoleShare(bb.enemyRoleCounts, EnemyRole::Raider) == Catch::Approx(1.0f / 7.0f));

        // And switched off it does not learn anything at all.
        profile.enemyCompositionMemory = false;
        AiBlackboard off;
        perception.refresh(sim, ai, profile, off);
        for (auto count : off.enemyRoleCounts)
        {
            CHECK(count == 0);
        }
    }

    TEST_CASE("the composition memory halves on its decay clock", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        std::vector<UnitId> raiders;
        for (int i = 0; i < 4; ++i)
        {
            raiders.push_back(addUnit(sim, "ARMPW", human, SimVector(SimScalar(60.0f + i * 20.0f), 0_ss, 20_ss), script));
        }

        auto profile = makeDefaultStandardProfile();
        profile.compositionDecaySeconds = 30;
        AiBlackboard bb;
        PerceptionManager perception;
        sim.tick();
        perception.refresh(sim, ai, profile, bb);
        REQUIRE(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Raider)] == 4);

        // The contacts are gone, but the memory is not: it halves on the
        // clock rather than vanishing with the sighting. Clear the contact
        // list as well so this section pins the decay rather than the
        // forget-on-looking rule.
        for (auto id : raiders)
        {
            sim.getUnitState(id).markAsDead();
        }
        bb.knownEnemies.clear();
        bb.now = GameTime(30u * SimTicksPerSecond);
        perception.refresh(sim, ai, profile, bb);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Raider)] == 2);

        bb.now = GameTime(60u * SimTicksPerSecond);
        perception.refresh(sim, ai, profile, bb);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Raider)] == 1);

        bb.now = GameTime(90u * SimTicksPerSecond);
        perception.refresh(sim, ai, profile, bb);
        CHECK(bb.enemyRoleCounts[enemyRoleIndex(EnemyRole::Raider)] == 0);
    }

    TEST_CASE("both armies' metal value is exposed for the odds gate", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(120.0f);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto ours = addUnit(sim, "ARMPW", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMTHUND", human, SimVector(120_ss, 60_ss, 60_ss), script);

        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        bb.combatUnits = {ours};
        PerceptionManager perception;
        sim.tick();
        perception.refresh(sim, ai, profile, bb);

        // Ours is the army list the count gate also uses. Theirs is mobile
        // armed ground value; aircraft are kept apart because they do not
        // stand in the line the odds gate is judging.
        CHECK(bb.armyMetal == 120.0f);
        CHECK(bb.knownEnemyArmyMetal == 120.0f);
        CHECK(bb.knownEnemyAirMetal == 100.0f);
    }

    TEST_CASE("the attack posture uses value odds with hysteresis", "[ai]")
    {
        auto profile = makeDefaultStandardProfile();
        REQUIRE(profile.attackUsesValueOdds);
        REQUIRE(profile.attackArmySize == 8);
        REQUIRE(profile.retreatArmySize == 3);
        StrategicManager strategic;

        auto state = [](float ours, float theirs, int count) {
            AiBlackboard bb;
            bb.phase = GamePhase::Boom;
            bb.enemyBasePosition = SimVector(2000_ss, 0_ss, 0_ss);
            bb.armyMetal = ours;
            bb.knownEnemyArmyMetal = theirs;
            bb.armySize = count;
            return bb;
        };

        SECTION("ahead in value attacks before the headcount gate")
        {
            auto bb = state(120.0f, 100.0f, 5);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Attack);
            CHECK(bb.attackOddsCommitted);
        }

        SECTION("behind in value holds")
        {
            auto bb = state(50.0f, 100.0f, 5);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Boom);
            CHECK_FALSE(bb.attackOddsCommitted);
        }

        SECTION("between the two ratios the committed gate holds, and only the lower one closes it")
        {
            auto bb = state(120.0f, 100.0f, 5);
            strategic.update(profile, bb);
            REQUIRE(bb.attackOddsCommitted);

            // 0.8 is below attackValueRatio (1.1) but above
            // retreatValueRatio (0.7): the gate that was already open stays
            // open rather than thrashing on a single remembered unit.
            bb.phase = GamePhase::Boom;
            bb.armyMetal = 80.0f;
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Attack);
            CHECK(bb.attackOddsCommitted);

            // 0.6 is below the retreat ratio, so it closes.
            bb.phase = GamePhase::Boom;
            bb.armyMetal = 60.0f;
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Boom);
            CHECK_FALSE(bb.attackOddsCommitted);
        }

        SECTION("an unknown enemy falls back to the count gate")
        {
            auto bb = state(0.0f, 0.0f, profile.attackArmySize);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Attack);
            CHECK_FALSE(bb.attackOddsCommitted);
        }

        SECTION("and to patience when the count is out of reach")
        {
            auto bb = state(0.0f, 0.0f, profile.retreatArmySize);
            bb.lastAttackPhase = GameTime(1);
            bb.now = GameTime((static_cast<unsigned int>(profile.attackPatienceSeconds) + 10u) * SimTicksPerSecond);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Attack);
        }

        SECTION("neither count, patience nor value holds")
        {
            auto bb = state(0.0f, 0.0f, profile.retreatArmySize);
            bb.lastAttackPhase = GameTime(1);
            bb.now = GameTime(2);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Boom);
            CHECK_FALSE(bb.attackOddsCommitted);
        }

        SECTION("the knob off restores the count gate")
        {
            profile.attackUsesValueOdds = false;
            auto bb = state(1000.0f, 100.0f, 5);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Boom);
            CHECK_FALSE(bb.attackOddsCommitted);
        }

        SECTION("a value lead with too few units to send still holds")
        {
            auto halfWave = (profile.attackArmySize + 1) / 2;
            auto needed = std::max(profile.retreatArmySize, halfWave);
            auto bb = state(1000.0f, 100.0f, needed - 1);
            strategic.update(profile, bb);
            CHECK(bb.phase == GamePhase::Boom);
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
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 5u, 0, 0);
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

    TEST_CASE("an armed aircraft over a factory losing its frames besieges it, and refuses no build site", "[ai]")
    {
        // Issue #152. The two answers to "is an armed enemy on this place"
        // differ on aircraft, and on purpose; see the besieged-factory pass
        // in PerceptionManager and siteUnderEnemyGuns in BuildManager. The
        // besieged answer is only ever asked of a factory that has already
        // lost frames on its own pad, and there the aeroplane over it is the
        // likeliest thing doing it. The site answer has no such evidence to
        // lean on, and an aeroplane is somewhere else by the time a builder
        // arrives.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto bomber = sim.unitDefinitions["ARMPEEP"];
        bomber.weapon1 = "LASER";
        sim.unitDefinitions["ARMTHUND"] = bomber;

        addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 0_ss, 0_ss), script);
        auto labId = addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto frameId = addUnit(sim, "ARMPW", ai, SimVector(0_ss, 0_ss, 30_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        const SimVector overTheLab(60_ss, 0_ss, 0_ss);

        auto besiegedAndRefused = [&](const std::string& attackerType) {
            addUnit(sim, attackerType, human, overTheLab, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 2, commands);
            const auto& bb = controller.getBlackboard();
            BuildManager planner;
            return std::make_pair(bb.besiegedFactories == std::vector<UnitId>{labId}, planner.siteUnderEnemyGuns(sim, profile, bb, overTheLab));
        };

        SECTION("an aircraft: besieged, and the ground under it is not refused")
        {
            auto [besieged, refused] = besiegedAndRefused("ARMTHUND");
            REQUIRE(besieged);
            REQUIRE_FALSE(refused);
        }

        SECTION("a gun on the ground: both")
        {
            auto [besieged, refused] = besiegedAndRefused("ARMPW");
            REQUIRE(besieged);
            REQUIRE(refused);
        }
    }

    TEST_CASE("A building that is still standing is not reported lost", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 5u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
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

    TEST_CASE("a finished air plant keeps a standing patrol of fighters and bombers, with no enemy aircraft seen", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        (void)human;
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

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetScoutPlaneCount = 0;
        profile.targetAirConstructorCount = 0;
        profile.targetBomberCount = 0;

        SECTION("an empty hangar asks for fighters first")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE_FALSE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 1);
        }

        SECTION("with the fighters standing, it asks for bombers")
        {
            addUnit(sim, "ARMFIG", ai, SimVector(-100_ss, 60_ss, 100_ss), script);
            addUnit(sim, "ARMFIG", ai, SimVector(-140_ss, 60_ss, 100_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 0);
            REQUIRE(countQueueCommands(commands, "ARMTHUND") == 1);
        }

        SECTION("both knobs at zero: neither, as before")
        {
            profile.airPatrolFighters = 0;
            profile.airPatrolBombers = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 0);
            REQUIRE(countQueueCommands(commands, "ARMTHUND") == 0);
        }
    }

    TEST_CASE("a contact's frame and builder flags are remembered, and not refreshed in the fog", "[ai]")
    {
        // The raid target pick needs to know which contact is a nanoframe the
        // enemy has only just started and which one puts frames up. Both are
        // properties of the unit when it was last seen, so they belong on the
        // remembered record; a contact that has gone back out of sight keeps
        // the answer it was last seen with rather than being re-read from the
        // simulation.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // A commander with no builder flag: isBuilder has to come from either
        // def.builder or def.commander, not only the first.
        sim.unitDefinitions["ENEMYCOM"] = makeDef(true, false, true, "", 300u);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto frameId = addUnit(sim, "ARMSOLAR", human, SimVector(100_ss, 0_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = sim.unitDefinitions.at("ARMSOLAR").buildTime / 2u;
        auto builderId = addUnit(sim, "ARMCK", human, SimVector(-100_ss, 0_ss, 0_ss), script);
        auto commanderId = addUnit(sim, "ENEMYCOM", human, SimVector(150_ss, 0_ss, 60_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetScoutPlaneCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.knownEnemies.count(frameId.value) == 1);
        REQUIRE(bb.knownEnemies.count(builderId.value) == 1);
        REQUIRE(bb.knownEnemies.count(commanderId.value) == 1);

        const auto& frame = bb.knownEnemies.at(frameId.value);
        CHECK(frame.underConstruction);
        CHECK_FALSE(frame.isBuilder);
        const auto& builder = bb.knownEnemies.at(builderId.value);
        CHECK_FALSE(builder.underConstruction);
        CHECK(builder.isBuilder);
        const auto& commander = bb.knownEnemies.at(commanderId.value);
        CHECK(commander.isBuilder);

        // firstSeen is when the sighting began, not when the record was last
        // rebuilt: the record is rebuilt on every visible tick.
        auto frameFirstSeen = frame.firstSeen;
        runTicks(sim, controller, 2, commands);
        REQUIRE(controller.getBlackboard().knownEnemies.count(frameId.value) == 1);
        CHECK(controller.getBlackboard().knownEnemies.at(frameId.value).firstSeen == frameFirstSeen);

        SECTION("a contact out of sight is left exactly as it was remembered")
        {
            // A second enemy, well outside the commander's 300 sight.
            auto farId = addUnit(sim, "ARMCK", human, SimVector(1200_ss, 0_ss, 1200_ss), script);
            sim.updateVisibility();
            REQUIRE_FALSE(sim.isVisibleTo(ai, SimVector(1200_ss, 0_ss, 1200_ss)));

            KnownEnemy remembered{};
            remembered.unitId = farId;
            remembered.unitType = "ARMCK";
            remembered.lastKnownPosition = SimVector(1200_ss, 0_ss, 1200_ss);
            remembered.lastSeen = GameTime(7u);
            remembered.isCommander = false;
            remembered.underConstruction = true;
            remembered.isBuilder = true;
            remembered.firstSeen = GameTime(5u);

            AiBlackboard fogged;
            fogged.now = GameTime(100u);
            fogged.knownEnemies[farId.value] = remembered;
            PerceptionManager perception;
            perception.refresh(sim, ai, profile, fogged);

            REQUIRE(fogged.knownEnemies.count(farId.value) == 1);
            const auto& kept = fogged.knownEnemies.at(farId.value);
            CHECK(kept.underConstruction);
            CHECK(kept.isBuilder);
            CHECK(kept.firstSeen == GameTime(5u));
            CHECK(kept.lastSeen == GameTime(7u));
        }
    }

    TEST_CASE("attacks are remembered by the sector they came from, and decay", "[ai]")
    {
        // The threat memory is fed only by the armed contacts already
        // inside defendRadius, which is the same list that triggers Defend;
        // nothing here looks at anything the AI could not see.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto base = SimVector(0_ss, 0_ss, 0_ss);
        auto east = SimVector(150_ss, 0_ss, 0_ss);
        auto north = SimVector(0_ss, 0_ss, 150_ss);
        auto eastId = addUnit(sim, "ARMPW", human, east, script);

        auto profile = makeDefaultStandardProfile();
        profile.threatMemoryDecaySeconds = 60;
        AiBlackboard bb;
        bb.baseAnchor = base;
        PerceptionManager perception;
        sim.tick();
        perception.refresh(sim, ai, profile, bb);

        // The sector numbering and its inverse agree; if they did not, a
        // tower would be pointed at a different direction from the one the
        // raid was remembered from.
        for (std::size_t sector = 0; sector < ThreatSectorCount; ++sector)
        {
            CHECK(threatSectorOf(base, base + threatSectorDirection(sector)) == static_cast<int>(sector));
        }

        auto eastSector = threatSectorOf(base, east);
        auto northSector = threatSectorOf(base, north);
        REQUIRE(eastSector != northSector);
        CHECK(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 100);
        CHECK(bb.threatTotal == 100);
        CHECK(bb.threatPeakSector == eastSector);

        SECTION("a second direction accumulates in its own sector")
        {
            addUnit(sim, "ARMPW", human, north, script);
            sim.tick();
            perception.refresh(sim, ai, profile, bb);
            CHECK(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 100);
            CHECK(bb.threatBySector[static_cast<std::size_t>(northSector)] == 100);
            CHECK(bb.threatTotal == 200);
        }

        SECTION("the levels halve on the decay clock")
        {
            sim.getUnitState(eastId).markAsDead();
            bb.knownEnemies.clear();
            bb.now = GameTime(60u * SimTicksPerSecond);
            perception.refresh(sim, ai, profile, bb);
            CHECK(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 50);
            CHECK(bb.threatTotal == 50);

            bb.now = GameTime(120u * SimTicksPerSecond);
            perception.refresh(sim, ai, profile, bb);
            CHECK(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 25);
        }

        SECTION("a fresh raid adds to what is left rather than replacing it")
        {
            sim.getUnitState(eastId).markAsDead();
            bb.knownEnemies.clear();
            bb.now = GameTime(60u * SimTicksPerSecond);
            perception.refresh(sim, ai, profile, bb);
            REQUIRE(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 50);

            addUnit(sim, "ARMPW", human, east, script);
            sim.tick();
            perception.refresh(sim, ai, profile, bb);
            CHECK(bb.threatBySector[static_cast<std::size_t>(eastSector)] == 150);
            CHECK(bb.threatTotal == 150);
        }

        SECTION("switched off, nothing is remembered and the total is zero")
        {
            profile.threatMemory = false;
            perception.refresh(sim, ai, profile, bb);
            for (auto level : bb.threatBySector)
            {
                CHECK(level == 0);
            }
            CHECK(bb.threatTotal == 0);
            CHECK(bb.threatPeakSector == -1);
            CHECK(bb.threatCountedContacts.empty());
        }

        SECTION("the peak sector is the highest, ties going to the lowest index")
        {
            AiBlackboard seeded;
            seeded.threatBySector[3] = 100;
            seeded.threatBySector[9] = 100;
            seeded.threatBySector[5] = 50;
            CHECK(strongestThreatSector(seeded) == 3);

            seeded.threatBySector[9] = 101;
            CHECK(strongestThreatSector(seeded) == 9);

            seeded.threatBySector.fill(0);
            CHECK(strongestThreatSector(seeded) == -1);
        }
    }

    TEST_CASE("the wave's value is exposed for the recall test", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(120.0f);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto first = addUnit(sim, "ARMPW", ai, SimVector(40_ss, 0_ss, 0_ss), script);
        auto second = addUnit(sim, "ARMPW", ai, SimVector(80_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        bb.combatUnits = {first, second};
        bb.attackGroup = {first.value, second.value};
        PerceptionManager perception;
        sim.tick();
        perception.refresh(sim, ai, profile, bb);
        CHECK(bb.waveMetal == 240.0f);

        // A member that is gone no longer counts towards what the wave is
        // worth, so a spent wave is easier to recall.
        sim.getUnitState(first).markAsDead();
        sim.tick();
        perception.refresh(sim, ai, profile, bb);
        CHECK(bb.waveMetal == 120.0f);
    }
}

