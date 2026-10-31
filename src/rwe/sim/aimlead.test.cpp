#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

/**
 * Aim lead, TOTALA-EXE-WEAPONS.md S:126 (0x48A1E0): a shooter with six or more
 * kills aims at where a moving unit target will be in 0.8 * D / S ticks.
 */
namespace rwe
{
    namespace
    {
        constexpr int FixedOne = 65536;

        SimVector fromFixedVector(int x, int y, int z)
        {
            return SimVector(simScalarFromFixed(x), simScalarFromFixed(y), simScalarFromFixed(z));
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makeGunDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.sightDistance = 1000u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeTargetDef()
        {
            auto d = makeGunDef();
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = false;
            d.maxVelocity = 10_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.sightDistance = 100u;
            return d;
        }

        UnitId addTestUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            unit.hitPoints = 1000000;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /**
         * A gun at the origin and a target 300 units away on +z, driving
         * along +x at three units a tick. Returns where the first shot was
         * aimed (the projectile's recorded target position), and the
         * target's own position on the tick it was fired.
         */
        struct Shot
        {
            SimVector aimedAt;
            SimVector targetWas;
        };

        std::optional<Shot> fireAtDrivingTarget(unsigned int kills, bool cruise)
        {
            static const auto script = makeEmptyCobScript({"base"});
            auto op = [](OpCode code) { return static_cast<uint32_t>(code); };
            script->functions = {{"AimPrimary", 0}, {"QueryPrimary", 3}};
            script->instructions = {op(OpCode::PUSH_CONSTANT), 1u, op(OpCode::RETURN), op(OpCode::PUSH_CONSTANT), 0u, op(OpCode::RETURN)};
            GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
            // Terrain sight caps at 256 units; this target begins 300 away.
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto us = addPlayer(sim, "us");
            auto them = addPlayer(sim, "them");
            registerModel(sim);
            sim.unitDefinitions["GUN"] = makeGunDef();
            sim.unitDefinitions["TARGET"] = makeTargetDef();

            // Velocity 300 / 30 = 10 per tick; a straight line-of-sight round.
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            if (cruise)
            {
                ProjectilePhysicsTypeSelfPropelled p{};
                p.cruise = true;
                w.physicsType = p;
            }
            w.turret = true;
            w.maxRange = 600_ss;
            w.reloadTime = 1_ss;
            w.burst = 1;
            w.velocity = SimScalar(300.0f / 30.0f);
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 10;
            w.tolerance = SimAngle(32768);
            w.pitchTolerance = SimAngle(32768);
            sim.weaponDefinitions["gunweapon"] = w;
            sim.unitDefinitions["GUN"].weapon1 = "gunweapon";

            auto gunId = addTestUnit(sim, "GUN", us, SimVector(100_ss, 0_ss, 100_ss), script);
            auto& gun = sim.getUnitState(gunId);
            UnitWeapon weapon;
            weapon.weaponType = "gunweapon";
            gun.weapons[0] = weapon;
            gun.kills = kills;

            auto targetId = addTestUnit(sim, "TARGET", them, SimVector(100_ss, 0_ss, 400_ss), script);

            for (int i = 0; i < 200; ++i)
            {
                // The target keeps its drive: three units a tick along +x.
                auto& target = sim.getUnitState(targetId);
                target.rotation = SimAngle(16384);
                if (auto ground = std::get_if<UnitPhysicsInfoGround>(&target.physics))
                {
                    ground->currentSpeed = 3_ss;
                    ground->steeringInfo.targetSpeed = 3_ss;
                    ground->steeringInfo.targetAngle = SimAngle(16384);
                }

                sim.tick();

                for (const auto& p : sim.projectiles)
                {
                    if (p.second.targetPosition)
                    {
                        return Shot{*p.second.targetPosition, sim.getUnitState(targetId).position};
                    }
                }
            }

            return std::nullopt;
        }
    }

    TEST_CASE("computeAimLead: the arithmetic is the original's, integer for integer", "[aimlead]")
    {
        // A gun at the origin, the sweet spot 300 units down +x and the
        // target moving at 3 units a tick along +x, weaponvelocity 300.
        //   D = 300 << 16 = 19660800
        //   S = 300 * 65536 / 30 = 655360
        //   T = (D << 16) / S = 1966080            (30 ticks)
        //   F = (T * 0xCCCC) >> 16 = 1572840       (0.8 of that)
        //   lead = (3 * 65536 * F) >> 16 = 4718520 (71.9993 units)
        auto lead = computeAimLead(
            SimVector(0_ss, 0_ss, 0_ss),
            SimVector(300_ss, 0_ss, 0_ss),
            SimVector(3_ss, 0_ss, 0_ss),
            SimScalar(300.0f / 30.0f),
            false,
            6,
            defaultVeterancy());

        REQUIRE(lead.x == simScalarFromFixed(4718520));
        REQUIRE(lead.y == 0_ss);
        REQUIRE(lead.z == 0_ss);
    }

