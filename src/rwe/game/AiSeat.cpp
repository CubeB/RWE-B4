#include "AiSeat.h"

#include <cstdint>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/ai/classic/ClassicAiController.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <stdexcept>
#include <utility>

namespace rwe
{
    AiEngine chooseAiEngine(const std::optional<AiPersonality>& personality, std::optional<AiEngine> savedEngine, AiEngine defaultEngine)
    {
        if (personality)
        {
            return personality->engine;
        }
        if (savedEngine)
        {
            return *savedEngine;
        }
        return defaultEngine;
    }

    ClassicAiProfileFile readClassicAiProfile(const AbstractVirtualFileSystem& vfs, const std::string& profileName)
    {
        std::vector<std::string> candidates;
        if (!profileName.empty())
        {
            candidates.push_back("ai/" + profileName + ".txt");
        }
        candidates.push_back("ai/default.txt");

        for (const auto& path : candidates)
        {
            if (auto bytes = vfs.readFile(path))
            {
                return ClassicAiProfileFile{std::string(bytes->begin(), bytes->end()), path};
            }
        }
        return ClassicAiProfileFile{};
    }

    namespace
    {
        /**
         * The --ai-tune entries that name this player, as knob and value.
         * A malformed entry is fatal wherever it is aimed, as it always was:
         * one that silently did nothing would make an arena comparison of two
         * identical AIs look like a result.
         */
        std::vector<std::pair<std::string, std::string>> tuningFor(const std::vector<std::string>& entries, PlayerId playerId)
        {
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto& entry : entries)
            {
                auto colon = entry.find(':');
                auto equals = entry.find('=');
                if (colon == std::string::npos || equals == std::string::npos || equals < colon)
                {
                    throw std::runtime_error("--ai-tune wants <player>:<knob>=<value>, got " + entry);
                }
                if (std::stoul(entry.substr(0, colon)) != static_cast<unsigned long>(playerId.value))
                {
                    continue;
                }
                out.emplace_back(entry.substr(colon + 1, equals - colon - 1), entry.substr(equals + 1));
            }
            return out;
        }
    }

    std::unique_ptr<AiController> createAiController(
        GameSimulation& simulation,
        const AiSeatRequest& request,
        const MapIntel& mapIntel,
        const AiBuildTree& buildTree)
    {
        const auto playerId = request.playerId;
        const auto i = playerId.value;
        const auto& personality = request.personality;
        const auto engine = chooseAiEngine(personality, request.savedEngine, request.defaultEngine);
        const auto tuning = tuningFor(request.tuning, playerId);

        // Every computer player in a game shares the difficulty chosen for
        // the game (--ai-difficulty, rwe.cfg or the skirmish menu), unless
        // its personality names one of its own.
        const auto difficulty = personality && personality->difficulty ? *personality->difficulty : request.gameDifficulty;

        if (engine == AiEngine::Classic)
        {
            // The original's computer player has no knobs: its behaviour is
            // its profile file and its difficulty, and nothing else. A knob
            // set on it would be silently ignored, which is the failure
            // --ai-tune errors on a misspelt knob to avoid.
            if (!tuning.empty())
            {
                throw std::runtime_error(
                    "--ai-tune: player " + std::to_string(i) + " plays the classic engine, which has no knobs (use --ai-engine rwe)");
            }

            ClassicAiConfig config;
            config.difficulty = difficulty;
            config.idle = request.idle;
            config.unitLimit = request.unitLimit;
            // Read-only and the same for every seat; a copy per seat keeps
            // the controller from outliving the loader's table.
            config.buildTree = std::make_shared<const AiBuildTree>(buildTree);
            if (request.vfs != nullptr)
            {
                auto file = readClassicAiProfile(*request.vfs, request.aiProfileName);
                if (file.path.empty())
                {
                    LOG_INFO << "Player " << i << ": no AI profile file (" << request.aiProfileName << " or default); the classic engine plays with its defaults";
                }
                else
                {
                    LOG_INFO << "Player " << i << ": classic AI profile read from " << file.path;
                }
                config.profileText = std::move(file.text);
            }
            if (personality)
            {
                LOG_INFO << "Player " << i << " plays the " << personality->name << " personality";
            }
            LOG_INFO << "Player " << i << " is a computer player at " << aiDifficultyName(difficulty) << " difficulty, classic engine";

            // One value from the sim RNG to seed the controller's own, here
            // and in the RWE branch below and nowhere else: see the header.
            const std::uint64_t aiSeed = static_cast<std::uint64_t>(simulation.rng());
            return std::make_unique<ClassicAiController>(playerId, std::move(config), aiSeed);
        }

        auto profile = makeProfileForDifficulty(difficulty);
        // What this player's faction plays differently, before its
        // personality and any --ai-tune, so that either can set a knob
        // back.
        applyFactionDefaults(profile, request.side);
        if (personality)
        {
            if (auto knob = applyAiPersonality(profile, *personality))
            {
                LOG_WARN << "Player " << i << ": the " << personality->name << " personality sets " << *knob << ", which is no AI knob";
            }
            LOG_INFO << "Player " << i << " plays the " << personality->name << " personality";
        }
        for (const auto& [knob, value] : tuning)
        {
            // A misspelt knob that silently did nothing would make an
            // arena comparison between two identical AIs look like a
            // result, so it is fatal.
            if (!applyAiTuning(profile, knob, value))
            {
                throw std::runtime_error("--ai-tune: no such AI knob: " + knob);
            }
            LOG_INFO << "Player " << i << " AI knob " << knob << " = " << value;
        }
        if (request.idle)
        {
            // Watching rather than playing: the commands come out of the
            // file, so a thinking AI would only add its own on top.
            //
            // The controller is still CONSTRUCTED, and that is the whole
            // point of idling it here rather than skipping it. Building
            // one draws a value from simulation.rng, and the start
            // positions are dealt from that same stream a few lines
            // further down -- skip the draw and every commander spawns
            // somewhere else, which is a divergence on the very first
            // tick and looks like anything except an off-by-one in the
            // random number generator.
            profile.idle = true;
        }
        LOG_INFO << "Player " << i << " is a computer player at " << aiDifficultyName(profile.difficulty) << " difficulty";

        // Pull a single value from the sim RNG to seed the AI's
        // sub-RNG. This keeps AI choices reproducible across clients
        // that share `simulation.rng`'s seed.
        const std::uint64_t aiSeed = static_cast<std::uint64_t>(simulation.rng());
        return std::make_unique<AiPlayerController>(playerId, std::move(profile), aiSeed, mapIntel, buildTree);
    }
}
