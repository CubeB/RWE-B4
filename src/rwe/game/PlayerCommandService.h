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

    /**
     * How deep a computer player's command buffer is kept: a constant, and the
     * same constant on every peer.
     *
     * A computer player's orders never cross the network -- every peer runs its
     * own copy of the AI over the same simulation and gets the same orders --
     * so there is no round trip to cover for, and taking the figure from one
     * would make the tick an AI order lands on a function of somebody's ping.
     * It is the no-peer depth, which is what a single-player game and the
     * headless arena have always used, so the delay between the AI deciding and
     * the simulation acting is exactly what it was.
     */
    inline unsigned int aiCommandBufferDepth()
    {
        return commandBufferTargetForRttMillis(0.0f);
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

        /**
         * How many sets have ever been pushed for each player, which is the
         * tick the next one will be consumed at: the Nth set a player sends is
         * the one the simulation runs on tick N.
         *
         * Only a drop needs this. Cutting a lost peer's stream at an agreed
         * tick means knowing how far along the stream already is, and the
         * buffer alone cannot say, having had most of it taken out of it.
         */
        std::unordered_map<PlayerId, unsigned int> pushedCount;

        /** Players whose stream has been cut, and the tick it was cut at. */
        std::unordered_map<PlayerId, unsigned int> droppedFromTick;

        bool isDroppedLocked(PlayerId player) const;
        std::optional<PlayerId> droppingPlayerForLocked(PlayerId dropped) const;
        void dropPlayerLocked(PlayerId player, unsigned int fromTick);

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

        /**
         * The players the next tick is waiting on: those with nothing buffered
         * who have not been dropped. Empty when the tick can go ahead.
         *
         * This is what the waiting overlay names. It is ordinarily empty, and
         * ordinarily non-empty for a frame or two when a peer's packet is late,
         * which is why the overlay waits a moment before believing it.
         */
        std::vector<PlayerId> playersNotReady() const;

        /**
         * Cuts a player's command stream at `fromTick` and carries on without
         * them: everything they sent from that tick on is discarded, everything
         * missing below it is filled in with empty sets, and every tick after it
         * takes an empty set from them.
         *
         * The cut is what makes this safe to apply whenever it arrives. Two
         * peers may hold different amounts of a lost peer's stream -- a packet
         * that reached one and not the other -- and forcing both to the same
         * length at the same tick is what stops that becoming a desync. It also
         * makes the call idempotent: a second drop for the same player is
         * ignored, so it may be applied on arrival and again when it is popped.
         *
         * They stop being a hash source too: a peer that is not answering is
         * not submitting sync hashes either, and a hash buffer nobody fills
         * stalls the comparison for everybody.
         */
        void dropPlayer(PlayerId player, unsigned int fromTick);

        /** Whether this player's stream has been cut. */
        bool isDropped(PlayerId player) const;

        /**
         * Which player is the one to declare `dropped` lost: the lowest-numbered
         * peer that is neither the player in question nor already dropped.
         *
         * Exactly one peer may issue a drop, or two drops naming different ticks
         * would cut the stream in two different places. The rule is computed
         * rather than agreed, from state every peer already shares -- who the
         * peers are, and who has been dropped -- so every peer works out the
         * same answer without a message being sent. It is the host in the
         * ordinary case, player 0 being the seat the lobby gives them; the point
         * of the rest of it is that the host dropping is the likeliest failure
         * of all, and somebody has to be able to say so.
         *
         * Nothing if this player is the only peer left, there being nobody to
         * declare anything.
         */
        std::optional<PlayerId> droppingPlayerFor(PlayerId dropped) const;
    };
}
