// ArmyManager's tactical choices under an attack: reinforcements joining a
// wave that has already set out, and a raiding party sent at the enemy's
// undefended outlying economy instead of at the main target.

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <rwe/ai/ArmyManager.h>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
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
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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
}
