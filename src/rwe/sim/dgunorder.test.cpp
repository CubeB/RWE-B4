#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerModel(GameSimulation& sim)
        {
            // Twenty units up, because the piece doubles as the muzzle: a
            // shot spawned at ground level buries itself on the tick it is
            // created.
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** ARM_DISINTEGRATOR, out of WEAPONS.TDF: 240 range, 400 energy a shot. */
        void defineDisintegrator(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 240_ss;
            w.reloadTime = SimScalar(1.2f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            // weaponvelocity is per second in the TDF; the field here is
            // per tick, and a bolt thirty times too fast tunnels straight
            // through what it was aimed at.
            w.velocity = 200_ss / 30_ss;
            w.damageRadius = 24_ss;
            w.energyPerShot = Energy(400.0f);
            w.commandFire = true;
            w.damage["DEFAULT"] = 30000;
            sim.weaponDefinitions["disintegrator"] = w;

            // Weapon1, the commander's ordinary laser: no commandfire, so it
            // picks its own targets and has nothing to do with the D-gun.
            WeaponDefinition laser{};
            laser.physicsType = ProjectilePhysicsTypeLineOfSight();
            laser.maxRange = 250_ss;
            laser.reloadTime = SimScalar(1.0f);
            laser.burst = 1;
            laser.velocity = 200_ss / 30_ss;
            laser.damage["DEFAULT"] = 20;
            sim.weaponDefinitions["laser"] = laser;
        }

        UnitDefinition makeCommanderDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canDgun = true;
            d.maxVelocity = 1_ss;
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.1_ssf;
            d.turnRate = 360_ss;
            d.maxHitPoints = 3000;
            d.buildTime = 0u;
            d.sightDistance = 300;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeVictimDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 500;
            d.buildTime = 0u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /** A commander with its laser in slot 0 and its disintegrator in slot 2. */
        UnitId spawnCommander(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto id = spawnUnit(sim, "commander", owner, pos, script);
            auto& unit = sim.getUnitState(id);
            UnitWeapon laser;
            laser.weaponType = "laser";
            unit.weapons[0] = laser;
            UnitWeapon dgun;
            dgun.weaponType = "disintegrator";
            unit.weapons[2] = dgun;
            return id;
        }

        PlayerId addRichPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }
    }

    TEST_CASE("a D-gun order fires the third weapon and then ends", "[dgun]")
    {
        // The BLAST button had no order behind it at all, so the D-gun could
        // not be fired: a commandfire weapon is invisible to every automatic
        // path by design (0x408A88), and an order is the only thing that can
        // ever point it at anything.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addRichPlayer(sim, "us");
        auto them = addRichPlayer(sim, "them");
        sim.unitDefinitions["commander"] = makeCommanderDef();
        sim.unitDefinitions["victim"] = makeVictimDef();
        registerModel(sim);
        defineDisintegrator(sim);

        auto commanderId = spawnCommander(sim, us, SimVector(0_ss, 0_ss, 0_ss), script);

        SECTION("at a unit standing inside its range")
        {
            // Well inside the disintegrator's 240.
            auto victimId = spawnUnit(sim, "victim", them, SimVector(100_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).orders.push_back(DgunOrder(victimId));

            auto energyBefore = sim.getPlayer(us).energy;

            for (int tick = 0; tick < 180; ++tick)
            {
                sim.tick();
            }

            // Thirty thousand damage against five hundred hit points: the
            // disintegrator does not chip at things.
            REQUIRE(!sim.units.tryGet(victimId).has_value());
            REQUIRE(sim.getPlayer(us).energy < energyBefore);
        }

        SECTION("one shot, and the order is done")
        {
            auto victimId = spawnUnit(sim, "victim", them, SimVector(100_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).orders.push_back(DgunOrder(victimId));

            for (int tick = 0; tick < 120; ++tick)
            {
                sim.tick();
            }

            // Any commandfire shot raises the original's event bit 11 and the
            // attack handler deletes its own mission on it (0x4034AF). The
            // D-gun is not held down.
            REQUIRE(sim.getUnitState(commanderId).orders.empty());
        }

        SECTION("at bare ground, which the original allows")
        {
            // 0x43F7E8 tests candgun and nothing else -- not the target, not
            // the position -- so a click on empty dirt is a valid D-gun order.
            sim.getUnitState(commanderId).orders.push_back(DgunOrder(SimVector(100_ss, 0_ss, 0_ss)));

            auto energyBefore = sim.getPlayer(us).energy;

            for (int tick = 0; tick < 120; ++tick)
            {
                sim.tick();
            }

            // Nothing to damage and nothing left in the air to look at: the
            // beam covers a hundred units inside one tick. What the shot
            // leaves behind is the bill.
            REQUIRE(sim.getPlayer(us).energy < energyBefore);
            REQUIRE(sim.getUnitState(commanderId).orders.empty());
        }

        SECTION("walking into range first")
        {
            // Beyond 240, so the order is mostly a move order.
            auto victimId = spawnUnit(sim, "victim", them, SimVector(400_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).orders.push_back(DgunOrder(victimId));

            // Out of range on the first tick, so the weapon is idle and the
            // order is a move order for now.
            sim.tick();
            REQUIRE(sim.getUnitState(commanderId).weapons[2]->state.index() == 0);

            for (int tick = 0; tick < 1200; ++tick)
            {
                sim.tick();
            }

            REQUIRE(!sim.units.tryGet(victimId).has_value());
        }

        SECTION("with the stores empty, it waits rather than losing the order")
        {
            // Short of energypershot the original raises no event at all --
            // the cost check at 0x49E420 jumps to the loop tail -- so the
            // commander stands there holding the target and fires on the
            // first tick it can pay.
            auto& player = sim.getPlayer(us);
            player.energy = Energy(0.0f);

            auto victimId = spawnUnit(sim, "victim", them, SimVector(100_ss, 0_ss, 0_ss), script);
            sim.getUnitState(commanderId).orders.push_back(DgunOrder(victimId));

            for (int tick = 0; tick < 60; ++tick)
            {
                sim.tick();
            }

            bool fired = false;
            for ([[maybe_unused]] const auto& p : sim.projectiles)
            {
                fired = true;
            }
            REQUIRE(!fired);
            REQUIRE(!sim.getUnitState(commanderId).orders.empty());
        }
    }
}
