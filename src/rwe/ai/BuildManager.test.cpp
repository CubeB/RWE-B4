#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>
#include <rwe/sim/SimTicksPerSecond.h>

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

    TEST_CASE("the two difficulties that build a guard at all gate it on danger", "[ai]")
    {
        // The floor and the size are one switch between them, and a floor
        // without a size guards nothing at all. Hard and Brutal are the only
        // profiles that ask for guards, so they are the only ones where the
        // gate has to be on for it to mean anything -- and an earlier version
        // of this had it the other way round, with both defaults at zero and
        // the gate never once live in a played game.
        for (auto difficulty : {AiDifficulty::Hard, AiDifficulty::Brutal})
        {
            auto profile = makeProfileForDifficulty(difficulty);
            CAPTURE(difficulty == AiDifficulty::Brutal ? "BRUTAL" : "HARD");
            REQUIRE(profile.buildSiteGuardSize > 0);
            REQUIRE(profile.buildSiteGuardThreat > 0.0f);
        }

        // The header default, which is Standard, asks for no guard and so has
        // no floor to set: the measured cost of the distance-only trigger is
        // why the feature is off there at all, and a floor on a profile with
        // no guard would be a number that governs nothing.
        auto standard = makeProfileForDifficulty(AiDifficulty::Standard);
        REQUIRE(standard.buildSiteGuardSize == 0);
        REQUIRE(standard.buildSiteGuardThreat == 0.0f);
    }

    namespace
    {
        /**
         * A base with one empty lab, a commander for the anchor, and nothing
         * else: enough for planFactories to run, and small enough that what
         * it asks the lab for is unambiguous (the constructor, first).
         */
        struct FactoryQueueFixture
        {
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            PlayerId ai;
            UnitId labId;

            FactoryQueueFixture()
                : ai(addPlayer(sim, "ai", GamePlayerType::Computer, "ARM"))
            {
                defineWorld(sim);
                addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
                labId = addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
            }

            AiTuningProfile profile(int depth, bool dropStalled) const
            {
                auto p = makeDefaultStandardProfile();
                // Every controller tick is a planning pass, so the test does
                // not depend on the shipped cadence.
                p.buildPlannerTickInterval = 1;
                p.factoryQueueDepth = depth;
                p.factoryQueueDropStalled = dropStalled;
                p.tierTwoEconomyReserve = false;
                return p;
            }
        };

        /** The negative or positive queue changes addressed to one factory. */
        std::vector<PlayerUnitCommand::ModifyBuildQueue> queueChangesFor(const std::vector<PlayerCommand>& commands, UnitId factory)
        {
            std::vector<PlayerUnitCommand::ModifyBuildQueue> found;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand || unitCommand->unit != factory)
                {
                    continue;
                }
                if (auto queue = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unitCommand->command))
                {
                    found.push_back(*queue);
                }
            }
            return found;
        }

        /** One controller pass, with the simulation's clock advanced by a tick. */
        void controllerTick(GameSimulation& sim, AiPlayerController& controller, std::vector<PlayerCommand>& commands)
        {
            sim.gameTime = GameTime(sim.gameTime.value + 1);
            controller.tick(sim, commands);
        }
    }

    TEST_CASE("a factory tops its queue up to the configured depth", "[ai]")
    {
        FactoryQueueFixture f;

        SECTION("depth two asks for a second unit while one is still queued")
        {
            AiPlayerController controller(f.ai, f.profile(2, false), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;

            controllerTick(f.sim, controller, commands);
            auto first = queueChangesFor(commands, f.labId);
            REQUIRE(first.size() == 1);
            CHECK(first.front().count == 1);
            REQUIRE_FALSE(first.front().unitType.empty());

            // Apply the order by hand: the command stream is not fed back
            // into the simulation by the harness, and the point is what the
            // planner does when the queue already holds something.
            f.sim.getUnitState(f.labId).buildQueue.emplace_back(first.front().unitType, 1);
            commands.clear();
            controllerTick(f.sim, controller, commands);
            auto second = queueChangesFor(commands, f.labId);
            REQUIRE(second.size() == 1);
            CHECK(second.front().count == 1);
        }

        SECTION("depth two stops at two")
        {
            AiPlayerController controller(f.ai, f.profile(2, false), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            controllerTick(f.sim, controller, commands);
            auto first = queueChangesFor(commands, f.labId);
            REQUIRE(first.size() == 1);
            f.sim.getUnitState(f.labId).buildQueue.emplace_back(first.front().unitType, 1);
            f.sim.getUnitState(f.labId).buildQueue.emplace_back(first.front().unitType, 1);

            commands.clear();
            controllerTick(f.sim, controller, commands);
            CHECK(queueChangesFor(commands, f.labId).empty());
        }

        SECTION("depth one is the old rule: nothing while anything is queued")
        {
            AiPlayerController controller(f.ai, f.profile(1, false), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            controllerTick(f.sim, controller, commands);
            auto first = queueChangesFor(commands, f.labId);
            REQUIRE(first.size() == 1);
            f.sim.getUnitState(f.labId).buildQueue.emplace_back(first.front().unitType, 1);

            commands.clear();
            controllerTick(f.sim, controller, commands);
            CHECK(queueChangesFor(commands, f.labId).empty());
        }
    }

    TEST_CASE("a stalled factory queue head is dropped only after the timeout", "[ai]")
    {
        // Pure deadline/progress guard first: it is the whole safety rule and
        // needs no simulation to pin.
        CHECK_FALSE(factoryQueueHeadIsStalled(0, 0, 100, 60, false));
        CHECK_FALSE(factoryQueueHeadIsStalled((60 * SimTicksPerSecond) - 1, 0, 100, 60, false));
        CHECK(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 0, 100, 60, false));
        // Affordable: not a stall, however long it has waited.
        CHECK_FALSE(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 0, 100, 60, true));
        // A hundredth of the build in the window is progress (100 -> 1).
        CHECK(factoryQueueProgressFloor(100) == 1);
        CHECK(factoryQueueProgressFloor(1000) == 10);
        CHECK_FALSE(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 1, 100, 60, false));
        CHECK(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 9, 1000, 60, false));
        CHECK_FALSE(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 10, 1000, 60, false));
        // Zero seconds disables it rather than dropping on the first pass.
        CHECK_FALSE(factoryQueueHeadIsStalled(60 * SimTicksPerSecond, 0, 100, 0, false));

        FactoryQueueFixture f;
        // No metal, no income, so the head cannot be paid for. The harness
        // drives the controller without ticking the simulation, so the
        // production buffers stay at zero.
        f.sim.getPlayer(f.ai).metal = Metal(0.0f);
        f.sim.getUnitState(f.labId).buildQueue.emplace_back("ARMCK", 1);

        auto profile = f.profile(1, true);
        profile.factoryQueueStallSeconds = 60;
        AiPlayerController dropper(f.ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        for (unsigned int i = 0;
             i < static_cast<unsigned int>(profile.factoryQueueStallSeconds) * static_cast<unsigned int>(SimTicksPerSecond);
             ++i)
        {
            controllerTick(f.sim, dropper, commands);
        }
        CHECK(queueChangesFor(commands, f.labId).empty());

        // The pass that reaches the deadline drops the head: the same
        // negative ModifyBuildQueue the besieged path sends.
        commands.clear();
        controllerTick(f.sim, dropper, commands);
        auto changes = queueChangesFor(commands, f.labId);
        REQUIRE(changes.size() == 1);
        CHECK(changes.front().unitType == "ARMCK");
        CHECK(changes.front().count == -1);

        SECTION("the control arm leaves the queue alone when the knob is off")
        {
            FactoryQueueFixture quiet;
            quiet.sim.getPlayer(quiet.ai).metal = Metal(0.0f);
            quiet.sim.getUnitState(quiet.labId).buildQueue.emplace_back("ARMCK", 1);
            auto quietProfile = quiet.profile(1, false);
            quietProfile.factoryQueueStallSeconds = 60;
            AiPlayerController controller(quiet.ai, quietProfile, 42u, MapIntel{});
            std::vector<PlayerCommand> quietCommands;
            for (unsigned int i = 0; i < (60u * static_cast<unsigned int>(SimTicksPerSecond)) + 5u; ++i)
            {
                controllerTick(quiet.sim, controller, quietCommands);
            }
            CHECK(queueChangesFor(quietCommands, quiet.labId).empty());
        }
    }
}
