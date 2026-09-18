// AirManager's targeting: bombers go at the dearest enemy building that is
// not ringed with anti-air, take whatever comes to our door whatever is
// covering it, and fall back on an army standing together; fighters answer
// aircraft and nothing else, falling back to base cover when the sky is
// clear.

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
#include <rwe/sim/WeaponDefinition.h>
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

        /** A known enemy on the ground: armed or not, building or not, as asked. */
        KnownEnemy makeKnownGroundUnit(UnitId id, const SimVector& position, const std::string& unitType, bool armed = true)
        {
            KnownEnemy enemy{};
            enemy.unitId = id;
            enemy.unitType = unitType;
            enemy.lastKnownPosition = position;
            enemy.lastSeen = GameTime(0u);
            enemy.isBuilding = false;
            enemy.isArmed = armed;
            enemy.isAir = false;
            return enemy;
        }

        /** A weapon that can reach an aircraft, and the unit type that carries it. */
        void addAntiAirType(GameSimulation& sim, const std::string& unitType)
        {
            WeaponDefinition aa;
            aa.toAirWeapon = true;
            aa.maxRange = 300_ss;
            aa.reloadTime = 1_ss;
            aa.burst = 1;
            aa.damage["DEFAULT"] = 50;
            sim.weaponDefinitions["AAGUN"] = aa;

            UnitDefinition def;
            def.weapon1 = "AAGUN";
            def.buildCostMetal = Metal(200.0f);
            sim.unitDefinitions[unitType] = def;
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

    TEST_CASE("bombers leave the dear thing that is ringed with anti-air", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        UnitDefinition dearDef;
        dearDef.buildCostMetal = Metal(2000.0f);
        sim.unitDefinitions["DEARBUILDING"] = dearDef;

        UnitDefinition plainDef;
        plainDef.buildCostMetal = Metal(150.0f);
        sim.unitDefinitions["PLAINBUILDING"] = plainDef;

        sim.unitDefinitions["BOMBER"] = UnitDefinition{};
        addAntiAirType(sim, "AATURRET");

        auto dearPos = SimVector(400_ss, 0_ss, 0_ss);
        auto plainPos = SimVector(0_ss, 0_ss, 400_ss);
        auto dearId = addUnitOfType(sim, "DEARBUILDING", enemy, dearPos, script);
        auto plainId = addUnitOfType(sim, "PLAINBUILDING", enemy, plainPos, script);

        auto bomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto secondBomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(20_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        // Narrowed, so the door rule is not what answers here: this case is
        // about which of two buildings gets bombed.
        profile.bomberHomeDefenseRadius = 200_ss;

        AiBlackboard bb;
        bb.sideUnitsResolved = true;
        bb.sideUnits.bomber = "BOMBER";
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.knownEnemies[dearId.value] = makeKnownBuilding(dearId, dearPos, "DEARBUILDING");
        bb.knownEnemies[plainId.value] = makeKnownBuilding(plainId, plainPos, "PLAINBUILDING");

        // Three of them, over the limit of two, standing on the dear thing.
        for (int i = 0; i < 3; ++i)
        {
            auto aaPos = SimVector(400_ss + SimScalar(static_cast<float>(i) * 20.0f), 0_ss, 0_ss);
            auto aaId = addUnitOfType(sim, "AATURRET", enemy, aaPos, script);
            bb.knownEnemies[aaId.value] = makeKnownGroundUnit(aaId, aaPos, "AATURRET");
        }

        ThreatMap threatMap(64, 64);
        threatMap.rebuild(sim, ai, bb, true);
        // The setup itself, so a geometry mistake fails here and not silently.
        REQUIRE(threatMap.antiAirCoverAt(dearPos) >= 3.0f);
        REQUIRE(threatMap.antiAirCoverAt(plainPos) == 0.0f);

        AirManager air;
        std::vector<PlayerCommand> commands;
        air.update(sim, ai, profile, threatMap, bb, commands);

        auto attacks = ordersFor<AttackOrder>(commands, bomberId);
        REQUIRE(attacks.size() == 1);
        auto target = std::get_if<UnitId>(&attacks.front().target);
        REQUIRE(target != nullptr);
        REQUIRE(*target == plainId);
    }

    TEST_CASE("bombers take what is at the door whatever is covering it", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai");
        auto enemy = addPlayer(sim, "enemy");
        auto script = makeEmptyCobScript();

        UnitDefinition plainDef;
        plainDef.buildCostMetal = Metal(150.0f);
        sim.unitDefinitions["PLAINBUILDING"] = plainDef;
        sim.unitDefinitions["BOMBER"] = UnitDefinition{};
        addAntiAirType(sim, "AATURRET");

        auto plainPos = SimVector(0_ss, 0_ss, 700_ss);
        auto plainId = addUnitOfType(sim, "PLAINBUILDING", enemy, plainPos, script);

        auto bomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto secondBomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(20_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;

        AiBlackboard bb;
        bb.sideUnitsResolved = true;
        bb.sideUnits.bomber = "BOMBER";
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        bb.knownEnemies[plainId.value] = makeKnownBuilding(plainId, plainPos, "PLAINBUILDING");

        // Four anti-air units, right on top of our base anchor.
        UnitId intruderId(0);
        for (int i = 0; i < 4; ++i)
        {
            auto aaPos = SimVector(SimScalar(100.0f + static_cast<float>(i) * 20.0f), 0_ss, 0_ss);
            auto aaId = addUnitOfType(sim, "AATURRET", enemy, aaPos, script);
            bb.knownEnemies[aaId.value] = makeKnownGroundUnit(aaId, aaPos, "AATURRET");
            if (i == 0)
            {
                intruderId = aaId;
            }
        }

        ThreatMap threatMap(64, 64);
        threatMap.rebuild(sim, ai, bb, true);
        REQUIRE(threatMap.antiAirCoverAt(sim.getUnitState(intruderId).position) >= 4.0f);

        AirManager air;
        std::vector<PlayerCommand> commands;
        air.update(sim, ai, profile, threatMap, bb, commands);

        auto attacks = ordersFor<AttackOrder>(commands, bomberId);
        REQUIRE(attacks.size() == 1);
        auto target = std::get_if<UnitId>(&attacks.front().target);
        REQUIRE(target != nullptr);
        REQUIRE(*target == intruderId);
    }

    TEST_CASE("one kbot on its own is not a sortie, three standing together are", "[ai]")
    {
        auto run = [&](int kbotCount) {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto ai = addPlayer(sim, "ai");
            auto enemy = addPlayer(sim, "enemy");
            auto script = makeEmptyCobScript();

            UnitDefinition kbotDef;
            kbotDef.buildCostMetal = Metal(60.0f);
            sim.unitDefinitions["KBOT"] = kbotDef;
            sim.unitDefinitions["BOMBER"] = UnitDefinition{};

            auto bomberId = addUnitOfType(sim, "BOMBER", ai, SimVector(0_ss, 0_ss, 0_ss), script);
            addUnitOfType(sim, "BOMBER", ai, SimVector(20_ss, 0_ss, 0_ss), script);

            auto profile = makeDefaultStandardProfile();
            profile.tacticalTickInterval = 1;

            AiBlackboard bb;
            bb.sideUnitsResolved = true;
            bb.sideUnits.bomber = "BOMBER";
            bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);

            for (int i = 0; i < kbotCount; ++i)
            {
                // Well outside bomberHomeDefenseRadius, so this is the army
                // rule answering and not the one about our own door.
                auto pos = SimVector(SimScalar(1400.0f + static_cast<float>(i) * 30.0f), 0_ss, 0_ss);
                auto id = addUnitOfType(sim, "KBOT", enemy, pos, script);
                bb.knownEnemies[id.value] = makeKnownGroundUnit(id, pos, "KBOT");
            }

            ThreatMap threatMap(64, 64);
            threatMap.rebuild(sim, ai, bb, true);

            AirManager air;
            std::vector<PlayerCommand> commands;
            air.update(sim, ai, profile, threatMap, bb, commands);
            return ordersFor<AttackOrder>(commands, bomberId).size();
        };

        REQUIRE(run(1) == 0);
        REQUIRE(run(3) == 1);
    }
}
