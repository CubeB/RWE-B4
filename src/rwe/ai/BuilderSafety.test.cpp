#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/BuilderSafety.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        using Catch::Approx;

        MapTerrain safetyMakeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId safetyAddPlayer(GameSimulation& sim, const std::string& name, GamePlayerType type, const std::string& side)
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
                std::nullopt,
            };
            return sim.addPlayer(p);
        }

        UnitDefinition safetyMakeDef(bool commander, bool builder, bool mobile, const std::string& weapon, unsigned int sight)
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

        /** A world with one weapon and four units: an unarmed mobile builder, an armed mobile unit, an armed tower and an armed commander. */
        void safetyDefineWorld(GameSimulation& sim)
        {
            WeaponDefinition laser{};
            laser.maxRange = 200_ss;
            laser.reloadTime = 1_ss;
            laser.burst = 1;
            sim.weaponDefinitions["LASER"] = laser;

            sim.unitDefinitions["ARMCK"] = safetyMakeDef(false, true, true, "", 100u);
            sim.unitDefinitions["ARMPW"] = safetyMakeDef(false, false, true, "LASER", 200u);
            sim.unitDefinitions["ARMLLT"] = safetyMakeDef(false, false, false, "LASER", 200u);
            sim.unitDefinitions["ARMCOM"] = safetyMakeDef(true, true, true, "LASER", 300u);
        }

        UnitId safetyAddUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            return unitId;
        }

        /** Adds a live enemy unit and records it in the blackboard the way PerceptionManager would. */
        UnitId safetyAddEnemy(GameSimulation& sim, AiBlackboard& bb, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script, bool isBuilding, bool isArmed, bool isAir, GameTime lastSeen)
        {
            auto id = safetyAddUnit(sim, type, owner, pos, script);
            bb.knownEnemies[id.value] = KnownEnemy{id, type, pos, lastSeen, isBuilding, isArmed, isAir};
            return id;
        }
    }

    TEST_CASE("assessExposure with no remembered enemies reports nothing to fear", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE_FALSE(exposure.exposed);
        REQUIRE(exposure.threatMetal == Approx(0.0f));
        REQUIRE(exposure.protectionMetal == Approx(0.0f));
        REQUIRE_FALSE(exposure.threatCentre.has_value());
    }

    TEST_CASE("a single armed enemy within reach is a threat with no cover", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        auto enemyPos = SimVector(250_ss, 0_ss, 0_ss);
        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, enemyPos, script, false, true, false, bb.now);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        // Range 200 plus the mobile margin 150 reaches 350, and the enemy
        // is only 250 away, so it counts.
        REQUIRE(exposure.exposed);
        REQUIRE(exposure.threatMetal == Approx(100.0f));
        REQUIRE(exposure.protectionMetal == Approx(0.0f));
        REQUIRE(rweAbs(exposure.maxThreatRange - 200_ss) <= 1_ss);
        REQUIRE(exposure.threatCentre.has_value());
        REQUIRE(rweAbs(exposure.threatCentre->x - enemyPos.x) <= 1_ss);
        REQUIRE(rweAbs(exposure.threatCentre->z - enemyPos.z) <= 1_ss);
    }

    TEST_CASE("two of our armed units standing between the position and the enemy are enough cover", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, false, bb.now);

        safetyAddUnit(sim, "ARMPW", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        safetyAddUnit(sim, "ARMPW", ai, SimVector(120_ss, 0_ss, 30_ss), script);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        // 200 metal of cover against 100 of threat: safe.
        REQUIRE(exposure.threatMetal == Approx(100.0f));
        REQUIRE(exposure.protectionMetal == Approx(200.0f));
        REQUIRE_FALSE(exposure.exposed);
    }

    TEST_CASE("an armed unit standing behind the position, away from the threat, is not counted as cover", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, false, bb.now);

        // 200 on the far side of the position from the enemy, well past
        // behindSlack (64): it cannot stand between the position and the
        // shot.
        safetyAddUnit(sim, "ARMPW", ai, SimVector(-200_ss, 0_ss, 0_ss), script);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE(exposure.protectionMetal == Approx(0.0f));
        REQUIRE(exposure.exposed);
    }

    TEST_CASE("a tower whose range reaches the threat protects the position", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, false, bb.now);

        // Distance from the tower to the threat centre is about 111.8,
        // comfortably inside its own 200 range plus the 32 slack.
        safetyAddUnit(sim, "ARMLLT", ai, SimVector(300_ss, 0_ss, 100_ss), script);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE(exposure.protectionMetal == Approx(100.0f));
        REQUIRE_FALSE(exposure.exposed);
    }

    TEST_CASE("an enemy not seen recently enough is forgotten", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        // Just past memoryTicks (150): 1000 > 849 + 150.
        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, false, GameTime(1000 - 151));

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE_FALSE(exposure.exposed);
        REQUIRE(exposure.threatMetal == Approx(0.0f));
    }

    TEST_CASE("a remembered threat that died out of sight is still a threat, unless the AI sees everything", "[ai]")
    {
        // A threat dropped the moment its unit dies is knowledge the AI does
        // not have unless it is looking there. The builder keeps retreating
        // from the marker until something goes and looks.
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        auto enemyId = safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, false, bb.now);
        sim.getUnitState(enemyId).markAsDeadNoCorpse();

        SECTION("an honest AI keeps counting it")
        {
            BuilderSafetyParams params;
            auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));
            REQUIRE(exposure.exposed);
            REQUIRE(exposure.threatMetal == Approx(100.0f));
        }

        SECTION("an omniscient AI knows it is gone")
        {
            BuilderSafetyParams params;
            params.omniscient = true;
            auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));
            REQUIRE_FALSE(exposure.exposed);
            REQUIRE(exposure.threatMetal == Approx(0.0f));
        }
    }

    TEST_CASE("an airborne enemy is never a threat here", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(250_ss, 0_ss, 0_ss), script, false, true, true, bb.now);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE_FALSE(exposure.exposed);
        REQUIRE(exposure.threatMetal == Approx(0.0f));
    }

    TEST_CASE("an enemy out of reach is not a threat", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        // 400 away is past 200 + 150 = 350.
        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, SimVector(400_ss, 0_ss, 0_ss), script, false, true, false, bb.now);

        BuilderSafetyParams params;
        auto exposure = assessExposure(sim, ai, bb, params, SimVector(0_ss, 0_ss, 0_ss));

        REQUIRE_FALSE(exposure.exposed);
        REQUIRE(exposure.threatMetal == Approx(0.0f));
    }

    TEST_CASE("planBuilderRetreats sends an exposed builder away from the enemy and never touches the commander", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(safetyMakeFlatTerrain(), 0u, 0, 0);
        auto ai = safetyAddPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto enemyOwner = safetyAddPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
        safetyDefineWorld(sim);

        AiBlackboard bb;
        bb.now = GameTime(1000);

        auto enemyPos = SimVector(250_ss, 0_ss, 0_ss);
        safetyAddEnemy(sim, bb, "ARMPW", enemyOwner, enemyPos, script, false, true, false, bb.now);

        auto builderPos = SimVector(0_ss, 0_ss, 0_ss);
        auto builderId = safetyAddUnit(sim, "ARMCK", ai, builderPos, script);
        // A commander near the same enemy, far enough from the builder
        // (past coverRadius) that it cannot be mistaken for the builder's
        // own cover: it stands just as exposed, but must never be told to
        // retreat, because it fights rather than flees.
        safetyAddUnit(sim, "ARMCOM", ai, SimVector(550_ss, 0_ss, 0_ss), script);

        BuilderSafetyParams params;

        SECTION("with no base to retreat to")
        {
            auto plan = planBuilderRetreats(sim, ai, bb, params);

            REQUIRE(plan.size() == 1);
            REQUIRE(plan[0].builder == builderId);

            auto newDistance = plan[0].destination.distance(enemyPos);
            REQUIRE(newDistance > 250_ss);
            // At least as far as the enemy's own range, and clear of it by
            // the retreat margin's worth of slack in practice.
            REQUIRE(newDistance >= 200_ss);
        }

        SECTION("with a base on the safe side, close enough to just go home")
        {
            bb.baseAnchor = SimVector(-90_ss, 0_ss, 0_ss);

            auto plan = planBuilderRetreats(sim, ai, bb, params);

            REQUIRE(plan.size() == 1);
            REQUIRE(plan[0].builder == builderId);
            REQUIRE(rweAbs(plan[0].destination.x - (-90_ss)) <= 1_ss);
            REQUIRE(rweAbs(plan[0].destination.z - 0_ss) <= 1_ss);
        }

        SECTION("a builder already moving to (near enough) the chosen point is left alone")
        {
            // Compute the same destination the plan would choose, then put
            // the builder on its way there already: a second, identical
            // order should not be issued.
            auto exposure = assessExposure(sim, ai, bb, params, builderPos, builderId);
            auto destination = retreatPoint(sim, bb, params, builderPos, exposure);

            auto& unit = sim.getUnitState(builderId);
            unit.orders.push_back(MoveOrder(destination));

            auto plan = planBuilderRetreats(sim, ai, bb, params);
            REQUIRE(plan.empty());
        }
    }
}
