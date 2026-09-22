// ArmyManager's tactical choices under an attack: reinforcements joining a
// wave that has already set out, and a raiding party sent at the enemy's
// undefended outlying economy instead of at the main target.

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <rwe/ai/ArmyManager.h>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/StrategicManager.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /** A stationary building, standing wherever it is put. */
        KnownEnemy makeKnownBuilding(UnitId id, const SimVector& position)
        {
            KnownEnemy enemy{};
            enemy.unitId = id;
            enemy.unitType = "UNIT";
            enemy.lastKnownPosition = position;
            enemy.lastSeen = GameTime(0u);
            enemy.isBuilding = true;
            enemy.isArmed = false;
            enemy.isAir = false;
            return enemy;
        }

        /** As above, but of a named type, so its cost and its guns are its own. */
        KnownEnemy makeKnownBuildingOfType(UnitId id, const SimVector& position, const std::string& type)
        {
            auto enemy = makeKnownBuilding(id, position);
            enemy.unitType = type;
            return enemy;
        }

        std::vector<UnitId> ascendingIds(std::vector<UnitId> ids)
        {
            std::sort(ids.begin(), ids.end(), [](UnitId a, UnitId b) { return a.value < b.value; });
            return ids;
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
    }

    TEST_CASE("reinforcements join a wave that is still out", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        sim.unitDefinitions["UNIT"] = UnitDefinition{};
        auto script = makeEmptyCobScript();

        std::vector<UnitId> units;
        for (int i = 0; i < 5; ++i)
        {
            units.push_back(addUnitOfType(sim, "UNIT", ai, SimVector(SimScalar(i * 40.0f), 0_ss, 0_ss), script));
        }
        units = ascendingIds(units);

        auto profile = makeDefaultStandardProfile();
        profile.attackInWaves = true;
        profile.reinforcementGroupSize = 3;
        profile.raidingParties = false;
        profile.tacticalTickInterval = 1;

        ThreatMap threatMap(64, 64);
        ArmyManager army;
        std::vector<PlayerCommand> commands;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.enemyBasePosition = SimVector(2000_ss, 0_ss, 0_ss);
        bb.waveSpent = false;
        bb.attackGroup.insert(units[0].value);
        bb.attackGroup.insert(units[1].value);

        SECTION("three spare, the batch size, join")
        {
            bb.combatUnits = units;
            army.update(sim, ai, profile, threatMap, bb, commands);
            REQUIRE(bb.attackGroup.size() == 5);
        }

        SECTION("two spare, below the batch size, wait")
        {
            bb.combatUnits.assign(units.begin(), units.end() - 1);
            army.update(sim, ai, profile, threatMap, bb, commands);
            REQUIRE(bb.attackGroup.size() == 2);
        }
    }

    TEST_CASE("a raid goes at the enemy's undefended expansion", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["UNIT"] = UnitDefinition{};
        auto script = makeEmptyCobScript();

        std::vector<UnitId> units;
        for (int i = 0; i < 4; ++i)
        {
            units.push_back(addUnitOfType(sim, "UNIT", ai, SimVector(SimScalar(i * 40.0f), 0_ss, 0_ss), script));
        }
        units = ascendingIds(units);

        auto profile = makeDefaultStandardProfile();
        profile.raidingParties = true;
        profile.raidPartySize = 2;
        profile.raidAvoidBaseRadius = 900_ss;
        profile.attackInWaves = true;
        profile.tacticalTickInterval = 1;

        ThreatMap threatMap(64, 64);
        ArmyManager army;
        std::vector<PlayerCommand> commands;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.enemyBasePosition = SimVector(4000_ss, 0_ss, 0_ss);
        bb.combatUnits = units;
        bb.attackGroup.insert(units[0].value);
        bb.attackGroup.insert(units[1].value);
        // bb.enemiesNearBase stays empty: the base is not under attack,
        // which is what lets the reserve go raiding at all.

        // One building beside the enemy base (inside raidAvoidBaseRadius of
        // it), one well out on a flank and nearer to us -- an outlying
        // extractor of the kind nothing ever used to walk at.
        auto nearBaseBuildingPos = SimVector(4300_ss, 0_ss, 0_ss);
        auto outlyingBuildingPos = SimVector(1500_ss, 0_ss, 0_ss);

        SECTION("an outlying building is raided over one the base already covers")
        {
            auto nearBaseBuildingId = addUnitOfType(sim, "UNIT", enemy, nearBaseBuildingPos, script);
            auto outlyingBuildingId = addUnitOfType(sim, "UNIT", enemy, outlyingBuildingPos, script);
            bb.knownEnemies[nearBaseBuildingId.value] = makeKnownBuilding(nearBaseBuildingId, nearBaseBuildingPos);
            bb.knownEnemies[outlyingBuildingId.value] = makeKnownBuilding(outlyingBuildingId, outlyingBuildingPos);

            army.update(sim, ai, profile, threatMap, bb, commands);

            REQUIRE(bb.raidGroup.size() == 2);
            REQUIRE(bb.raidGroup.count(units[2].value) == 1);
            REQUIRE(bb.raidGroup.count(units[3].value) == 1);
            REQUIRE(bb.raidTarget.has_value());
            REQUIRE(bb.raidTarget->distanceSquared(outlyingBuildingPos) == 0_ss);
            REQUIRE(bb.raidTarget->distanceSquared(nearBaseBuildingPos) > 0_ss);

            auto raider1Moves = ordersFor<MoveOrder>(commands, units[2]);
            REQUIRE(!raider1Moves.empty());
            REQUIRE(raider1Moves.front().destination.distanceSquared(outlyingBuildingPos) == 0_ss);
            auto raider2Moves = ordersFor<MoveOrder>(commands, units[3]);
            REQUIRE(!raider2Moves.empty());
            REQUIRE(raider2Moves.front().destination.distanceSquared(outlyingBuildingPos) == 0_ss);
        }

        SECTION("nothing to raid when the only known building sits beside the enemy base")
        {
            auto nearBaseBuildingId = addUnitOfType(sim, "UNIT", enemy, nearBaseBuildingPos, script);
            bb.knownEnemies[nearBaseBuildingId.value] = makeKnownBuilding(nearBaseBuildingId, nearBaseBuildingPos);

            army.update(sim, ai, profile, threatMap, bb, commands);

            REQUIRE(bb.raidGroup.empty());
            REQUIRE_FALSE(bb.raidTarget.has_value());
        }
    }

    TEST_CASE("a unit that has outrun the wave waits for it", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        UnitDefinition kbotDef{};
        kbotDef.isMobile = true;
        sim.unitDefinitions["KBOT"] = kbotDef;
        auto script = makeEmptyCobScript();

        std::vector<UnitId> units;
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(0_ss, 0_ss, 0_ss), script));
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(40_ss, 0_ss, 0_ss), script));
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(1200_ss, 0_ss, 0_ss), script));
        units = ascendingIds(units);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        // The rule ships off -- it was measured harmful, see S:18.3 -- so the
        // case that covers it has to switch it on.
        profile.waveCohesionRadius = 400_ss;

        ThreatMap threatMap(64, 64);
        ArmyManager army;
        std::vector<PlayerCommand> commands;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        // update() resets bb.attackTarget and recomputes it itself from the
        // (empty) threat map, falling back to enemyBasePosition -- so that,
        // not attackTarget directly, is what puts the wave's objective at
        // 2000.
        bb.enemyBasePosition = SimVector(2000_ss, 0_ss, 0_ss);
        bb.combatUnits = units;
        for (auto unitId : units)
        {
            bb.attackGroup.insert(unitId.value);
        }
        // knownEnemies stays empty: nothing to trigger the meet-the-army
        // rule, and nothing within engageRadius either.

        army.update(sim, ai, profile, threatMap, bb, commands);

        // The far-ahead unit holds where it stands instead of carrying on
        // alone to the objective. It is told to stand rather than to walk
        // back: walking back does not converge, because the centre it would
        // walk to is itself dragged along behind by whoever is last.
        auto farAheadMoves = ordersFor<MoveOrder>(commands, units[2]);
        REQUIRE(!farAheadMoves.empty());
        REQUIRE(farAheadMoves.front().destination.x > 1100_ss);
        REQUIRE(farAheadMoves.front().destination.x < 1300_ss);

        // One of the rear units is still walking to the objective itself.
        auto rearMoves = ordersFor<MoveOrder>(commands, units[0]);
        REQUIRE(!rearMoves.empty());
        REQUIRE(rearMoves.front().destination.x > 1900_ss);
    }

    TEST_CASE("the wave turns on an army it meets instead of walking on", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        UnitDefinition kbotDef{};
        kbotDef.isMobile = true;
        sim.unitDefinitions["KBOT"] = kbotDef;
        auto script = makeEmptyCobScript();

        std::vector<UnitId> units;
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(0_ss, 0_ss, 0_ss), script));
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(20_ss, 0_ss, 0_ss), script));
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(40_ss, 0_ss, 0_ss), script));
        units = ascendingIds(units);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        // Otherwise the per-unit "anything within reach gets shot at" rule
        // that sits above the phase switch fires first and masks the
        // behaviour under test.
        profile.engageRadius = 50_ss;

        ThreatMap threatMap(64, 64);
        ArmyManager army;
        std::vector<PlayerCommand> commands;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        // As in the test above: update() rebuilds bb.attackTarget from
        // enemyBasePosition rather than from anything set on attackTarget
        // directly.
        bb.enemyBasePosition = SimVector(3000_ss, 0_ss, 0_ss);
        bb.combatUnits = units;
        for (auto unitId : units)
        {
            bb.attackGroup.insert(unitId.value);
        }

        // Four armed enemy ground units standing together well short of the
        // objective: real units in the sim, not just knownEnemies entries,
        // because the rule under test calls sim.tryGetUnitState on each.
        std::vector<SimVector> enemyPositions{
            SimVector(300_ss, 0_ss, 0_ss),
            SimVector(330_ss, 0_ss, 0_ss),
            SimVector(360_ss, 0_ss, 0_ss),
            SimVector(390_ss, 0_ss, 0_ss),
        };
        for (const auto& pos : enemyPositions)
        {
            auto enemyUnitId = addUnitOfType(sim, "KBOT", enemy, pos, script);
            KnownEnemy known{};
            known.unitId = enemyUnitId;
            known.unitType = "KBOT";
            known.lastKnownPosition = pos;
            known.lastSeen = GameTime(0u);
            known.isBuilding = false;
            known.isArmed = true;
            known.isAir = false;
            bb.knownEnemies[enemyUnitId.value] = known;
        }

        army.update(sim, ai, profile, threatMap, bb, commands);

        // The whole wave turns on the enemy army's centre rather than
        // walking on towards the base at 3000.
        for (auto unitId : units)
        {
            auto moves = ordersFor<MoveOrder>(commands, unitId);
            REQUIRE(!moves.empty());
            REQUIRE(moves.front().destination.x > 250_ss);
            REQUIRE(moves.front().destination.x < 450_ss);
        }
    }

    TEST_CASE("the army does not shoot at a contact nobody has seen for a while", "[ai]")
    {
        // A play-test asked whether CORE Storms were shooting from outside
        // what the player could see. Their rocket reaches 400 and they see
        // 265, so the shot itself is legitimate whenever their side has eyes
        // on the target from somewhere -- the original's eligibility test
        // (S:10) has no visibility rule in it at all, and sight is applied
        // when the candidate list is built. What was not legitimate is RWE's
        // blackboard: it keeps a contact until the AI is standing where it
        // last saw it, so an army went on firing at a unit its side had lost
        // entirely.
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["UNIT"] = UnitDefinition{};
        auto script = makeEmptyCobScript();

        auto soldier = addUnitOfType(sim, "UNIT", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto quarry = addUnitOfType(sim, "UNIT", them, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.attackInWaves = false;
        profile.raidingParties = false;
        // The tactical pass runs on its own cadence; these cases drive it by
        // hand, one call at a time, as the others in this file do.
        profile.tacticalTickInterval = 1;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.combatUnits.push_back(soldier);
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);

        KnownEnemy known{};
        known.unitId = quarry;
        known.unitType = "UNIT";
        known.lastKnownPosition = SimVector(100_ss, 0_ss, 0_ss);
        known.isArmed = true;
        bb.knownEnemies[quarry.value] = known;

        ThreatMap threatMap(64, 64);
        ArmyManager manager;

        SECTION("freshly seen, it is attacked")
        {
            bb.now = GameTime(100u);
            bb.knownEnemies[quarry.value].lastSeen = GameTime(100u);

            std::vector<PlayerCommand> commands;
            manager.update(sim, ai, profile, threatMap, bb, commands);

            REQUIRE_FALSE(ordersFor<AttackOrder>(commands, soldier).empty());
        }

        SECTION("last seen long ago, it is left alone")
        {
            bb.now = GameTime(100u + static_cast<unsigned int>(profile.targetMemoryTicks) + 1u);
            bb.knownEnemies[quarry.value].lastSeen = GameTime(100u);

            std::vector<PlayerCommand> commands;
            manager.update(sim, ai, profile, threatMap, bb, commands);

            REQUIRE(ordersFor<AttackOrder>(commands, soldier).empty());
        }
    }

    TEST_CASE("the fleet trigger does not count a hull that is out scouting", "[ai]")
    {
        // Pinned here rather than through a controller, because a controller
        // cannot reach this case at all: ScoutManager only borrows a hull
        // while the enemy is unfound, and the Attack gate needs a target, so
        // the two conditions exclude one another. The phase machine takes
        // nothing but a profile and a blackboard, so the arithmetic can be
        // asked directly.
        auto profile = makeDefaultStandardProfile();
        profile.attackNavalSize = 3;

        AiBlackboard bb;
        bb.phase = GamePhase::Boom;
        // The target half of the gate, without needing a known enemy.
        bb.enemyBasePosition = SimVector(2000_ss, 0_ss, 0_ss);
        bb.armySize = 0;
        bb.navalCombatUnits = {UnitId(1), UnitId(2), UnitId(3)};

        StrategicManager strategic;

        SECTION("three hulls, none of them borrowed, call the attack")
        {
            strategic.update(profile, bb);
            REQUIRE(bb.phase == GamePhase::Attack);
        }

        SECTION("one of the three out scouting leaves two, and it waits")
        {
            // Eyes are not strength. Counting the borrowed hull would have
            // the AI attack sooner for having built a scout, which is the
            // same fault the anti-air bucket exists to avoid.
            bb.navalScoutUnitId = UnitId(2);
            strategic.update(profile, bb);
            REQUIRE(bb.phase == GamePhase::Boom);
        }

        SECTION("at zero the knob is off and hulls are never enough")
        {
            profile.attackNavalSize = 0;
            strategic.update(profile, bb);
            REQUIRE(bb.phase == GamePhase::Boom);
        }
    }
    TEST_CASE("the wave's objective is their commander, then their base", "[ai]")
    {
        // "The overall objective for the AI should always be to destroy the
        // enemy base and be sending units to destroy the enemy commander."
        //
        // It was doing neither, and not by oversight in one place. The wave
        // walked at whatever cell the threat map scored highest, and that
        // score is value minus defence -- so a base worth taking, which is a
        // base with guns on it, loses to any undefended extractor anywhere
        // on the map for as long as the guns are there. Picking off outliers
        // is what the raiding party is for. And the commander was not a
        // candidate at all: the threat map scores cells holding BUILDINGS,
        // so the one unit whose death ends the game could not be handed to
        // the army as an objective.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        UnitDefinition kbotDef{};
        kbotDef.isMobile = true;
        sim.unitDefinitions["KBOT"] = kbotDef;

        // Their base: a plant with a gun standing over it. The gun is what
        // makes the base the wrong answer to "value minus defence", and a
        // base without one is not a base worth the name.
        WeaponDefinition gun{};
        gun.maxRange = 300_ss;
        gun.reloadTime = 1_ss;
        gun.burst = 1;
        gun.damage["DEFAULT"] = 100u;
        sim.weaponDefinitions["GUN"] = gun;

        UnitDefinition plantDef{};
        plantDef.isMobile = false;
        plantDef.buildCostMetal = Metal(500.0f);
        sim.unitDefinitions["PLANT"] = plantDef;

        UnitDefinition towerDef{};
        towerDef.isMobile = false;
        towerDef.buildCostMetal = Metal(150.0f);
        towerDef.weapon1 = "GUN";
        sim.unitDefinitions["TOWER"] = towerDef;

        // Their outlying economy: worth MORE than the plant and covered by
        // nothing, so the old rule prefers it and the new one does not.
        UnitDefinition mexDef{};
        mexDef.isMobile = false;
        mexDef.buildCostMetal = Metal(700.0f);
        sim.unitDefinitions["MEX"] = mexDef;

        auto basePos = SimVector(800_ss, 0_ss, 0_ss);
        auto towerPos = SimVector(860_ss, 0_ss, 0_ss);
        auto outlierPos = SimVector(-800_ss, 0_ss, 800_ss);
        // Well outside attackBaseRadius of their base, which is what makes
        // it the raiding party's business and not the wave's.
        REQUIRE(basePos.distance(outlierPos) > 900_ss);

        auto plantId = addUnitOfType(sim, "PLANT", enemy, basePos, script);
        auto towerId = addUnitOfType(sim, "TOWER", enemy, towerPos, script);
        auto mexId = addUnitOfType(sim, "MEX", enemy, outlierPos, script);

        std::vector<UnitId> units;
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(0_ss, 0_ss, 0_ss), script));
        units.push_back(addUnitOfType(sim, "KBOT", ai, SimVector(40_ss, 0_ss, 0_ss), script));
        units = ascendingIds(units);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        // Two rules that sit above the one under test and would answer
        // first: shooting whatever is already in reach, and turning on an
        // army met on the road.
        profile.engageRadius = 50_ss;
        profile.waveMeetEnemyCount = 0;
        // And the detachment that has a target of its own.
        profile.raidingParties = false;

        AiBlackboard bb;
        bb.phase = GamePhase::Attack;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.enemyBasePosition = basePos;
        bb.combatUnits = units;
        for (auto unitId : units)
        {
            bb.attackGroup.insert(unitId.value);
        }
        bb.knownEnemies[plantId.value] = makeKnownBuildingOfType(plantId, basePos, "PLANT");
        bb.knownEnemies[towerId.value] = makeKnownBuildingOfType(towerId, towerPos, "TOWER");
        bb.knownEnemies[mexId.value] = makeKnownBuildingOfType(mexId, outlierPos, "MEX");

        ThreatMap threatMap(128, 128);
        threatMap.rebuild(sim, ai, bb, true);

        // Where the wave is told to walk. Read off the orders rather than
        // off the blackboard, because what matters is where the units go.
        auto objective = [&](const AiTuningProfile& p) {
            ArmyManager army;
            std::vector<PlayerCommand> commands;
            army.update(sim, ai, p, threatMap, bb, commands);
            auto moves = ordersFor<MoveOrder>(commands, units[0]);
            REQUIRE(!moves.empty());
            return moves.front().destination;
        };

        SECTION("their commander outranks their base")
        {
            // Out in the open, away from their base: the case worth having,
            // because it is the one where the two objectives disagree.
            auto commanderPos = SimVector(0_ss, 0_ss, -800_ss);
            REQUIRE(profile.huntEnemyCommander);
            bb.enemyCommanderPosition = commanderPos;

            auto destination = objective(profile);
            CHECK(commanderPos.distance(destination) < 100_ss);
        }

        SECTION("without a commander, the objective is inside their base")
        {
            REQUIRE(profile.attackBaseRadius > 0_ss);
            auto destination = objective(profile);
            CHECK(basePos.distance(destination) <= profile.attackBaseRadius);
            // And specifically the plant, not the gun standing beside it:
            // inside the base, value minus defence is still what chooses,
            // and there it is the right question.
            CHECK(destination.x < 830_ss);
        }

        SECTION("switched off, it walks to the far corner as it used to")
        {
            // The fault, pinned. Both knobs off is the old behaviour
            // exactly: two kbots sent the length of the map after a metal
            // extractor while the plant and the commander stand.
            auto old = profile;
            old.huntEnemyCommander = false;
            old.attackBaseRadius = 0_ss;
            bb.enemyCommanderPosition = SimVector(0_ss, 0_ss, -800_ss);

            auto destination = objective(old);
            CHECK(outlierPos.distance(destination) < 100_ss);
        }

        SECTION("nothing of theirs known in their base: the pick ranges the map again")
        {
            // Early on, all we have seen is one extractor in the open. A
            // radius with nothing inside it must not leave the wave with no
            // objective at all, so the pick falls back to the whole map --
            // which here is the only target there is.
            bb.knownEnemies.erase(plantId.value);
            bb.knownEnemies.erase(towerId.value);
            threatMap.rebuild(sim, ai, bb, true);

            auto destination = objective(profile);
            CHECK(outlierPos.distance(destination) < 100_ss);
        }
    }
}
