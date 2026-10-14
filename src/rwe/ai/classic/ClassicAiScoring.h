#pragma once

#include <rwe/ai/classic/ClassicRandom.h>
#include <vector>

namespace rwe
{
    /**
     * What the original computer player scores a unit type by
     * (TOTALA-EXE-COMPUTER-PLAYER.md S:120). Everything here is a pure
     * function of numbers, so each formula is tested against hand-computed
     * values and no simulation is needed to read it.
     *
     * Integer arithmetic throughout, and the truncations are part of the
     * finding: "the source converts at each term, so a single final cast is
     * not equivalent".
     */

    /** The facts of one type that the scores read. */
    struct ClassicTypeFacts
    {
        bool canAttack{false};
        bool builder{false};
        bool mobile{false};
        bool canFly{false};
        bool canLoad{false};
        bool isFeature{false};

        /** extractsMetal > 0. */
        bool extractor{false};

        /** makesMetal > 0: a metal converter. */
        bool converter{false};
        bool sonar{false};
        bool radar{false};
        bool windGenerator{false};

        /**
         * The `MinWaterDepth >= 0` predicate, exactly as the register writes
         * it. RWE stores the key as unsigned and reads an absent key as 0, so
         * this is true of every type RWE loads. [open]
         */
        bool waterDepthWord{true};

        double metalCost{0.0};
        double energyCost{0.0};

        /** EnergyMake: what the type produces. */
        double energyMake{0.0};

        /** EnergyUse. */
        double energyUse{0.0};

        /** Energy use less energy made: negative for a producer. */
        double netEnergyUse() const { return energyUse - energyMake; }
    };

    /** One weapon slot's contribution to the weapon score. */
    struct ClassicWeaponFacts
    {
        unsigned int damage{0};
        double range{0.0};
    };

    /**
     * 1, or 11 for a type that can attack, plus damage / 40 + range / 100 + 5
     * for each weapon given, clamped to [-100, 100].
     */
    int classicWeaponScore(bool canAttack, const std::vector<ClassicWeaponFacts>& weapons);

    /**
     * The type value, a signed byte: 11 for an extractor and 1 otherwise, plus
     * 10 for a converter, 10 for a negative net energy use,
     * trunc(metalCost * 0.01), trunc(energyCost * 0.002) and the weapon score,
     * clamped to [-128, 127].
     */
    int classicTypeValue(const ClassicTypeFacts& facts, int weaponScore);

    struct ClassicRatings
    {
        int normal{0};
        int metal{0};
        int energy{0};
    };

    /** What a rating depends on outside the type: counts, the unit limit and the wind. */
    struct ClassicRatingContext
    {
        /** Completed units of this type. */
        int completedOfType{0};

        /** All completed units of the player. */
        int completedTotal{0};

        /** The unit limit; the "over half" rule compares against half of it. */
        int unitLimit{200};

        /** The code's low-wind test, decided by the caller. */
        bool lowWind{false};
    };

    /**
     * The three ratings. The normal rating starts at 1 (21 if the type can
     * attack), adds 30 for a builder when fewer than 3 exist, 50 for a
     * negative net energy use, 50 for an extractor, 25 for a converter, 40 for
     * an aircraft, 15 for sonar, 5 for radar and the energy it produces
     * clamped to [0, 30]; is multiplied by 4 if none exist, 2 if exactly one
     * does and 3 for `MinWaterDepth >= 0` (sequentially: [open]); adds half
     * the type value when the player is over half its unit limit; becomes 0
     * for a canload or isfeature type and for a wind generator in low wind;
     * and is capped at 100.
     */
    ClassicRatings classicRatings(const ClassicTypeFacts& facts, int typeValue, const ClassicRatingContext& context);

    /** The player's stock and flow, as the final rating reads them. */
    struct ClassicEconomy
    {
        double energy{0.0};
        double metal{0.0};
        double energyStorage{0.0};
        double metalStorage{0.0};

        /** Production less consumption, per second. */
        double netEnergy{0.0};
        double netMetal{0.0};

        double energyIncome{0.0};
        double metalIncome{0.0};
    };

    /** How much the final rating leans on each of the three. They sum to 100 unless clamped. */
    struct ClassicShortage
    {
        int metalWeight{0};
        int energyWeight{0};
        int normalWeight{0};
    };

    /**
     * The shortage weights. Metal shortage claims priority before energy:
     * energyWeight is what is left of E once metalWeight is taken off.
     */
    ClassicShortage classicShortage(const ClassicEconomy& economy);

    /**
     * The final rating for one candidate (0x40bb00): 0 under 50 stored energy
     * or 25 stored metal, and when `completedOfType >= limit` for a
     * non-negative limit; otherwise
     * (normalWeight * normal + metalWeight * metal + energyWeight * energy)
     * * multiplier / 10000, in integers.
     */
    int classicFinalRating(const ClassicRatings& ratings, const ClassicEconomy& economy, int multiplier, int limit, int completedOfType);

    /**
     * The weighted reservoir draw (0x40bdb0): each positive rating r adds to a
     * running total and replaces the current choice when rand(total) < r, so
     * every positive candidate consumes exactly one draw (and a zero or
     * negative one none). Returns the index chosen, or -1 if no rating was
     * positive.
     */
    int classicWeightedDraw(const std::vector<int>& ratings, ClassicRandom& rng);
}
