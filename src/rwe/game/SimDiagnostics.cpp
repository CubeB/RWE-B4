#include "SimDiagnostics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <rwe/game/save_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    SimDiagnostics::SimDiagnostics()
    {
        if (const char* path = std::getenv("RWE_HASH_LOG"))
        {
            hashLog.emplace(path, std::ios::binary);
        }
    }

    bool SimDiagnostics::hashLogEnabled() const
    {
        return hashLog.has_value();
    }

    GameHash SimDiagnostics::record(const GameSimulation& simulation, unsigned int tick)
    {
        GameHash gameHash{0};
        {
            RWE_RENDERPROF("u.hash");
            gameHash = simulation.computeHash();
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
