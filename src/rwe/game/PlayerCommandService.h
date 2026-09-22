#pragma once

#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>
#include <rwe/game/DesyncReport.h>
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

        /**
         * The tick the front of every hash buffer belongs to.
         *
         * A peer submits one sync hash a tick, starting with the state at the
         * end of tick 1, and the buffers are drained a tick at a time, so
         * counting the rounds drained is what turns "these two hashes differ"
         * into "tick 4,281 differs". Nothing on the wire carries the tick: the
         * hash stream is ordered and gapless by the same sequence numbers the
         * command stream uses, and this is the cheaper half of that bargain.
         *
         * It starts at 1 because every peer starts its scene at tick 0 and
         * submits nothing for it, loaded saved games included -- the scene time
         * restarts at zero there while the simulation's own clock does not. A
         * peer that rejoined a game in progress would break that, and would
         * have to be given the tick its first hash belongs to (issue #44).
         */
        SceneTime nextHashTick{1};

        /**
         * Kept once found, and handed back to every later caller.
         *
         * Comparing consumes the hashes, so a report that was not kept could
         * not be asked for twice -- and the tick it names is the first divergent
         * one, which stops being knowable the moment the evidence for it is
         * dropped. A desync is terminal anyway; this only makes the terminal
         * path say the same thing however many times it is walked.
         */
        std::optional<DesyncReport> desync;

    public:
        std::optional<std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>> tryPopCommands();

        void pushCommands(PlayerId player, const std::vector<PlayerCommand>& commands);

        void pushHash(PlayerId player, const GameHash& gameHash);

        unsigned int bufferedCommandCount(PlayerId player) const;

        /** Registers a player whose commands the simulation consumes: everyone in the game. */
        void registerPlayer(PlayerId playerId);

        /**
         * Registers a player who also reports a sync hash every tick: this peer
         * and the peers on the other end of the network, and nobody else.
         *
         * Kept apart from registerPlayer because a computer player has commands
         * and no hash of its own, and a hash buffer that is never fed stalls the
         * comparison for everybody -- which is how desync detection came to be
         * silently off in any game with an AI in it.
         */
        void registerHashSource(PlayerId playerId);

        /**
         * Compares as far through every peer's hash stream as it can, and
         * reports the first tick they disagreed on. Nothing while they agree,
         * and nothing while any peer's hashes have yet to arrive.
         */
        std::optional<DesyncReport> checkHashes();
    };
}
