#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * What makes a builder placed away from the base worth a guard.
 *
 * The trigger used to be nothing but distance: any site
 * buildSiteGuardMinDistance out from the anchor got a combat unit torn off the
 * army, whether or not anything could reach it. Measured over ten games at
 * hard, 69 of 81 guards ended with the builder having simply finished and only
 * 6 with it lost, so two units were taken off an army averaging 7.4 to escort
 * jobs nothing was threatening. The trigger reads the influence map as well now
 * (BuildManager, against ThreatMap::antiGroundInRadius), and these pin the
 * gate: a site the map rates dangerous earns the guard, and a site with
 * nothing in reach of it does not.
 */
namespace rwe
{
    namespace
    {
        /** How long the planner needs to issue the tower order. */
        const int guardTriggerTicks = 31;

        /**
         * A base past its opening, three Peewees to draw a guard from, and a
         * buildSiteGuardMinDistance short enough that the front-of-base tower
         * the planner puts up already counts as "away from the base" -- which
         * is the point of holding it that low: every section here is about the
         * threat half of the test, and the distance half is stepped out of the
         * way.
         *
         * The raider carries a gun that reaches over the map, so the danger
         * can stand well outside defendRadius and the AI goes on building what
         * it would otherwise have built. A Peewee's own 200 is not enough to
         * cover a site that far out, and parking the raider next to the base
         * instead would change the opening rather than the guard.
         */
        struct GuardTriggerFixture
        {
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            PlayerId ai;
            PlayerId enemy;
            SimVector anchor{-300_ss, 0_ss, 0_ss};

            GuardTriggerFixture()
                : ai(addPlayer(sim, "ai", GamePlayerType::Computer, "ARM"))
            {
                enemy = addPlayer(sim, "enemy", GamePlayerType::Computer, "CORE");
                defineWorld(sim);
                layOutBase(sim, ai, script, anchor);
                for (int i = 0; i < 3; ++i)
                {
                    addUnit(sim, "ARMPW", ai, anchor + SimVector(0_ss, 0_ss, SimScalar(-200.0f - i * 40.0f)), script);
                }

                auto longGun = sim.weaponDefinitions.at("LASER");
                longGun.maxRange = 1400_ss;
                sim.weaponDefinitions["LONGGUN"] = longGun;
                sim.unitDefinitions["ARMGUN"] = makeDef(false, false, true, "LONGGUN", 200u);
            }

            /** A long-ranged enemy gun, parked `offset` from our base anchor. */
            UnitId standOff(const SimVector& offset)
            {
                return addUnit(sim, "ARMGUN", enemy, anchor + offset, script);
            }

            AiTuningProfile profileWithGate(float threatFloor) const
            {
                auto profile = makeDefaultStandardProfile();
                profile.buildSiteGuardMinDistance = 50_ss;
                profile.buildSiteGuardSize = 2;
                profile.buildSiteGuardThreat = threatFloor;
                // The raider is 1200 out, past defendRadius, and nothing this
                // side owns can see that far. Omniscient is how the fixture
                // gives the AI the knowledge the gate is supposed to be
                // reading -- a switch any difficulty can be given, rather than
                // a line of sight the test would have to plumb.
                profile.cheatModeOmniscient = true;
                return profile;
            }
        };
    }

    TEST_CASE("the build-site guard is triggered by remembered danger, not by distance alone", "[ai]")
    {
        // The distance test is unchanged and still there. What the audit
        // measured is that it was the only test, and that on its own it
        // answered "guard this" to jobs nothing was threatening, which is what
        // the arena kept scoring the feature for.
        GuardTriggerFixture threatened;
        threatened.standOff(SimVector(1200_ss, 0_ss, 0_ss));
        auto profile = threatened.profileWithGate(1000.0f);

        AiPlayerController controller(threatened.ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(threatened.sim, controller, guardTriggerTicks, commands);

        // The site is the one the planner actually chose, and the reading it
        // carries is the number the floor is compared against -- so a fixture
        // that has moved cannot pass itself off as the gate working.
        auto order = theBuildOrder(commands, "ARMLLT");
        auto& map = controller.getThreatMap();
        auto siteThreat = map.antiGroundInRadius(order.position, profile.buildSiteGuardThreatRadius);
        REQUIRE(siteThreat > profile.buildSiteGuardThreat);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.buildSiteGuardRequest.has_value());
        // Compared through a named bool because Catch2 wants to print the
        // operands, and a SimVector has no stream operator.
        bool guardsTheExactSite = bb.buildSiteGuardRequest->position == order.position;
        REQUIRE(guardsTheExactSite);
        REQUIRE(bb.buildSiteGuardRequest->threat == siteThreat);
        REQUIRE(bb.guardGroup.size() == static_cast<std::size_t>(profile.buildSiteGuardSize));
    }

    TEST_CASE("a remote build site with nothing in reach of it is not guarded", "[ai]")
    {
        // Same base, same seed, same build order, same distance from the
        // anchor -- and no raider. The distance test still passes; what has
        // changed is that it is no longer the only test, so the guard the
        // feature used to send for this job is correctly withheld.
        GuardTriggerFixture quiet;
        auto profile = quiet.profileWithGate(1000.0f);

        AiPlayerController controller(quiet.ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(quiet.sim, controller, guardTriggerTicks, commands);

        auto order = theBuildOrder(commands, "ARMLLT");
        auto& map = controller.getThreatMap();
        REQUIRE(flatDistanceBetween(order.position, quiet.anchor) >= profile.buildSiteGuardMinDistance);
        REQUIRE(map.antiGroundInRadius(order.position, profile.buildSiteGuardThreatRadius) == 0.0f);
        REQUIRE_FALSE(controller.getBlackboard().buildSiteGuardRequest.has_value());
        REQUIRE(controller.getBlackboard().guardGroup.empty());
    }

    TEST_CASE("the threat floor at zero leaves the distance-only trigger exactly as it was", "[ai]")
    {
        // The kill switch, and the control arm any measurement of the gate is
        // played against: at zero the floor can never be failed, so a site
        // nothing threatens is guarded precisely as it was before the map was
        // read at all.
        GuardTriggerFixture quiet;
        auto profile = quiet.profileWithGate(0.0f);

        AiPlayerController controller(quiet.ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(quiet.sim, controller, guardTriggerTicks, commands);

        auto order = theBuildOrder(commands, "ARMLLT");
        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.buildSiteGuardRequest.has_value());
        bool guardsTheExactSite = bb.buildSiteGuardRequest->position == order.position;
        REQUIRE(guardsTheExactSite);
        REQUIRE(bb.buildSiteGuardRequest->threat == 0.0f);
    }
}
