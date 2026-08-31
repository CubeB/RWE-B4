#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeDamageTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addDamagePlayer(GameSimulation& sim, const std::string& name)
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

        std::shared_ptr<CobScript> makeDamageScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerDamageModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** A stand-in for anything that just sits there and gets shot. */
        UnitDefinition makeTargetDef(unsigned int hitPoints)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = hitPoints;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnDamageUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            return sim.tryAddUnit(std::move(unit)).value();
        }

        Projectile makeBlast(PlayerId owner, const SimVector& position, unsigned int damage, SimScalar radius, SimScalar edgeEffectiveness)
        {
            Projectile p{};
            p.owner = owner;
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            p.damage["DEFAULT"] = damage;
            p.damageRadius = radius;
            p.edgeEffectiveness = edgeEffectiveness;
            return p;
        }

        /** Damage actually taken, so the test does not care how the unit stores it. */
        unsigned int damageTaken(GameSimulation& sim, UnitId unitId)
        {
            const auto& unit = sim.getUnitState(unitId);
            return sim.unitDefinitions.at(unit.unitType).maxHitPoints - unit.hitPoints;
        }
    }

    TEST_CASE("blast damage falls away quadratically and lands on edgeeffectiveness at the rim", "[damage]")
    {
        // TotalA.exe 0x49A3B6 computes (1 - d/r)^2 * (1 - E) + E. Two things
        // follow that a straight line does not give you: an ordinary weapon
        // loses damage much faster than distance, and a weapon that names an
        // EdgeEffectiveness keeps that fraction all the way out.
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto us = addDamagePlayer(sim, "us");
        auto them = addDamagePlayer(sim, "them");
        sim.unitDefinitions["target"] = makeTargetDef(100000);
        registerDamageModel(sim, "model");

        // Distance is measured to the unit's bounding box, not to its origin,
        // so the offsets below are worked out from the box the simulation
        // actually builds and checked before each case is trusted.
        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto probeId = spawnDamageUnit(sim, "target", them, targetPosition, script);
        auto probeBox = sim.createBoundingBox(sim.getUnitState(probeId));
        auto boxEdge = probeBox.center.x + probeBox.extents.x;
        sim.quietlyKillUnit(probeId);
        sim.deleteDeadUnits();

        struct Case
        {
            SimScalar distance;
            SimScalar edgeEffectiveness;
            unsigned int expected;
        };

        // Radius 100, damage 1000.
        const Case cases[] = {
            {0_ss, 0_ss, 1000},     // dead centre: full damage
            {50_ss, 0_ss, 250},     // half way out: a quarter, not a half
            {99_ss, 0_ss, 0},       // all but at the rim: nothing left
            {0_ss, SimScalar(0.5f), 1000},
            {50_ss, SimScalar(0.5f), 625}, // half way out with a floor of 0.5
            {99_ss, SimScalar(0.5f), 500}, // the rim pays out the floor itself
            // 924 rather than 925: the scale lands a hair under 0.925 and the
            // result is truncated, which is what the original's ftol does too.
            {50_ss, SimScalar(0.9f), 924},
            {99_ss, SimScalar(0.9f), 900},
        };

        for (const auto& c : cases)
        {
            auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
            auto blastPosition = SimVector(boxEdge + c.distance, 0_ss, 0_ss);
            REQUIRE(rweSqrt(sim.createBoundingBox(sim.getUnitState(targetId)).distanceSquared(blastPosition)) == c.distance);
            auto projectile = makeBlast(us, blastPosition, 1000, 100_ss, c.edgeEffectiveness);
            sim.applyDamageInRadius(blastPosition, projectile.damageRadius, projectile);
            REQUIRE(damageTaken(sim, targetId) == c.expected);
            sim.quietlyKillUnit(targetId);
            sim.deleteDeadUnits();
        }
    }

    TEST_CASE("a blast too small to spread pays out in full", "[damage]")
    {
        // The original stops bothering with falloff below an AreaOfEffect of
        // sixteen (0x49A049) and pays a flat 1.0 (0x49A05B). Our radius is
        // half the AreaOfEffect, so the line is at eight. Forty-three of the
        // hundred and thirty-six shipped weapons sit under it, including the
        // twenty-eight at AreaOfEffect eight, so getting this wrong would take
        // most of the bite out of ordinary small-arms fire.
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto us = addDamagePlayer(sim, "us");
        auto them = addDamagePlayer(sim, "them");
        sim.unitDefinitions["target"] = makeTargetDef(100000);
        registerDamageModel(sim, "model");

        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);
        auto probeId = spawnDamageUnit(sim, "target", them, targetPosition, script);
        auto probeBox = sim.createBoundingBox(sim.getUnitState(probeId));
        auto boxEdge = probeBox.center.x + probeBox.extents.x;
        sim.quietlyKillUnit(probeId);
        sim.deleteDeadUnits();

        struct Case
        {
            SimScalar radius;
            SimScalar distance;
            unsigned int expected;
        };

        const Case cases[] = {
            // AreaOfEffect 8, the commonest value in the game: full damage
            // even three quarters of the way out, where the curve would have
            // paid a sixteenth.
            {4_ss, 0_ss, 100},
            {4_ss, 3_ss, 100},
            // AreaOfEffect 16 is still inside the rule.
            {8_ss, 6_ss, 100},
            // One unit wider and the curve takes over again.
            {9_ss, 6_ss, 11},
        };

        for (const auto& c : cases)
        {
            auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
            auto blastPosition = SimVector(boxEdge + c.distance, 0_ss, 0_ss);
            REQUIRE(rweSqrt(sim.createBoundingBox(sim.getUnitState(targetId)).distanceSquared(blastPosition)) == c.distance);
            auto projectile = makeBlast(us, blastPosition, 100, c.radius, 0_ss);
            sim.applyDamageInRadius(blastPosition, projectile.damageRadius, projectile);
            CAPTURE(simScalarToFloat(c.radius));
            CAPTURE(simScalarToFloat(c.distance));
            REQUIRE(damageTaken(sim, targetId) == c.expected);
            sim.quietlyKillUnit(targetId);
            sim.deleteDeadUnits();
        }
    }

    TEST_CASE("armour halves what an armoured unit takes until the hit is big enough to ignore it", "[damage]")
    {
        // TotalA.exe 0x489BC3: (damage * DamageModifier) >> 16, but only while
        // the script has said ARMORED and only below 30000. The cut-out is why
        // a D-gun goes straight through a closed solar collector.
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto them = addDamagePlayer(sim, "them");
        registerDamageModel(sim, "model");

        auto solar = makeTargetDef(1000000);
        solar.damageModifier = simScalarToFixed(SimScalar(0.33333f));
        sim.unitDefinitions["solar"] = solar;

        auto plain = makeTargetDef(1000000);
        sim.unitDefinitions["plain"] = plain;

        SECTION("an unarmoured unit is untouched by its own DamageModifier")
        {
            auto id = spawnDamageUnit(sim, "solar", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.applyDamage(id, 300);
            REQUIRE(damageTaken(sim, id) == 300);
        }

        SECTION("an armoured unit takes its DamageModifier share")
        {
            auto id = spawnDamageUnit(sim, "solar", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(id).armored = true;
            sim.applyDamage(id, 300);
            REQUIRE(damageTaken(sim, id) == 99);
        }

        SECTION("armour with no DamageModifier in the FBI changes nothing")
        {
            auto id = spawnDamageUnit(sim, "plain", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(id).armored = true;
            sim.applyDamage(id, 300);
            REQUIRE(damageTaken(sim, id) == 300);
        }

        SECTION("a hit just under the threshold is still reduced")
        {
            auto id = spawnDamageUnit(sim, "solar", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(id).armored = true;
            sim.applyDamage(id, 29999);
            REQUIRE(damageTaken(sim, id) == 9999);
        }

        SECTION("a hit at the threshold ignores armour entirely")
        {
            auto id = spawnDamageUnit(sim, "solar", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(id).armored = true;
            sim.applyDamage(id, 30000);
            REQUIRE(damageTaken(sim, id) == 30000);
        }
    }

    TEST_CASE("veterans hit harder and are hit softer", "[damage]")
    {
        // Two separate steps in the original, both bucketing kills into tiers
        // of five and clamping at five: the attacker gains 6% a tier
        // (0x499DAE) and the victim sheds 4% a tier (0x489BF3).
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto us = addDamagePlayer(sim, "us");
        auto them = addDamagePlayer(sim, "them");
        sim.unitDefinitions["target"] = makeTargetDef(1000000);
        registerDamageModel(sim, "model");

        auto attackerId = spawnDamageUnit(sim, "target", us, SimVector(32_ss, 0_ss, 32_ss), script);

        SECTION("a green attacker and a green victim trade at face value")
        {
            auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.applyDamage(id, 1000, attackerId);
            REQUIRE(damageTaken(sim, id) == 1000);
        }

        SECTION("the attacker's kills raise the damage a tier at a time")
        {
            struct Case
            {
                unsigned int kills;
                unsigned int expected;
            };
            const Case cases[] = {
                {4, 1000},
                {5, 1060},
                {14, 1120},
                {25, 1300},
                {1000, 1300}, // capped at five tiers
            };
            for (const auto& c : cases)
            {
                sim.getUnitState(attackerId).kills = c.kills;
                auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
                sim.applyDamage(id, 1000, attackerId);
                REQUIRE(damageTaken(sim, id) == c.expected);
                sim.quietlyKillUnit(id);
                sim.deleteDeadUnits();
            }
        }

        SECTION("the victim's own kills take damage back off again")
        {
            struct Case
            {
                unsigned int kills;
                unsigned int expected;
            };
            const Case cases[] = {
                {4, 1000},
                {5, 960},
                {14, 920},
                {25, 800},
                {1000, 800}, // capped at five tiers
            };
            for (const auto& c : cases)
            {
                auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
                sim.getUnitState(id).kills = c.kills;
                sim.applyDamage(id, 1000);
                REQUIRE(damageTaken(sim, id) == c.expected);
                sim.quietlyKillUnit(id);
                sim.deleteDeadUnits();
            }
        }

        SECTION("damage with no known attacker gets no veterancy bonus")
        {
            sim.getUnitState(attackerId).kills = 25;
            auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.applyDamage(id, 1000);
            REQUIRE(damageTaken(sim, id) == 1000);
        }

        SECTION("the attacker's bonus is taken before armour, the victim's discount after")
        {
            // The original's order: 0x499DAE, then 0x489BC3, then 0x489BF3.
            // 1000 * 1.30 = 1300; 1300 * 0.5 = 650; 650 * 0.80 = 520.
            auto armouredDef = makeTargetDef(1000000);
            armouredDef.damageModifier = simScalarToFixed(SimScalar(0.5f));
            sim.unitDefinitions["armoured"] = armouredDef;

            sim.getUnitState(attackerId).kills = 25;
            auto id = spawnDamageUnit(sim, "armoured", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(id).armored = true;
            sim.getUnitState(id).kills = 25;
            sim.applyDamage(id, 1000, attackerId);
            REQUIRE(damageTaken(sim, id) == 520);
        }
    }
}
