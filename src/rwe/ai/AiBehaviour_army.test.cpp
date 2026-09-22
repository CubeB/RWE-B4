#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * What the army does with itself: gathering, attacking, breaking off, and
  * going home to be mended.
 *
 * Split out of AiBehaviour.test.cpp, which was 6,906 lines over 58 commits.
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    namespace
    {
        template <typename Order>
        int countOrders(const std::vector<PlayerCommand>& commands)
        {
            int n = 0;
            for (const auto& c : commands)
            {
                auto unitCommand = std::get_if<PlayerUnitCommand>(&c);
                if (!unitCommand)
                {
                    continue;
                }
                auto issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand->command);
                if (issue && std::holds_alternative<Order>(issue->order))
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("the army defends, gathers and attacks", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.attackArmySize = 3;
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("an enemy at the gates puts the AI on the defensive and its units attack it")
        {
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
            REQUIRE(countOrders<AttackOrder>(commands) >= 1);
            (void)raiderId;
        }

        SECTION("with no enemy in sight the army waits at the rally point")
        {
            addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Boom);
            REQUIRE(countOrders<MoveOrder>(commands) >= 1);
            REQUIRE(countOrders<AttackOrder>(commands) == 0);
        }

        SECTION("a big enough army with a known enemy base goes on the attack")
        {
            // Let the AI see the enemy base via radar-less omniscience for the test.
            auto brutal = makeDefaultBrutalProfile();
            brutal.attackArmySize = 3;
            brutal.scoutCount = 0;
            AiPlayerController cheat(ai, brutal, 42u, MapIntel{});
            addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 200_ss), script);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script);
            }
            runTicks(sim, cheat, 20, commands);
            REQUIRE(cheat.getBlackboard().phase == GamePhase::Attack);
            REQUIRE(cheat.getBlackboard().attackTarget.has_value());
            REQUIRE(countOrders<MoveOrder>(commands) >= 3);
        }
    }

    TEST_CASE("an army that will never reach the threshold attacks anyway", "[ai]")
    {
        // Watched on Dark Side, ARM against CORE, forty minutes. CORE's
        // faction default puts attackArmySize at fourteen; on that map its
        // economy never carried more than six, so it was in Attack for 7
        // status samples out of 80 and in Boom or Defend for 68, and at the
        // end every one of its combat units was standing within 440 of its
        // own commander while ARM's were three quarters of the way across
        // the map. Reported, fairly, as the armies just sitting in base.
        //
        // attackArmySize is the number that says a wave is worth sending.
        // Where the economy cannot reach it, it is instead the number that
        // says never attack -- and six that never leave cannot even trade.
        // So the build-up has a deadline: see attackPatienceSeconds.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        // Something of theirs to go at, far enough off not to be an intruder.
        addUnit(sim, "ARMSOLAR", human, SimVector(900_ss, 0_ss, 900_ss), script);

        // An army of three against a threshold of twenty: the shape of the
        // game above, where the threshold is simply out of reach.
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-100.0f - (i * 40.0f)), 0_ss, 0_ss), script);
        }

        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 20;
        REQUIRE(profile.retreatArmySize <= 3);

        SECTION("before the deadline it goes on building up, as it always did")
        {
            profile.attackPatienceSeconds = 60;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            CHECK(controller.getBlackboard().phase == GamePhase::Boom);
        }

        SECTION("once the deadline passes it attacks with what it has")
        {
            // One second of patience, and rather more than that of ticking.
            profile.attackPatienceSeconds = 1;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 3 * SimTicksPerSecond, commands);
            CHECK(controller.getBlackboard().phase == GamePhase::Attack);
        }

        SECTION("switched off, it never attacks at all -- which is the fault")
        {
            profile.attackPatienceSeconds = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 3 * SimTicksPerSecond, commands);
            CHECK(controller.getBlackboard().phase == GamePhase::Boom);
        }

        SECTION("and it still needs something worth sending")
        {
            // Below retreatArmySize a wave is spent the moment it forms, so
            // patience alone is not enough.
            GameSimulation bare(makeFlatTerrain(64, 64), 0u, 0, 0);
            addPlayer(bare, "human", GamePlayerType::Human, "ARM");
            auto lonely = addPlayer(bare, "ai", GamePlayerType::Computer, "ARM");
            defineWorld(bare);
            addUnit(bare, "ARMCOM", lonely, SimVector(0_ss, 0_ss, 0_ss), script);
            addUnit(bare, "ARMLAB", lonely, SimVector(100_ss, 0_ss, 0_ss), script);
            addUnit(bare, "ARMSOLAR", PlayerId(0), SimVector(900_ss, 0_ss, 900_ss), script);

            profile.attackPatienceSeconds = 1;
            AiPlayerController controller(lonely, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(bare, controller, 3 * SimTicksPerSecond, commands);
            CHECK(controller.getBlackboard().phase == GamePhase::Boom);
        }

        SECTION("and being raided in the meantime does not restart the clock")
        {
            // The first version of this measured time in the current Boom,
            // and on the map it was written for it never fired once: a side
            // being raided goes Boom, Defend, Boom, Defend all game -- CORE
            // did it thirty-one times in that run -- and each new Boom put
            // the clock back to nothing. So the clock runs from the last
            // tick spent ATTACKING, which a raid does not touch.
            profile.attackPatienceSeconds = 2;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;

            // Into Boom, so the clock starts.
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Boom);

            // Raided for most of the waiting time, then the raider dies.
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(250_ss, 0_ss, 0_ss), script);
            runTicks(sim, controller, 2 * SimTicksPerSecond, commands);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
            sim.killUnit(raiderId);

            // The deadline has passed while it was defending, so the wave
            // goes as soon as there is nothing at the gates. Measured from
            // the Boom it would have re-entered, there would be seconds
            // left to wait and the army would stay at home.
            runTicks(sim, controller, SimTicksPerSecond, commands);
            CHECK(controller.getBlackboard().phase == GamePhase::Attack);
        }
    }

    TEST_CASE("construction units keep out of fights nothing of ours covers", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMCK"].canReclamate = true;
        // Far enough to see the raider it is running from.
        sim.unitDefinitions["ARMCK"].sightDistance = 300u;
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-250_ss, 0_ss, -150_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(100_ss, 0_ss, 100_ss), script);
        // A raider within reach of the kbot.
        addUnit(sim, "ARMPW", human, SimVector(300_ss, 0_ss, 100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("alone, it backs off, away from the raider")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            auto moves = ordersFor<MoveOrder>(commands, kbotId);
            REQUIRE_FALSE(moves.empty());
            REQUIRE(moves.front().destination.x < 100_ss);
        }

        SECTION("with three kbots of ours between it and the raider, it stays")
        {
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(200_ss, 0_ss, SimScalar(60.0f + i * 40.0f)), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(ordersFor<MoveOrder>(commands, kbotId).empty());
        }

        SECTION("switched off, it stays")
        {
            profile.builderSafety = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(ordersFor<MoveOrder>(commands, kbotId).empty());
        }

        SECTION("a frame nearly finished is worth more than the margin: it stays on it")
        {
            // The tower it is two thirds of the way through, standing where
            // it is building. Backing off would throw the order away -- an
            // immediate move does -- and the replay this comes from showed
            // exactly that: an LLT abandoned at the last moment and the
            // builder standing in the open with nothing to do.
            auto frameId = addUnit(sim, "ARMLLT", ai, SimVector(110_ss, 0_ss, 100_ss), script);
            sim.getUnitState(frameId).buildTimeCompleted = (sim.unitDefinitions.at("ARMLLT").buildTime * 80u) / 100u;
            sim.getUnitState(kbotId).orders.push_back(CompleteBuildOrder(frameId));

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(ordersFor<MoveOrder>(commands, kbotId).empty());
        }

        SECTION("a frame barely started is left, but it is queued to come back and finish")
        {
            auto frameId = addUnit(sim, "ARMLLT", ai, SimVector(110_ss, 0_ss, 100_ss), script);
            sim.getUnitState(frameId).buildTimeCompleted = (sim.unitDefinitions.at("ARMLLT").buildTime * 10u) / 100u;
            sim.getUnitState(kbotId).orders.push_back(CompleteBuildOrder(frameId));

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE_FALSE(ordersFor<MoveOrder>(commands, kbotId).empty());
            auto resumed = ordersFor<CompleteBuildOrder>(commands, kbotId);
            REQUIRE_FALSE(resumed.empty());
            REQUIRE(resumed.front().target == frameId);
        }

        SECTION("switched off, the job is simply dropped")
        {
            profile.resumeAfterBackingOff = false;
            auto frameId = addUnit(sim, "ARMLLT", ai, SimVector(110_ss, 0_ss, 100_ss), script);
            sim.getUnitState(frameId).buildTimeCompleted = (sim.unitDefinitions.at("ARMLLT").buildTime * 10u) / 100u;
            sim.getUnitState(kbotId).orders.push_back(CompleteBuildOrder(frameId));

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE_FALSE(ordersFor<MoveOrder>(commands, kbotId).empty());
            REQUIRE(ordersFor<CompleteBuildOrder>(commands, kbotId).empty());
        }

        (void)commanderId;
        (void)human;
    }

    TEST_CASE("a unit stops shooting at what it is not hurting", "[ai]")
    {
        // From a replay: "a lot of units will all repeatedly shoot at a
        // structure their projectiles cant reach for ages and get stuck in
        // that loop until another unit is able to destroy it". The target is
        // there, in range, and never gets any less alive, so nothing in the
        // ordinary rules ever moves the unit on.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(-600_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-550_ss, 0_ss, -100_ss), script);
        // Far enough to see what it is shooting at: the simulation throws
        // away an attack order whose target it cannot see, and the default
        // sight in this world is exactly the distance below.
        sim.unitDefinitions["ARMPW"].sightDistance = 400u;
        auto kbotId = addUnit(sim, "ARMPW", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // A tower of theirs it can see and is already firing at, whose hit
        // points never move -- which is what standing below it looks like.
        auto towerId = addUnit(sim, "ARMLLT", human, SimVector(200_ss, 0_ss, 0_ss), script);
        sim.getUnitState(kbotId).orders.push_back(AttackOrder(towerId));

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;

        // A gun that goes off and does nothing, which is what a shell into
        // the slope below the target amounts to: the unit fires, the target
        // never loses a hit point, and the flat test terrain does not have
        // to be given a hill to say so.
        auto dud = [&]() {
            auto w = sim.weaponDefinitions.at("LASER");
            w.damage["DEFAULT"] = 0u;
            sim.weaponDefinitions["DUD"] = w;
            sim.unitDefinitions["ARMPW"].weapon1 = "DUD";
        };
        // The test harness never applies the AI's own commands, and the
        // simulation drops an attack order it has finished with, so the
        // order is put back each tick: in a real game the AI's attack
        // command is applied and the unit stays on the target, which is the
        // situation being tested.
        auto runAttacking = [&](AiPlayerController& controller, std::vector<PlayerCommand>& commands, int ticks) {
            for (int i = 0; i < ticks; ++i)
            {
                if (sim.getUnitState(kbotId).orders.empty())
                {
                    sim.getUnitState(kbotId).orders.push_back(AttackOrder(towerId));
                }
                runTicks(sim, controller, 1, commands);
            }
        };

        // Giving up on the target is how the attack order comes off: a move
        // to where the unit already stands. Moving to try for a shot is the
        // same kind of command to somewhere else, so the two are told apart
        // by where they point and not by their being there at all.
        auto standsStill = [&](const MoveOrder& order) {
            return order.destination.distance(sim.getUnitState(kbotId).position) < 8_ss;
        };

        SECTION("first it moves, because the fault is usually where it stands")
        {
            // Reported from a replay: units "firing but failing to inflict
            // damage should recalculate their position so theyre not firing
            // into elevated terrain". Dropping the target answers the wrong
            // half of that -- the enemy is worth shooting, the spot is not.
            dud();
            REQUIRE(profile.stalledAttackRepositionTries > 0);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runAttacking(controller, commands, (profile.stalledAttackSeconds + 2) * 30);

            auto moves = ordersFor<MoveOrder>(commands, kbotId);
            REQUIRE_FALSE(moves.empty());
            for (const auto& move : moves)
            {
                CHECK_FALSE(standsStill(move));
            }
        }

        SECTION("when the moves are spent it drops the target after all")
        {
            dud();
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            // Long enough for every reposition plus the give-up: the clock
            // restarts at each new position, so each attempt costs a full
            // stalledAttackSeconds. The unit does not actually move -- this
            // harness never applies the AI's own commands -- so every
            // attempt fails, which is the case being pinned.
            runAttacking(controller, commands, (profile.stalledAttackRepositionTries + 1) * (profile.stalledAttackSeconds + 2) * 30);

            auto moves = ordersFor<MoveOrder>(commands, kbotId);
            REQUIRE_FALSE(moves.empty());
            CHECK(standsStill(moves.back()));
        }

        SECTION("with the repositioning off, it drops the target at once as it used to")
        {
            dud();
            profile.stalledAttackRepositionTries = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runAttacking(controller, commands, (profile.stalledAttackSeconds + 2) * 30);

            auto moves = ordersFor<MoveOrder>(commands, kbotId);
            REQUIRE_FALSE(moves.empty());
            CHECK(standsStill(moves.front()));
        }

        SECTION("while it is getting hurt, it stays on it")
        {
            dud();
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            // The same run, except that its hit points move every second,
            // which is the whole question.
            for (int i = 0; i < (profile.stalledAttackSeconds + 2) * 30; ++i)
            {
                if (i % 30 == 0 && sim.getUnitState(towerId).hitPoints > 10)
                {
                    sim.getUnitState(towerId).hitPoints -= 5;
                }
                if (sim.getUnitState(kbotId).orders.empty())
                {
                    sim.getUnitState(kbotId).orders.push_back(AttackOrder(towerId));
                }
                runTicks(sim, controller, 1, commands);
            }

            REQUIRE(ordersFor<MoveOrder>(commands, kbotId).empty());
        }

        SECTION("switched off, it never gives up")
        {
            dud();
            profile.answerStalledAttacks = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runAttacking(controller, commands, (profile.stalledAttackSeconds + 2) * 30);

            REQUIRE(ordersFor<MoveOrder>(commands, kbotId).empty());
        }
    }

    TEST_CASE("nobody is sent to mend a commander hurt in a fight nothing covers", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMCK"].canReclamate = true;
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(250_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).hitPoints = 40;
        addUnit(sim, "ARMLAB", ai, SimVector(-250_ss, 0_ss, -150_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(400_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        auto sentToCommander = [&](const std::vector<PlayerCommand>& commands) {
            auto repairs = ordersFor<RepairOrder>(commands, kbotId);
            return std::any_of(repairs.begin(), repairs.end(), [&](const RepairOrder& o) { return o.target == commanderId; });
        };

        SECTION("with the raider on it and nothing of ours near, nobody goes")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            REQUIRE_FALSE(sentToCommander(commands));
        }

        SECTION("switched off, the kbot is sent")
        {
            profile.builderSafety = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            REQUIRE(sentToCommander(commands));
        }
    }

    TEST_CASE("contacts closing on the base put the army in a line across their path", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        auto defenderId = addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);

        // Brutal sees everything, which stands in for a radar the fixture
        // has no definition for; the rule counts a column in plain view the
        // same way it counts blips.
        auto profile = makeDefaultBrutalProfile();
        profile.scoutCount = 0;
        profile.attackArmySize = 50;
        // The fixture's map is 1024 across, so the rings are drawn to fit it.
        profile.defendRadius = 200_ss;
        // And short enough sighted that the defender does not simply go for them.
        profile.engageRadius = 150_ss;
        // Nor is this the commander's alarm, which would send him at them too.
        profile.commanderDangerRadius = 0_ss;
        auto outside = 450_ss;

        SECTION("two of them, walking in")
        {
            auto a = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            auto b = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 80_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 35, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());

            // runTicks applies no orders, so they are walked by hand.
            sim.getUnitState(a).position.x -= 60_ss;
            sim.getUnitState(b).position.x -= 60_ss;
            commands.clear();
            // A second for the contacts to be read again, and the army's own pass after it.
            runTicks(sim, controller, 70, commands);

            REQUIRE(controller.getBlackboard().incomingAttackFrom.has_value());
            auto moves = ordersFor<MoveOrder>(commands, defenderId);
            REQUIRE(!moves.empty());
            // On the enemy's side of the base, inside the defended ring.
            REQUIRE(moves.back().destination.x.value > 60.0f);
            REQUIRE(moves.back().destination.x.value < profile.defendRadius.value);
        }

        SECTION("standing still, they are not an attack")
        {
            addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 80_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());
        }

        SECTION("one is a scout")
        {
            auto a = addUnit(sim, "ARMPW", human, SimVector(outside, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 35, commands);
            sim.getUnitState(a).position.x -= 60_ss;
            runTicks(sim, controller, 35, commands);
            REQUIRE(!controller.getBlackboard().incomingAttackFrom.has_value());
        }
    }

    TEST_CASE("an attack is ordered at the factory, not at the frame on its pad", "[ai]")
    {
        // A frame on a factory's pad stands where the factory stands, so a
        // nearest-enemy search picked it ahead of the factory: a raid killed
        // the same cheap frame over and over while the factory that kept
        // making it stood untouched. Reported from play.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Home well away, so this is a raid and not a defence.
        addUnit(sim, "ARMCOM", ai, SimVector(-900_ss, 0_ss, -900_ss), script);

        auto labId = addUnit(sim, "ARMLAB", human, SimVector(500_ss, 0_ss, 0_ss), script);
        // Nearer the raider than the lab is, so a search that took frames
        // would take this one.
        auto frameId = addUnit(sim, "ARMPW", human, SimVector(490_ss, 0_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(420_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 20, commands);

        auto attacks = ordersFor<AttackOrder>(commands, raiderId);
        REQUIRE(!attacks.empty());
        for (const auto& attack : attacks)
        {
            auto target = std::get_if<UnitId>(&attack.target);
            REQUIRE(target != nullptr);
            REQUIRE(*target != frameId);
        }
        REQUIRE(*std::get_if<UnitId>(&attacks.front().target) == labId);
    }

    TEST_CASE("the army attacks in waves and holds when outnumbered", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        // Omniscient, so the enemy base is known without a scout.
        auto profile = makeDefaultBrutalProfile();
        profile.attackArmySize = 3;
        profile.retreatArmySize = 2;
        profile.scoutCount = 0;

        SECTION("a unit built during the attack gathers for the next wave instead of walking to the front")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 200_ss), script);
            std::vector<UnitId> wave;
            for (int i = 0; i < 3; ++i)
            {
                wave.push_back(addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script));
            }
            runTicks(sim, controller, 20, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.attackGroup.size() == 3);
            REQUIRE(bb.attackTarget.has_value());

            auto lateId = addUnit(sim, "ARMPW", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
            commands.clear();
            runTicks(sim, controller, 20, commands);
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.attackGroup.count(lateId.value) == 0);
            auto legs = ordersFor<MoveOrder>(commands, lateId);
            REQUIRE(!legs.empty());
            REQUIRE(bb.rallyPoint.has_value());
            REQUIRE(legs.front().destination.distanceSquared(*bb.rallyPoint) < (64_ss * 64_ss));
            REQUIRE(legs.front().destination.distanceSquared(*bb.attackTarget) > (200_ss * 200_ss));

            SECTION("and the attack ends when the wave that set out is spent, not when the army is")
            {
                sim.getUnitState(wave[0]).markAsDeadNoCorpse();
                sim.getUnitState(wave[1]).markAsDeadNoCorpse();
                runTicks(sim, controller, 20, commands);
                REQUIRE(bb.phase == GamePhase::Boom);
                REQUIRE(bb.attackGroup.empty());
                REQUIRE_FALSE(bb.waveSpent);
            }

            SECTION("and one intruder at home is met by the reserve, not by recalling the wave")
            {
                // A raider inside the defend radius, out of everyone's
                // engage radius. Before, this put the AI in Defend and the
                // wave turned round.
                auto raiderId = addUnit(sim, "ARMPW", human, SimVector(-600_ss, 0_ss, 300_ss), script);
                commands.clear();
                runTicks(sim, controller, 20, commands);
                REQUIRE(bb.phase == GamePhase::Attack);
                REQUIRE(bb.enemiesNearBase == std::vector<UnitId>{raiderId});
                auto answered = ordersFor<AttackOrder>(commands, lateId);
                REQUIRE(answered.size() >= 1);
                REQUIRE(std::get<UnitId>(answered.front().target) == raiderId);
                for (auto waveUnit : wave)
                {
                    REQUIRE(ordersFor<AttackOrder>(commands, waveUnit).empty());
                }
            }
        }

        SECTION("outnumbered at home, the army holds at the rally point rather than charging")
        {
            // One of ours against three of theirs, inside the defend radius
            // but beyond anything's engage radius.
            auto ownId = addUnit(sim, "ARMPW", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMPW", human, SimVector(650_ss, 0_ss, SimScalar(i * 40.0f)), script);
            }

            SECTION("held")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);
                REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
                REQUIRE(controller.getBlackboard().enemiesNearBase.size() == 3);
                REQUIRE(ordersFor<AttackOrder>(commands, ownId).empty());
                REQUIRE(!ordersFor<MoveOrder>(commands, ownId).empty());
            }

            SECTION("or, with the rule off, thrown at them one at a time")
            {
                profile.holdWhenOutnumbered = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 20, commands);
                REQUIRE(controller.getBlackboard().phase == GamePhase::Defend);
                REQUIRE(!ordersFor<AttackOrder>(commands, ownId).empty());
            }
        }
    }

    TEST_CASE("raiders at an extractor out on the map are answered by the reserve near enough to go", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 100_ss), script);
        addUnit(sim, "ARMMEX", ai, SimVector(-420_ss, 0_ss, -420_ss), script);
        // One raider at the extractor, well outside the base's radius and
        // out of reach of anything at home.
        auto raiderId = addUnit(sim, "ARMPW", human, SimVector(-440_ss, 0_ss, -380_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.defendRadius = 200_ss;
        profile.engageRadius = 150_ss;
        profile.scoutCount = 0;
        profile.attackArmySize = 50;
        profile.raidingParties = 0;

        SECTION("three kbots at home: they go")
        {
            std::vector<UnitId> ours;
            for (int i = 0; i < 3; ++i)
            {
                ours.push_back(addUnit(sim, "ARMPW", ai, SimVector(SimScalar(40.0f * i), 0_ss, -60_ss), script));
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().outpostRaidAnswered == std::optional<UnitId>(raiderId));
            for (auto id : ours)
            {
                auto attacks = ordersFor<AttackOrder>(commands, id);
                REQUIRE_FALSE(attacks.empty());
                REQUIRE(*std::get_if<UnitId>(&attacks.back().target) == raiderId);
            }
        }

        SECTION("one kbot against three raiders: it stays")
        {
            addUnit(sim, "ARMPW", human, SimVector(-400_ss, 0_ss, -380_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(-420_ss, 0_ss, -360_ss), script);
            auto oursId = addUnit(sim, "ARMPW", ai, SimVector(0_ss, 0_ss, -60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE_FALSE(controller.getBlackboard().outpostRaidAnswered.has_value());
            REQUIRE(ordersFor<AttackOrder>(commands, oursId).empty());
        }

        SECTION("switched off: they stay")
        {
            profile.answerOutpostRaids = false;
            std::vector<UnitId> ours;
            for (int i = 0; i < 3; ++i)
            {
                ours.push_back(addUnit(sim, "ARMPW", ai, SimVector(SimScalar(40.0f * i), 0_ss, -60_ss), script));
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            for (auto id : ours)
            {
                REQUIRE(ordersFor<AttackOrder>(commands, id).empty());
            }
        }
    }

    TEST_CASE("a hurt unit leaves the fight for the base until it is mended", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        // Ours out at the front, with an enemy in reach of it, and the pair
        // of them well outside the base's own radius -- inside it the base
        // being attacked is what decides, and that is a separate rule.
        auto kbotId = addUnit(sim, "ARMPW", ai, SimVector(1400_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMPW", human, SimVector(1500_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;

        SECTION("hurt, it walks home instead of fighting")
        {
            sim.getUnitState(kbotId).hitPoints = 20;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto moves = ordersFor<MoveOrder>(commands, kbotId);
            REQUIRE_FALSE(moves.empty());
            CHECK(moves.front().destination.x < 1400_ss);
            CHECK(ordersFor<AttackOrder>(commands, kbotId).empty());
            CHECK(controller.getBlackboard().mendingUnits.count(kbotId.value) == 1);
        }

        SECTION("whole, it fights")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK_FALSE(ordersFor<AttackOrder>(commands, kbotId).empty());
        }

        SECTION("switched off, it fights hurt")
        {
            sim.getUnitState(kbotId).hitPoints = 20;
            profile.retreatDamagedUnits = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK_FALSE(ordersFor<AttackOrder>(commands, kbotId).empty());
        }

        SECTION("too far from home to be worth the walk, it fights hurt")
        {
            // From a replay on Crystal Maze: the retreat micro "doesn't do
            // much good when an army should be advancing through a maze like
            // map". In a corridor the way home runs back through our own
            // army and takes long enough that the wave is a unit down while
            // it is pushing (mendMaxWalkHome).
            auto farId = addUnit(sim, "ARMPW", ai, SimVector(2400_ss, 0_ss, 0_ss), script);
            addUnit(sim, "ARMPW", human, SimVector(2500_ss, 0_ss, 0_ss), script);
            sim.getUnitState(farId).hitPoints = 20;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK(controller.getBlackboard().mendingUnits.count(farId.value) == 0);
            CHECK_FALSE(ordersFor<AttackOrder>(commands, farId).empty());
        }
    }

    TEST_CASE("a construction unit mends what came home hurt", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMCK"].canReclamate = true;
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        auto builderId = addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, 50_ss), script);
        auto hurtId = addUnit(sim, "ARMPW", ai, SimVector(60_ss, 0_ss, 60_ss), script);
        sim.getUnitState(hurtId).hitPoints = 30;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        // Nothing else for a builder to want, so the mend is what is left.
        profile.targetMetalExtractorCount = 3;
        profile.targetSolarCount = 4;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.targetAirPlantCount = 0;
        profile.spendSurplusOnCapacity = false;
        profile.expansionConstructors = 0;

        SECTION("it is put on the most hurt of ours near the base")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 130, commands);
            auto repairs = ordersFor<RepairOrder>(commands, builderId);
            CHECK(std::any_of(repairs.begin(), repairs.end(), [&](const RepairOrder& o) { return o.target == hurtId; }));
        }

        SECTION("switched off, it is left hurt")
        {
            profile.mendDamagedUnits = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 130, commands);
            auto repairs = ordersFor<RepairOrder>(commands, builderId);
            CHECK(std::none_of(repairs.begin(), repairs.end(), [&](const RepairOrder& o) { return o.target == hurtId; }));
        }
    }

    TEST_CASE("a gun of theirs that cannot move keeps us off exactly what it reaches", "[ai]")
    {
        // The test tower's laser reaches 200; a long gun of theirs reaches
        // 600. The old rule refused a flat 400 around either.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        WeaponDefinition longGun{};
        longGun.maxRange = 600_ss;
        sim.weaponDefinitions["LONGGUN"] = longGun;
        sim.unitDefinitions["ARMGUARD"] = sim.unitDefinitions["ARMLLT"];
        sim.unitDefinitions["ARMGUARD"].weapon1 = "LONGGUN";
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.scoutCount = 0;

        auto refused = [&](const SimVector& site) {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            BuildManager planner;
            return planner.siteUnderEnemyGuns(sim, profile, controller.getBlackboard(), site);
        };

        SECTION("a small tower of theirs refuses only what its own laser covers")
        {
            addUnit(sim, "ARMLLT", human, SimVector(800_ss, 0_ss, 0_ss), script);
            CHECK(refused(SimVector(700_ss, 0_ss, 0_ss)));
            // 300 away: inside the old flat 400, outside the laser's 200.
            CHECK_FALSE(refused(SimVector(500_ss, 0_ss, 0_ss)));
        }

        SECTION("a long gun of theirs refuses ground far past the old radius")
        {
            addUnit(sim, "ARMGUARD", human, SimVector(800_ss, 0_ss, 0_ss), script);
            CHECK(refused(SimVector(300_ss, 0_ss, 0_ss)));
            CHECK_FALSE(refused(SimVector(100_ss, 0_ss, 0_ss)));
        }

        SECTION("switched off, one radius answers for both")
        {
            profile.enemyGunRangeFromWeapon = false;
            addUnit(sim, "ARMGUARD", human, SimVector(800_ss, 0_ss, 0_ss), script);
            CHECK_FALSE(refused(SimVector(300_ss, 0_ss, 0_ss)));
            CHECK(refused(SimVector(500_ss, 0_ss, 0_ss)));
        }

        SECTION("what they can drive keeps the flat radius")
        {
            addUnit(sim, "ARMPW", human, SimVector(800_ss, 0_ss, 0_ss), script);
            CHECK(refused(SimVector(500_ss, 0_ss, 0_ss)));
        }
    }

    TEST_CASE("a unit that outranges what it faces stands where it cannot be answered", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Ours reaches 400, theirs the test laser's 200.
        WeaponDefinition longRocket{};
        longRocket.maxRange = 400_ss;
        sim.weaponDefinitions["ROCKET"] = longRocket;
        sim.unitDefinitions["ARMROCK"] = sim.unitDefinitions["ARMPW"];
        sim.unitDefinitions["ARMROCK"].weapon1 = "ROCKET";
        addUnit(sim, "ARMCOM", ai, SimVector(-400_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-300_ss, 0_ss, -300_ss), script);
        auto oursId = addUnit(sim, "ARMROCK", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;

        SECTION("inside their reach, it backs off instead of closing")
        {
            addUnit(sim, "ARMPW", human, SimVector(0_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto moves = ordersFor<MoveOrder>(commands, oursId);
            REQUIRE_FALSE(moves.empty());
            CHECK(moves.front().destination.x >= 240_ss);
            CHECK(ordersFor<AttackOrder>(commands, oursId).empty());
        }

        SECTION("already standing off, it shoots")
        {
            addUnit(sim, "ARMPW", human, SimVector(-200_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK_FALSE(ordersFor<AttackOrder>(commands, oursId).empty());
        }

        SECTION("a tower of theirs is not backed away from")
        {
            addUnit(sim, "ARMLLT", human, SimVector(0_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK(ordersFor<MoveOrder>(commands, oursId).empty());
            CHECK_FALSE(ordersFor<AttackOrder>(commands, oursId).empty());
        }

        SECTION("switched off, it closes as before")
        {
            profile.kiteWithLongerRange = false;
            addUnit(sim, "ARMPW", human, SimVector(0_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            CHECK(ordersFor<MoveOrder>(commands, oursId).empty());
            CHECK_FALSE(ordersFor<AttackOrder>(commands, oursId).empty());
        }
    }

    TEST_CASE("a unit waiting to be mended does not stand about for ever", "[ai]")
    {
        // From a replay: damaged units gathered in the middle of the base and
        // did nothing, the base being attacked included.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        auto hurtId = addUnit(sim, "ARMPW", ai, SimVector(60_ss, 0_ss, 60_ss), script);
        sim.getUnitState(hurtId).hitPoints = 20;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.mendDamagedUnits = false;

        SECTION("an enemy in the base is answered hurt")
        {
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(150_ss, 0_ss, 150_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto attacks = ordersFor<AttackOrder>(commands, hurtId);
            REQUIRE_FALSE(attacks.empty());
            CHECK(*std::get_if<UnitId>(&attacks.front().target) == raiderId);
        }

        SECTION("after the wait it goes back to the fight whether mended or not")
        {
            // Well outside the base, so it is the wait that decides and not
            // the base being attacked.
            addUnit(sim, "ARMPW", human, SimVector(1500_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            REQUIRE(controller.getBlackboard().mendingUnits.count(hurtId.value) == 1);
            CHECK(ordersFor<MoveOrder>(commands, hurtId).empty());

            // Past the wait: it is offered the ordinary behaviour again,
            // which here is the wave forming, so it is no longer held at the
            // base doing nothing.
            commands.clear();
            runTicks(sim, controller, profile.mendWaitSeconds * 30 + 60, commands);
            bool told = !ordersFor<MoveOrder>(commands, hurtId).empty() || !ordersFor<AttackOrder>(commands, hurtId).empty();
            CHECK(told);
        }

        SECTION("two of them stand in different places")
        {
            auto secondId = addUnit(sim, "ARMPW", ai, SimVector(-60_ss, 0_ss, -60_ss), script);
            sim.getUnitState(secondId).hitPoints = 20;
            sim.getUnitState(hurtId).position = SimVector(600_ss, 0_ss, 600_ss);
            sim.getUnitState(secondId).position = SimVector(-600_ss, 0_ss, -600_ss);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);
            auto first = ordersFor<MoveOrder>(commands, hurtId);
            auto second = ordersFor<MoveOrder>(commands, secondId);
            REQUIRE_FALSE(first.empty());
            REQUIRE_FALSE(second.empty());
            bool apart = first.front().destination.x != second.front().destination.x
                || first.front().destination.z != second.front().destination.z;
            CHECK(apart);
        }
    }
}
