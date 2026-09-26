#include "EngagementPredictor.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    float bestDps(const GameSimulation& sim, const UnitDefinition& def)
    {
        // Identical to BuildManager::unitCombatValuePerMetal's own loop
        // before this consolidation: the best of the three weapon slots, not
        // the sum, because a unit rarely brings two weapons to bear on the
        // same target at once.
        float best = 0.0f;
        for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
        {
            if (weaponName.empty())
            {
                continue;
            }
            // Both the weapon table and the damage classes within it are
            // keyed upper case by the loader, while the FBI's own spelling
            // is whatever the unit's author typed.
            auto weaponIt = sim.weaponDefinitions.find(toUpper(weaponName));
            if (weaponIt == sim.weaponDefinitions.end())
            {
                continue;
            }
            const auto& weapon = weaponIt->second;
            auto damageIt = weapon.damage.find("DEFAULT");
            if (damageIt == weapon.damage.end())
            {
                continue;
            }
            auto reload = weapon.reloadTime.value;
            if (reload <= 0.0f)
            {
                continue;
            }
            auto shots = static_cast<float>(std::max(1, weapon.burst));
            best = std::max(best, static_cast<float>(damageIt->second) * shots / reload);
        }
        return best;
    }

    float bestRange(const GameSimulation& sim, const UnitDefinition& def)
    {
        // Identical to ThreatMap::weaponRange's own loop before this
        // consolidation.
        float best = 0.0f;
        for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
        {
            if (weaponName.empty())
            {
                continue;
            }
            auto it = sim.weaponDefinitions.find(toUpper(weaponName));
            if (it != sim.weaponDefinitions.end())
            {
                best = std::max(best, it->second.maxRange.value);
            }
        }
        return best;
    }

    UnitCombatStats combatStats(const GameSimulation& sim, const std::string& unitType)
    {
        auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return UnitCombatStats{};
        }
        const auto& def = defIt->second;
        UnitCombatStats stats;
        stats.hp = static_cast<float>(def.maxHitPoints);
        stats.dps = bestDps(sim, def);
        stats.range = bestRange(sim, def);
        stats.speed = def.maxVelocity.value;
        stats.metal = def.buildCostMetal.value;
        return stats;
    }

    namespace
    {
        /**
         * How many ticks of free, unanswered fire a range advantage is worth
         * before the bonus it grants a unit's effective dps saturates.
         *
         * Fitted, not derived: docs/TOTALA-EXE-WEAPONS.md and the shipped
         * weapon TDFs give the ingredients (range, reload, projectile
         * travel) but nothing hands over a ready-made "how much does a
         * standoff actually matter" number, so this is the same kind of
         * small fitted coefficient the tech-ratio work already carries
         * (BuildManager's techMinArmyValueRatio). It is calibrated against
         * two worked examples the user's own TA doctrine names
         * (core-anti-rush-doctrine): a Storm (CORSTORM, 400-range rockets)
         * against a Peewee (ARMPW, 180-range EMG) and a Slasher (CORMIST,
         * 600-range missiles) against a Flash (ARMFLASH, 180-range EMG),
         * read out of units/CORSTORM.FBI, units/ARMPW.FBI, units/CORMIST.FBI
         * and units/ARMFLASH.FBI plus weapons/ROCKETS.TDF,
         * weapons/WEAPONS.TDF and weapons/MISSILES.TDF (totala1.hpi). 45
         * ticks is a second and a half at the sim's 30 ticks/second -- about
         * what it takes a Slasher's closing-speed gap against a Flash
         * (rangeDiff 420, closing speed 3.45 units/tick, so 121.7 ticks) to
         * clear the cap comfortably while a Storm's shorter gap against a
         * Peewee (rangeDiff 220, closing speed 3.05, 72.1 ticks) still lands
         * short of it.
         */
        constexpr float RangeAdvantageCalibrationTicks = 1.5f * SimTicksPerSecond;

        /**
         * The range-advantage bonus is a multiplier on effectiveDps of
         * (1 + bonus), so this caps a unit's effective dps at four times its
         * plain dps. Bounded so that a closing speed near zero (two units
         * that are both nearly stationary, e.g. a pair of towers) cannot
         * send the bonus to infinity; sized so it does not bind on either of
         * the two worked examples above (Slasher vs Flash reaches ~2.7,
         * Storm vs Peewee ~1.6).
         */
        constexpr float RangeAdvantageBonusCap = 3.0f;

        /** Floor under the two sides' combined closing speed, in world units/tick, to keep the ratio finite. */
        constexpr float MinClosingSpeed = 0.05f;

        /**
         * Extra effective-dps fraction a unit earns for outranging the
         * opposing side's average weapon range: proportional to how many
         * ticks the shorter-ranged side needs to close that gap (the range
         * difference divided by how fast the two sides close on one
         * another), bounded above so neither an enormous range gap nor a
         * near-zero closing speed can blow the bonus up without limit.
         * Zero when this unit does not outrange the opposing average.
         */
        float rangeAdvantageBonus(float unitRange, float unitSpeed, float opposingAvgRange, float opposingAvgSpeed)
        {
            auto rangeDiff = unitRange - opposingAvgRange;
            if (rangeDiff <= 0.0f)
            {
                return 0.0f;
            }
            auto closingSpeed = std::max(MinClosingSpeed, unitSpeed + opposingAvgSpeed);
            auto ticksToClose = rangeDiff / closingSpeed;
            return std::clamp(ticksToClose / RangeAdvantageCalibrationTicks, 0.0f, RangeAdvantageBonusCap);
        }

        struct SideAverage
        {
            float range{0.0f};
            float speed{0.0f};
        };

        SideAverage averageOf(const std::vector<UnitCombatStats>& side)
        {
            if (side.empty())
            {
                return SideAverage{};
            }
            float totalRange = 0.0f;
            float totalSpeed = 0.0f;
            for (const auto& u : side)
            {
                totalRange += u.range;
                totalSpeed += u.speed;
            }
            auto n = static_cast<float>(side.size());
            return SideAverage{totalRange / n, totalSpeed / n};
        }
    }

    float engagementScore(const std::vector<UnitCombatStats>& side, const std::vector<UnitCombatStats>& opposing)
    {
        if (side.empty())
        {
            return 0.0f;
        }
        // An empty opposing side has no average range or speed to be
        // measured against, so nobody gets a range-advantage bonus -- there
        // is no one to have an advantage over.
        auto opposingAvg = opposing.empty() ? SideAverage{} : averageOf(opposing);
        float total = 0.0f;
        for (const auto& u : side)
        {
            auto bonus = opposing.empty() ? 0.0f : rangeAdvantageBonus(u.range, u.speed, opposingAvg.range, opposingAvg.speed);
            auto effectiveDps = u.dps * (1.0f + bonus);
            total += u.hp * effectiveDps;
        }
        return total;
    }

    float engagementMargin(const std::vector<UnitCombatStats>& a, const std::vector<UnitCombatStats>& b)
    {
        return engagementScore(a, b) - engagementScore(b, a);
    }
}
