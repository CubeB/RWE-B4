#include <algorithm>
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
        // square's units before the water (0x49B090), so the Storm takes it --
        // and throws up spray, the square being under the sea (0x499ECF).
        auto shot = fireAtStorm(8_ss, 8_ss, false);
        CHECK(shot.stormDamaged);
        CHECK(shot.deathType == ProjectileDiedEvent::DeathType::WaterImpact);
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
        // Still spray when it gets there: the art is the square's, and
        // nosealeveltrigger switches off the sea test and nothing else.
        auto shot = fireAtStorm(80_ss, 10_ss, true);
        CHECK(shot.stormDamaged);
        CHECK(shot.deathType == ProjectileDiedEvent::DeathType::WaterImpact);
    }

    namespace
    {
        /** A plain shell, with `groundbounce` or without. */
        void defineShell(GameSimulation& sim, bool groundBounce)
        {
            WeaponDefinition w{};
            w.maxRange = 300_ss;
            w.reloadTime = 1_ss;
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 8_ss;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 10;
            w.physicsType = ProjectilePhysicsTypeBallistic();
            w.groundBounce = groundBounce;
            sim.weaponDefinitions["SHELL"] = w;
        }

        /**
         * Drops a shell from `from` at eight units a tick and runs one tick:
         * the deaths it causes, and whether the shell is still in the air.
         */
        std::pair<std::vector<ProjectileDiedEvent>, bool> dropShell(GameSimulation& sim, const SimVector& from)
        {
            auto gunner = addPlayer(sim, "gunner");
            sim.spawnProjectile(ProjectileSpawn{
                .owner = gunner,
                .weaponType = "SHELL",
                .position = from,
                .direction = SimVector(0_ss, -1_ss, 0_ss),
                .distanceToTarget = 100_ss});
            REQUIRE(sim.projectiles.begin() != sim.projectiles.end());
            sim.projectiles.begin()->second.velocity = SimVector(0_ss, -8_ss, 0_ss);

            sim.events.clear();
            sim.tick();
            std::vector<ProjectileDiedEvent> deaths;
            for (const auto& e : sim.events)
            {
                if (const auto* died = std::get_if<ProjectileDiedEvent>(&e); died != nullptr)
                {
                    deaths.push_back(*died);
                }
            }
            auto flying = std::any_of(sim.projectiles.begin(), sim.projectiles.end(), [](const auto& p) { return !p.second.isDead; });
            return {deaths, flying};
        }
    }

    TEST_CASE("a bouncing round that passes the sea and the ground in one tick bounces", "[weapon][water]")
    {
        // Issue #350. Four units of water over a seabed at 16: a shell at 21
        // falling eight a tick goes through the surface and into the ground
        // in one step. The original tests the ground before the sea
        // (0x49B36D, then 0x49B3A1), so a `groundbounce` round bounces off
        // the seabed rather than going out in the water.
        Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(16));
        GameSimulation sim(MapTerrain(std::move(heights), SeaLevel), 0u, 0, 0);
        defineShell(sim, true);

        auto [deaths, flying] = dropShell(sim, SimVector(0_ss, 21_ss, 0_ss));
        CHECK(deaths.empty());
        CHECK(flying);
    }

    TEST_CASE("a bouncing round rises at a quarter of its fall and is left where it struck", "[weapon][water]")
    {
        // 0x49B37F: vy becomes -(vy >> 2) and the position is not touched.
        // Dry ground at 16 and nothing else, so only the ground is in play.
        Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(16));
        GameSimulation sim(MapTerrain(std::move(heights), 0_ss), 0u, 0, 0);
        defineShell(sim, true);

        auto [deaths, flying] = dropShell(sim, SimVector(0_ss, 21_ss, 0_ss));
        REQUIRE(deaths.empty());
        REQUIRE(flying);
        const auto& shell = sim.projectiles.begin()->second;
        // Eight a tick down and a tick's gravity on top, then a quarter of
        // that back up.
        auto fall = -8_ss - (112_ss / (30_ss * 30_ss));
        CHECK(shell.velocity.y == -(fall / 4_ss));
        // Left under the ground: 21 less the fall, not put back at 21.
        CHECK(shell.position.y == 21_ss + fall);
        CHECK(shell.position.y < 16_ss);
    }

    TEST_CASE("a round's splash is the square's, not the surface it struck", "[weapon][water]")
    {
        // A seabed at 0 under twenty units of water, with one corner of one
        // square standing at 30. A shell that goes into the water near that
        // square's low corner struck the sea, but the square is not wholly
        // under it, so it throws up earth (0x499ECF). One square over, every
        // corner is under the sea, and the same shell splashes.
        auto dropNear = [](SimScalar dx) {
            Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(0));
            heights.get(8, 8) = 30;
            GameSimulation sim(MapTerrain(std::move(heights), SeaLevel), 0u, 0, 0);
            defineShell(sim, false);

            auto corner = sim.terrain.heightmapIndexToWorldCorner(7, 7);
            auto at = SimVector(corner.x + dx, 12_ss, corner.z + 2_ss);
            REQUIRE(sim.terrain.getHeightAt(at.x, at.z) < 4_ss);
            auto underSea = sim.terrain.isSquareUnderSea(at.x, at.z);

            auto [deaths, flying] = dropShell(sim, at);
            REQUIRE(deaths.size() == 1u);
            CHECK_FALSE(flying);
            return std::make_pair(underSea, deaths.front().deathType);
        };

        auto [shoreUnderSea, shoreDeath] = dropNear(2_ss);
        CHECK_FALSE(shoreUnderSea);
        CHECK(shoreDeath == ProjectileDiedEvent::DeathType::NormalImpact);

        auto [wetUnderSea, wetDeath] = dropNear(-14_ss);
        CHECK(wetUnderSea);
        CHECK(wetDeath == ProjectileDiedEvent::DeathType::WaterImpact);
    }

    TEST_CASE("the last row and column of heightmap corners start no square", "[weapon][water]")
    {
        // All under twenty units of water. The squares are the cells between
        // four corners, as they are to tryGetHeightAt, so a point past the
        // last full cell is off the map and dry.
        Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(0));
        MapTerrain terrain(std::move(heights), SeaLevel);

        auto lastCell = terrain.heightmapIndexToWorldCorner(14, 14);
        CHECK(terrain.isSquareUnderSea(lastCell.x + 2_ss, lastCell.z + 2_ss));

        auto lastCorner = terrain.heightmapIndexToWorldCorner(15, 15);
        CHECK_FALSE(terrain.isSquareUnderSea(lastCorner.x + 2_ss, lastCorner.z + 2_ss));
    }
}
