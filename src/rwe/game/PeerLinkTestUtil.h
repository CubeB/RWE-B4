#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <rwe/game/PeerLink.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/rwe_time.h>
#include <vector>

namespace rwe
{
    /**
     * Two PeerLinks joined by a fake link: a clock the test drives, and a
     * seeded one-way delay, jitter, loss and reorder model. Each side feeds its
     * own PlayerCommandService, so a test reads exactly what crossed.
     *
     * Nothing here reads the wall clock or a real socket, so a run is
     * reproducible and two runs with the same seed send the same packets.
     */
    class FakePeerLink
    {
    public:
        struct Options
        {
            /** One-way propagation delay; the round trip is twice this. */
            std::chrono::milliseconds oneWayDelay{50};

            /** Added to the one-way delay, up to this many milliseconds. */
            std::chrono::milliseconds maxJitter{0};

            /** Probability in [0, 1] that a packet never arrives. */
            double loss{0.0};

            std::uint32_t seed{1};

            /** Passed to PeerLink::makePacket; the real transport uses 1496. */
            std::size_t packetSizeLimit{1496};
        };

        FakePeerLink() : FakePeerLink(Options{}) {}

        explicit FakePeerLink(const Options& options)
            : options_(options), rng_(options.seed)
        {
            services_[0].registerPlayer(PlayerId(1));
            services_[0].registerHashSource(PlayerId(1));
            services_[1].registerPlayer(PlayerId(0));
            services_[1].registerHashSource(PlayerId(0));

            links_[0] = std::make_unique<PeerLink>(
                PlayerId(0),
                PlayerId(1),
                &services_[0],
                [this]() { return nextPacketId_++; },
                PeerLink::ResumeState{});
            links_[1] = std::make_unique<PeerLink>(
                PlayerId(1),
                PlayerId(0),
                &services_[1],
                [this]() { return nextPacketId_++; },
                PeerLink::ResumeState{});

            nextSend_[0] = now_;
            nextSend_[1] = now_;
        }

        /** Side 0 is the local peer and side 1 the remote one, or the reverse. */
        PeerLink& link(int side) { return *links_[side]; }

        PlayerCommandService& service(int side) { return services_[side]; }

        Timestamp now() const { return now_; }

        const Options& options() const { return options_; }

        std::size_t packetsSent() const { return packetsSent_; }
        std::size_t packetsLost() const { return packetsLost_; }
        std::size_t packetsDelivered() const { return packetsDelivered_; }

        /**
         * The arrival time of each command set delivered to `side`, in order,
         * one entry per set rather than per packet. A set retransmitted after
         * loss is stamped when the retransmission arrives.
         */
        const std::vector<Timestamp>& setArrivals(int side) const { return setArrivals_[side]; }

        /**
         * Moves the clock without running either send timer or delivering
         * anything, for a test that only wants time to pass for the submit
         * rate limit.
         */
        void advance(std::chrono::milliseconds duration) { now_ += duration; }

        void run(std::chrono::milliseconds duration) { runUntil(now_ + duration); }

        /**
         * Submit a set the way the transport's game thread does: the link
         * takes it, and a packet leaves at once unless one went out within the
         * submit rate limit.
         */
        void submitCommands(int side, SceneTime sceneTime, const PeerLink::CommandSet& commands)
        {
            links_[side]->submitCommands(sceneTime, commands);
            send(side);
        }

        void submitGameHash(int side, GameHash hash)
        {
            links_[side]->submitGameHash(hash);
            send(side);
        }

        /**
         * Moves the clock to `end`, in event order: each side sends on its own
         * SendInterval and packets are handed over at the instant they arrive,
         * so the ack delay is whatever the gap between the two really was.
         */
        void runUntil(Timestamp end)
        {
            while (true)
            {
                auto next = end;
                if (nextSend_[0] < next)
                {
                    next = nextSend_[0];
                }
                if (nextSend_[1] < next)
                {
                    next = nextSend_[1];
                }
                for (const auto& packet : inFlight_)
                {
                    if (packet.arrival < next)
                    {
                        next = packet.arrival;
                    }
                }

                now_ = next;
                deliverDue();

                for (int side = 0; side < 2; ++side)
                {
                    while (nextSend_[side] <= now_)
                    {
                        send(side);
                        nextSend_[side] += PeerLink::SendInterval;
                    }
                }

                if (now_ >= end)
                {
                    return;
                }
            }
        }

    private:
        struct InFlight
        {
            Timestamp arrival;
            int toSide;
            std::uint64_t order;
            std::vector<char> bytes;
        };

        Options options_;
        std::mt19937 rng_;
        std::array<PlayerCommandService, 2> services_;
        std::array<std::unique_ptr<PeerLink>, 2> links_;
        std::array<Timestamp, 2> nextSend_;
        std::vector<InFlight> inFlight_;
        Timestamp now_{};
        std::uint64_t nextOrder_{0};
        int nextPacketId_{0};

        std::size_t packetsSent_{0};
        std::size_t packetsLost_{0};
        std::size_t packetsDelivered_{0};
        std::array<std::vector<Timestamp>, 2> setArrivals_;

        void send(int side)
        {
            if (!links_[side]->sendIsDue(now_))
            {
                return;
            }

            auto bytes = links_[side]->makePacket(now_, options_.packetSizeLimit);
            if (bytes.empty())
            {
                return;
            }
            ++packetsSent_;

            if (options_.loss > 0.0)
            {
                auto draw = rng_() % 10000u;
                if (draw < static_cast<std::uint32_t>(options_.loss * 10000.0))
                {
                    ++packetsLost_;
                    return;
                }
            }

            std::uint32_t jitter = 0;
            if (options_.maxJitter.count() > 0)
            {
                jitter = rng_() % static_cast<std::uint32_t>(options_.maxJitter.count() + 1);
            }

            inFlight_.push_back(InFlight{
                now_ + options_.oneWayDelay + std::chrono::milliseconds(jitter),
                1 - side,
                nextOrder_++,
                std::move(bytes)});
        }

        void deliverDue()
        {
            while (true)
            {
                auto best = inFlight_.end();
                for (auto it = inFlight_.begin(); it != inFlight_.end(); ++it)
                {
                    if (it->arrival > now_)
                    {
                        continue;
                    }
                    if (best == inFlight_.end()
                        || it->arrival < best->arrival
                        || (it->arrival == best->arrival && it->order < best->order))
                    {
                        best = it;
                    }
                }

                if (best == inFlight_.end())
                {
                    return;
                }

                auto packet = std::move(*best);
                inFlight_.erase(best);

                proto::NetworkMessage message;
                if (!message.ParseFromArray(packet.bytes.data(), static_cast<int>(packet.bytes.size())))
                {
                    continue;
                }
                if (!message.has_game_update())
                {
                    continue;
                }

                auto receiver = PlayerId(1 - packet.toSide);
                auto before = services_[packet.toSide].bufferedCommandCount(receiver);
                links_[packet.toSide]->onPacket(message.game_update(), packet.arrival);
                auto after = services_[packet.toSide].bufferedCommandCount(receiver);
                for (auto i = before; i < after; ++i)
                {
                    setArrivals_[packet.toSide].push_back(packet.arrival);
                }
                ++packetsDelivered_;
            }
        }
    };
}