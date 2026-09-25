#pragma once

#include <array>
#include <optional>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rwe
{
    struct PlayerControllerTypeHuman
    {
    };
    struct PlayerControllerTypeComputer
    {
    };
    struct PlayerControllerTypeNetwork
    {
        std::string host;
        std::string port;
    };

    using PlayerControllerType = std::variant<PlayerControllerTypeHuman, PlayerControllerTypeComputer, PlayerControllerTypeNetwork>;

    class IsHumanVisitor
    {
    public:
        bool operator()(const PlayerControllerTypeHuman&) const { return true; }
        bool operator()(const PlayerControllerTypeComputer&) const { return false; }
        bool operator()(const PlayerControllerTypeNetwork&) const { return false; }
    };

    class IsComputerVisitor
    {
    public:
        bool operator()(const PlayerControllerTypeHuman&) const { return false; }
        bool operator()(const PlayerControllerTypeComputer&) const { return true; }
        bool operator()(const PlayerControllerTypeNetwork&) const { return false; }
    };

    class GetNetworkAddressVisitor
    {
    public:
        std::optional<std::pair<std::reference_wrapper<const std::string>, std::reference_wrapper<const std::string>>> operator()(const PlayerControllerTypeHuman&) const { return std::nullopt; }
        std::optional<std::pair<std::reference_wrapper<const std::string>, std::reference_wrapper<const std::string>>> operator()(const PlayerControllerTypeComputer&) const { return std::nullopt; }
        std::optional<std::pair<std::reference_wrapper<const std::string>, std::reference_wrapper<const std::string>>> operator()(const PlayerControllerTypeNetwork& p) const { return std::make_pair(std::cref(p.host), std::cref(p.port)); }
    };

    struct PlayerInfo
    {
        std::optional<std::string> name;
        PlayerControllerType controller;
        std::string side;
        PlayerColorIndex color;

        Metal metal;
        Energy energy;

        /**
         * The lobby's team letter, or nothing for a player who fights alone.
         * Players sharing a team share sight and radar.
         */
        std::optional<int> teamId{};

        /**
         * The AI personality a computer player plays, by name (see
         * AiPersonality.h), or nothing for its difficulty's profile as tuned.
         * Read from the built-ins and the local `ai` folder when the game
         * loads, so a peer whose folder differs would play it differently:
         * the launcher does not send one, and a network game has none.
         */
        std::optional<std::string> aiPersonality{};
    };

    /** SKIRMISH.GUI's Line of Sight button. */
    enum class LineOfSightMode
    {
        /** Everything the map has shown stays visible; nothing fades back to memory. */
        Permanent = 0,
        /** Ground gets in the way: a hill blocks what is behind it. */
        True = 1,
        /** A plain radius, blind to elevation. */
        Circular = 2,
    };

    /** SKIRMISH.GUI's Mapping button. */
    enum class MappingMode
    {
        /** The map starts black and is uncovered by going to look. */
        Unmapped = 0,
        /** The ground is known from the first frame; units still have to be seen. */
        Mapped = 1,
    };

    /** SKIRMISH.GUI's Start Location button. */
    enum class StartLocationMode
    {
        /** Player n takes the map's StartPos n. */
        Fixed = 0,
        /** The start positions are dealt out at random. */
        Random = 1,
    };

    /** SKIRMISH.GUI's Commander button. */
    enum class CommanderDeathMode
    {
        /** Losing the commander loses the game. */
        GameEnds = 0,
        /** The commander is just another unit; the game runs on. */
        GameContinues = 1,
    };

    struct GameParameters
    {
        std::string mapName;
        unsigned int schemaIndex;
        std::array<std::optional<PlayerInfo>, 10> players;

        /**
         * Play the map as a mission (--mission): the schema's [units] for the
         * players seated in its slots, `Player` N in slot N-1, instead of a
         * commander at each start position, and the schema's HumanMetal /
         * ComputerMetal (and the energy pair) as the starting resources. See
         * spawnMissionUnits and issue #293.
         */
        bool mission{false};
        std::string localNetworkPort{"1337"};
        std::optional<std::string> stateLogFile;
        /** Tuning profile given to every computer player in this game. */
        AiDifficulty aiDifficulty{AiDifficulty::Standard};

        /**
         * Single AI knobs overridden for one player, each as
         * "<player>:<knob>=<value>", from --ai-tune. For the arena: a
         * behaviour change cannot be judged in a mirror match, so this is
         * how one setting is played against the other in the same game.
         */
        std::vector<std::string> aiTuning;

        /** Set when this game is a saved game being resumed rather than a fresh start. */
        std::optional<std::string> loadFromSaveFile;

        /**
         * Seconds of game time to run before writing an AI report and
         * quitting, when this is a computer-versus-computer measurement run
         * rather than a game anyone is watching. See AiArenaReport.
         */
        std::optional<unsigned int> aiArenaSeconds;

        /** Write every command to this replay file as the game is played. */
        std::optional<std::string> recordReplayFile;

        /**
         * Write a TA demo of this game to this file as it is played. A demo
         * is a stream of state and effects, readable by tools that have never
         * heard of RWE, where a replay is RWE's own command stream; both can
         * be recorded at once.
         */
        std::optional<std::string> recordDemoFile;

        /**
         * Watch this replay instead of playing. The parameters around it are
         * rebuilt from the replay's own header, so the simulation stands up
         * exactly as it did when the game was recorded; this field is what
         * tells the loader to idle the computer players and feed the recorded
         * commands instead of live ones.
         */
        std::optional<std::string> replayFile;

        /**
         * With replayFile: keep the computer players thinking instead of
         * idling them, so their decisions reach the event log. Whoever runs
         * the replay must then take and discard their commands every tick,
         * because the recording already carries the ones that were played.
         * Harmless to the game: the AI reads the simulation through a const
         * reference and draws only from its own generator, seeded at
         * construction either way.
         */
        bool replayShadowAi{false};

        /**
         * Start a replay already wound forward to here. Seeking backwards
         * means starting the simulation again, because a lockstep game can
         * only be run forwards, and this is how the viewer asks for that.
         */
        unsigned int replaySeekToTick{0};

        /**
         * Rejoining a game already in progress: every command the game has
         * consumed so far, as a replay, and the tick this peer's own stream
         * reopens at. Issue #188.
         *
         * The recording runs through rejoinAtTick-1 and no further, which is
         * the whole of the arrangement. A lockstep game can only be caught up
         * with by being replayed, so the peers that stayed stall at that tick
         * -- they need this player's commands from it -- and everything, in
         * both directions and in both streams, resumes there. One tick rather
         * than two is what keeps a returning peer from having to be handed a
         * gap it could not have filled.
         */
        std::optional<std::string> rejoinFromReplayFile;
        unsigned int rejoinAtTick{0};

        /**
         * Mixed into the simulation's seed when set. Without it the seed
         * comes from the map and the players alone, so two arena runs of the
         * same match-up are the same game -- fine for reproducing one, no use
         * for averaging over twenty.
         */
        std::optional<unsigned int> randomSeed;

        /** The skirmish screen's own options, as chosen in the lobby. */
        LineOfSightMode lineOfSight{LineOfSightMode::True};
        MappingMode mapping{MappingMode::Unmapped};
        StartLocationMode startLocation{StartLocationMode::Fixed};
        CommanderDeathMode commanderDeath{CommanderDeathMode::GameEnds};

        /**
         * The battle test: instead of a commander each, every player is kept
         * topped up to this many units, which walk at the enemy's start
         * position and are replaced as they die. Nothing about it belongs in
         * a real game -- it exists so a fight can be watched for as long as
         * it takes to see what breaks.
         */
        std::optional<unsigned int> battleTestUnitsPerSide;

        /**
         * What the battle test spawns, one per player slot. Hover tanks by
         * default, since they can cross water and most of the stock maps
         * have some: a ground unit on a sea map never reaches the enemy and
         * the fight never happens.
         */
        std::vector<std::string> battleTestUnitTypes{"ARMAH", "CORAH"};

        GameParameters(const std::string& mapName, unsigned int schemaIndex);
    };
}
