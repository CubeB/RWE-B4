#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/Veterancy.h>
#include <rwe/sim/WeaponDefinition.h>
#include <string>
#include <unordered_map>

namespace rwe
{
    /**
     * TADR `reloadbar` (#934): which weapon a unit's reload bar follows and
     * the numbers it is drawn from.
     *
     * The bar is presentation only. It reads the simulation's weapon state
     * and never writes it, so it adds nothing to the hash, the save or the
     * network.
     */
    struct ReloadBarState
    {
        /** The weapon slot the bar follows: 0, 1 or 2. */
        std::size_t weaponIndex{0};

        /** When the chosen weapon next becomes ready to fire. */
        GameTime readyTime{0};

        /** The chosen weapon's full reload, in ticks. */
        GameTime reloadTicks{1};
    };

    /**
     * Picks the weapon whose reload bar a unit shows: among its weapons that
     * carry `reloadbar=1`, the one with the longest reload. Presence of the
     * slot is what matters rather than a recent shot, so a long-reload weapon
     * is still chosen while it is fully ready. Ties go to the lower slot.
     *
     * Returns nullopt when no slot carries the flag or the weapon definition
     * cannot be found. `health`, `maxHealth`, `kills` and `veterancy` are the
     * shooter's, so the stored reload is the one the simulation actually
     * waits: the original scales `reloadTime` by damage and veterancy.
     */
    std::optional<ReloadBarState> chooseReloadBarWeapon(
        const std::array<std::optional<UnitWeapon>, 3>& weapons,
        const std::unordered_map<std::string, WeaponDefinition>& weaponDefinitions,
        unsigned int health,
        unsigned int maxHealth,
        unsigned int kills,
        const VeterancyDefinition& veterancy);

    /**
     * How full the reload bar is: 1 when the weapon is ready (including
     * before its first shot), 0 at the tick it fired, filling linearly over
     * the chosen weapon's reload time.
     */
    float reloadBarProgress(const ReloadBarState& state, GameTime now);
}
