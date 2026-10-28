#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>

/**
 * The Community Patch ruleset (issue #934) and the TADR weapon flags it
 * honours. The flags are parsed from weapon TDFs whatever the mode; only the
 * mode makes them affect targeting and firing.
 */
namespace rwe
{
    namespace
    {
        GamePlayerInfo communityPlayer(const std::string& name)
        {
            return GamePlayerInfo{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
        }

        std::shared_ptr<CobScript> communityScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void defineCommunityUnits(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            UnitDefinition ordinary{};
            ordinary.objectName = "model";
            ordinary.isMobile = true;
            ordinary.canMove = false;
            ordinary.canAttack = true;
            ordinary.sightDistance = 2000u;
            ordinary.maxHitPoints = 100;
            ordinary.buildTime = 0u;
            ordinary.shootMe = true;
            ordinary.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["ordinary"] = ordinary;

            UnitDefinition hover = ordinary;
            hover.canMove = true;
            hover.canHover = true;
            hover.floater = true;
            sim.unitDefinitions["hover"] = hover;

            UnitDefinition ship = ordinary;
            ship.canMove = true;
            ship.floater = true;
            sim.unitDefinitions["ship"] = ship;

            UnitDefinition sub = ordinary;
            sub.canMove = true;
            sim.unitDefinitions["sub"] = sub;
        }

        UnitId spawnCommunityUnit(
            GameSimulation& sim,
            const std::string& type,
            PlayerId owner,
            const SimVector& position,
            const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }
    }

    TEST_CASE("the Community Patch position gate holds the shot on the wrong ground", "[communitypatch]")
    {
        // TADR's notoverwater/notoverland: the shot is held, not cancelled,
        // and no ammunition is spent. The caller only asks under the mode.
        WeaponDefinition w{};
        REQUIRE_FALSE(communityPatchSuppressesFire(w, true));
        REQUIRE_FALSE(communityPatchSuppressesFire(w, false));

        w.notOverWater = true;
        REQUIRE(communityPatchSuppressesFire(w, true));
        REQUIRE_FALSE(communityPatchSuppressesFire(w, false));

        w.notOverWater = false;
        w.notOverLand = true;
        REQUIRE_FALSE(communityPatchSuppressesFire(w, true));
        REQUIRE(communityPatchSuppressesFire(w, false));

        w.notOverWater = true;
        REQUIRE(communityPatchSuppressesFire(w, true));
        REQUIRE(communityPatchSuppressesFire(w, false));
    }

    TEST_CASE("the Community Patch weapon flags only bite under the ruleset", "[communitypatch]")
    {
        // Sea level is 255 over a flat seabed, so the positions below are
        // unambiguous: a flying unit, a fully submerged one, and a hovercraft
        // riding the surface.
        GameSimulation sim(MapTerrain(Grid<unsigned char>(64, 64, static_cast<unsigned char>(0)), 255_ss), 0u, 0, 0);
        auto us = sim.addPlayer(communityPlayer("us"));
        auto them = sim.addPlayer(communityPlayer("them"));
        defineCommunityUnits(sim);
        auto script = communityScript();

        auto attacker = spawnCommunityUnit(sim, "ordinary", us, SimVector(64_ss, 260_ss, 64_ss), script);
        auto flyer = spawnCommunityUnit(sim, "ordinary", them, SimVector(128_ss, 400_ss, 128_ss), script);
        auto submerged = spawnCommunityUnit(sim, "sub", them, SimVector(256_ss, 0_ss, 256_ss), script);
        auto surface = spawnCommunityUnit(sim, "hover", them, SimVector(320_ss, 255_ss, 320_ss), script);
        auto ship = spawnCommunityUnit(sim, "ship", them, SimVector(384_ss, 255_ss, 384_ss), script);
        sim.getUnitState(flyer).physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};

        WeaponDefinition gun{};
        gun.maxRange = 400_ss;

        SECTION("off, every flag is ignored")
        {
            sim.communityPatchMode = false;
            gun.notToAir = true;
            gun.notToUnderwater = true;
            gun.surfaceFire = true;
            REQUIRE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(flyer)));
        }

        SECTION("nottoair refuses a flying target")
        {
            sim.communityPatchMode = true;
            gun.notToAir = true;
            REQUIRE_FALSE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(flyer)));
            REQUIRE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(submerged)) == false);
        }

        SECTION("nottounderwater refuses a submerged target but not a surface one")
        {
            sim.communityPatchMode = true;
            gun.notToUnderwater = true;
            gun.waterWeapon = true;
            REQUIRE_FALSE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(submerged)));
            REQUIRE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(ship)));
        }

        SECTION("surfacefire lets a water weapon engage a hovercraft")
        {
            gun.waterWeapon = true;
            REQUIRE_FALSE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(surface)));

            sim.communityPatchMode = true;
            gun.surfaceFire = true;
            REQUIRE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(surface)));
            // It does not turn a torpedo into an anti-air weapon.
            REQUIRE_FALSE(sim.weaponCanHitUnit(gun, sim.getUnitState(attacker), sim.getUnitState(flyer)));
        }
    }
}
