#include "SimDiagnostics.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <rwe/game/save_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <string>

namespace rwe
{
    std::optional<SimLag> parseSimLag(const char* spec)
    {
        if (spec == nullptr)
        {
            return std::nullopt;
        }

        auto specText = std::string(spec);
        auto at = specText.find('@');
        auto millisecondsText = at == std::string::npos ? specText : specText.substr(0, at);

        char* end = nullptr;
        errno = 0;
        auto milliseconds = std::strtoll(millisecondsText.c_str(), &end, 10);
        if (end == millisecondsText.c_str() || *end != '\0' || errno == ERANGE || milliseconds < 0)
        {
            return std::nullopt;
        }

        unsigned int fromTick = 0;
        if (at != std::string::npos)
        {
            auto tickText = specText.substr(at + 1);
            char* tickEnd = nullptr;
            errno = 0;
            auto tick = std::strtoll(tickText.c_str(), &tickEnd, 10);
            if (tickEnd == tickText.c_str() || *tickEnd != '\0' || errno == ERANGE || tick < 0
                || tick > std::numeric_limits<unsigned int>::max())
            {
                return std::nullopt;
            }
            fromTick = static_cast<unsigned int>(tick);
        }

        return SimLag{std::chrono::milliseconds(milliseconds), fromTick};
    }

    SimDiagnostics::SimDiagnostics()
    {
        if (const char* path = std::getenv("RWE_HASH_LOG"))
        {
            hashLog.emplace(path, std::ios::binary);
        }

        if (const char* spec = std::getenv("RWE_SIM_LAG"))
        {
            if (auto lag = parseSimLag(spec))
            {
                simLag = lag;
                LOG_INFO << "RWE_SIM_LAG: sleeping " << lag->delay.count() << " ms after every tick from tick " << lag->fromTick;
            }
            else
            {
                LOG_WARN << "RWE_SIM_LAG: ignoring unreadable spec \"" << spec << "\"";
            }
        }

        if (const char* at = std::getenv("RWE_DESYNC_AT"))
        {
            if (auto tick = std::strtoul(at, nullptr, 10); tick > 0)
            {
                desyncFromTick = static_cast<unsigned int>(tick);
                LOG_WARN << "RWE_DESYNC_AT: this peer will report a wrong sync hash from tick " << tick << " onwards";
            }
        }
    }

    bool SimDiagnostics::hashLogEnabled() const
    {
        return hashLog.has_value();
    }

    std::optional<std::chrono::milliseconds> SimDiagnostics::simulationLagForTick(unsigned int tick) const
    {
        if (!simLag || tick < simLag->fromTick)
        {
            return std::nullopt;
        }
        return simLag->delay;
    }

    GameHash SimDiagnostics::record(const GameSimulation& simulation, unsigned int tick)
    {
        GameHash gameHash{0};
        {
            RWE_RENDERPROF("u.hash");
            gameHash = simulation.computeHash();
        }
        // Before the log, so that the log records what this peer actually told
        // its peers rather than what it privately knew.
        if (desyncFromTick && tick >= *desyncFromTick)
        {
            gameHash = GameHash(gameHash.value ^ 0x5eed5eedu);
        }
        if (hashLog)
        {
            *hashLog << tick << ' ' << gameHash.value << std::endl;
        }
        if (const char* spec = std::getenv("RWE_STATE_DUMP"))
        {
            unsigned int first = 0;
            unsigned int last = 0;
            char prefix[512] = {0};
            if (std::sscanf(spec, "%u:%u:%511s", &first, &last, prefix) == 3 && tick >= first && tick <= last
                && (tick - first) % static_cast<unsigned int>(std::max(1, std::atoi(std::getenv("RWE_STATE_DUMP_STEP") ? std::getenv("RWE_STATE_DUMP_STEP") : "1"))) == 0)
            {
                std::ofstream out(std::string(prefix) + std::to_string(tick) + ".json", std::ios::binary);
                out << saveSimulationToJson(simulation).dump(1);
            }
        }
        return gameHash;
    }
}
