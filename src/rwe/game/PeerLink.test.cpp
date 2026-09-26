#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <network.pb.h>
#include <rwe/game/PeerLinkTestUtil.h>
#include <rwe/proto/serialization.h>

namespace rwe
{
    namespace
    {
        std::vector<PlayerCommand> speedCommand(int index)
        {
            return {PlayerSetGameSpeedCommand{index}};
        }

        struct DepthFixtureResult
        {
            unsigned int stalls;
            double averageDepth;
        };

        /** The formula this replaces: 1.25 * rtt + 200 ms. */
        unsigned int legacyCommandBufferTarget(float averageRoundTripMillis)
        {
            auto maxRtt = std::clamp(averageRoundTripMillis, 16.0f, 2000.0f);
            auto millis = maxRtt + (maxRtt / 4.0f) + 200.0f;
            return static_cast<unsigned int>(millis / static_cast<float>(SimMillisecondsPerTick)) + 1;
        }

        /**
         * Drives the two-peer fixture the way a lockstep scene does: each tick
         * the local peer tops its stream up to the depth the formula gives, so
         * sets are sent ahead of the remote's one-set-a-tick drain. A set the
         * remote reaches before its arrival is a stall. Stalls are counted
         * only after warm-up, while the estimators are still converging.
         */
        DepthFixtureResult runDepthFixture(const FakePeerLink::Options& options, int warmupTicks, int measuredTicks, bool useNewFormula)
        {
            FakePeerLink link(options);
            const auto tick = std::chrono::milliseconds(SimMillisecondsPerTick);
            const auto start = link.now();

            std::vector<unsigned int> depths;
            unsigned int submitted = 0;

            for (int t = 0; t < warmupTicks + measuredTicks; ++t)
            {
                auto depth = useNewFormula
                    ? commandBufferTargetForRttMillis(link.link(0).averageRoundTripTime(), link.link(0).roundTripDeviation())
                    : legacyCommandBufferTarget(link.link(0).averageRoundTripTime());
                depths.push_back(depth);

                while (submitted < static_cast<unsigned int>(t) + depth)
                {
                    link.submitCommands(0, SceneTime(submitted), speedCommand(static_cast<int>(submitted)));
                    ++submitted;
                }

                link.run(tick);
            }

            const auto& arrivals = link.setArrivals(1);
            unsigned int stalls = 0;
            double depthSum = 0.0;
            unsigned int counted = 0;
            for (int k = warmupTicks; k < warmupTicks + measuredTicks; ++k)
            {
                depthSum += depths[static_cast<std::size_t>(k)];
                ++counted;
                if (static_cast<std::size_t>(k) >= arrivals.size() || arrivals[static_cast<std::size_t>(k)] > start + tick * (k + 1))
                {
                    ++stalls;
                }
            }

            return DepthFixtureResult{
                stalls,
                counted > 0 ? depthSum / counted : 0.0};
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

    TEST_CASE("A submit sends at once and other submits within the rate limit ride in one packet")
    {
        FakePeerLink link;

        link.submitCommands(0, SceneTime(0), speedCommand(0));
        link.submitCommands(0, SceneTime(1), speedCommand(1));
        link.submitCommands(0, SceneTime(2), speedCommand(2));

        REQUIRE(link.packetsSent() == 1);

        link.run(std::chrono::seconds(1));

        std::vector<int> arrived;
        while (auto popped = link.service(1).tryPopCommands())
        {
            for (const auto& [player, commands] : *popped)
            {
                REQUIRE(player == PlayerId(0));
                arrived.push_back(std::get<PlayerSetGameSpeedCommand>(commands.front()).speedIndex);
            }
        }
        REQUIRE(arrived == std::vector<int>{0, 1, 2});
    }

    TEST_CASE("A submit after the rate limit sends at once")
    {
        FakePeerLink link;

        link.submitCommands(0, SceneTime(0), speedCommand(0));
        REQUIRE(link.packetsSent() == 1);

        link.advance(PeerLink::SubmitSendInterval + std::chrono::milliseconds(1));
        link.submitCommands(0, SceneTime(1), speedCommand(1));

        REQUIRE(link.packetsSent() == 2);
    }

    TEST_CASE("A set submitted between timer ticks leaves without waiting for the send timer")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(5);
        FakePeerLink link(options);

        link.run(std::chrono::milliseconds(30));
        link.submitCommands(0, SceneTime(0), speedCommand(0));
        link.run(std::chrono::milliseconds(10));

        REQUIRE(link.service(1).bufferedCommandCount(PlayerId(0)) == 1);
    }

