#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        const SimScalar SeaLevel = 20_ss;

        /** A flat seabed at height zero under twenty units of water. */
        MapTerrain makeFloodedTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), SeaLevel);
        }

        /**
         * ARMKBOT_MISSILE out of MISSILES.TDF, the Skeeter's second weapon:
         * off the rail at 450, up to 650 at 101, 33000 a second of turn, and
         * 31 damage to anything not named in its [DAMAGE] block.
         */
        void defineSkeeterMissile(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 604_ss;
            w.reloadTime = SimScalar(2.4f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 650_ss / 30_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = 31;

            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = 450_ss / 30_ss;
            p.acceleration = 101_ss / 900_ss;
            p.maxVelocity = 650_ss / 30_ss;
            p.turnRate = SimAngle(33000u / 30u);
            p.guidance = true;
            p.tracks = true;
            p.twoPhase = false;
            p.vLaunch = false;
            p.flightTime = GameTime(0);
            p.burnBlow = false;
            p.cruise = false;
            p.autoRange = true;
            w.physicsType = p;

            sim.weaponDefinitions["ARMKBOT_MISSILE"] = w;
        }

        /**
         * CORSTORM wading on the seabed: a 2x2 footprint, `MaxWaterDepth=21`,
         * and the 25.94 its 3DO stands to, so its origin is under the water
         * and its top is out of it.
         */
        UnitId addWadingStorm(GameSimulation& sim, PlayerId owner)
        {
            UnitDefinition d{};
            d.objectName = "CORSTORM";
            d.isMobile = true;
            d.maxHitPoints = 1000;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 21u};
            sim.unitDefinitions["CORSTORM"] = d;

            UnitModelDefinition model{};
            model.height = simScalarFromFixed(1700296);
            sim.unitModelDefinitions["CORSTORM"] = model;

            auto script = makeEmptyCobScript();
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "CORSTORM";
            unit.owner = owner;
            unit.position = SimVector(0_ss, 0_ss, 0_ss);
            unit.previousPosition = unit.position;
            unit.hitPoints = 1000;
            unit.buildTimeCompleted = 0;
            auto id = sim.tryAddUnit(std::move(unit));
            REQUIRE(id.has_value());
            return *id;
        }

        struct Shot
        {
            ProjectileDiedEvent::DeathType deathType;
            bool stormDamaged;
        };

        /**
         * Fires the missile at the Storm from `outside` units east of its
         * footprint's edge and `aboveSea` units over the water, pointed at the
         * Storm's origin -- which is where the original steers a missile with
         * a unit target (0x49B495 returns `unit+0x6A`) -- and says how the
         * missile died.
         */
        Shot fireAtStorm(SimScalar outside, SimScalar aboveSea, bool noSeaLevelTrigger)
        {
            GameSimulation sim(makeFloodedTerrain(64, 64), 0u, 0, 0);
            sim.noSeaLevelTrigger = noSeaLevelTrigger;
            auto skeeter = addPlayer(sim, "skeeter");
            auto storm = addPlayer(sim, "storm");
            defineSkeeterMissile(sim);
            auto stormId = addWadingStorm(sim, storm);

            const auto& stormState = sim.getUnitState(stormId);
            auto footprint = sim.computeFootprintRegion(stormState.position, 2u, 2u);
            auto eastEdge = sim.terrain.heightmapIndexToWorldCorner(footprint.x + footprint.width, footprint.y).x;

            auto from = SimVector(eastEdge + outside, SeaLevel + aboveSea, stormState.position.z);
            auto toTarget = stormState.position - from;

            UnitWeapon weapon;
            weapon.weaponType = "ARMKBOT_MISSILE";
            sim.spawnProjectile(ProjectileSpawn{
                .owner = skeeter,
                .weapon = &weapon,
                .position = from,
                .direction = toTarget.normalized(),
                .distanceToTarget = toTarget.length(),
                .targetUnit = stormId,
                .attacker = std::nullopt,
                .inheritedVelocity = std::nullopt,
                .targetPosition = stormState.position});

            for (int ticks = 0; ticks < 300; ++ticks)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    if (const auto* died = std::get_if<ProjectileDiedEvent>(&e); died != nullptr)
                    {
                        return Shot{died->deathType, sim.getUnitState(stormId).hitPoints < 1000};
                    }
                }
            }

            FAIL("the missile never came down");
            return Shot{};
        }
    }

    TEST_CASE("a missile that reaches sea level over a wading unit's footprint hits the unit", "[weapon][water]")
    {
        // Eight units up and eight out, the missile's first step takes it past
        // the edge and below the surface at once. The original tests the
        // square's units before the water (0x49B090), so the Storm takes it.
        auto shot = fireAtStorm(8_ss, 8_ss, false);
        CHECK(shot.deathType == ProjectileDiedEvent::DeathType::NormalImpact);
        CHECK(shot.stormDamaged);
    }

    TEST_CASE("a missile that reaches sea level short of a wading unit hits the water", "[weapon][water]")
    {
        // Aimed at an origin twenty units under the surface, the missile is in
        // the water well before it is over the footprint -- in the original too.
        auto shot = fireAtStorm(80_ss, 10_ss, false);
        CHECK(shot.deathType == ProjectileDiedEvent::DeathType::WaterImpact);
        CHECK_FALSE(shot.stormDamaged);
    }

    TEST_CASE("on a map with nosealeveltrigger a missile goes through the water to its target", "[weapon][water]")
    {
        auto shot = fireAtStorm(80_ss, 10_ss, true);
        CHECK(shot.deathType == ProjectileDiedEvent::DeathType::NormalImpact);
        CHECK(shot.stormDamaged);
    }
}
