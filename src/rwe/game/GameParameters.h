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
        std::optional<int> teamId;
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
        std::string localNetworkPort{"1337"};
        std::optional<std::string> stateLogFile;
        /** Tuning profile given to every computer player in this game. */
        AiDifficulty aiDifficulty{AiDifficulty::Standard};

        /** Set when this game is a saved game being resumed rather than a fresh start. */
        std::optional<std::string> loadFromSaveFile;

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
