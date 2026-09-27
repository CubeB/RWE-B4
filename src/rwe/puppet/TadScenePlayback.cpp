#include <rwe/puppet/TadScenePlayback.h>
#include <algorithm>
#include <fstream>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/sim/GameSimulation.h>
#include <stdexcept>
#include <utility>

namespace rwe
{
    struct TadScenePlayback::Impl
    {
        /**
         * Reads the demo and builds the driver in one pass. The header is what
         * supplies maxUnits, so the driver is made when it arrives; the unit
         * table is checked as soon as it is read, before any position is
         * trusted, and a sender is given a seat the first time it is seen.
         */
        struct BuildHandler : TadHandler
        {
            GameSimulation& sim;
            std::vector<std::string> unitLoadOrder;
            std::unique_ptr<TadPuppetDriver> driver;
            std::vector<uint8_t> knownSenders;
            std::size_t nextSlot{0};

            BuildHandler(GameSimulation& sim, std::vector<std::string> unitLoadOrder)
                : sim(sim), unitLoadOrder(std::move(unitLoadOrder))
            {
            }

            void onHeader(const TadHeader& h) override
            {
                if (h.maxUnits == 0)
                {
                    throw std::runtime_error("Demo header says maxUnits 0");
                }
                driver = std::make_unique<TadPuppetDriver>(sim, h.maxUnits, unitLoadOrder);
                driver->setExternalClock(true);
            }

            void onUnitData(const TadBytes& data) override
            {
                auto table = tadDecodeUnitTable(data);
                if (table && table->restricted.size() != unitLoadOrder.size())
                {
                    throw std::runtime_error(
                        "Demo's unit table has " + std::to_string(table->restricted.size())
                        + " types, the loaded data has " + std::to_string(unitLoadOrder.size()));
                }
            }

            void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, const TadWalkStats&) override
            {
                if (!driver)
                {
                    return;
                }
                if (std::find(knownSenders.begin(), knownSenders.end(), packet.sender) == knownSenders.end())
                {
                    knownSenders.push_back(packet.sender);
                    if (nextSlot < sim.players.size())
                    {
                        driver->addPlayer(packet.sender, PlayerId(static_cast<unsigned int>(nextSlot)));
                        ++nextSlot;
                    }
                }
                driver->onPacket(packet, subPackets);
            }
        };

        std::unique_ptr<TadPuppetDriver> driver;
    };

    TadScenePlayback::TadScenePlayback(GameSimulation& sim, const std::string& path, std::vector<std::string> unitLoadOrder)
        : impl(std::make_unique<Impl>())
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Could not open demo: " + path);
        }

        Impl::BuildHandler handler(sim, std::move(unitLoadOrder));
        readTad(stream, handler);
        if (!handler.driver)
        {
            throw std::runtime_error("Demo has no header: " + path);
        }
        impl->driver = std::move(handler.driver);
    }

    TadScenePlayback::~TadScenePlayback() = default;

    void TadScenePlayback::applyTick(uint32_t tick)
    {
        impl->driver->applyTick(tick);
    }

    std::optional<uint32_t> TadScenePlayback::lastTick() const
    {
        return impl->driver->lastTick();
    }

    const TadPuppetStats& TadScenePlayback::stats() const
    {
        return impl->driver->stats();
    }

    std::vector<TadChatLine> TadScenePlayback::takeChat()
    {
        return impl->driver->takeChat();
    }
}
