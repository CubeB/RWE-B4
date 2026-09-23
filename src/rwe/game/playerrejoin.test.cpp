#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/proto/serialization.h>

// The other half of a drop: letting the peer that went quiet come back, and
// the two things that have to line up for that to be a recovery rather than a
// desync -- where its reopened command stream is spliced in, and which tick
// its first sync hash belongs to. Issue #188.

namespace rwe
{
    namespace
    {
        void addPeers(PlayerCommandService& service, unsigned int count)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                service.registerPlayer(PlayerId(i));
                service.registerHashSource(PlayerId(i));
            }
        }

        void pushEmpty(PlayerCommandService& service, PlayerId player, unsigned int count)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                service.pushCommands(player, std::vector<PlayerCommand>());
            }
        }

        /** Runs the given number of ticks, requiring each one to go ahead. */
        void runTicks(PlayerCommandService& service, unsigned int count)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                REQUIRE(service.tryPopCommands());
            }
        }

        /** A command that is recognisably itself when it comes back out. */
        PlayerCommand markerCommand(int speedIndex)
        {
            return PlayerSetGameSpeedCommand{speedIndex};
        }

        std::optional<std::vector<PlayerCommand>> popFor(PlayerCommandService& service, PlayerId player)
        {
            auto popped = service.tryPopCommands();
            if (!popped)
            {
                return std::nullopt;
            }
            for (const auto& [id, commands] : *popped)
            {
                if (id == player)
                {
                    return commands;
                }
            }
            return std::nullopt;
        }
    }

    TEST_CASE("PlayerCommandService::rejoinPlayer")
    {
        SECTION("makes the tick wait for the returning player again")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 20);
            service.dropPlayer(PlayerId(1), 1);

            // Without player 1 the game runs on whatever player 0 supplies.
            runTicks(service, 5);
            REQUIRE(service.playersNotReady().empty());

            service.rejoinPlayer(PlayerId(1), 10);
            REQUIRE(!service.isDropped(PlayerId(1)));

            // Ticks 6 to 9 are still empty for them and still go ahead.
            runTicks(service, 4);

            // Tick 10 is theirs, and nothing has arrived: the game stalls,
            // which is exactly what it does for any peer that is behind.
            REQUIRE(!service.tryPopCommands());
            REQUIRE(service.playersNotReady() == std::vector<PlayerId>{PlayerId(1)});

            service.pushCommands(PlayerId(1), {markerCommand(3)});
            auto popped = popFor(service, PlayerId(1));
            REQUIRE(popped);
            REQUIRE(popped->size() == 1);
        }

        SECTION("splices the reopened stream at the tick it names, not after the ticks it missed")
        {
            // The counter this exercises: while a player is dropped the game
            // consumes ticks without popping anything from their buffer, so
            // padding by push count alone would put their first real set at
            // tick (rejoin + ticks missed) on this peer and at the rejoin tick
            // on one that had not run as far.
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 40);
            pushEmpty(service, PlayerId(1), 3);

            service.dropPlayer(PlayerId(1), 4);
            runTicks(service, 30);

            service.rejoinPlayer(PlayerId(1), 35);
            service.pushCommands(PlayerId(1), {markerCommand(7)});

            // Ticks 31 to 34 are empty, and tick 35 is the marker.
            for (unsigned int tick = 31; tick <= 34; ++tick)
            {
                auto popped = popFor(service, PlayerId(1));
                REQUIRE(popped);
                REQUIRE(popped->empty());
            }

            auto atRejoin = popFor(service, PlayerId(1));
            REQUIRE(atRejoin);
            REQUIRE(atRejoin->size() == 1);
            REQUIRE(std::get<PlayerSetGameSpeedCommand>(atRejoin->front()).speedIndex == 7);
        }

        SECTION("keeps the real sets the player sent before they went quiet")
        {
            // A rejoin can arrive before the game has reached the tick the drop
            // cut at, in which case the buffer still holds that player's own
            // commands for the ticks below the cut. Those are what everyone
            // else simulated, and they have to survive.
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 40);
            pushEmpty(service, PlayerId(1), 1);
            service.pushCommands(PlayerId(1), {markerCommand(2)});

            service.dropPlayer(PlayerId(1), 10);
            service.rejoinPlayer(PlayerId(1), 20);

            REQUIRE(popFor(service, PlayerId(1))->empty());
            auto second = popFor(service, PlayerId(1));
            REQUIRE(second);
            REQUIRE(second->size() == 1);
            REQUIRE(std::get<PlayerSetGameSpeedCommand>(second->front()).speedIndex == 2);
        }

        SECTION("is ignored for a player who never left")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);
            pushEmpty(service, PlayerId(1), 5);

            service.rejoinPlayer(PlayerId(1), 3);

            // Unchanged: five sets still waiting, none invented, none dropped.
            REQUIRE(service.bufferedCommandCount(PlayerId(1)) == 5);
        }

        SECTION("refuses a tick that is not after the one the stream was cut at")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 10);
            service.dropPlayer(PlayerId(1), 8);

            service.rejoinPlayer(PlayerId(1), 8);
            REQUIRE(service.isDropped(PlayerId(1)));
        }

        SECTION("refuses a tick the game has already run")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 30);
            service.dropPlayer(PlayerId(1), 2);
            runTicks(service, 20);

            service.rejoinPlayer(PlayerId(1), 15);
            REQUIRE(service.isDropped(PlayerId(1)));
        }
    }

    TEST_CASE("a rejoining peer's sync hashes")
    {
        SECTION("are compared from the tick it came back at, not from tick 1")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 40);
            service.dropPlayer(PlayerId(1), 1);

            // Player 0 alone, agreeing with itself, for ticks 1 to 5.
            for (unsigned int i = 0; i < 5; ++i)
            {
                service.pushHash(PlayerId(0), GameHash(100 + i));
            }
            REQUIRE(!service.checkHashes());

            runTicks(service, 5);
            service.rejoinPlayer(PlayerId(1), 10);

            // Player 0 reports on to tick 12. Player 1 has no hashes at all for
            // the ticks it missed and is left out of the comparison rather than
            // stalling it, which is what the whole mechanism is for.
            for (unsigned int i = 5; i < 12; ++i)
            {
                service.pushHash(PlayerId(0), GameHash(100 + i));
            }
            REQUIRE(!service.checkHashes());

            // Its first hash is tick 10's, and it agrees; so does tick 11's.
            service.pushHash(PlayerId(1), GameHash(109));
            service.pushHash(PlayerId(1), GameHash(110));
            REQUIRE(!service.checkHashes());
        }

        SECTION("are compared at all on the peer that joined a game in progress")
        {
            // Every source starts at the rejoin tick there, this peer's own
            // included -- so the comparison has to be moved up to meet them.
            // Left where it starts it would wait for tick 1 from sources that
            // have no tick 1, which is every tick of the rest of the game not
            // compared and a desync that nothing would ever report.
            PlayerCommandService service;
            service.registerPlayer(PlayerId(0));
            service.registerPlayer(PlayerId(1));
            service.registerHashSource(PlayerId(0), SceneTime(900));
            service.registerHashSource(PlayerId(1), SceneTime(900));

            service.pushHash(PlayerId(0), GameHash(11));
            service.pushHash(PlayerId(1), GameHash(11));
            REQUIRE(!service.checkHashes());

            service.pushHash(PlayerId(0), GameHash(12));
            service.pushHash(PlayerId(1), GameHash(99));

            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(901));
        }

        SECTION("name the right tick when they disagree after a rejoin")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 40);
            service.dropPlayer(PlayerId(1), 1);
            runTicks(service, 5);
            service.rejoinPlayer(PlayerId(1), 10);

            for (unsigned int tick = 1; tick <= 11; ++tick)
            {
                service.pushHash(PlayerId(0), GameHash(tick));
            }

            // Tick 10 agrees; tick 11 does not.
            service.pushHash(PlayerId(1), GameHash(10));
            service.pushHash(PlayerId(1), GameHash(999));

            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(11));
        }
    }

    TEST_CASE("PlayerCommandService::needsCommandsForTick")
    {
        SECTION("is false for the ticks a rejoin filled in, and true from the one it reopened at")
        {
            // What a recording asks before supplying a set. It supplies one per
            // player per tick whatever else is going on, so a peer winding
            // itself forward across its own rejoin would otherwise supply the
            // padded ticks a second time -- and carry every later tick of its
            // own game that many ticks behind the rest.
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 600);
            service.dropPlayer(PlayerId(1), 400);
            runTicks(service, 500);

            service.rejoinPlayer(PlayerId(1), 550);

            REQUIRE(!service.needsCommandsForTick(PlayerId(1), 500));
            REQUIRE(!service.needsCommandsForTick(PlayerId(1), 549));
            REQUIRE(service.needsCommandsForTick(PlayerId(1), 550));
            REQUIRE(service.needsCommandsForTick(PlayerId(1), 551));

            // Their neighbour, who never left, is asked for every tick.
            REQUIRE(service.needsCommandsForTick(PlayerId(0), 601));
        }

        SECTION("is false for a player whose stream is cut")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 10);
            service.dropPlayer(PlayerId(1), 4);

            REQUIRE(!service.needsCommandsForTick(PlayerId(1), 9000));
            REQUIRE(service.needsCommandsForTick(PlayerId(0), 11));
        }
    }

    TEST_CASE("PlayerCommandService::rewindTo")
    {
        SECTION("puts every stream back where the recording is about to start again")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 20);
            pushEmpty(service, PlayerId(1), 20);
            runTicks(service, 10);

            service.rewindTo(5);

            REQUIRE(service.bufferedCommandCount(PlayerId(0)) == 0);
            REQUIRE(service.bufferedCommandCount(PlayerId(1)) == 0);
            REQUIRE(service.needsCommandsForTick(PlayerId(0), 6));
            REQUIRE(service.needsCommandsForTick(PlayerId(1), 6));

            // And the ticks run from there are the ticks the file supplies.
            pushEmpty(service, PlayerId(0), 1);
            pushEmpty(service, PlayerId(1), 1);
            REQUIRE(service.tryPopCommands());
            REQUIRE(!service.needsCommandsForTick(PlayerId(0), 6));
            REQUIRE(service.needsCommandsForTick(PlayerId(0), 7));
        }

        SECTION("undoes a drop the viewer has wound back past, and keeps one it has not")
        {
            PlayerCommandService service;
            addPeers(service, 3);
            service.dropPlayer(PlayerId(1), 3);
            service.dropPlayer(PlayerId(2), 15);

            service.rewindTo(5);

            // Player 2 goes quiet at tick 15, which from tick 5 has not
            // happened yet -- the command saying so is ahead in the file.
            REQUIRE(!service.isDropped(PlayerId(2)));
            REQUIRE(service.isDropped(PlayerId(1)));
        }
    }

    TEST_CASE("a rejoin arriving in the command stream")
    {
        SECTION("takes effect as it arrives")
        {
            PlayerCommandService service;
            addPeers(service, 3);
            pushEmpty(service, PlayerId(0), 10);
            pushEmpty(service, PlayerId(1), 10);
            service.dropPlayer(PlayerId(2), 2);

            service.pushCommands(PlayerId(0), {PlayerRejoinedCommand{PlayerId(2), 6}});
            REQUIRE(!service.isDropped(PlayerId(2)));
        }

        SECTION("is ignored from a peer who is not the one to issue it")
        {
            PlayerCommandService service;
            addPeers(service, 3);
            pushEmpty(service, PlayerId(0), 10);
            service.dropPlayer(PlayerId(2), 2);

            // Player 0 is the lowest peer other than player 2, so this is not
            // player 1's to declare -- the same rule that decides who may drop
            // them, asked of the state a drop leaves behind.
            service.pushCommands(PlayerId(1), {PlayerRejoinedCommand{PlayerId(2), 6}});
            REQUIRE(service.isDropped(PlayerId(2)));
        }

        SECTION("survives a round trip through the wire format")
        {
            proto::PlayerCommand wire;
            serializePlayerCommand(PlayerCommand(PlayerRejoinedCommand{PlayerId(2), 9042}), wire);
            auto restored = deserializeCommand(wire);

            REQUIRE(std::holds_alternative<PlayerRejoinedCommand>(restored));
            REQUIRE(std::get<PlayerRejoinedCommand>(restored).player == PlayerId(2));
            REQUIRE(std::get<PlayerRejoinedCommand>(restored).fromTick == 9042u);
        }
    }
}
