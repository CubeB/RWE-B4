#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * BT-253: what a round does in a cell the occupancy grid says is taken.
 * Static byte-tactics reading, not executed against the original.
 *
 * The cell's unit slots (a mobile ground unit, then a building) are asked with
 * the round's height against the TOP of the unit only -- there is no lower
 * bound -- and a mobile unit of the round's own side in the cell does not end
 * the test: the building and feature slots are still asked. The flying-unit
 * test keeps both bounds and is not touched here.
 */
namespace rwe
{
    namespace
    {
        constexpr const char* ModelName = "MODEL20";

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[ModelName] = createUnitModelDefinition(20_ss, std::move(pieces));
        }

        void defineSlug(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 600_ss;
            w.reloadTime = 1_ss;
            w.burst = 1;
            w.burstInterval = 0_ss;
            // Slow enough that the first few ticks stay inside the one cell.
            w.velocity = 1_ss;
            w.damageRadius = 0_ss;
            w.damage["DEFAULT"] = 31;
            w.physicsType = ProjectilePhysicsTypeLineOfSight{};
            sim.weaponDefinitions["SLUG"] = w;
        }

        UnitDefinition makeStandingDef(bool mobile)
        {
            UnitDefinition d{};
            d.objectName = ModelName;
            d.isMobile = mobile;
            d.canMove = false;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            if (!mobile)
            {
                d.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
            }
            return d;
        }

        /** A finished mobile unit with its origin at the given height, put in through the real path so it stamps the grid. */
        UnitId addMobileAt(GameSimulation& sim, PlayerId owner, const SimVector& position, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["WALKER"] = makeStandingDef(true);
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "WALKER";
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = 1000;
            unit.buildTimeCompleted = 0;
            auto id = sim.tryAddUnit(std::move(unit));
            REQUIRE(id.has_value());
            return *id;
        }

