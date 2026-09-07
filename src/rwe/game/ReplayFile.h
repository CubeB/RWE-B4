#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <rwe/game/GameParameters.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * Everything needed to stand the game back up before the first recorded
     * command lands. The simulation is lockstep, so a game is completely
     * determined by the conditions it started under and the commands issued
     * into it: this is the first half, and the record stream is the second.
     *
     * It carries the same lobby fields as SaveFile's header, plus the two
     * that a replay cannot do without. The seed, because a game started with
     * one and replayed without it deals different start positions and diverges
     * on the first tick. And teamId, which SaveFile's header drops -- it gets
     * away with that because a save also restores the simulation's own player
     * table, where the alliances live; a replay has no such second copy, so a
     * team lost here is a team lost.
     */
    struct ReplayHeader
    {
        std::string mapName;
        unsigned int schemaIndex{0};
        std::optional<unsigned int> randomSeed;
        AiDifficulty aiDifficulty{AiDifficulty::Standard};
        LineOfSightMode lineOfSight{LineOfSightMode::True};
        MappingMode mapping{MappingMode::Unmapped};
        StartLocationMode startLocation{StartLocationMode::Fixed};
        CommanderDeathMode commanderDeath{CommanderDeathMode::GameEnds};

        /**
         * Slot for slot as the lobby dealt them, empty slots included: a
         * slot's index is the PlayerId that the recorded commands name, so
         * closing up the gaps would misattribute every one of them.
         */
        std::vector<std::optional<PlayerInfo>> players;
    };

    ReplayHeader replayHeaderFromParameters(const GameParameters& parameters);

    GameParameters gameParametersFromReplayHeader(const ReplayHeader& header);

    /**
     * Appends to the replay as the game is played.
     *
     * Every record is flushed as it is written, because a replay is most
     * wanted for the game that ended in a crash: buffering a few dozen
     * commands would lose exactly the last few seconds worth watching. The
     * traffic is a few dozen records over ten minutes, so the flush costs
     * nothing anyone can measure.
     */
    class ReplayWriter
    {
    public:
        /** Writes the magic and header immediately. Throws if the file will not open. */
        ReplayWriter(const std::filesystem::path& path, const ReplayHeader& header);
        ~ReplayWriter();

        ReplayWriter(const ReplayWriter&) = delete;
        ReplayWriter& operator=(const ReplayWriter&) = delete;

        /**
         * A tick with no commands in it writes nothing; most ticks are that.
         * The tick number is still remembered, so that closing can say how
         * long the game ran -- a scrub bar that stopped at the last command
         * would have run out well before the game did.
         */
        void recordTick(unsigned int tick, PlayerId player, const std::vector<PlayerCommand>& commands);

        void close();

    private:
        std::ofstream out;
        unsigned int lastTickSeen{0};
    };

    struct Replay
    {
        ReplayHeader header;

        /** Commands by tick, then by player. Ticks with nothing in them are absent. */
        std::map<unsigned int, std::map<unsigned int, std::vector<PlayerCommand>>> commands;

        /**
         * The tick of the last record read whole. On a file truncated by a
         * crash this is where the recording actually stops, which is not the
         * tick the game reached.
         */
        unsigned int lastTick{0};
    };

    /**
     * Nothing for a file that is missing, or that is not a replay. A file that
     * is a replay but stops in the middle of a record yields everything up to
     * the truncation rather than nothing: that case is the normal ending for a
     * game that crashed, not a corruption to report.
     */
    std::optional<Replay> readReplayFile(const std::filesystem::path& path);

    /**
     * Where replays are kept when no path is given: <local data path>/Replays,
     * created on demand.
     *
     * Nothing rather than SaveFile's throw for a machine with no data path.
     * The front end asks this to decide whether to offer a replay menu at all,
     * and on such a machine the answer is that there are no replays, which is
     * not a failure worth an exception.
     */
    std::optional<std::filesystem::path> replaysDirectory();

    /**
     * A bare name becomes a file in replaysDirectory(); anything already
     * carrying a separator or an extension is a path the caller picked out and
     * is left exactly as given.
     */
    std::filesystem::path replayPathForName(const std::string& name);

    /** Enough to put a replay in a list and let someone choose one. */
    struct ReplaySummary
    {
        std::filesystem::path path;
        std::string mapName;

        /** The filled slots' sides, in slot order: "ARM v CORE". */
        std::string players;

        /** Length of the recording in seconds. */
        unsigned int seconds{0};

        /** Last write time, so the list can be newest first. */
        std::filesystem::file_time_type modified{};
    };

    /**
     * Every readable replay in replaysDirectory(), newest first.
     *
     * A directory that was never made, a file that will not open and a file
     * that is not a replay are all ordinary: the directory holds whatever
     * anyone drops in it. Each is skipped, and none of them throws.
     */
    std::vector<ReplaySummary> listReplays();

    /**
     * The same for a directory named outright, which is what a viewer pointed
     * at one wants. The other form reads the machine's own data path, which a
     * test has no way to move.
     */
    std::vector<ReplaySummary> listReplays(const std::filesystem::path& directory);

    /** Nothing for a file readReplayFile will not read. */
    std::optional<ReplaySummary> summariseReplay(const std::filesystem::path& path);
}
