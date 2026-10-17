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

        /** A unit type nothing can finish, so a spawned one is always a nanoframe. */
        UnitDefinition makeUnfinishableDef(unsigned int hitPoints)
        {
            auto d = makeTargetDef(hitPoints);
            d.buildTime = 100u;
            return d;
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

    TEST_CASE("a small blast hurts a struck unit alone and spreads like any other blast when it strikes nothing", "[damage]")
    {
        // 0x49A049 sends a blast of AreaOfEffect 16 or less down the
        // single-target path only when it is also handed a unit (0x49A053): one
        // hit at scale 1.0 on that unit and nothing else. With no unit it goes
        // to the area routine at radius AreaOfEffect/2, falloff and all
        // (0x49A0A7). Forty-three of the hundred and thirty-six shipped
        // weapons sit under the line, the twenty-eight at AreaOfEffect eight
        // among them. BT-112.
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

        SECTION("with a struck unit, that unit takes the full damage whatever the falloff would say")
        {
            auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
            auto blastPosition = SimVector(boxEdge + 3_ss, 0_ss, 0_ss);
            auto projectile = makeBlast(us, blastPosition, 100, 4_ss, 0_ss);
            sim.doProjectileImpact(projectile, ImpactType::Normal, std::nullopt, targetId);
            REQUIRE(damageTaken(sim, targetId) == 100);
        }

        SECTION("with a struck unit, a neighbour inside the radius is left alone")
        {
            auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
            auto neighbourId = spawnDamageUnit(sim, "target", them, SimVector(16_ss, 0_ss, 0_ss), script);
            auto blastPosition = SimVector(boxEdge, 0_ss, 0_ss);
            auto projectile = makeBlast(us, blastPosition, 100, 8_ss, 0_ss);
            sim.doProjectileImpact(projectile, ImpactType::Normal, std::nullopt, targetId);
            REQUIRE(damageTaken(sim, targetId) == 100);
            REQUIRE(damageTaken(sim, neighbourId) == 0);
        }

        SECTION("a blast bigger than AreaOfEffect 16 ignores the struck unit and spreads")
        {
            auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
            auto blastPosition = SimVector(boxEdge + 50_ss, 0_ss, 0_ss);
            auto projectile = makeBlast(us, blastPosition, 1000, 100_ss, 0_ss);
            sim.doProjectileImpact(projectile, ImpactType::Normal, std::nullopt, targetId);
            REQUIRE(damageTaken(sim, targetId) == 250);
        }

        SECTION("with no struck unit, a small blast falls off like any other")
        {
            struct Case
            {
                SimScalar radius;
                SimScalar distance;
                unsigned int expected;
            };

            const Case cases[] = {
                {4_ss, 0_ss, 100},
                // AreaOfEffect 8, three units off: a quarter of the way in
                // from the rim, so a sixteenth of the damage.
                {4_ss, 3_ss, 6},
                // AreaOfEffect 16, six units off.
                {8_ss, 6_ss, 6},
                // At the radius itself, nothing (strictly inside only).
                {4_ss, 4_ss, 0},
            };

            for (const auto& c : cases)
            {
                auto targetId = spawnDamageUnit(sim, "target", them, targetPosition, script);
                auto blastPosition = SimVector(boxEdge + c.distance, 0_ss, 0_ss);
                REQUIRE(rweSqrt(sim.createBoundingBox(sim.getUnitState(targetId)).distanceSquared(blastPosition)) == c.distance);
                auto projectile = makeBlast(us, blastPosition, 100, c.radius, 0_ss);
                sim.doProjectileImpact(projectile, ImpactType::Normal);
                CAPTURE(simScalarToFloat(c.radius));
                CAPTURE(simScalarToFloat(c.distance));
                REQUIRE(damageTaken(sim, targetId) == c.expected);
                sim.quietlyKillUnit(targetId);
                sim.deleteDeadUnits();
            }
        }
    }

    TEST_CASE("the area routine stops listing at twenty units but keeps hitting a big unit once per square", "[damage]")
    {
        // The unit list holds twenty (0x49A28F). One met after it is full is
        // not recorded and is still processed, so it takes the hit again for
        // every further square it covers inside the blast. BT-112.
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto us = addDamagePlayer(sim, "us");
        auto them = addDamagePlayer(sim, "them");
        sim.unitDefinitions["small"] = makeTargetDef(100000);
        auto big = makeTargetDef(100000);
        big.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["big"] = big;
        registerDamageModel(sim, "model");

        // Twenty-one small units in the first row of squares, then the big one
        // two rows down, so the walk meets it after the list has filled.
        std::vector<UnitId> smalls;
        for (int i = 0; i < 20; ++i)
        {
            smalls.push_back(spawnDamageUnit(sim, "small", them, SimVector(SimScalar(static_cast<float>(i * 16 + 8)), 0_ss, 8_ss), script));
        }
        auto bigId = spawnDamageUnit(sim, "big", them, SimVector(160_ss, 0_ss, 64_ss), script);
        auto bigFootprint = sim.computeFootprintRegion(sim.getUnitState(bigId).position, sim.unitDefinitions.at("big").movementCollisionInfo);
        REQUIRE(bigFootprint.width * bigFootprint.height == 4);

        // Edge effectiveness 1 pays the full damage at every distance inside the radius.
        auto blastPosition = SimVector(160_ss, 0_ss, 64_ss);
        auto projectile = makeBlast(us, blastPosition, 100, 400_ss, 1_ss);
        sim.applyDamageInRadius(blastPosition, projectile.damageRadius, projectile);

        for (auto id : smalls)
        {
            REQUIRE(damageTaken(sim, id) == 100);
        }
        REQUIRE(damageTaken(sim, bigId) == 400);
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

    TEST_CASE("what actually earns a unit its veterancy", "[damage]")
    {
        // Two tests stand in front of the increment at 0x4869CA, and RWE used
        // to fail both. A kill counts only if the victim was finished
        // (0x4869A7 compares its build progress against zero) and only if the
        // attacker's player differs from the victim's owner (0x4869BA against
        // the owner byte at victim+0xFF) -- so friendly fire buys nothing, and
        // neither does flattening a half-built nanoframe.
        auto script = makeDamageScript();
        GameSimulation sim(makeDamageTerrain(64, 64), 0u, 0, 0);
        auto us = addDamagePlayer(sim, "us");
        auto them = addDamagePlayer(sim, "them");
        sim.unitDefinitions["target"] = makeTargetDef(100);
        sim.unitDefinitions["frame"] = makeUnfinishableDef(100);
        registerDamageModel(sim, "model");

        auto attackerId = spawnDamageUnit(sim, "target", us, SimVector(32_ss, 0_ss, 32_ss), script);

        SECTION("killing an enemy earns one")
        {
            auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.killUnit(id, attackerId);
            REQUIRE(sim.getUnitState(attackerId).kills == 1);
        }

        SECTION("killing one of your own earns nothing")
        {
            auto id = spawnDamageUnit(sim, "target", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.killUnit(id, attackerId);
            REQUIRE(sim.getUnitState(attackerId).kills == 0);
        }

        SECTION("flattening an enemy nanoframe earns nothing")
        {
            auto id = spawnDamageUnit(sim, "frame", them, SimVector(0_ss, 0_ss, 0_ss), script);
            REQUIRE(sim.getUnitState(id).isBeingBuilt(sim.unitDefinitions.at("frame")));
            sim.killUnit(id, attackerId);
            REQUIRE(sim.getUnitState(attackerId).kills == 0);
        }

        SECTION("a unit that dies with nobody to blame earns nobody anything")
        {
            auto id = spawnDamageUnit(sim, "target", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.killUnit(id, std::nullopt);
            REQUIRE(sim.getUnitState(attackerId).kills == 0);
        }

        SECTION("the player's Kills column is gated by the same two tests")
        {
            // The death-cause table at 0x486E64 is decoded now (issue #51):
            // the weapon entry raises the killer's `player+0xFC` at 0x486906
            // behind exactly the pair above, so the end-of-game chart counts
            // neither friendly fire nor flattened frames. This section used to
            // pin the opposite, as a marker that the question was open.
            auto friendly = spawnDamageUnit(sim, "target", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.killUnit(friendly, attackerId);
            REQUIRE(sim.getPlayer(us).unitsKilled == 0);

            auto frame = spawnDamageUnit(sim, "frame", them, SimVector(96_ss, 0_ss, 0_ss), script);
            sim.killUnit(frame, attackerId);
            REQUIRE(sim.getPlayer(us).unitsKilled == 0);

            auto enemy = spawnDamageUnit(sim, "target", them, SimVector(192_ss, 0_ss, 0_ss), script);
            sim.killUnit(enemy, attackerId);
            REQUIRE(sim.getPlayer(us).unitsKilled == 1);
        }

        SECTION("but the Losses column counts every death, friendly fire and all")
        {
            // The other half of the same entry, and it is not symmetric:
            // 0x4868C1 raises the victim owner's `player+0xFE` with no tests in
            // front of it at all. Only reclaim (cause 5) asks who did it, and
            // only causes 1 to 6 reach the table.
            auto friendly = spawnDamageUnit(sim, "target", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.killUnit(friendly, attackerId);
            REQUIRE(sim.getPlayer(us).unitsLost == 1);

            auto frame = spawnDamageUnit(sim, "frame", us, SimVector(96_ss, 0_ss, 0_ss), script);
            sim.killUnit(frame, attackerId);
            REQUIRE(sim.getPlayer(us).unitsLost == 2);
        }
    }
}