        /** Flies a slug of the owner for a few ticks along +x from the given point and says whether it detonated. */
        bool fireSlugDetonates(GameSimulation& sim, PlayerId owner, const SimVector& from)
        {
            UnitWeapon weapon;
            weapon.weaponType = "SLUG";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = owner,
                .weapon = &weapon,
                .position = from,
                .direction = SimVector(1_ss, 0_ss, 0_ss),
                .distanceToTarget = 200_ss,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt,
                .inheritedVelocity = std::nullopt,
                .targetPosition = from + SimVector(200_ss, 0_ss, 0_ss)});

            for (int ticks = 0; ticks < 4; ++ticks)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    if (std::get_if<ProjectileDiedEvent>(&e) != nullptr)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    TEST_CASE("the Community Patch passes a shot through allies", "[projectilecell][communitypatch]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto attacker = addPlayer(sim, "attacker");
        auto victim = addPlayer(sim, "victim");
        registerModel(sim);
        defineSlug(sim);
        sim.players[attacker.value].teamId = 7;
        sim.players[victim.value].teamId = 7;

        auto at = SimVector(200_ss, 50_ss, 200_ss);
        auto unitId = addMobileAt(sim, victim, at, script);

        SECTION("off, an ally is hit like anybody else")
        {
            REQUIRE(fireSlugDetonates(sim, attacker, SimVector(at.x, 50_ss, at.z)));
            REQUIRE(sim.getUnitState(unitId).hitPoints < 1000u);
        }

        SECTION("on, an allied unit is passed through")
        {
            sim.communityPatchMode = true;
            REQUIRE_FALSE(fireSlugDetonates(sim, attacker, SimVector(at.x, 50_ss, at.z)));
            REQUIRE(sim.getUnitState(unitId).hitPoints == 1000u);
        }

        SECTION("on, an enemy is still hit")
        {
            sim.communityPatchMode = true;
            sim.players[victim.value].teamId = std::nullopt;
            REQUIRE(fireSlugDetonates(sim, attacker, SimVector(at.x, 50_ss, at.z)));
            REQUIRE(sim.getUnitState(unitId).hitPoints < 1000u);
        }
    }

    TEST_CASE("a round below a mobile unit's origin but inside its cell still strikes it (BT-253)", "[projectilecell][bt253]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto attacker = addPlayer(sim, "attacker");
        auto victim = addPlayer(sim, "victim");
        registerModel(sim);
        defineSlug(sim);

        // The unit stands with its origin fifty up, on ground at zero.
        auto at = SimVector(200_ss, 50_ss, 200_ss);
        auto unitId = addMobileAt(sim, victim, at, script);

        SECTION("thirty up: below the origin, which used to pass underneath")
        {
            REQUIRE(fireSlugDetonates(sim, attacker, SimVector(at.x, 30_ss, at.z)));
            REQUIRE(sim.getUnitState(unitId).hitPoints < 1000u);
        }

        SECTION("above the top of the model still passes over")
        {
            // Fifty plus a model twenty tall: seventy-one is over it.
            REQUIRE_FALSE(fireSlugDetonates(sim, attacker, SimVector(at.x, 71_ss, at.z)));
            REQUIRE(sim.getUnitState(unitId).hitPoints == 1000u);
        }
    }

    TEST_CASE("a round below a building's origin but inside its cells still strikes it (BT-253)", "[projectilecell][bt253]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto attacker = addPlayer(sim, "attacker");
        auto victim = addPlayer(sim, "victim");
        registerModel(sim);
        defineSlug(sim);
        sim.unitDefinitions["HUT"] = makeStandingDef(false);
        sim.unitScriptDefinitions["HUT"] = *script;

        auto site = SimVector(200_ss, 0_ss, 200_ss);
        auto hutId = sim.trySpawnCompletedUnit("HUT", victim, site, std::nullopt).value();
        // Raise the building's origin so that the round's height is below it.
        sim.getUnitState(hutId).position.y = 50_ss;

        REQUIRE(fireSlugDetonates(sim, attacker, SimVector(site.x, 30_ss, site.z)));
        REQUIRE(sim.getUnitState(hutId).hitPoints < 1000u);
    }

    TEST_CASE("a mobile unit of the rounds own side in the cell does not shield the building under it (BT-253)", "[projectilecell][bt253]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto attacker = addPlayer(sim, "attacker");
        auto victim = addPlayer(sim, "victim");
        registerModel(sim);
        defineSlug(sim);
        sim.unitDefinitions["HUT"] = makeStandingDef(false);
        sim.unitScriptDefinitions["HUT"] = *script;

        auto site = SimVector(200_ss, 0_ss, 200_ss);
        auto hutId = sim.trySpawnCompletedUnit("HUT", victim, site, std::nullopt).value();

        // A friendly walker of the attacker, standing elsewhere, then written
        // into the building's cells as well, as a unit crossing the ground
        // under a building's footprint would be.
        auto friendlyId = addMobileAt(sim, attacker, SimVector(100_ss, 0_ss, 100_ss), script);
        auto region = sim.occupiedGrid.tryToRegion(sim.computeFootprintRegion(site, sim.unitDefinitions.at("HUT").movementCollisionInfo));
        REQUIRE(region.has_value());
        sim.occupiedGrid.forEach(*region, [&](auto& cell) { cell.mobileUnitId = friendlyId; });

        REQUIRE(fireSlugDetonates(sim, attacker, SimVector(site.x, 5_ss, site.z)));
        REQUIRE(sim.getUnitState(hutId).hitPoints < 1000u);
        REQUIRE(sim.getUnitState(friendlyId).hitPoints == 1000u);
    }

    TEST_CASE("a five by five building of open ground stamps exactly twenty-five cells of the occupancy grid (BT-253)", "[projectilecell][bt253]")
    {
        // An ARMSOLAR-like building: 5x5, yardmap all 'o'. A stray stamp
        // anywhere would put a cell in the projectile test that is not the
        // building's.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto owner = addPlayer(sim, "owner");
        registerModel(sim);

        UnitDefinition solar = makeStandingDef(false);
        solar.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 0u};
        solar.yardMap = Grid<YardMapCell>(5, 5, YardMapCell::Ground);
        sim.unitDefinitions["SOLAR"] = solar;
        sim.unitScriptDefinitions["SOLAR"] = *script;

        auto id = sim.trySpawnCompletedUnit("SOLAR", owner, SimVector(300_ss, 0_ss, 300_ss), std::nullopt).value();

        std::size_t stamped = 0;
        std::size_t elsewhere = 0;
        for (const auto& cell : sim.occupiedGrid.getVector())
        {
            if (cell.buildingInfo)
            {
                ++stamped;
                if (cell.buildingInfo->unit != id)
                {
                    ++elsewhere;
                }
            }
        }
        REQUIRE(stamped == 25u);
        REQUIRE(elsewhere == 0u);
    }
}