    TEST_CASE("computeAimLead: six kills is the threshold, a step and not a ramp", "[aimlead]")
    {
        auto leadAt = [](unsigned int kills) {
            return computeAimLead(
                SimVector(0_ss, 0_ss, 0_ss),
                SimVector(0_ss, 0_ss, 300_ss),
                SimVector(2_ss, 0_ss, 0_ss),
                SimScalar(300.0f / 30.0f),
                false,
                kills,
                defaultVeterancy());
        };

        REQUIRE(leadAt(0).x == 0_ss);
        REQUIRE(leadAt(5).x == 0_ss);
        REQUIRE(leadAt(6).x > 0_ss);
        // Not a ramp: the same lead at every count from six on.
        REQUIRE(leadAt(7).x == leadAt(6).x);
        REQUIRE(leadAt(40).x == leadAt(6).x);
    }

    TEST_CASE("computeAimLead: a cruise missile and a weapon with no velocity never lead", "[aimlead]")
    {
        const SimVector origin(0_ss, 0_ss, 0_ss);
        const SimVector spot(0_ss, 0_ss, 300_ss);
        const SimVector velocity(2_ss, 0_ss, 0_ss);

        auto cruise = computeAimLead(origin, spot, velocity, SimScalar(10.0f), true, 20, defaultVeterancy());
        REQUIRE(cruise.x == 0_ss);

        auto bomb = computeAimLead(origin, spot, velocity, 0_ss, false, 20, defaultVeterancy());
        REQUIRE(bomb.x == 0_ss);
    }

    TEST_CASE("computeAimLead: all three axes are led, the vertical one too", "[aimlead]")
    {
        // A diving aircraft: the y term is included.
        auto lead = computeAimLead(
            SimVector(0_ss, 0_ss, 0_ss),
            fromFixedVector(0, 0, 300 * FixedOne),
            SimVector(0_ss, -2_ss, 1_ss),
            SimScalar(300.0f / 30.0f),
            false,
            10,
            defaultVeterancy());

        REQUIRE(lead.y == simScalarFromFixed(-2 * 1572840));
        REQUIRE(lead.z == simScalarFromFixed(1572840));
    }

    TEST_CASE("integerSquareRoot floors, with no floating point", "[aimlead]")
    {
        REQUIRE(integerSquareRoot(0) == 0u);
        REQUIRE(integerSquareRoot(1) == 1u);
        REQUIRE(integerSquareRoot(3) == 1u);
        REQUIRE(integerSquareRoot(4) == 2u);
        REQUIRE(integerSquareRoot(99) == 9u);
        REQUIRE(integerSquareRoot(100) == 10u);
        REQUIRE(integerSquareRoot(uint64_t{19660800} * 19660800u) == 19660800u);
        REQUIRE(integerSquareRoot((uint64_t{19660800} * 19660800u) - 1u) == 19660799u);
        REQUIRE(integerSquareRoot(~uint64_t{0}) == 4294967295u);
    }

    TEST_CASE("a veteran shooter leads a driving target; a rookie does not", "[aimlead][weapon]")
    {
        auto rookie = fireAtDrivingTarget(0, false);
        auto fiveKills = fireAtDrivingTarget(5, false);
        auto veteran = fireAtDrivingTarget(6, false);
        REQUIRE(rookie);
        REQUIRE(fiveKills);
        REQUIRE(veteran);

        // Five kills is a rookie: the same shot, to the bit.
        REQUIRE((fiveKills->aimedAt == rookie->aimedAt));

        // The rookie aimed at where the target stood on the tick it fired.
        REQUIRE(rookie->aimedAt.x <= rookie->targetWas.x);

        // The veteran aimed ahead of it, along +x, by tens of units: 0.8 of
        // the flight time (the target is ~300 away, at 10 a tick) times 3 a
        // tick is roughly 70.
        auto ahead = veteran->aimedAt.x - veteran->targetWas.x;
        REQUIRE(ahead > 40_ss);
        REQUIRE(ahead < 120_ss);
    }

    TEST_CASE("a cruise missile does not lead, however many kills", "[aimlead][weapon]")
    {
        auto rookie = fireAtDrivingTarget(0, true);
        auto veteran = fireAtDrivingTarget(30, true);
        REQUIRE(rookie);
        REQUIRE(veteran);
        REQUIRE((veteran->aimedAt == rookie->aimedAt));
    }
}
