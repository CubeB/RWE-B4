#include <rwe/puppet/TadDemo.h>
#include <algorithm>
#include <fstream>

namespace rwe
{
    namespace
    {
        struct MetaHandler : TadHandler
        {
            TadDemoMeta meta;

            void onHeader(const TadHeader& h) override { meta.header = h; }
            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override { meta.players.push_back(p); }
            void onUnitData(const TadBytes& d) override { meta.unitTable = tadDecodeUnitTable(d); }

            void onPacket(const TadPacket& packet, const std::vector<TadBytes>&, const TadWalkStats&) override
            {
                if (std::find(meta.senders.begin(), meta.senders.end(), packet.sender) == meta.senders.end())
                {
                    meta.senders.push_back(packet.sender);
                }
            }
        };
    }

    std::optional<TadDemoMeta> readTadDemoMeta(const std::string& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return std::nullopt;
        }

        MetaHandler handler;
        try
        {
            readTad(stream, handler);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
        return std::move(handler.meta);
    }

    std::optional<GameParameters> gameParametersForDemo(const TadDemoMeta& meta, const std::string& demoPath)
    {
        if (!meta.header || meta.header->maxUnits == 0)
        {
            return std::nullopt;
        }

        GameParameters params(meta.header->mapName, 0u);
        params.tadDemoFile = demoPath;

        std::size_t slot = 0;
        for (const auto& player : meta.players)
        {
            if (player.isWatcher())
            {
                continue;
            }
            if (slot >= params.players.size())
            {
                return std::nullopt;
            }

            std::string side = static_cast<TadSide>(player.side) == TadSide::Core ? "CORE" : "ARM";
            params.players[slot] = PlayerInfo{
                std::optional<std::string>(player.name),
                PlayerControllerType(PlayerControllerTypeNetwork{}),
                side,
                PlayerColorIndex(player.color),
                Metal(1000.0f),
                Energy(1000.0f)};
            ++slot;
        }

        return params;
    }
}
