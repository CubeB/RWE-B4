#include "ReloadBar.h"

#include <algorithm>
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    std::optional<ReloadBarState> chooseReloadBarWeapon(
        const std::array<std::optional<UnitWeapon>, 3>& weapons,
        const std::unordered_map<std::string, WeaponDefinition>& weaponDefinitions,
        unsigned int health,
        unsigned int maxHealth,
        unsigned int kills,
        const VeterancyDefinition& veterancy)
    {
        std::optional<ReloadBarState> selected;
        GameTime selectedReload{0};

        for (std::size_t i = 0; i < weapons.size(); ++i)
        {
            const auto& weapon = weapons[i];
            if (!weapon)
            {
                continue;
            }

            const auto definition = weaponDefinitions.find(weapon->weaponType);
            if (definition == weaponDefinitions.end() || !definition->second.reloadBar)
            {
                continue;
            }

            auto reloadTicks = computeReloadTicks(definition->second.reloadTime, health, maxHealth, kills, veterancy);
            if (!selected || reloadTicks.value > selectedReload.value)
            {
                selected = ReloadBarState{
                    i,
                    weapon->readyTime,
                    GameTime(std::max(1u, reloadTicks.value))};
                selectedReload = reloadTicks;
            }
        }

        return selected;
    }

    float reloadBarProgress(const ReloadBarState& state, GameTime now)
    {
        if (now.value >= state.readyTime.value)
        {
            // Ready, or never fired since the unit was built.
            return 1.0f;
        }

        const auto reloadTicks = std::max(1u, state.reloadTicks.value);
        const auto remaining = state.readyTime.value - now.value;
        const auto elapsed = remaining >= reloadTicks ? 0u : reloadTicks - remaining;
        return std::clamp(
            static_cast<float>(elapsed) / static_cast<float>(reloadTicks),
            0.0f,
            1.0f);
    }
}
