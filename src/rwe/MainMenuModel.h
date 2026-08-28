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
                Computer
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
        };

    public:
        Subject<int> teamChanges;

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
