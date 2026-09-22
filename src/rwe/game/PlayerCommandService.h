#pragma once

#include <algorithm>
#include <deque>
#include <mutex>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/SceneTime.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <unordered_map>
#include <vector>

namespace rwe
{
    /**
     * How deep every player's command buffer is kept, given the worst round
     * trip time among the peers. A command is held this many ticks before the
     * simulation sees it, so the depth shifts when orders land; the headless
     * arena has no peers and must use the same figure the game does or its
     * hashes diverge from a windowed run's on the first tick an order moves.
     */
    inline unsigned int commandBufferTargetForRttMillis(float maxAverageRttMillis)
    {
        auto maxRtt = std::clamp(maxAverageRttMillis, 16.0f, 2000.0f);
        auto highCommandLatencyMillis = maxRtt + (maxRtt / 4.0f) + 200.0f;
        return static_cast<unsigned int>(highCommandLatencyMillis / 16.0f) + 1;
    }

    class PlayerCommandService
    {
    private:
        mutable std::mutex mutex;
        std::unordered_map<PlayerId, std::deque<std::vector<PlayerCommand>>> commandBuffers;
        std::unordered_map<PlayerId, std::deque<GameHash>> gameTimeBuffers;

    public:
        std::optional<std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>> tryPopCommands();

        void pushCommands(PlayerId player, const std::vector<PlayerCommand>& commands);

        void pushHash(PlayerId player, const GameHash& gameHash);

        unsigned int bufferedCommandCount(PlayerId player) const;

        void registerPlayer(PlayerId playerId);

        bool checkHashes();
    };
}