    TEST_CASE("A mid-period submit's round trip is measured from when it was sent")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(40);
        FakePeerLink link(options);

        link.advance(std::chrono::milliseconds(30));
        link.submitCommands(0, SceneTime(0), speedCommand(0));
        link.run(std::chrono::seconds(6));

        auto status = link.link(0).status(link.now(), std::nullopt);
        REQUIRE(status.averageRoundTripMillis == Catch::Approx(80.0f).margin(1.0f));
    }

    TEST_CASE("A hash submit sends at once too")
    {
        FakePeerLink link;

        link.submitGameHash(0, GameHash(42));
        REQUIRE(link.packetsSent() == 1);

        link.run(std::chrono::seconds(1));

        REQUIRE(link.service(1).bufferedHashCount(PlayerId(0)) == 1);
    }

    TEST_CASE("The send timer still resends sets that have not been acknowledged")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(500);
        FakePeerLink link(options);

        link.submitCommands(0, SceneTime(0), speedCommand(0));
        REQUIRE(link.packetsSent() == 1);

        link.run(std::chrono::milliseconds(250));

        REQUIRE(link.packetsSent() >= 3);
        REQUIRE(link.link(0).status(link.now(), std::nullopt).unackedCommandSets == 1);
    }

    TEST_CASE("PeerLink tracks the round trip deviation next to the average")
    {
        FakePeerLink::Options options;
        options.oneWayDelay = std::chrono::milliseconds(40);
        options.maxJitter = std::chrono::milliseconds(60);
        options.seed = 4242;
        FakePeerLink link(options);

        for (int i = 0; i < 120; ++i)
        {
            link.link(0).submitCommands(SceneTime(i), speedCommand(i));
            link.run(std::chrono::milliseconds(SimMillisecondsPerTick));
        }

        auto status = link.link(0).status(link.now(), std::nullopt);
        REQUIRE(status.averageRoundTripMillis > 0.0f);
        REQUIRE(status.roundTripDeviationMillis > 0.0f);
        REQUIRE(status.roundTripDeviationMillis < status.averageRoundTripMillis);
        REQUIRE(status.roundTripDeviationMillis == Catch::Approx(link.link(0).roundTripDeviation()));
    }

    TEST_CASE("The jitter-aware depth is a smaller delay on a steady link and covers a lossy one")
    {
        SECTION("a steady link gets a smaller depth and no more stalls")
        {
            FakePeerLink::Options options;
            options.oneWayDelay = std::chrono::milliseconds(40);
            options.seed = 11;

            auto tuned = runDepthFixture(options, 150, 300, true);
            auto legacy = runDepthFixture(options, 150, 300, false);

            INFO("tuned depth " << tuned.averageDepth << " stalls " << tuned.stalls
                                << ", legacy depth " << legacy.averageDepth << " stalls " << legacy.stalls);
            REQUIRE(tuned.averageDepth < legacy.averageDepth);
            REQUIRE(tuned.stalls <= legacy.stalls);
        }

        SECTION("a jittery, lossy link is not stalled more, with no more than the legacy delay")
        {
            FakePeerLink::Options options;
            options.oneWayDelay = std::chrono::milliseconds(30);
            options.maxJitter = std::chrono::milliseconds(60);
            options.loss = 0.01;
            options.seed = 99;

            auto tuned = runDepthFixture(options, 150, 300, true);
            auto legacy = runDepthFixture(options, 150, 300, false);

            INFO("tuned depth " << tuned.averageDepth << " stalls " << tuned.stalls
                                << ", legacy depth " << legacy.averageDepth << " stalls " << legacy.stalls);
            REQUIRE(tuned.stalls <= legacy.stalls);
            REQUIRE(tuned.averageDepth <= legacy.averageDepth);
        }
    }
}
