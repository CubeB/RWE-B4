#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeParalyzerTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addParalyzerPlayer(GameSimulation& sim, const std::string& name)
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

        std::shared_ptr<CobScript> makeParalyzerScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerParalyzerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineVictim(GameSimulation& sim, const std::string& type, bool immune)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = false;
            d.maxVelocity = 4_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.sightDistance = 1000u;
            d.maxHitPoints = 3000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.immuneToParalyzer = immune;
            d.category = "CORE KBOT NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        /**
         * ARMEMP_WEAPON, the EMP missile: `paralyzer=1`, and a [DAMAGE] table
         * whose entries are stun times in ticks -- 1800 against every CORE unit.
         */
        void defineEmp(GameSimulation& sim, const std::string& name, unsigned int damage)
        {
            WeaponDefinition w{};
            w.maxRange = 32000_ss;
            w.damageRadius = 256_ss;
            w.edgeEffectiveness = 1.0_ssf;
            w.paralyzer = true;
            w.damage["DEFAULT"] = damage;
            sim.weaponDefinitions[name] = w;
        }

        void defineShell(GameSimulation& sim, const std::string& name, unsigned int damage)
        {
            WeaponDefinition w{};
            w.maxRange = 600_ss;
            w.damageRadius = 256_ss;
            w.edgeEffectiveness = 1.0_ssf;
            w.damage["DEFAULT"] = damage;
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnParalyzerUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        /** Drops one round of `weaponType` right on top of a unit. */
        void hit(GameSimulation& sim, const std::string& weaponType, const SimVector& position, PlayerId owner)
        {
            Projectile p{};
            p.weaponType = weaponType;
            p.owner = owner;
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            const auto& w = sim.weaponDefinitions.at(weaponType);
            p.damage = w.damage;
            p.damageRadius = w.damageRadius;
            p.edgeEffectiveness = w.edgeEffectiveness;
            sim.doProjectileImpact(p, ImpactType::Normal);
        }
    }

    // Priority 13. `paralyzer` is bit 7 of `wdef+0x111` (0x42EB95). A hit from
    // one becomes damage type 2 (0x499E20), which the damage handler routes down
    // its own branch at 0x489DEB -- returning before the hit-point subtraction at
    // 0x489EB1 -- and turns into a sleeping order whose duration is the damage
    // number in ticks (0x402D10).
    TEST_CASE("a paralyzer stuns instead of hurting", "[paralyzer]")
    {
        GameSimulation sim(makeParalyzerTerrain(), 0u, 0, 0);
        registerParalyzerModel(sim);
        auto script = makeParalyzerScript();

        auto attacker = addParalyzerPlayer(sim, "arm");
        auto defender = addParalyzerPlayer(sim, "core");

        defineVictim(sim, "victim", false);
        defineVictim(sim, "commander", true);

        SECTION("it takes no hit points off at all")
        {
            // The whole point of the EMP missile: 1800 against a CORE unit is a
            // minute of paralysis, not eighteen hundred points of damage.
            defineEmp(sim, "emp", 1800u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            REQUIRE(sim.getUnitState(victim).hitPoints == 3000u);
            REQUIRE(sim.getUnitState(victim).isParalyzed(sim.gameTime));
        }

        SECTION("the stun lasts as many ticks as the damage number")
        {
            defineEmp(sim, "emp", 90u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            REQUIRE(sim.getUnitState(victim).paralyzedUntil == sim.gameTime + GameTime(90));
        }

        SECTION("a stunned unit does not carry out its orders")
        {
            defineEmp(sim, "emp", 300u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(victim).addOrder(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            auto startPosition = sim.getUnitState(victim).position;
            for (int i = 0; i < 200; ++i)
            {
                sim.tick();
            }

            // It has not moved, and it still has the order waiting for it: the
            // original's stun sits in front of the order list rather than
            // clearing it, so a unit picks up where it left off.
            REQUIRE(sim.getUnitState(victim).position.distance(startPosition) < 1_ss);
            REQUIRE_FALSE(sim.getUnitState(victim).orders.empty());
        }

        SECTION("it moves again once the stun wears off")
        {
            defineEmp(sim, "emp", 60u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(victim).addOrder(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            auto startPosition = sim.getUnitState(victim).position;
            for (int i = 0; i < 200; ++i)
            {
                sim.tick();
            }

            REQUIRE_FALSE(sim.getUnitState(victim).isParalyzed(sim.gameTime));
            REQUIRE(sim.getUnitState(victim).position.distance(startPosition) > 100_ss);
        }

        SECTION("a second hit adds to what is left of the first")
        {
            defineEmp(sim, "emp", 300u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            for (int i = 0; i < 100; ++i)
            {
                sim.tick();
            }

            // Two hundred ticks left, plus three hundred more.
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);
            REQUIRE(sim.getUnitState(victim).paralyzedUntil == sim.gameTime + GameTime(500));
        }

        SECTION("stacked hits are capped at a minute")
        {
            defineEmp(sim, "emp", 1800u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            REQUIRE(sim.getUnitState(victim).paralyzedUntil == sim.gameTime + GameTime(GameSimulation::MaxParalysisTicks));
        }

        SECTION("immunetoparalyzer shrugs it off")
        {
            // Both commanders carry the flag, which is why an EMP missile
            // cannot simply switch a game off.
            defineEmp(sim, "emp", 1800u);

            auto victim = spawnParalyzerUnit(sim, "commander", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "emp", sim.getUnitState(victim).position, attacker);

            REQUIRE_FALSE(sim.getUnitState(victim).isParalyzed(sim.gameTime));
            REQUIRE(sim.getUnitState(victim).hitPoints == 3000u);
        }

        SECTION("an ordinary weapon still takes hit points and stuns nothing")
        {
            defineShell(sim, "shell", 500u);

            auto victim = spawnParalyzerUnit(sim, "victim", defender, SimVector(0_ss, 0_ss, 0_ss), script);
            hit(sim, "shell", sim.getUnitState(victim).position, attacker);

            REQUIRE(sim.getUnitState(victim).hitPoints == 2500u);
            REQUIRE_FALSE(sim.getUnitState(victim).isParalyzed(sim.gameTime));
        }
    }
}
