#include "MultiplayerSetup.h"

#include <algorithm>
#include <rwe/ip_util.h>

namespace rwe
{
    bool isUsablePort(const std::string& port)
    {
        if (port.empty() || port.size() > 5)
        {
            return false;
        }

        if (!std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
        {
            return false;
        }

        auto value = std::stoul(port);
        return value > 0 && value <= 65535;
    }

    std::optional<std::string> multiplayerSetupProblem(
        const std::vector<MultiplayerSlot>& slots,
        const std::string& localPort)
    {
        auto humans = std::count_if(slots.begin(), slots.end(), [](const auto& s) { return s.isLocalHuman; });
        if (humans == 0)
        {
            return "One slot has to be Player: that is the seat you play from.";
        }
        if (humans > 1)
        {
            return "Only one slot can be Player. The others are Network, Computer or Open.";
        }

        auto networkCount = std::count_if(slots.begin(), slots.end(), [](const auto& s) { return s.isNetwork; });
        if (networkCount == 0)
        {
            return "Set at least one slot to Network, with the address of the machine playing it.";
        }

        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto& slot = slots[i];
            if (!slot.isNetwork)
            {
                continue;
            }

            auto number = std::to_string(i + 1);
            if (slot.address.empty())
            {
                return "Player " + number + " is Network but has no address. Type host:port, such as 192.168.0.5:1337.";
            }

            auto hostAndPort = getHostAndPort(slot.address);
            if (!hostAndPort)
            {
                return "Player " + number + "'s address is not host:port: " + slot.address;
            }
            if (!isUsablePort(hostAndPort->second))
            {
                return "Player " + number + "'s port is not a port: " + hostAndPort->second;
            }
        }

        if (!isUsablePort(localPort))
        {
            return "This machine's port is not a port: " + localPort;
        }

        return std::nullopt;
    }
}
