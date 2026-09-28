#pragma once

#include <rwe/AudioService.h>

namespace rwe
{
    struct InGameSoundsInfo
    {
        std::optional<AudioService::SoundHandle> immediateOrders;
        std::optional<AudioService::SoundHandle> specialOrders;
        std::optional<AudioService::SoundHandle> setFireOrders;
        std::optional<AudioService::SoundHandle> setMoveOrders;

        std::optional<AudioService::SoundHandle> nextBuildMenu;

        std::optional<AudioService::SoundHandle> buildButton;
        std::optional<AudioService::SoundHandle> ordersButton;

        std::optional<AudioService::SoundHandle> addBuild;
        std::optional<AudioService::SoundHandle> okToBuild;
        std::optional<AudioService::SoundHandle> notOkToBuild;

        std::optional<AudioService::SoundHandle> selectMultipleUnits;

        /** The two the side panel's slide plays at its endpoints (76). */
        std::optional<AudioService::SoundHandle> panel;
        std::optional<AudioService::SoundHandle> options;

        /**
         * The click UiFactory::deduceButtonSound falls back to for a small
         * button carrying no art of its own. RWE's own gadgets have no gui
         * entry and so nothing to deduce from, and this is what the fallback
         * would have given them.
         */
        std::optional<AudioService::SoundHandle> smallButton;

        /** ALLSOUND's "Victory Condition" (VICTORY2), once for each mission objective met. */
        std::optional<AudioService::SoundHandle> victoryCondition;
    };
}
