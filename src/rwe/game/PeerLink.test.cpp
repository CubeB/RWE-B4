#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <network.pb.h>
#include <rwe/game/PeerLinkTestUtil.h>
#include <rwe/network_util.h>
#include <rwe/proto/serialization.h>

namespace rwe
{
    namespace
    {
        std::vector<PlayerCommand> speedCommand(int index)
        {
            return {PlayerSetGameSpeedCommand{index}};
        }
    }

    TEST_CASE("PeerLink delivers every command set in order and exactly once under loss and reorder")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(30);
        options.maxJitter = std::chrono::milliseconds(250);
        options.loss = 0.2;
        options.seed = 12345;
        FakePeerLink link(options);

        const int setCount = 20;
        for (int i = 0; i < setCount; ++i)
        {
            link.link(0).submitCommands(SceneTime(i), speedCommand(i));
        }

        link.run(std::chrono::seconds(10));

        std::vector<int> arrived;
        while (auto popped = link.service(1).tryPopCommands())
        {
            for (const auto& [player, commands] : *popped)
            {
                REQUIRE(player == PlayerId(0));
                REQUIRE(commands.size() == 1);
                arrived.push_back(std::get<PlayerSetGameSpeedCommand>(commands.front()).speedIndex);
            }
        }

        REQUIRE(arrived.size() == setCount);
        for (int i = 0; i < setCount; ++i)
        {
            REQUIRE(arrived[static_cast<std::size_t>(i)] == i);
        }
    }

    TEST_CASE("PeerLink's round trip estimate converges to the round trip with the ack delay removed")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(40);
        options.seed = 7;
        FakePeerLink link(options);

        for (int i = 0; i < 60; ++i)
        {
            link.link(0).submitCommands(SceneTime(i), speedCommand(i));
        }

        link.run(std::chrono::seconds(6));

        auto status = link.link(0).status(link.now(), std::nullopt);
        REQUIRE(status.averageRoundTripMillis > 0.0f);
        REQUIRE(status.averageRoundTripMillis == Catch::Approx(80.0f).margin(1.0f));
    }

    TEST_CASE("PeerLink cuts an oversized backlog to a prefix and sends the rest later")
    {
        FakePeerLink link;

        std::vector<PlayerCommand> bigSet;
        for (int i = 0; i < 60; ++i)
        {
            bigSet.push_back(PlayerSetGameSpeedCommand{i});
        }

        const int setCount = 20;
        for (int i = 0; i < setCount; ++i)
        {
            link.link(0).submitCommands(SceneTime(i), bigSet);
        }

        auto packet = link.link(0).makePacket(link.now(), link.options().packetSizeLimit);
        REQUIRE(!packet.empty());
        REQUIRE(packet.size() <= link.options().packetSizeLimit);

        proto::NetworkMessage parsed;
        REQUIRE(parsed.ParseFromArray(packet.data(), static_cast<int>(packet.size())));
        REQUIRE(parsed.game_update().command_set_size() > 0);
        REQUIRE(parsed.game_update().command_set_size() < setCount);

        // Nothing was acked, so the whole backlog is still owed.
        REQUIRE(link.link(0).status(link.now(), std::nullopt).unackedCommandSets == setCount);

        link.run(std::chrono::seconds(3));

        std::size_t received = 0;
        while (link.service(1).tryPopCommands())
        {
            ++received;
        }
        REQUIRE(received == setCount);
    }

    TEST_CASE("PeerLink refuses command sets while it is not accepting them")
    {
        FakePeerLink link;
        link.link(1).setAcceptingCommands(false);

        link.link(0).submitCommands(SceneTime(0), speedCommand(1));
        link.run(std::chrono::seconds(1));

        REQUIRE(link.service(1).bufferedCommandCount(PlayerId(0)) == 0);
        REQUIRE(link.link(0).status(link.now(), std::nullopt).unackedCommandSets == 1);

        link.link(1).setAcceptingCommands(true);
        link.run(std::chrono::seconds(1));

        REQUIRE(link.service(1).bufferedCommandCount(PlayerId(0)) == 1);
        REQUIRE(link.link(0).status(link.now(), std::nullopt).unackedCommandSets == 0);
    }

    TEST_CASE("PeerLink discards command sets from before the point a stream resumes")
    {
        PlayerCommandService service;
        service.registerPlayer(PlayerId(0));

        int nextPacketId = 0;
        PeerLink::ResumeState resume;
        resume.nextCommandToReceive = SequenceNumber(3);
        PeerLink link(
            PlayerId(1),
            PlayerId(0),
            &service,
            [&nextPacketId]() { return nextPacketId++; },
            resume);

        proto::GameUpdateMessage message;
        message.set_packet_id(0);
        message.set_player_id(0);
        message.set_next_command_set_to_send(0);
        message.set_next_command_set_to_receive(0);
        message.set_next_game_hash_to_send(0);
        message.set_next_game_hash_to_receive(0);
        message.set_next_chat_to_send(0);
        message.set_next_chat_to_receive(0);
        for (int i = 0; i < 5; ++i)
        {
            auto& set = *message.add_command_set();
            serializePlayerCommand(PlayerSetGameSpeedCommand{i}, *set.add_command());
        }

        link.onPacket(message, Timestamp{});

        REQUIRE(service.bufferedCommandCount(PlayerId(0)) == 2);
        std::vector<int> arrived;
        while (auto popped = service.tryPopCommands())
        {
            for (const auto& [player, commands] : *popped)
            {
                arrived.push_back(std::get<PlayerSetGameSpeedCommand>(commands.front()).speedIndex);
            }
        }
        REQUIRE(arrived == std::vector<int>{3, 4});
    }

    TEST_CASE("PeerLink carries the game hash stream")
    {
        FakePeerLink link;
        link.service(1).registerHashSource(PlayerId(1));

        link.link(0).submitGameHash(GameHash(111));
        link.service(1).pushHash(PlayerId(1), GameHash(111));
        link.run(std::chrono::seconds(1));

        REQUIRE(link.service(1).bufferedHashCount(PlayerId(0)) == 1);
        REQUIRE(!link.service(1).checkHashes());

        link.link(0).submitGameHash(GameHash(222));
        link.service(1).pushHash(PlayerId(1), GameHash(333));
        link.run(std::chrono::seconds(1));

        auto report = link.service(1).checkHashes();
        REQUIRE(report);
        REQUIRE(report->tick == SceneTime(2));
        REQUIRE(report->hashes.size() == 2);
    }

    TEST_CASE("PeerLink carries chat")
    {
        FakePeerLink link;
        link.link(0).submitChatMessage("hello there");
        link.run(std::chrono::seconds(1));

        auto messages = link.link(1).takeReceivedChat();
        REQUIRE(messages.size() == 1);
        REQUIRE(messages.front().sender == PlayerId(0));
        REQUIRE(messages.front().text == "hello there");
    }

    TEST_CASE("PeerLink carries the sender's run state")
    {
        FakePeerLink link;
        link.link(0).submitRunState(500, true, false, 420);
        link.run(std::chrono::seconds(1));

        REQUIRE(link.link(1).remoteSpeedPermille() == 500);
        REQUIRE(link.link(1).remotePaused());
        REQUIRE_FALSE(link.link(1).remoteStalled());
        REQUIRE(link.link(1).remoteSustainableSpeedPermille() == 420);
    }

    TEST_CASE("PeerLink treats an old peer with no run-state fields as 1x and moving")
    {
        FakePeerLink link;

        proto::GameUpdateMessage message;
        message.set_packet_id(0);
        message.set_player_id(0);
        message.set_next_command_set_to_send(0);
        message.set_next_command_set_to_receive(0);
        message.set_next_game_hash_to_send(0);
        message.set_next_game_hash_to_receive(0);
        message.set_next_chat_to_send(0);
        message.set_next_chat_to_receive(0);
        link.link(1).onPacket(message, link.now());

        REQUIRE(link.link(1).remoteSpeedPermille() == 1000);
        REQUIRE_FALSE(link.link(1).remotePaused());
        REQUIRE_FALSE(link.link(1).remoteStalled());
        REQUIRE(link.link(1).remoteSustainableSpeedPermille() == 1000);
    }

    TEST_CASE("The effective speed converges to what the slowest peer can sustain")
    {
        FakePeerLink link;

        const unsigned int chosen = 1000;
        auto fastCapacity = estimateSustainableSpeedPermille(chosen, 1, 0, 5.0f);
        auto slowCapacity = estimateSustainableSpeedPermille(chosen, 1, 0, 100.0f);
        REQUIRE(fastCapacity == chosen);
        REQUIRE(slowCapacity == 330);

        SpeedGovernor fastGovernor;
        SpeedGovernor slowGovernor;

        auto update = [&](SpeedGovernor& governor, PlayerId ownId, unsigned int own, PlayerId peerId, unsigned int peer) {
            return governor.update(
                link.now(),
                chosen,
                std::vector<PeerCapacity>{{ownId, own}, {peerId, peer}});
        };

        unsigned int fastEffective = chosen;
        unsigned int slowEffective = chosen;
        for (int frame = 0; frame < 40; ++frame)
        {
            link.link(0).submitRunState(fastEffective, false, false, fastCapacity);
            link.link(1).submitRunState(slowEffective, false, false, slowCapacity);
            link.run(std::chrono::milliseconds(100));

            fastEffective = update(fastGovernor, PlayerId(0), fastCapacity, PlayerId(1), link.link(0).remoteSustainableSpeedPermille());
            slowEffective = update(slowGovernor, PlayerId(1), slowCapacity, PlayerId(0), link.link(1).remoteSustainableSpeedPermille());
        }

        // Both peers settled on the slow machine's capacity and stayed there,
        // and each names the machine holding it down.
        REQUIRE(fastEffective == slowCapacity);
        REQUIRE(slowEffective == slowCapacity);
        REQUIRE(fastGovernor.limitingPeers() == std::vector<PlayerId>{PlayerId(1)});
        REQUIRE(slowGovernor.limitingPeers() == std::vector<PlayerId>{PlayerId(1)});
    }
}