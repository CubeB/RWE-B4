#include "PlayerCommandService.h"
#include <algorithm>

namespace rwe
{
    std::optional<std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>> PlayerCommandService::tryPopCommands()
    {
        std::scoped_lock<std::mutex> lock(mutex);

        for (const auto& p : commandBuffers)
        {
            if (p.second.empty())
            {
                return std::nullopt;
            }
        }

        std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>> out;
        for (auto& p : commandBuffers)
        {
            out.emplace_back(p.first, p.second.front());
            p.second.pop_front();
        }

        return out;
    }

    void PlayerCommandService::pushCommands(PlayerId player, const std::vector<PlayerCommand>& commands)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        commandBuffers.at(player).push_back(commands);
    }

    void PlayerCommandService::pushHash(PlayerId player, const GameHash& gameHash)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        gameTimeBuffers.at(player).push_back(gameHash);
    }

    void PlayerCommandService::registerPlayer(PlayerId playerId)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        auto result = commandBuffers.emplace(playerId, std::deque<std::vector<PlayerCommand>>());
        if (!result.second)
        {
            throw std::logic_error("Player already registered");
        }
    }

    void PlayerCommandService::registerHashSource(PlayerId playerId)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        auto result = gameTimeBuffers.emplace(playerId, std::deque<GameHash>());
        if (!result.second)
        {
            throw std::logic_error("Hash source already registered");
        }
    }

    unsigned int PlayerCommandService::bufferedCommandCount(PlayerId player) const
    {
        std::scoped_lock<std::mutex> lock(mutex);

        return commandBuffers.at(player).size();
    }

    std::optional<DesyncReport> PlayerCommandService::checkHashes()
    {
        std::scoped_lock<std::mutex> lock(mutex);

        if (desync)
        {
            return desync;
        }

        while (!std::any_of(gameTimeBuffers.begin(), gameTimeBuffers.end(), [](const auto& p) { return p.second.empty(); }))
        {
            std::vector<std::pair<PlayerId, GameHash>> round;
            round.reserve(gameTimeBuffers.size());
            for (auto& p : gameTimeBuffers)
            {
                round.emplace_back(p.first, p.second.front());
                p.second.pop_front();
            }

            // The buffers live in a hash table, so a round comes out in
            // whatever order the table happens to hold. A report is read by a
            // person and set beside another peer's, so put the players in
            // their own order before either happens.
            std::sort(round.begin(), round.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            auto matching = std::all_of(round.begin(), round.end(), [&](const auto& p) { return p.second == round.front().second; });

            if (!matching)
            {
                desync = DesyncReport{nextHashTick, std::move(round)};
                return desync;
            }

            nextHashTick += SceneTime(1);
        }

        return std::nullopt;
    }
}
