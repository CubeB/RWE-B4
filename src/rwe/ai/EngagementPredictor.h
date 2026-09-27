#pragma once

#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct UnitDefinition;

    /**
     * Coarse combat stats for one unit type, read straight from its FBI and
     * weapon TDF fields the way ThreatMap::estimateDps/weaponRange and
     * BuildManager::unitCombatValuePerMetal already did, independently, three
     * times over (see the issue this consolidates, #389). dps and range are
     * each the best (highest) figure across the unit's three weapon slots --
     * not the sum, matching unitCombatValuePerMetal's own reasoning ("a unit
     * rarely brings two weapons to bear on the same target") -- and are not
     * required to come from the same weapon slot when a unit carries more
     * than one of a different kind (e.g. a main gun and a token anti-air
     * gun); that is the same independence the two original helpers already
     * had, kept rather than tidied away so the consolidation stays
     * behaviour-identical for BuildManager's caller.
     *
     * Deliberately not modelling armour class (damage is always read from
     * the "DEFAULT" key) or air/ground weapon eligibility beyond what the
     * two originals already did (neither filtered on toAirWeapon).
     *
     * Neither combatStats nor engagementScore/engagementMargin below know
     * whether a unit is a ground unit or an aircraft, or filter one side's
     * list against the other's domain -- a caller with a mixed-domain
     * candidate list (ground and air together) has to split it itself
     * before calling in, the way the outpost-raid response already does
     * (both its raiders and its responders are pre-filtered to !isAir).
     */
    struct UnitCombatStats
    {
        float hp{0.0f};
        float dps{0.0f};
        float range{0.0f};
        float speed{0.0f};
        float metal{0.0f};
    };

    /**
     * Best (highest) damage per second across a unit definition's three
     * weapon slots, against the "DEFAULT" armour class: burst shots over
     * reload time, weapons looked up upper-cased the way the loader stores
     * them. Zero if the unit has no weapon that resolves. This is the same
     * arithmetic BuildManager::unitCombatValuePerMetal always used, pulled
     * out so ThreatMap and the engagement predictor can share it without a
     * third hand-copy.
     */
    float bestDps(const GameSimulation& sim, const UnitDefinition& def);

    /**
     * Best (highest) maxRange across the same three weapon slots, zero if
     * none resolve. The same arithmetic ThreatMap::weaponRange always used.
     */
    float bestRange(const GameSimulation& sim, const UnitDefinition& def);

    /**
     * hp/dps/range/speed/metal for a unit type, looked up by name. All zero
     * (a default-constructed UnitCombatStats) if the type is not known --
     * matching what the three original helpers all did on a failed lookup.
     */
    UnitCombatStats combatStats(const GameSimulation& sim, const std::string& unitType);

    /**
     * Sum, over one side of an engagement, of hp * effectiveDps for each of
     * its units, where effectiveDps folds in a bounded range-advantage term:
     * a unit that outranges the opposing side's average weapon range gets
     * extra effective dps for the free volleys it lands while the shorter
     * side is still closing the gap, proportional to how many ticks that
     * takes divided by how fast the two sides close on one another. See
     * EngagementPredictor.cpp for the calibration and the worked examples
     * (Storm outranging Peewee, the Slasher outranging Flash) it is pinned
     * against.
     *
     * side is one entry per unit (duplicates for more than one of a type,
     * no separate count), so a caller with an exact unit list -- as the
     * outpost-raid response already builds one -- does not need to
     * deduplicate first.
     */
    float engagementScore(const std::vector<UnitCombatStats>& side, const std::vector<UnitCombatStats>& opposing);

    /**
     * engagementScore(a, b) - engagementScore(b, a). Positive means a is
     * predicted to come out ahead. Each side's score already accounts for
     * its own range advantage (if any) against the other side.
     */
    float engagementMargin(const std::vector<UnitCombatStats>& a, const std::vector<UnitCombatStats>& b);
}
