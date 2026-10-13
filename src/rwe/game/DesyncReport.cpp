#include "DesyncReport.h"

#include <fstream>
#include <iomanip>
#include <rwe/BuildIdentity.h>
#include <rwe/config.h>
#include <rwe/game/dump_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util.h>
#include <rwe/util/SimpleLogger.h>
#include <sstream>

namespace rwe
{
    namespace
    {
        /**
         * Hashes are compared by eye across two machines' reports more often
         * than they are read as numbers, so they are written the width they
         * are: eight hex digits, zero padded, never shortened.
         */
        std::string formatHash(GameHash hash)
        {
            std::ostringstream s;
            s << "0x" << std::hex << std::setw(8) << std::setfill('0') << hash.value;
            return s.str();
        }
    }

    std::string describeDesync(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const std::optional<std::filesystem::path>& dumpPath)
    {
        std::ostringstream s;
        s << "Desync detected.\n\n";
        s << "The simulation diverged from the other players at tick " << report.tick.value << ".\n";
        s << "This peer is player " << localPlayer.value << ", and noticed at tick " << detectedAt.value << ".\n\n";

        for (const auto& [playerId, hash] : report.hashes)
        {
            s << "  player " << playerId.value << "  " << formatHash(hash);
            if (playerId == localPlayer)
            {
                s << "  (this peer)";
            }
            s << "\n";
        }

        s << "\n";

        if (dumpPath)
        {
            s << "A state dump for a bug report was written to:\n";
            s << "  " << dumpPath->string() << "\n\n";
            s << "Every peer writes one naming the same tick. A report wants them all.\n";
        }
        else
        {
            s << "A state dump could not be written; see the log.\n";
        }

        return s.str();
    }

    std::filesystem::path desyncDumpPath(
        const std::filesystem::path& directory,
        const DesyncReport& report,
        PlayerId localPlayer)
    {
        auto path = directory;
        path /= "rwe-desync-tick" + std::to_string(report.tick.value)
            + "-player" + std::to_string(localPlayer.value) + ".json";
        return path;
    }

    nlohmann::json desyncDumpJson(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const GameSimulation& simulation)
    {
        nlohmann::json hashes;
        for (const auto& [playerId, hash] : report.hashes)
        {
            hashes.push_back(nlohmann::json{
                {"player", playerId.value},
                {"hash", hash.value},
            });
        }

        return nlohmann::json{
            {"desync",
                nlohmann::json{
                    {"firstDivergentTick", report.tick.value},
                    {"localPlayer", localPlayer.value},
                    {"detectedAtTick", detectedAt.value},
                    {"hashes", hashes},
                    {"build", ProjectNameVersion},
                    {"commit", GitHash},
                    // "build" and "commit" are what CMake saw when the tree was
                    // last configured, and read as an older commit for as long
                    // as nobody re-ran it -- a report from 2026-10-13 named
                    // 950e38c7 for an executable that held 81db7443's code.
                    // This is what the lobby compares: regenerated at every
                    // build, so it is the commit the executable was made from.
                    {"buildIdentity", buildIdentity()},
                }},
            {"state", dumpJson(simulation)},
        };
    }

    std::optional<std::filesystem::path> writeDesyncDump(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const GameSimulation& simulation)
    {
        // Beside the log, or the working directory if there is no local data
        // path. The old name was rwe-dump-<rand()>.json in whatever directory
        // the game happened to start in, which made two peers' dumps a pair
        // only by whoever collected them remembering which was which.
        auto localDataPath = getLocalDataPath();
        auto path = desyncDumpPath(localDataPath ? *localDataPath : std::filesystem::path("."), report, localPlayer);

        try
        {
            std::ofstream out(path, std::ios::binary);
            out << desyncDumpJson(report, localPlayer, detectedAt, simulation).dump(1);
            if (!out)
            {
                throw std::runtime_error("write failed");
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Desync: could not write the state dump to " << path.string() << ": " << e.what();
            return std::nullopt;
        }

        return path;
    }
}
