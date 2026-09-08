// AirManager's targeting: bombers go at the dearest enemy building, and
// fighters answer aircraft and nothing else, falling back to base cover
// when there is nothing in the air.

#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/AirManager.h>
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

        /** A stationary enemy building, standing wherever it is put. */
        KnownEnemy makeKnownBuilding(UnitId id, const SimVector& position, const std::string& unitType = "UNIT")
        {
            KnownEnemy enemy{};
            enemy.unitId = id;
            enemy.unitType = unitType;
            enemy.lastKnownPosition = position;
            enemy.lastSeen = GameTime(0u);
            enemy.isBuilding = true;
            enemy.isArmed = false;
            enemy.isAir = false;
            return enemy;
        }

        /** A known enemy aircraft, airborne wherever it is put. */
        KnownEnemy makeKnownAircraft(UnitId id, const SimVector& position)
        {
            KnownEnemy enemy{};
            enemy.unitId = id;
            enemy.unitType = "PLANE";
            enemy.lastKnownPosition = position;
            enemy.lastSeen = GameTime(0u);
            enemy.isBuilding = false;
            enemy.isArmed = true;
            enemy.isAir = true;
            return enemy;
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

    TEST_CASE("bombers go at the dearest thing the enemy has built", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        UnitDefinition cheapDef;
        cheapDef.buildCostMetal = Metal(100.0f);
        sim.unitDefinitions["CHEAPBUILDING"] = cheapDef;

        UnitDefinition dearDef;
        dearDef.buildCostMetal = Metal(800.0f);
        sim.unitDefinitions["DEARBUILDING"] = dearDef;

        sim.unitDefinitions["BOMBER"] = UnitDefinition{};

        auto cheapPos = SimVector(500_ss, 0_ss, 0_ss);
        auto dearPos = SimVector(1500_ss, 0_ss, 0_ss);
        auto cheapId = addUnitOfType(sim, "CHEAPBUILDING", enemy, cheapPos, script);
        auto dearId = addUnitOfType(sim, "DEARBUILDING", enemy, dearPos, script);

        // Two, because a lone bomber is held back: see the case below.
        auto bomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto secondBomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(20_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;

        AiBlackboard bb;
        bb.sideUnitsResolved = true;
        bb.sideUnits.bomber = "BOMBER";
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.knownEnemies[cheapId.value] = makeKnownBuilding(cheapId, cheapPos, "CHEAPBUILDING");
        bb.knownEnemies[dearId.value] = makeKnownBuilding(dearId, dearPos, "DEARBUILDING");

        ThreatMap threatMap(64, 64);
        AirManager air;
        std::vector<PlayerCommand> commands;

        air.update(sim, ai, profile, threatMap, bb, commands);

        REQUIRE(commands.size() == 2);
        for (auto id : {bomberId, secondBomberId})
        {
            auto attacks = ordersFor<AttackOrder>(commands, id);
            REQUIRE(attacks.size() == 1);
            auto target = std::get_if<UnitId>(&attacks.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == dearId);
        }
    }

    TEST_CASE("a lone bomber waits for a second one", "[ai]")
    {
        // Sent out alone, each new bomber flies at the dearest thing the
        // enemy owns the moment it leaves the pad -- which is the thing
        // deepest inside their anti-air -- and is traded for a fraction of a
        // building. The same trickle the army's wave rule exists to stop.
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        UnitDefinition dearDef;
        dearDef.buildCostMetal = Metal(800.0f);
        sim.unitDefinitions["DEARBUILDING"] = dearDef;
        sim.unitDefinitions["BOMBER"] = UnitDefinition{};

        auto dearPos = SimVector(1500_ss, 0_ss, 0_ss);
        auto dearId = addUnitOfType(sim, "DEARBUILDING", enemy, dearPos, script);
        addUnitOfType(sim, "BOMBER", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;

        AiBlackboard bb;
        bb.sideUnitsResolved = true;
        bb.sideUnits.bomber = "BOMBER";
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.knownEnemies[dearId.value] = makeKnownBuilding(dearId, dearPos, "DEARBUILDING");

        ThreatMap threatMap(64, 64);
        AirManager air;
        std::vector<PlayerCommand> commands;

        air.update(sim, ai, profile, threatMap, bb, commands);

        // It is over the base already, so it is left alone entirely.
        REQUIRE(commands.empty());
    }

    TEST_CASE("fighters take what flies and nothing else", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        sim.unitDefinitions["UNIT"] = UnitDefinition{};
        sim.unitDefinitions["PLANE"] = UnitDefinition{};
        sim.unitDefinitions["FIGHTER"] = UnitDefinition{};

        auto buildingPos = SimVector(500_ss, 0_ss, 0_ss);
        auto aircraftPos = SimVector(600_ss, 0_ss, 0_ss);
        auto buildingId = addUnitOfType(sim, "UNIT", enemy, buildingPos, script);
        auto aircraftId = addUnitOfType(sim, "PLANE", enemy, aircraftPos, script);

        auto fighterId = addUnitOfType(sim, "FIGHTER", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;

        AiBlackboard bb;
        bb.sideUnitsResolved = true;
        bb.sideUnits.fighter = "FIGHTER";
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);

        ThreatMap threatMap(64, 64);
        AirManager air;

        SECTION("an aircraft in view is attacked, a building is not")
        {
            bb.knownEnemies[buildingId.value] = makeKnownBuilding(buildingId, buildingPos);
            bb.knownEnemies[aircraftId.value] = makeKnownAircraft(aircraftId, aircraftPos);

            std::vector<PlayerCommand> commands;
            air.update(sim, ai, profile, threatMap, bb, commands);

            auto attacksOnAircraft = ordersFor<AttackOrder>(commands, fighterId);
            REQUIRE(attacksOnAircraft.size() == 1);
            auto target = std::get_if<UnitId>(&attacksOnAircraft.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == aircraftId);

            // No command anywhere in the batch targets the building.
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                REQUIRE(unitCommand != nullptr);
                auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command);
                REQUIRE(issue != nullptr);
                auto attack = std::get_if<AttackOrder>(&issue->order);
                if (attack == nullptr)
                {
                    continue;
                }
                auto attacked = std::get_if<UnitId>(&attack->target);
                REQUIRE(attacked != nullptr);
                REQUIRE(*attacked != buildingId);
            }
        }

        SECTION("nothing flying, and the fighter is beyond its leash: it comes home")
        {
            bb.knownEnemies[buildingId.value] = makeKnownBuilding(buildingId, buildingPos);

            auto& fighterUnit = sim.getUnitState(fighterId);
            fighterUnit.position = *bb.baseAnchor + SimVector(profile.fighterLeash * 2_ss, 0_ss, 0_ss);
            fighterUnit.previousPosition = fighterUnit.position;

            std::vector<PlayerCommand> commands;
            air.update(sim, ai, profile, threatMap, bb, commands);

            auto moves = ordersFor<MoveOrder>(commands, fighterId);
            REQUIRE(moves.size() == 1);
            REQUIRE(moves.front().destination.distanceSquared(*bb.baseAnchor) == 0_ss);

            auto attacks = ordersFor<AttackOrder>(commands, fighterId);
            REQUIRE(attacks.empty());
        }
    }
}
