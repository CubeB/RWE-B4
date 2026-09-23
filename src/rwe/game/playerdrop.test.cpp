#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/proto/serialization.h>

namespace rwe
{
    namespace
    {
        /** Registers players 0..n-1, all of them peers reporting a sync hash. */
        void addPeers(PlayerCommandService& service, unsigned int count)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                service.registerPlayer(PlayerId(i));
                service.registerHashSource(PlayerId(i));
            }
        }

        /** A command that is recognisably itself when it comes back out. */
        PlayerCommand markerCommand(int speedIndex)
        {
            return PlayerSetGameSpeedCommand{speedIndex};
        }

        void pushEmpty(PlayerCommandService& service, PlayerId player, unsigned int count)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                service.pushCommands(player, std::vector<PlayerCommand>());
            }
        }

        /** The set the given player contributed to one popped round. */
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

    TEST_CASE("PlayerCommandService::dropPlayer")
    {
        SECTION("lets the tick go ahead without the player it names")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);

            // Player 1 has sent nothing: every tick is blocked on them.
            REQUIRE(!service.tryPopCommands());

            service.dropPlayer(PlayerId(1), 1);

            REQUIRE(service.tryPopCommands());
            REQUIRE(service.isDropped(PlayerId(1)));
        }

        SECTION("hands back an empty set for them from then on")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 3);
            service.dropPlayer(PlayerId(1), 1);

            for (int i = 0; i < 3; ++i)
            {
                auto set = popFor(service, PlayerId(1));
                REQUIRE(set);
                REQUIRE(set->empty());
            }
        }

        SECTION("keeps what they sent below the cut")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);

            service.pushCommands(PlayerId(1), {markerCommand(1)});
            service.pushCommands(PlayerId(1), {markerCommand(2)});

            // Sets 1 and 2 are theirs; everything from tick 3 on is empty.
            service.dropPlayer(PlayerId(1), 3);

            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            REQUIRE(popFor(service, PlayerId(1))->empty());
        }

        SECTION("discards what they sent past the cut")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);

            // This peer received four sets from them. Another peer may have
            // received only two, which is why the cut is forced rather than
            // taken from whatever each happened to have.
            for (int i = 1; i <= 4; ++i)
            {
                service.pushCommands(PlayerId(1), {markerCommand(i)});
            }

            service.dropPlayer(PlayerId(1), 3);

            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            REQUIRE(popFor(service, PlayerId(1))->empty());
            REQUIRE(popFor(service, PlayerId(1))->empty());
        }

        SECTION("fills in what they never sent below the cut")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 10);

            service.pushCommands(PlayerId(1), {markerCommand(1)});

            // A peer that had more of their stream than this one cut it at 5,
            // so ticks 2 to 4 have to be filled in rather than left missing.
            service.dropPlayer(PlayerId(1), 5);

            REQUIRE(service.bufferedCommandCount(PlayerId(1)) == 4);
            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            for (int i = 0; i < 3; ++i)
            {
                REQUIRE(popFor(service, PlayerId(1))->empty());
            }
        }

        SECTION("refuses anything further from them")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);
            service.dropPlayer(PlayerId(1), 1);

            // A packet still in flight, or a peer that has come back. Taking it
            // would put a command after the cut on this peer and nowhere else.
            service.pushCommands(PlayerId(1), {markerCommand(9)});

            REQUIRE(popFor(service, PlayerId(1))->empty());
        }

        SECTION("is idempotent, so it may be applied on arrival and again when popped")
        {
            PlayerCommandService service;
            addPeers(service, 2);
            pushEmpty(service, PlayerId(0), 5);
            service.pushCommands(PlayerId(1), {markerCommand(1)});

            service.dropPlayer(PlayerId(1), 2);
            // A second drop naming a different tick must not move the cut that
            // the rest of the game has already been played against.
            service.dropPlayer(PlayerId(1), 99);

            REQUIRE(popFor(service, PlayerId(1))->size() == 1);
            REQUIRE(popFor(service, PlayerId(1))->empty());
        }

        SECTION("stops them stalling the sync hash comparison")
        {
            PlayerCommandService service;
            addPeers(service, 2);

            service.pushHash(PlayerId(0), GameHash(7));
            service.pushHash(PlayerId(0), GameHash(9));

            // Nothing to compare against while they are still expected.
            REQUIRE(!service.checkHashes());

            service.dropPlayer(PlayerId(1), 1);

            // Now the surviving peer's hashes are consumed rather than piling
            // up behind one that is never coming.
            REQUIRE(!service.checkHashes());
            service.pushHash(PlayerId(0), GameHash(11));
            REQUIRE(!service.checkHashes());
        }
    }

    TEST_CASE("PlayerCommandService::playersNotReady")
    {
        PlayerCommandService service;
        addPeers(service, 2);

        SECTION("names the player a tick is waiting on")
        {
            pushEmpty(service, PlayerId(0), 1);
            REQUIRE(service.playersNotReady() == std::vector<PlayerId>{PlayerId(1)});
        }

        SECTION("is empty when every player is ready")
        {
            pushEmpty(service, PlayerId(0), 1);
            pushEmpty(service, PlayerId(1), 1);
            REQUIRE(service.playersNotReady().empty());
        }

        SECTION("stops naming a player once they have been dropped")
        {
            pushEmpty(service, PlayerId(0), 1);
            service.dropPlayer(PlayerId(1), 1);
            REQUIRE(service.playersNotReady().empty());
        }
    }

    TEST_CASE("PlayerCommandService::droppingPlayerFor")
    {
        SECTION("is the lowest-numbered peer other than the one being dropped")
        {
            PlayerCommandService service;
            addPeers(service, 3);

            REQUIRE(service.droppingPlayerFor(PlayerId(2)) == PlayerId(0));
            REQUIRE(service.droppingPlayerFor(PlayerId(1)) == PlayerId(0));
        }

        SECTION("is the next peer up when the host is the one who went")
        {
            // The likeliest failure of all, and the reason the rule is a rule
            // rather than "the host decides": if only player 0 could declare a
            // loss, player 0 going would hang the game for everybody.
            PlayerCommandService service;
            addPeers(service, 3);

            REQUIRE(service.droppingPlayerFor(PlayerId(0)) == PlayerId(1));
        }

        SECTION("skips peers already dropped")
        {
            PlayerCommandService service;
            addPeers(service, 3);
            service.dropPlayer(PlayerId(0), 1);

            REQUIRE(service.droppingPlayerFor(PlayerId(2)) == PlayerId(1));
        }

        SECTION("skips a computer player, which is no peer")
        {
            // A computer player is every peer's own copy of the same AI. It has
            // no machine of its own and cannot notice anything, which is why
            // the rule is over hash sources rather than over players.
            PlayerCommandService service;
            service.registerPlayer(PlayerId(0));
            service.registerPlayer(PlayerId(1));
            service.registerHashSource(PlayerId(1));
            service.registerPlayer(PlayerId(2));
            service.registerHashSource(PlayerId(2));

            REQUIRE(service.droppingPlayerFor(PlayerId(2)) == PlayerId(1));
        }

        SECTION("is nobody when there would be no one left to say so")
        {
            PlayerCommandService service;
            addPeers(service, 1);

            REQUIRE(!service.droppingPlayerFor(PlayerId(0)));
        }
    }

    TEST_CASE("a drop arriving in the command stream")
    {
        SECTION("takes effect as it arrives, which is what unblocks the tick")
        {
            PlayerCommandService service;
            addPeers(service, 3);
            pushEmpty(service, PlayerId(1), 1);

            // Player 2 has gone. Player 0 is the one to say so, and says it in
            // the ordinary stream -- which is also the stream that cannot be
            // popped until the drop has been applied.
            service.pushCommands(PlayerId(0), {PlayerDroppedCommand{PlayerId(2), 1}});

            REQUIRE(service.isDropped(PlayerId(2)));
            REQUIRE(service.tryPopCommands());
        }

        SECTION("is ignored from a peer who is not the one to issue it")
        {
            PlayerCommandService service;
            addPeers(service, 3);

            // Player 1 is not the lowest peer other than player 2, so this is
            // not theirs to declare. Honouring it would let two peers cut the
            // same stream at two different ticks.
            service.pushCommands(PlayerId(1), {PlayerDroppedCommand{PlayerId(2), 1}});

            REQUIRE(!service.isDropped(PlayerId(2)));
        }

        SECTION("survives a round trip through the wire format")
        {
            proto::PlayerCommand wire;
            serializePlayerCommand(PlayerCommand(PlayerDroppedCommand{PlayerId(3), 4281}), wire);
            auto restored = deserializeCommand(wire);

            REQUIRE(std::holds_alternative<PlayerDroppedCommand>(restored));
            REQUIRE(std::get<PlayerDroppedCommand>(restored).player == PlayerId(3));
            REQUIRE(std::get<PlayerDroppedCommand>(restored).fromTick == 4281u);
        }
    }
}
