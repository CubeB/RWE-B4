#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /** A computer player, which is what feedAiCommands looks for. */
        PlayerId addComputerPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("ai"),
                GamePlayerType::Computer,
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

        /** A command that is recognisably itself when it comes back out. */
        PlayerCommand markerCommand(int speedIndex)
        {
            return PlayerSetGameSpeedCommand{speedIndex};
        }

        std::optional<int> speedIndexOf(const std::vector<PlayerCommand>& commands)
        {
            if (commands.size() != 1)
            {
                return std::nullopt;
            }
            if (const auto* c = std::get_if<PlayerSetGameSpeedCommand>(&commands.front()); c != nullptr)
            {
                return c->speedIndex;
            }
            return std::nullopt;
        }

        /**
         * One tick of the drain, returning what the AI's set held.
         *
         * The human is fed an empty set first because the service pops a round
         * only when every registered player has one waiting -- which is the
         * whole reason the AI's buffer has to be kept topped up at all.
         *
         * Named for what it does rather than `tick`: an anonymous namespace
         * inside namespace rwe joins the enclosing overload set rather than
         * shadowing it, and sim_test_util already has a `tick`.
         */
        std::vector<PlayerCommand> popOneTick(PlayerCommandService& service, PlayerId human, PlayerId ai)
        {
            service.pushCommands(human, std::vector<PlayerCommand>());
            auto popped = service.tryPopCommands();
            REQUIRE(popped);
            for (const auto& [id, commands] : *popped)
            {
                if (id == ai)
                {
                    return commands;
                }
            }
            FAIL("the computer player was not in the popped round");
            return {};
        }
    }

    TEST_CASE("feedAiCommands")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 100, 3000);
        auto human = addPlayer(sim);
        auto ai = addComputerPlayer(sim);

        PlayerCommandService service;
        service.registerPlayer(human);
        service.registerPlayer(ai);

        auto depth = aiCommandBufferDepth();

        SECTION("fills a computer player's buffer to the depth, and nobody else's")
        {
            feedAiCommands(sim, service, depth);

            REQUIRE(service.bufferedCommandCount(ai) == depth);
            REQUIRE(service.bufferedCommandCount(human) == 0);
        }

        SECTION("takes what the AI asked for, leaving its queue empty")
        {
            sim.aiPendingCommands[ai].push_back(markerCommand(3));

            feedAiCommands(sim, service, depth);

            REQUIRE(sim.aiPendingCommands[ai].empty());
        }

        SECTION("holds an order for the depth in ticks, once the buffer is primed")
        {
            // The first feed fills a cold buffer, so what it takes goes to the
            // front and is seen on the very next tick. That is the priming
            // round; the delay the depth buys starts after it.
            feedAiCommands(sim, service, depth);
            popOneTick(service, human, ai);

            sim.aiPendingCommands[ai].push_back(markerCommand(7));
            feedAiCommands(sim, service, depth);
            REQUIRE(service.bufferedCommandCount(ai) == depth);

            for (unsigned int i = 1; i < depth; ++i)
            {
                REQUIRE(popOneTick(service, human, ai).empty());
            }
            REQUIRE(speedIndexOf(popOneTick(service, human, ai)) == 7);
        }

        SECTION("fed a tick at a time, an order's delay is the same number every time")
        {
            // The property the whole thing exists for: two peers running the
            // same simulation must apply an AI order on the same tick, and
            // they only do if the number of ticks it waits is a constant
            // rather than a function of anything local. Feed and drain in
            // step, twice over, and the same order comes out at the same
            // remove both times.
            feedAiCommands(sim, service, depth);
            popOneTick(service, human, ai);

            for (int round = 0; round < 2; ++round)
            {
                sim.aiPendingCommands[ai].push_back(markerCommand(round));

                unsigned int ticksWaited = 0;
                std::optional<int> seen;
                while (!seen)
                {
                    feedAiCommands(sim, service, depth);
                    seen = speedIndexOf(popOneTick(service, human, ai));
                    ++ticksWaited;
                }

                REQUIRE(*seen == round);
                REQUIRE(ticksWaited == depth);
            }
        }

        SECTION("a buffer that is already deep enough grows by one and no further")
        {
            feedAiCommands(sim, service, depth);
            feedAiCommands(sim, service, depth);

            // A second feed with nothing popped in between pushes the set it
            // took and pads nothing. It cannot run away.
            REQUIRE(service.bufferedCommandCount(ai) == depth + 1);

            feedAiCommands(sim, service, depth);
            REQUIRE(service.bufferedCommandCount(ai) == depth + 1);
        }
    }

    TEST_CASE("aiCommandBufferDepth")
    {
        SECTION("is the depth a peer with no peers would use, and does not move")
        {
            REQUIRE(aiCommandBufferDepth() == commandBufferTargetForRttMillis(0.0f));
            REQUIRE(aiCommandBufferDepth() == 7);
        }

        SECTION("is not the humans' depth, which follows the round trip time")
        {
            // The two being different is the point. A human's orders cross the
            // network and have to be waited for; an AI's are issued by every
            // peer's own copy of the AI and are not. Tying the AI's delay to a
            // measured latency made it a different number on each peer, and
            // the depth decides the game -- the same seed at two different
            // depths diverges within two seconds.
            REQUIRE(commandBufferTargetForRttMillis(60.0f) != aiCommandBufferDepth());
        }
    }

    TEST_CASE("a unit command reaches the simulation only if its player may give it", "[network]")
    {
        // Issue #75. The issuing player is where a command came from -- a
        // network endpoint, the computer player, the recording -- and before
        // this, nothing compared it with the unit's owner, nor the unit types
        // and counts it named with anything the game knew.
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto owner = addPlayer(sim, "owner");
        auto other = addPlayer(sim, "other");
        UnitDefinition def{};
        def.isMobile = true;
        def.builder = true;
        sim.unitDefinitions["ARMPW"] = def;
        auto script = makeEmptyCobScript();
        auto unitId = addUnitOfType(sim, "ARMPW", owner, SimVector(100_ss, 0_ss, 100_ss), script);
        auto destination = SimVector(200_ss, 0_ss, 200_ss);
        using IssueKind = PlayerUnitCommand::IssueOrder::IssueKind;

        SECTION("its owner's order is applied")
        {
            REQUIRE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(MoveOrder(destination), IssueKind::Immediate))));
            REQUIRE(sim.getUnitState(unitId).orders.size() == 1);
        }

        SECTION("anyone else's is refused, and changes nothing")
        {
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, other, PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(MoveOrder(destination), IssueKind::Immediate))));
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, other, PlayerUnitCommand(unitId, PlayerUnitCommand::SelfDestruct{})));
            REQUIRE(sim.getUnitState(unitId).orders.empty());
        }

        SECTION("a unit type the game has no definition for is refused")
        {
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(BuildOrder("NOSUCHUNIT", destination), IssueKind::Immediate))));
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{1, "NOSUCHUNIT"})));
            REQUIRE(sim.getUnitState(unitId).orders.empty());
        }

        SECTION("a queue change out of range is refused")
        {
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{1 << 30, "ARMPW"})));
            REQUIRE_FALSE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyStockpile{std::numeric_limits<int>::min()})));
            REQUIRE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{20, "ARMPW"})));
        }

        SECTION("a resurrection starts from nothing, whatever progress the command claims")
        {
            ResurrectOrder claimed(FeatureId(3));
            claimed.remainingTicks = 0;
            REQUIRE(applyUnitCommandToSimulation(sim, owner, PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(claimed, IssueKind::Immediate))));
            const auto& orders = sim.getUnitState(unitId).orders;
            REQUIRE(orders.size() == 1);
            const auto* resurrect = std::get_if<ResurrectOrder>(&orders.front());
            REQUIRE(resurrect != nullptr);
            REQUIRE_FALSE(resurrect->remainingTicks.has_value());
        }
    }
}
