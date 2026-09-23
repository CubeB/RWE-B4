#pragma once

#include <array>
#include <optional>
#include <rwe/events.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/observable/BehaviorSubject.h>
#include <rwe/render/Sprite.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <string>
#include <variant>

namespace rwe
{
    class MainMenuModel
    {
    public:
        struct SelectedMapInfo
        {
            std::string name;
            std::string description;
            std::string size;

            std::shared_ptr<Sprite> minimap;

            SelectedMapInfo(
                const std::string& name,
                const std::string& description,
                const std::string& size,
                const std::shared_ptr<Sprite>& minimap);

            bool operator==(const SelectedMapInfo& rhs) const;

            bool operator!=(const SelectedMapInfo& rhs) const;
        };

        struct PlayerSettings
        {
            enum class Type
            {
                Open,
                Human,
                Computer,
                /**
                 * Somebody on another machine, in a direct-connect game. Only
                 * the multiplayer screen offers it -- a skirmish has nobody to
                 * connect to.
                 */
                Network
            };
            enum class Side
            {
                Arm,
                Core
            };

            BehaviorSubject<Type> type;
            BehaviorSubject<Side> side;
            BehaviorSubject<PlayerColorIndex> colorIndex;
            BehaviorSubject<std::optional<int>> teamIndex;
            BehaviorSubject<Metal> metal;
            BehaviorSubject<Energy> energy;
            /** The AI personality a computer player plays, by name (see AiPersonality.h). */
            BehaviorSubject<std::string> personality{std::string("Balanced")};

            /**
             * Where a Network player is, as typed: host:port, and the same
             * forms the --player argument takes, so [::1]:15338 as well as
             * 192.168.0.5:1337. Kept on the model rather than read off the
             * text box alone, because changing a slot's type destroys and
             * rebuilds that box and what was typed should survive it.
             */
            BehaviorSubject<std::string> networkAddress{std::string()};
        };

    public:
        Subject<int> teamChanges;

        /**
         * The port this machine listens on in a direct-connect game, which is
         * what the other peers put in their address for this seat. The default
         * is the one --port takes.
         */
        BehaviorSubject<std::string> localNetworkPort{std::string("1337")};

        BehaviorSubject<std::optional<SelectedMapInfo>> selectedMap;

        BehaviorSubject<std::optional<SelectedMapInfo>> candidateSelectedMap;

        /**
         * The staged option buttons down the right of the skirmish screen.
         * Each value is the stage shown on the button, in the order the
         * stages are listed in SKIRMISH.GUI.
         */
        struct SkirmishOptions
        {
            /** Game ends | Continues */
            BehaviorSubject<unsigned int> commanderDeath{0u};
            /** Fixed | Random */
            BehaviorSubject<unsigned int> startLocation{0u};
            /** Unmapped | Mapped */
            BehaviorSubject<unsigned int> mapping{0u};
            /** Permanent | True | Circular */
            BehaviorSubject<unsigned int> lineOfSight{1u};
            /** Easy | Medium | Hard; decides the AI tuning profile. */
            BehaviorSubject<unsigned int> difficulty{1u};
        };

        SkirmishOptions skirmishOptions;

        std::array<PlayerSettings, 10> players{{
            {PlayerSettings::Type::Human, PlayerSettings::Side::Arm, PlayerColorIndex(0), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Computer, PlayerSettings::Side::Core, PlayerColorIndex(1), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Arm, PlayerColorIndex(2), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Core, PlayerColorIndex(3), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Arm, PlayerColorIndex(4), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Core, PlayerColorIndex(5), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Arm, PlayerColorIndex(6), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Core, PlayerColorIndex(7), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Arm, PlayerColorIndex(8), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
            {PlayerSettings::Type::Open, PlayerSettings::Side::Core, PlayerColorIndex(9), std::optional<int>(std::nullopt), Metal(1000), Energy(1000)},
        }};

        /**
         * Returns true when the team contains two or more players.
         */
        bool isTeamShared(int index) const;

        /**
         * Returns true if there is a player using the given color index.
         */
        bool isColorInUse(const PlayerColorIndex& colorIndex) const;

        /**
         * Returns the first available player color index.
         */
        std::optional<PlayerColorIndex> getFirstFreeColor() const;
    };
}
