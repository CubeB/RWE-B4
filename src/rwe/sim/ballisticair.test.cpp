#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeSeaTerrain()
        {
            // Ground well below the surface, so a ship floats and nothing is
            // refused for being under water -- this test is about the arc.
            Grid<unsigned char> heights(160, 160, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(20_ss, std::move(pieces));
        }

        /** ARM_ROY, the Crusader's heavy cannon: ballistic, 300 velocity, 660 range. */
        void defineHeavyCannon(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBallistic();
            w.maxRange = 660_ss;
            w.reloadTime = SimScalar(2.0f);
            w.burst = 1;
            w.velocity = 300_ss / 30_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = 165;
            sim.weaponDefinitions["heavycannon"] = w;
        }

        UnitDefinition makeShipDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.floater = true;
            d.maxHitPoints = 3000;
            d.buildTime = 0u;
            d.sightDistance = 500;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeAircraftDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.cruiseAltitude = 200_ss;
            d.maxHitPoints = 300;
            d.buildTime = 0u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            auto id = sim.tryAddUnit(std::move(unit)).value();

            // Put it back where it was asked for: an aircraft here is a
            // height, and placement would otherwise settle it on the ground.
            auto& placed = sim.getUnitState(id);
            placed.position = pos;
            placed.previousPosition = pos;
            if (sim.unitDefinitions.at(type).canFly)
            {
                placed.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                sim.flyingUnitsSet.insert(id);
            }
            return id;
        }
    }

    TEST_CASE("a ballistic gun refuses a target it cannot arc a shell to", "[targeting]")
    {
        // 0x49ABB0's fourth refusal, which RWE did not have: a ballistic
        // weapon whose solver finds no arc drops the candidate. It is what
        // keeps ships and tanks from firing at aeroplanes -- a play-test found
        // a Crusader shooting at aircraft, and nothing else in the game would
        // have stopped it, since that ship declares no bad target category and
        // the preference machinery is not a rule anyway.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeSeaTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["ship"] = makeShipDef();
        sim.unitDefinitions["aircraft"] = makeAircraftDef();
        registerModel(sim);
        defineHeavyCannon(sim);

        auto shipId = spawn(sim, "ship", us, SimVector(0_ss, 0_ss, 0_ss), script);
        {
            auto& ship = sim.getUnitState(shipId);
            UnitWeapon gun;
            gun.weaponType = "heavycannon";
            ship.weapons[0] = gun;
        }
        const auto& ship = sim.getUnitState(shipId);
        const auto& cannon = sim.weaponDefinitions.at("heavycannon");

        SECTION("a surface target well inside its range is fair game")
        {
            auto boatId = spawn(sim, "ship", them, SimVector(300_ss, 0_ss, 0_ss), script);
            REQUIRE(sim.weaponCanHitUnit(cannon, ship, sim.getUnitState(boatId)));
        }

        SECTION("an aircraft at cruise altitude across the gun's reach is not")
        {
            // Six hundred out and two hundred up. That is inside the gun's
            // 660 range and there is still no arc to it: a ten-a-tick shell
            // spends too much of itself climbing. So the rule is not "ships
            // cannot shoot at aircraft" -- it is that the far half of the
            // range is unusable against anything with height on you, which
            // over a whole engagement is most of it.
            auto planeId = spawn(sim, "aircraft", them, SimVector(600_ss, 200_ss, 0_ss), script);
            REQUIRE_FALSE(sim.weaponCanHitUnit(cannon, ship, sim.getUnitState(planeId)));
        }

        SECTION("the same aircraft closer in is fair game, and that is the original too")
        {
            // Three hundred out at the same height has an arc, and the
            // original would take the shot. There is no rule anywhere in it
            // that refuses an ordinary weapon an airborne target.
            auto planeId = spawn(sim, "aircraft", them, SimVector(300_ss, 200_ss, 0_ss), script);
            REQUIRE(sim.weaponCanHitUnit(cannon, ship, sim.getUnitState(planeId)));
        }

        SECTION("an aircraft that comes down low enough is fair game again")
        {
            // Nothing here is about the target being an aircraft -- it is
            // about the arc. One flying at wave-top height can be shot at.
            auto planeId = spawn(sim, "aircraft", them, SimVector(120_ss, 4_ss, 0_ss), script);
            REQUIRE(sim.weaponCanHitUnit(cannon, ship, sim.getUnitState(planeId)));
        }
    }
}
