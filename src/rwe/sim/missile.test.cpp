#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeMissileTerrain(int width, int height)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addMissilePlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("us"),
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

        /**
         * ARMKBOT_MISSILE, the Rocko's rocket, straight out of MISSILES.TDF:
         * it leaves the rail at 450 and works up to 650 at 101, steers at
         * 33000 a second, and its motor is timed off its own 604 range.
         */
        void defineRocketMissile(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 604_ss;
            w.reloadTime = SimScalar(0.7f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 650_ss / 30_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = 100;

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

            sim.weaponDefinitions["rocket"] = w;
        }

        /**
         * ARMTRUCK_ROCKET, the Diplomat's vertical launch, out of ROCKETS.TDF:
         * no launch speed at all, five seconds of climb on `weapontimer`
         * because `noautorange` is set, then ten seconds of guided flight.
         */
        void defineTruckRocket(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 800_ss;
            w.reloadTime = SimScalar(10.0f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 400_ss / 30_ss;
            w.damageRadius = 48_ss;
            w.damage["DEFAULT"] = 250;
            w.weaponTimer = GameTime(150);
            w.randomDecay = GameTime(0);

            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = 0_ss;
            p.acceleration = 40_ss / 900_ss;
            p.maxVelocity = 400_ss / 30_ss;
            p.turnRate = SimAngle(24384u / 30u);
            p.guidance = true;
            p.tracks = false;
            p.twoPhase = true;
            p.vLaunch = true;
            p.flightTime = GameTime(300);
            p.burnBlow = false;
            p.cruise = false;
            p.autoRange = false;
            w.physicsType = p;

            sim.weaponDefinitions["truckrocket"] = w;
        }

        struct Flight
        {
            /** Position at the end of every tick the projectile was alive for. */
            std::vector<SimVector> track;
            /** Speed at the end of every one of those ticks. */
            std::vector<SimScalar> speed;
            /** The tick the missile stopped climbing blind, or -1 if it never did. */
            int turnoverTick{-1};
            SimScalar heightAtTurnover{0};
            SimScalar peakHeight{0};
            SimScalar closestApproach{SimScalar(1.0e9f)};
            /** The tick it came closest on, and whether its motor had already stopped by then. */
            int closestTick{-1};
            bool coastingAtClosest{false};
        };

        Flight fireMissile(GameSimulation& sim, const std::string& weaponType, const SimVector& from, const SimVector& at, int ticks)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            auto toTarget = at - from;
            sim.spawnProjectile(PlayerId(0), weapon, from, toTarget.normalized(), toTarget.length(), std::nullopt, std::nullopt, std::nullopt, at);

            Flight f;
            f.peakHeight = from.y;
            for (int t = 0; t < ticks; ++t)
            {
                sim.tick();
                if (sim.projectiles.begin() == sim.projectiles.end())
                {
                    break;
                }
                const auto& p = sim.projectiles.begin()->second;
                f.track.push_back(p.position);
                f.speed.push_back(p.velocity.length());
                if (p.secondPhase && f.turnoverTick < 0)
                {
                    f.turnoverTick = t;
                    f.heightAtTurnover = p.position.y;
                }
                f.peakHeight = rweMax(f.peakHeight, p.position.y);
                if ((p.position - at).length() < f.closestApproach)
                {
                    f.closestApproach = (p.position - at).length();
                    f.closestTick = t;
                    f.coastingAtClosest = p.motorOut;
                }

                if (std::getenv("RWE_TRACE_MISSILE") && t % 10 == 0)
                {
                    std::cout << "t=" << t
                              << " pos=" << simScalarToFloat(p.position.x) << "," << simScalarToFloat(p.position.y) << "," << simScalarToFloat(p.position.z)
                              << " speed=" << simScalarToFloat(p.velocity.length())
                              << " phase=" << (p.secondPhase ? 1 : 0)
                              << " motorOut=" << p.motorOut << "\n";
                }
            }
            return f;
        }
    }

    TEST_CASE("a self-propelled missile leaves the rail slowly and works its speed up", "[missile]")
    {
        // The bug this guards against: every projectile in RWE used to fly at
        // weaponVelocity from the moment it was fired, so a Rocko's rocket
        // arrived at 21.67 a tick instead of starting at 15 and taking most of
        // its motor's life to reach the cap. That resolves every missile duel
        // at the wrong range and with the wrong lead.
        GameSimulation sim(makeMissileTerrain(128, 128), 0u, 0, 0);
        addMissilePlayer(sim);
        defineRocketMissile(sim);

        // Level, and far enough away that the whole profile is on show.
        auto f = fireMissile(sim, "rocket", SimVector(0_ss, 20_ss, 0_ss), SimVector(2000_ss, 20_ss, 0_ss), 60);

        REQUIRE(f.speed.size() > 25);

        INFO("first tick " << simScalarToFloat(f.speed[0])
                           << ", tick 20 " << simScalarToFloat(f.speed[19])
                           << ", distance by tick 20 " << simScalarToFloat(f.track[19].x));

        // 450/30 plus one step of 101/900.
        REQUIRE(simScalarToFloat(f.speed[0]) > 15.0f);
        REQUIRE(simScalarToFloat(f.speed[0]) < 15.3f);

        // Twenty ticks of 15 + 0.1122k is 323.6 world units. At the cap it
        // would have been 433.3, which is the number the old model produced.
        REQUIRE(simScalarToFloat(f.track[19].x) > 310.0f);
        REQUIRE(simScalarToFloat(f.track[19].x) < 340.0f);

        // The motor never overruns weaponVelocity, and it is still short of it
        // when it stops: 604/(650/30) is 27 ticks, and reaching 650 from 450 at
        // 101 a second would take 59.
        for (const auto& s : f.speed)
        {
            REQUIRE(simScalarToFloat(s) < 21.7f);
        }
        REQUIRE(simScalarToFloat(f.speed[25]) > 17.7f);
        REQUIRE(simScalarToFloat(f.speed[25]) < 18.1f);

        // Once the motor is out the missile is a falling object: it stops
        // accelerating and starts dropping out of level flight.
        const auto& last = f.track.back();
        REQUIRE(simScalarToFloat(last.y) < 20.0f);
    }

    TEST_CASE("a self-propelled missile still gets to the far edge of its own range", "[missile]")
    {
        // A sanity check on the burn time. `range / weaponvelocity` ticks of
        // motor is not enough to cover the range while the missile is still
        // building speed, so it has to arrive on the coast; if the reading of
        // any of the three speeds were wrong it would fall out of the sky well
        // short of 604.
        GameSimulation sim(makeMissileTerrain(128, 128), 0u, 0, 0);
        addMissilePlayer(sim);
        defineRocketMissile(sim);

        auto target = SimVector(604_ss, 20_ss, 0_ss);
        auto f = fireMissile(sim, "rocket", SimVector(0_ss, 20_ss, 0_ss), target, 120);

        INFO("closest approach " << simScalarToFloat(f.closestApproach)
                                 << " on tick " << f.closestTick
                                 << ", coasting " << f.coastingAtClosest
                                 << ", finished at " << simScalarToFloat(f.track.back().x) << "," << simScalarToFloat(f.track.back().y));

        REQUIRE(simScalarToFloat(f.closestApproach) < 24.0f);

        // And it takes the time an accelerating missile takes to get there.
        // Twenty-seven ticks of motor only covers 447 of the 604, so it arrives
        // on the coast around tick 35; at the speed cap the whole way it would
        // have been there on tick 28 and still under power.
        REQUIRE(f.closestTick > 31);
        REQUIRE(f.coastingAtClosest);
    }

    TEST_CASE("a vertical launch missile climbs before it turns over", "[missile]")
    {
        // The Diplomat, the Merl, the missile ships and both nuclear silos all
        // launch straight up and only turn onto the target once the climb is
        // over. RWE used to send them out of the tube on a flat line to the
        // target, which puts a silo's missile through its own building.
        GameSimulation sim(makeMissileTerrain(128, 128), 0u, 0, 0);
        addMissilePlayer(sim);
        defineTruckRocket(sim);

        auto launch = SimVector(0_ss, 20_ss, 0_ss);
        auto target = SimVector(800_ss, 0_ss, 0_ss);
        auto f = fireMissile(sim, "truckrocket", launch, target, 600);

        REQUIRE(f.track.size() > 150);

        INFO("at t=90 " << simScalarToFloat(f.track[89].x) << "," << simScalarToFloat(f.track[89].y)
                        << "; turnover t=" << f.turnoverTick << " at " << simScalarToFloat(f.heightAtTurnover)
                        << "; peak " << simScalarToFloat(f.peakHeight)
                        << "; closest " << simScalarToFloat(f.closestApproach));

        // Ninety ticks in it is still going straight up: 40/900 accumulated
        // over 90 ticks is 182 world units of climb and nothing sideways.
        REQUIRE(simScalarToFloat(f.track[89].x) < 2.0f);
        REQUIRE(simScalarToFloat(f.track[89].y) > 190.0f);
        REQUIRE(simScalarToFloat(f.track[89].y) < 215.0f);

        // `weapontimer` of five seconds times the climb because `noautorange`
        // is set; 40/900 over 150 ticks is 503 units of it.
        REQUIRE(f.turnoverTick >= 145);
        REQUIRE(f.turnoverTick <= 155);
        REQUIRE(simScalarToFloat(f.heightAtTurnover) > 500.0f);
        REQUIRE(simScalarToFloat(f.heightAtTurnover) < 545.0f);

        // It carries on up for a moment while it turns over, then comes down
        // on the target well inside a 96 blast.
        REQUIRE(simScalarToFloat(f.peakHeight) > simScalarToFloat(f.heightAtTurnover));
        REQUIRE(simScalarToFloat(f.peakHeight) < 660.0f);
        REQUIRE(simScalarToFloat(f.closestApproach) < 48.0f);
    }
}
