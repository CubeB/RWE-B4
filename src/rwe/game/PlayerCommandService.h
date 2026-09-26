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
#include <rwe/sim/SimTicksPerSecond.h>
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
        return static_cast<unsigned int>(highCommandLatencyMillis / static_cast<float>(SimMillisecondsPerTick)) + 1;
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
         * restarts at zero there while the simulation's own clock does not.
         *
         * A peer that rejoins a game in progress does break that, which is
         * what hashSourceFromTick is for: it submits its first hash for the
         * tick its stream reopened at, not for tick 1, and until this counter
         * reaches that tick it is left out of the comparison rather than
         * waited for.
         */
        SceneTime nextHashTick{1};

        /**
         * The tick each source's first hash belongs to: 1 for everyone who
         * started the game, and the rejoin tick for anyone who came back.
         *
         * A source is compared only from its own first tick. Before that it is
         * neither compared nor waited on -- waiting would stall the comparison
         * for everybody until the rejoin, and comparing would set one peer's
         * hash for tick 9000 beside another's for tick 1.
         */
        std::unordered_map<PlayerId, SceneTime> hashSourceFromTick;

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

        /**
         * How many rounds have been popped, which is how many ticks the
         * simulation has consumed commands for: one round is one tick, for
         * every player at once.
         *
         * Only a rejoin needs this, and it needs it because pushedCount alone
         * cannot say how much of a buffer is still ahead of the game. For a
         * player who is present the two are tied -- a buffer holds pushed minus
         * popped sets -- but a dropped player's ticks are consumed without
         * popping anything, so that tie is exactly what a drop breaks. Padding
         * a reopened stream by push count alone would hand the ticks it missed
         * to the ticks still to come.
         */
        unsigned int poppedRounds{0};

        /** Players whose stream has been cut, and the tick it was cut at. */
        std::unordered_map<PlayerId, unsigned int> droppedFromTick;

        bool isDroppedLocked(PlayerId player) const;
        std::optional<PlayerId> droppingPlayerForLocked(PlayerId dropped) const;
        void dropPlayerLocked(PlayerId player, unsigned int fromTick);
        void rejoinPlayerLocked(PlayerId player, unsigned int fromTick);

    public:
        /**
         * How far past the tick the game has reached a drop or a rejoin may be
         * set: thirty seconds. Every peer chooses its tick as the furthest any
         * peer has reached plus a margin of a few seconds, so an honest one is
         * well inside this. The padding a drop or rejoin writes is a set per
         * tick up to the one it names, and a tick near 2^32 from the wire used
         * to mean four billion of them on every peer. Issue #75.
         */
        static constexpr unsigned int MaxDropLeadTicks = 900;

        /**
         * The next tick's commands, a set for every player, or nothing while
         * any player still owes one. The sets come out highest player first,
         * whatever order the players were registered in; see the body for why
         * that order and why it has to be fixed.
         */
        std::optional<std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>> tryPopCommands();

        void pushCommands(PlayerId player, const std::vector<PlayerCommand>& commands);

        void pushHash(PlayerId player, const GameHash& gameHash);

        unsigned int bufferedCommandCount(PlayerId player) const;

        /** Hashes from this player waiting to be compared; 0 for one that is no hash source. */
        unsigned int bufferedHashCount(PlayerId player) const;

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
        void registerHashSource(PlayerId playerId, SceneTime fromTick = SceneTime(1));

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

        /**
         * Reopens a dropped player's stream at `fromTick`: the inverse of
         * dropPlayer, and the other half of issue #188.
         *
         * Everything below `fromTick` stays empty, which is what the rest of
         * the game simulated while they were away, and from `fromTick` on the
         * tick needs their commands again -- so the game stalls there until
         * they arrive, exactly as it would for any peer that is behind.
         *
         * They become a hash source again, from `fromTick` rather than from
         * tick 1: they have no hashes for the ticks they missed and the others
         * have long since compared and discarded theirs.
         *
         * Idempotent, and ignored for a player who is not dropped, for the same
         * reason dropPlayer is: it arrives once over the wire and again when
         * the command carrying it is popped.
         */
        void rejoinPlayer(PlayerId player, unsigned int fromTick);

        /** Whether this player's stream has been cut. */
        bool isDropped(PlayerId player) const;

        /**
         * Whether this player still owes the simulation their set for tick
         * `tick`, counting the sets from one as the rest of this class does.
         *
         * It is what a recording has to ask before supplying one, because a
         * recording supplies a set per player per tick unconditionally and two
         * things make that wrong. A dropped player's stream is closed, and
         * everything pushed to it is discarded. And a rejoin fills in every
         * tick below the one it reopens at, so a recording replayed across
         * that point would supply those ticks a second time -- which does not
         * lose them, it *delays* them: the buffer runs that many sets long and
         * every later tick of that player's game is popped that many ticks
         * late. On a peer winding itself forward into a game in progress that
         * surplus is the margin the rejoin was agreed with, some hundred ticks
         * of its own commands standing between it and the game, and it sends
         * nothing at all until they drain. See pushReplayCommandsForTick.
         */
        bool needsCommandsForTick(PlayerId player, unsigned int tick) const;

        /**
         * Puts every stream back to the start of tick `tick`: nothing buffered,
         * every player having supplied exactly that many sets, and every drop
         * that happens at or after it undone.
         *
         * Only a recording does this, and only when the viewer jumps backwards
         * to a keyframe. The scene time moves and the streams have to move with
         * it, because the recording is about to supply the same ticks again --
         * and the drop the viewer has just wound back past has not happened
         * where it now stands, its command being ahead of it in the file.
         */
        void rewindTo(unsigned int tick);

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
