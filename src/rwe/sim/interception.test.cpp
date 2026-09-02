#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeInterceptTerrain()
        {
            // Big enough that a nuke can sit four thousand units out and still
            // be on the map: the terrain is centred on the origin and reaches
            // half its heightmap width each way.
            Grid<unsigned char> heights(1024, 1024, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addInterceptPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(1000000.0f),
                Energy(1000000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeInterceptScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerInterceptModel(GameSimulation& sim)
        {
            // Twenty units up, because the piece doubles as the muzzle: a round
            // spawned at ground level buries itself on the tick it is created.
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineInterceptUnit(GameSimulation& sim, const std::string& type)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 10000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL3 SPECIAL NOTAIR NOTSUB";
            d.metalStorage = Metal(1000000.0f);
            d.energyStorage = Energy(1000000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        /** The Arm anti-nuke's AMD_ROCKET, as the shipped TDF describes it. */
        void defineInterceptorWeapon(GameSimulation& sim, const std::string& name, float coverage)
        {
            WeaponDefinition w{};
            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = 0_ss;
            p.acceleration = SimScalar(60.0f / 900.0f);
            p.maxVelocity = SimScalar(800.0f / 30.0f);
            p.turnRate = SimAngle(32768u / 30u);
            p.guidance = true;
            p.tracks = true;
            p.twoPhase = true;
            p.vLaunch = true;
            p.flightTime = GameTime(60 * 30);
            p.burnBlow = false;
            p.cruise = false;
            p.autoRange = false;
            w.physicsType = p;
            w.maxRange = 32000_ss;
            w.reloadTime = 120_ss;
            w.weaponTimer = GameTime(4 * 30);
            w.randomDecay = GameTime(0);
            w.burst = 1;
            w.velocity = SimScalar(800.0f / 30.0f);
            w.damageRadius = 96_ss;
            w.damage["DEFAULT"] = 500;
            w.tolerance = SimAngle(4000u);
            w.pitchTolerance = SimAngle(4000u);
            w.metalPerShot = Metal(200.0f);
            w.energyPerShot = Energy(10000.0f);
            w.stockpile = true;
            w.interceptor = true;
            w.coverage = SimScalar(coverage);
            w.verticalLaunch = true;
            w.turret = false;
            sim.weaponDefinitions[name] = w;
        }

        /** The nuclear missile: what an interceptor is allowed to shoot at. */
        void defineTargetableWeapon(GameSimulation& sim, const std::string& name, bool targetable)
        {
            WeaponDefinition w{};
            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = 0_ss;
            p.acceleration = SimScalar(50.0f / 900.0f);
            p.maxVelocity = SimScalar(350.0f / 30.0f);
            p.turnRate = SimAngle(32768u / 30u);
            p.guidance = true;
            p.tracks = false;
            p.twoPhase = true;
            p.vLaunch = true;
            p.flightTime = GameTime(400 * 30);
            p.burnBlow = false;
            p.cruise = true;
            p.autoRange = false;
            w.physicsType = p;
            w.maxRange = 32000_ss;
            w.reloadTime = 180_ss;
            w.weaponTimer = GameTime(5 * 30);
            w.randomDecay = GameTime(0);
            w.burst = 1;
            w.velocity = SimScalar(350.0f / 30.0f);
            w.damageRadius = 512_ss;
            w.damage["DEFAULT"] = 2000;
            w.tolerance = SimAngle(4000u);
            w.pitchTolerance = SimAngle(4000u);
            w.stockpile = true;
            w.targetable = targetable;
            w.verticalLaunch = true;
            w.turret = false;
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnInterceptUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        void armWith(GameSimulation& sim, UnitId id, const std::string& weaponType, int rounds)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            weapon.stockedRounds = rounds;
            sim.getUnitState(id).weapons[0] = weapon;
        }

        /** A missile in the air, aimed somewhere, put there without a launcher. */
        ProjectileId putMissileInAir(GameSimulation& sim, const std::string& weaponType, PlayerId owner, const SimVector& position, const SimVector& aimedAt)
        {
            auto projectile = sim.createProjectileFromWeapon(owner, weaponType, position, SimVector(0_ss, 1_ss, 0_ss), 1000_ss, std::nullopt, std::nullopt, std::nullopt, aimedAt);
            // Left standing still: these tests are about who shoots at what,
            // not about the flight, which the missile tests already cover.
            projectile.velocity = SimVector(0_ss, 0_ss, 0_ss);
            projectile.speed = 0_ss;
            return sim.projectiles.emplace(std::move(projectile));
        }

        /** Dead or already swept up: the tick drops dead projectiles outright. */
        bool gone(const GameSimulation& sim, ProjectileId id)
        {
            auto p = sim.projectiles.tryGet(id);
            return !p || p->get().isDead;
        }

        int liveProjectiles(const GameSimulation& sim)
        {
            int n = 0;
            for (const auto& p : sim.projectiles)
            {
                if (!p.second.isDead)
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("an interceptor's coverage is a square and ignores height", "[interception]")
    {
        // 0x49D18D adds the coverage to the difference and compares the result
        // against twice the coverage as an unsigned quantity, which is how the
        // original writes -c <= d <= c in one branch. It does that for x and
        // for z and never touches y.
        auto launcher = SimVector(0_ss, 0_ss, 0_ss);

        SECTION("inside the box on both axes")
        {
            REQUIRE(GameSimulation::isWithinCoverage(launcher, SimVector(1900_ss, 0_ss, 1900_ss), 2000_ss));
        }

        SECTION("a corner a circle would have refused is still inside")
        {
            // 1900 away on each axis is 2687 as the crow flies, well outside a
            // 2000 radius, and the original takes it.
            REQUIRE(GameSimulation::isWithinCoverage(launcher, SimVector(1900_ss, 0_ss, 1900_ss), 2000_ss));
            REQUIRE_FALSE(GameSimulation::isWithinCoverage(launcher, SimVector(2100_ss, 0_ss, 0_ss), 2000_ss));
        }

        SECTION("height is not looked at")
        {
            REQUIRE(GameSimulation::isWithinCoverage(launcher, SimVector(0_ss, 30000_ss, 0_ss), 2000_ss));
        }

        SECTION("outside on either axis is outside")
        {
            REQUIRE_FALSE(GameSimulation::isWithinCoverage(launcher, SimVector(2001_ss, 0_ss, 0_ss), 2000_ss));
            REQUIRE_FALSE(GameSimulation::isWithinCoverage(launcher, SimVector(0_ss, 0_ss, -2001_ss), 2000_ss));
        }
    }

    TEST_CASE("an anti-nuke picks its target out of the projectiles in the air", "[interception]")
    {
        auto script = makeInterceptScript();
        GameSimulation sim(makeInterceptTerrain(), 0u, 0, 0);
        auto us = addInterceptPlayer(sim, "us");
        auto them = addInterceptPlayer(sim, "them");
        registerInterceptModel(sim);
        defineInterceptUnit(sim, "amd");
        defineInterceptorWeapon(sim, "amd_rocket", 500.0f);
        defineTargetableWeapon(sim, "nuke", true);
        defineTargetableWeapon(sim, "rocket", false);

        auto amdId = spawnInterceptUnit(sim, "amd", us, SimVector(500_ss, 0_ss, 500_ss), script);
        armWith(sim, amdId, "amd_rocket", 1);

        SECTION("a hostile missile aimed inside the coverage is taken")
        {
            auto nukeId = putMissileInAir(sim, "nuke", them, SimVector(2000_ss, 400_ss, 2000_ss), SimVector(600_ss, 0_ss, 600_ss));
            REQUIRE(sim.findInterceptTarget(amdId, 0) == nukeId);
        }

        SECTION("an empty magazine does not even look")
        {
            putMissileInAir(sim, "nuke", them, SimVector(2000_ss, 400_ss, 2000_ss), SimVector(600_ss, 0_ss, 600_ss));
            armWith(sim, amdId, "amd_rocket", 0);
            REQUIRE_FALSE(sim.findInterceptTarget(amdId, 0).has_value());
        }

        SECTION("a missile without targetable is not a target")
        {
            putMissileInAir(sim, "rocket", them, SimVector(600_ss, 400_ss, 600_ss), SimVector(600_ss, 0_ss, 600_ss));
            REQUIRE_FALSE(sim.findInterceptTarget(amdId, 0).has_value());
        }

        SECTION("our own missiles are left alone")
        {
            putMissileInAir(sim, "nuke", us, SimVector(600_ss, 400_ss, 600_ss), SimVector(600_ss, 0_ss, 600_ss));
            REQUIRE_FALSE(sim.findInterceptTarget(amdId, 0).has_value());
        }

        SECTION("the aim point decides, not where the missile is now")
        {
            // Directly overhead but aimed at the far corner of the map: the
            // launcher is defending a box, and this one is not going to land
            // in it.
            putMissileInAir(sim, "nuke", them, SimVector(500_ss, 400_ss, 500_ss), SimVector(4000_ss, 0_ss, 4000_ss));
            REQUIRE_FALSE(sim.findInterceptTarget(amdId, 0).has_value());

            // And the other way round: a long way off, but coming here.
            auto comingHere = putMissileInAir(sim, "nuke", them, SimVector(4000_ss, 400_ss, 4000_ss), SimVector(500_ss, 0_ss, 500_ss));
            REQUIRE(sim.findInterceptTarget(amdId, 0) == comingHere);
        }

        SECTION("a missile another round is already chasing is passed over")
        {
            auto nukeId = putMissileInAir(sim, "nuke", them, SimVector(2000_ss, 400_ss, 2000_ss), SimVector(600_ss, 0_ss, 600_ss));
            REQUIRE(sim.findInterceptTarget(amdId, 0) == nukeId);

            // Somebody else's round, already in the air after it.
            auto chaser = sim.createProjectileFromWeapon(us, "amd_rocket", SimVector(500_ss, 100_ss, 500_ss), SimVector(0_ss, 1_ss, 0_ss), 100_ss, std::nullopt, std::nullopt, std::nullopt, std::nullopt, nukeId);
            sim.projectiles.emplace(std::move(chaser));

            REQUIRE_FALSE(sim.findInterceptTarget(amdId, 0).has_value());
        }
    }

    TEST_CASE("an anti-nuke launches at a nuke and spends a round doing it", "[interception]")
    {
        auto script = makeInterceptScript();
        GameSimulation sim(makeInterceptTerrain(), 0u, 0, 0);
        auto us = addInterceptPlayer(sim, "us");
        auto them = addInterceptPlayer(sim, "them");
        registerInterceptModel(sim);
        defineInterceptUnit(sim, "amd");
        defineInterceptorWeapon(sim, "amd_rocket", 500.0f);
        defineTargetableWeapon(sim, "nuke", true);

        auto amdId = spawnInterceptUnit(sim, "amd", us, SimVector(500_ss, 0_ss, 500_ss), script);
        armWith(sim, amdId, "amd_rocket", 1);

        auto nukeId = putMissileInAir(sim, "nuke", them, SimVector(1000_ss, 400_ss, 1000_ss), SimVector(600_ss, 0_ss, 600_ss));

        // The interceptor weapon carries no commandfire in the shipped data,
        // which is what lets it engage on its own the moment a round is ready.
        for (int i = 0; i < 30 && liveProjectiles(sim) < 2; ++i)
        {
            sim.tick();
        }

        REQUIRE(liveProjectiles(sim) == 2);
        REQUIRE(sim.getUnitState(amdId).weapons[0]->stockedRounds == 0);

        // Whatever came off the rail is chasing the nuke.
        bool foundChaser = false;
        for (const auto& p : sim.projectiles)
        {
            if (p.second.weaponType == "amd_rocket")
            {
                REQUIRE(p.second.targetProjectile == nukeId);
                foundChaser = true;
            }
        }
        REQUIRE(foundChaser);
    }

    TEST_CASE("an interceptor goes off beside the missile it is chasing", "[interception]")
    {
        // 0x49B106: the interceptor's collision check has an extra clause that
        // detonates it when it is inside its own areaofeffect of the target,
        // and 0x49A664 then takes the target with it.
        auto script = makeInterceptScript();
        GameSimulation sim(makeInterceptTerrain(), 0u, 0, 0);
        auto us = addInterceptPlayer(sim, "us");
        auto them = addInterceptPlayer(sim, "them");
        registerInterceptModel(sim);
        defineInterceptorWeapon(sim, "amd_rocket", 500.0f);
        defineTargetableWeapon(sim, "nuke", true);

        auto nukeId = putMissileInAir(sim, "nuke", them, SimVector(1000_ss, 400_ss, 1000_ss), SimVector(600_ss, 0_ss, 600_ss));

        SECTION("inside the blast radius, both die")
        {
            auto chaser = sim.createProjectileFromWeapon(us, "amd_rocket", SimVector(1050_ss, 400_ss, 1000_ss), SimVector(0_ss, 1_ss, 0_ss), 100_ss, std::nullopt, std::nullopt, std::nullopt, std::nullopt, nukeId);
            chaser.velocity = SimVector(0_ss, 0_ss, 0_ss);
            chaser.speed = 0_ss;
            auto chaserId = sim.projectiles.emplace(std::move(chaser));

            sim.tick();

            REQUIRE(gone(sim, chaserId));
            REQUIRE(gone(sim, nukeId));
        }

        SECTION("outside it, the round keeps flying")
        {
            auto chaser = sim.createProjectileFromWeapon(us, "amd_rocket", SimVector(1200_ss, 400_ss, 1000_ss), SimVector(0_ss, 1_ss, 0_ss), 100_ss, std::nullopt, std::nullopt, std::nullopt, std::nullopt, nukeId);
            chaser.velocity = SimVector(0_ss, 0_ss, 0_ss);
            chaser.speed = 0_ss;
            auto chaserId = sim.projectiles.emplace(std::move(chaser));

            sim.tick();

            REQUIRE_FALSE(gone(sim, chaserId));
            REQUIRE_FALSE(gone(sim, nukeId));
        }
    }

    TEST_CASE("an interceptor's blast takes every projectile inside it", "[interception]")
    {
        // 0x49A664 walks the whole projectile list and detonates each one
        // within areaofeffect of the blast, re-checking neither targetable nor
        // the owner. That is what lets one anti-nuke clear a salvo, and it is
        // the piece the flight section was missing.
        GameSimulation sim(makeInterceptTerrain(), 0u, 0, 0);
        auto us = addInterceptPlayer(sim, "us");
        auto them = addInterceptPlayer(sim, "them");
        defineInterceptorWeapon(sim, "amd_rocket", 500.0f);
        defineTargetableWeapon(sim, "nuke", true);
        defineTargetableWeapon(sim, "rocket", false);

        auto blastAt = SimVector(1000_ss, 400_ss, 1000_ss);

        // Three missiles: one it was chasing, one it was not, and one that is
        // not even a legal target. All three are inside 96.
        auto chased = putMissileInAir(sim, "nuke", them, blastAt + SimVector(10_ss, 0_ss, 0_ss), SimVector(600_ss, 0_ss, 600_ss));
        auto bystander = putMissileInAir(sim, "nuke", them, blastAt + SimVector(0_ss, 0_ss, 40_ss), SimVector(600_ss, 0_ss, 600_ss));
        auto notTargetable = putMissileInAir(sim, "rocket", them, blastAt + SimVector(0_ss, 50_ss, 0_ss), SimVector(600_ss, 0_ss, 600_ss));

        // And one just outside, which lives.
        auto outside = putMissileInAir(sim, "nuke", them, blastAt + SimVector(200_ss, 0_ss, 0_ss), SimVector(600_ss, 0_ss, 600_ss));

        // Even one of our own, since the blast does not ask whose it is.
        auto ours = putMissileInAir(sim, "nuke", us, blastAt + SimVector(0_ss, 0_ss, 20_ss), SimVector(600_ss, 0_ss, 600_ss));

        auto chaser = sim.createProjectileFromWeapon(us, "amd_rocket", blastAt, SimVector(0_ss, 1_ss, 0_ss), 100_ss, std::nullopt, std::nullopt, std::nullopt, std::nullopt, chased);
        auto chaserId = sim.projectiles.emplace(std::move(chaser));

        sim.doProjectileImpact(sim.projectiles.tryGet(chaserId)->get(), ImpactType::Normal, chaserId);

        REQUIRE(sim.projectiles.tryGet(chased)->get().isDead);
        REQUIRE(sim.projectiles.tryGet(bystander)->get().isDead);
        REQUIRE(sim.projectiles.tryGet(notTargetable)->get().isDead);
        REQUIRE(sim.projectiles.tryGet(ours)->get().isDead);
        REQUIRE_FALSE(sim.projectiles.tryGet(outside)->get().isDead);

        // And the round that went off is not caught by its own blast.
        REQUIRE_FALSE(sim.projectiles.tryGet(chaserId)->get().isDead);
    }

    TEST_CASE("an ordinary weapon's blast leaves projectiles alone", "[interception]")
    {
        // The blast above is gated on the exploding weapon's own interceptor
        // flag (0x49A669). Without that gate every shell in the game would be
        // knocking missiles down.
        GameSimulation sim(makeInterceptTerrain(), 0u, 0, 0);
        auto them = addInterceptPlayer(sim, "them");
        defineTargetableWeapon(sim, "nuke", true);

        auto blastAt = SimVector(1000_ss, 400_ss, 1000_ss);
        auto nearby = putMissileInAir(sim, "nuke", them, blastAt + SimVector(10_ss, 0_ss, 0_ss), SimVector(600_ss, 0_ss, 600_ss));

        auto other = sim.createProjectileFromWeapon(them, "nuke", blastAt, SimVector(0_ss, 1_ss, 0_ss), 100_ss, std::nullopt);
        auto otherId = sim.projectiles.emplace(std::move(other));

        sim.doProjectileImpact(sim.projectiles.tryGet(otherId)->get(), ImpactType::Normal, otherId);

        REQUIRE_FALSE(sim.projectiles.tryGet(nearby)->get().isDead);
    }
}
