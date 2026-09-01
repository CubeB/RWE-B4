#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeStandingOrdersTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addStandingOrdersPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
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
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeStandingOrdersScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerStandingOrdersModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * Runs a scrap of FBI through the real parser and the real definition
         * mapping, so a mistyped key name fails here rather than silently
         * leaving every unit on the built-in default.
         */
        UnitDefinition definitionFromFbi(const std::string& keys)
        {
            auto tdf = parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=TEST;\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n");
            auto fbi = parseUnitFbi(tdf);
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        /** A gun emplacement that can see the whole of a test map. */
        UnitDefinition makeShooterDef(UnitFireOrders fireOrders, UnitMovementOrders moveOrders)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            d.fireStandOrders = true;
            d.mobileStandOrders = true;
            d.standingFireOrder = fireOrders;
            d.standingMoveOrder = moveOrders;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            return d;
        }

        void defineStandingOrdersWeapon(GameSimulation& sim, const std::string& name)
        {
            WeaponDefinition w{};
            w.maxRange = 400_ss;
            w.reloadTime = 1000_ss;
            w.burst = 1;
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 1;
            sim.weaponDefinitions[name] = w;
        }

        /**
         * Goes through trySpawnUnit rather than assembling a UnitState by
         * hand, because the seeding under test happens in createUnit.
         */
        UnitId spawnThroughTheOrdinaryPath(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos)
        {
            auto id = sim.trySpawnUnit(unitType, owner, pos, std::nullopt).value();
            sim.getUnitState(id).hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            return id;
        }

        void armWith(GameSimulation& sim, UnitId id, const std::string& weaponType)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
        }

        std::optional<UnitId> weaponTarget(const GameSimulation& sim, UnitId id)
        {
            const auto& weapon = sim.getUnitState(id).weapons[0];
            if (!weapon)
            {
                return std::nullopt;
            }

            auto attacking = std::get_if<UnitWeaponStateAttacking>(&weapon->state);
            if (attacking == nullptr)
            {
                return std::nullopt;
            }

            auto target = std::get_if<UnitId>(&attacking->target);
            return target == nullptr ? std::nullopt : std::optional<UnitId>(*target);
        }

        /** Visibility lands at the end of a tick, so acquisition is on the second. */
        void tickTwice(GameSimulation& sim)
        {
            sim.tick();
            sim.tick();
        }
    }

    TEST_CASE("the FBI names each unit's standing orders", "[standingorders]")
    {
        SECTION("a nuke silo is built holding its fire")
        {
            // ARMSILO offers the fire button and starts it on hold, which is
            // the whole reason a silo does not launch at the first scout that
            // wanders into range. It names no move keys at all.
            auto d = definitionFromFbi("BMcode=0;\nfirestandorders=1;\nStandingFireOrder=0;\n");
            REQUIRE(d.standingFireOrder == UnitFireOrders::HoldFire);
            REQUIRE(d.fireStandOrders);
            REQUIRE_FALSE(d.mobileStandOrders);
        }

        SECTION("the Commander is built holding position")
        {
            auto d = definitionFromFbi(
                "BMcode=1;\nfirestandorders=1;\nStandingFireOrder=2;\nmobilestandorders=1;\nStandingMoveOrder=0;\n");
            REQUIRE(d.standingMoveOrder == UnitMovementOrders::HoldPosition);
            REQUIRE(d.standingFireOrder == UnitFireOrders::FireAtWill);
            REQUIRE(d.mobileStandOrders);
            REQUIRE(d.fireStandOrders);
        }

        SECTION("a gun tower offers the fire button but not the move one")
        {
            auto d = definitionFromFbi("BMcode=0;\nfirestandorders=1;\nStandingFireOrder=2;\n");
            REQUIRE(d.fireStandOrders);
            REQUIRE_FALSE(d.mobileStandOrders);
        }

        SECTION("a unit that names nothing takes the original's parser defaults")
        {
            // Both keys default to 2 in the original, which in the shipped
            // data only ever lands on buildings: every mobile unit names a
            // StandingMoveOrder of its own.
            auto d = definitionFromFbi("BMcode=0;\n");
            REQUIRE(d.standingMoveOrder == UnitMovementOrders::Roam);
            REQUIRE(d.standingFireOrder == UnitFireOrders::FireAtWill);
            REQUIRE_FALSE(d.mobileStandOrders);
            REQUIRE_FALSE(d.fireStandOrders);
        }

        SECTION("a transport turns both buttons off outright")
        {
            auto d = definitionFromFbi("BMcode=1;\nfirestandorders=0;\nmobilestandorders=0;\nStandingMoveOrder=1;\n");
            REQUIRE_FALSE(d.mobileStandOrders);
            REQUIRE_FALSE(d.fireStandOrders);
        }

        SECTION("maneuver and return fire come through as themselves")
        {
            auto d = definitionFromFbi("BMcode=1;\nStandingMoveOrder=1;\nStandingFireOrder=1;\n");
            REQUIRE(d.standingMoveOrder == UnitMovementOrders::Maneuver);
            REQUIRE(d.standingFireOrder == UnitFireOrders::ReturnFire);
        }
    }

    TEST_CASE("a freshly built unit starts on the orders its definition names", "[standingorders]")
    {
        auto script = makeStandingOrdersScript();
        GameSimulation sim(makeStandingOrdersTerrain(), 0u, 0, 0);
        auto us = addStandingOrdersPlayer(sim, "us");
        registerStandingOrdersModel(sim);

        sim.unitDefinitions["SILO"] = makeShooterDef(UnitFireOrders::HoldFire, UnitMovementOrders::HoldPosition);
        sim.unitDefinitions["TANK"] = makeShooterDef(UnitFireOrders::FireAtWill, UnitMovementOrders::Maneuver);
        sim.unitScriptDefinitions["SILO"] = *script;
        sim.unitScriptDefinitions["TANK"] = *script;

        auto siloId = spawnThroughTheOrdinaryPath(sim, "SILO", us, SimVector(0_ss, 0_ss, 0_ss));
        auto tankId = spawnThroughTheOrdinaryPath(sim, "TANK", us, SimVector(300_ss, 0_ss, 0_ss));

        REQUIRE(sim.getUnitState(siloId).fireOrders == UnitFireOrders::HoldFire);
        REQUIRE(sim.getUnitState(siloId).moveOrders == UnitMovementOrders::HoldPosition);
        REQUIRE(sim.getUnitState(tankId).fireOrders == UnitFireOrders::FireAtWill);
        REQUIRE(sim.getUnitState(tankId).moveOrders == UnitMovementOrders::Maneuver);
    }

    TEST_CASE("a unit built on hold fire keeps quiet", "[standingorders]")
    {
        // The pair are identical but for the standing fire order, and they are
        // given the same enemy at the same distance, so anything that reaches
        // one reaches the other.
        auto script = makeStandingOrdersScript();
        GameSimulation sim(makeStandingOrdersTerrain(), 0u, 0, 0);
        auto us = addStandingOrdersPlayer(sim, "us");
        auto them = addStandingOrdersPlayer(sim, "them");
        registerStandingOrdersModel(sim);
        defineStandingOrdersWeapon(sim, "gun");

        sim.unitDefinitions["SILO"] = makeShooterDef(UnitFireOrders::HoldFire, UnitMovementOrders::HoldPosition);
        sim.unitDefinitions["TANK"] = makeShooterDef(UnitFireOrders::FireAtWill, UnitMovementOrders::Maneuver);
        sim.unitDefinitions["PREY"] = makeShooterDef(UnitFireOrders::HoldFire, UnitMovementOrders::HoldPosition);
        sim.unitScriptDefinitions["SILO"] = *script;
        sim.unitScriptDefinitions["TANK"] = *script;
        sim.unitScriptDefinitions["PREY"] = *script;

        auto siloId = spawnThroughTheOrdinaryPath(sim, "SILO", us, SimVector(-100_ss, 0_ss, 0_ss));
        auto tankId = spawnThroughTheOrdinaryPath(sim, "TANK", us, SimVector(100_ss, 0_ss, 0_ss));
        auto preyId = spawnThroughTheOrdinaryPath(sim, "PREY", them, SimVector(0_ss, 0_ss, 200_ss));

        armWith(sim, siloId, "gun");
        armWith(sim, tankId, "gun");

        tickTwice(sim);

        REQUIRE(weaponTarget(sim, tankId).has_value());
        REQUIRE(weaponTarget(sim, tankId)->value == preyId.value);
        REQUIRE_FALSE(weaponTarget(sim, siloId).has_value());
    }
}
