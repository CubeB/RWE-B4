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
}
