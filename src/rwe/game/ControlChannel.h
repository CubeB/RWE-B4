#pragma once

#include <string>
#include <vector>

namespace rwe
{
    /**
     * Something whoever launched this game has asked it to do.
     *
     * A command and a player, because that is the whole of what is asked so
     * far. A struct rather than a variant on purpose: this header is included
     * by GameScene, and a variant declared here would be instantiated in every
     * translation unit that reaches it. See CLAUDE.md on the section budget.
     */
    struct ControlRequest
    {
        /** "rejoin", and nothing else yet. */
        std::string command;

        unsigned int player{0};
    };

    /**
     * The launcher's bridge, to a game that is already running.
     *
     * `rwe_bridge` answers questions about the data files before a game starts;
     * this is the same dialect -- one JSON object a line, commands in on
     * standard input and events out on standard output -- for the things only a
     * game in progress can answer. It exists for the rejoin: a peer that has
     * been dropped can only be let back in by the peer entitled to say so,
     * which is in the middle of a game, and the recording it hands over can
     * only be cut by the peer holding it.
     *
     * It is a side channel and never a simulation input. A request arriving
     * here is exactly like a key being pressed: it is turned into an ordinary
     * PlayerCommand on the tick the local player next submits one, and every
     * peer sees it in the same place in the same stream. Nothing read here is
     * hashed, saved or replayed.
     *
     * Off unless --bridge was given, in which case every method below is a
     * cheap no-op; the game is ordinarily started by a person and not by a
     * program, and a program that did not ask to be spoken to should not have
     * JSON appearing in its console.
     */
    class ControlChannel
    {
    public:
        /**
         * Starts reading standard input on a thread of its own.
         *
         * A thread because the read blocks and the game has frames to draw,
         * and a detached one because there is no way to interrupt a blocking
         * read to join it -- see getControlChannel for what that costs.
         */
        void start();

        bool enabled() const { return on; }

        /** Everything that has arrived since the last call. */
        std::vector<ControlRequest> take();

        /** A peer has been dropped, and could be asked back. */
        void sendPlayerDropped(unsigned int player, unsigned int fromTick);

        /**
         * The recording a returning peer needs is on disk at this path, and
         * covers everything below `atTick`. Carrying it is the launcher's
         * half: it holds a reliable connection to the game server and this
         * does not.
         */
        void sendRejoinBundle(unsigned int player, unsigned int atTick, const std::string& file);

        /** A rejoin was asked for and will not happen, with the reason why. */
        void sendRejoinRefused(unsigned int player, const std::string& reason);

    private:
        bool on{false};
    };

    /**
     * The one channel, standard input being one thing.
     *
     * Deliberately never destroyed: the reader thread is detached and blocked
     * in a read, so it can wake after main has returned, and a channel
     * destroyed at exit would be written into after it had gone. One leak the
     * size of a mutex, against a crash at shutdown.
     */
    ControlChannel& getControlChannel();
}
