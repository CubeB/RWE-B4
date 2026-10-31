#pragma once

#include <array>
#include <string>

namespace rwe
{
    /**
     * How a unit earns and spends veterancy.
     *
     * The original buckets kills in fives and stops at five tiers, and that is
     * what the default below says. TADR's per-unit `VeterancyThresholds` and
     * `VeterancyAccuracyBuffRate` FBI keys (TA Community Patch, #934) let a
     * mod replace those numbers for one unit; RWE parses them whatever the
     * ruleset and reads them only under `--community-patch`, so a file with
     * no keys behaves exactly as before in both modes.
     *
     * This is data, not per-unit state: it lives on the unit's definition, is
     * identical on every peer that has the same data, and is never saved,
     * hashed or dumped.
     */
    struct VeterancyDefinition
    {
        /**
         * The most thresholds a definition may carry. TADR has no limit; RWE
         * bounds the list so a malformed file cannot size anything unbounded.
         * A longer list is refused outright rather than truncated, because
         * truncating would change the extrapolation step as well.
         */
        static constexpr unsigned int MaxThresholds = 32;

        /**
         * The kills needed for levels 1..thresholdCount, strictly increasing.
         * The default is the original's 5 10 15 20 25, which is why a unit
         * that names no key keeps the original's arithmetic to the bit.
         */
        unsigned int thresholdCount{5};
        std::array<unsigned int, MaxThresholds> thresholds{5u, 10u, 15u, 20u, 25u};

        /**
         * Kills per +1 to the accuracy divisor; 0 switches the accuracy buff
         * off. The original's rate is twelve.
         */
        unsigned int accuracyBuffRate{12};
    };

    /** The original's rules: thresholds 5 10 15 20 25 and an accuracy rate of 12. */
    const VeterancyDefinition& defaultVeterancy();

    /**
     * Which rules a unit plays by: its own parsed keys under the Community
     * Patch, the original's defaults otherwise. A unit that names no keys has
     * the defaults either way, so this only ever changes a mod's unit in the
     * mode it opted into.
     */
    const VeterancyDefinition& veterancyRules(const VeterancyDefinition& unitRules, bool communityPatch);

    /**
     * The unit's level, counting thresholds reached and stopping at the last
     * one. This is the tier damage dealt and reload time scale by (TADR caps
     * those at the number of thresholds), and the number the COB
     * `VeteranLevel` query answers with.
     */
    unsigned int veterancyLevel(const VeterancyDefinition& rules, unsigned int kills);

    /**
     * The level with no ceiling: past the last threshold the ladder carries
     * on with the step between the last two (TADR's rule for capture cost).
     * With a single threshold the step is that threshold, which is what
     * TADR does; with the default list this is exactly `kills / 5`.
     */
    unsigned int veterancyLevelUnbounded(const VeterancyDefinition& rules, unsigned int kills);

    /**
     * The divisor the accuracy cone is divided by, or 0 when the buff is off.
     * The original reads the kill count as a word and divides it by twelve;
     * it only divides the cone when the quotient is greater than one, which
     * is why the first effect lands at twice the rate rather than at it.
     * TADR's written rule states the factor as `1 + floor(kills / rate)`;
     * its own hook replaces the divisor with `kills / rate` and leaves the
     * original's `> 1` test in place, which is the shape kept here so that an
     * absent key is bit-identical.
     */
    unsigned int veterancyAccuracyDivisor(const VeterancyDefinition& rules, unsigned int kills);

    /**
     * Parses a `VeterancyThresholds` value: whitespace-separated whole
     * numbers, strictly increasing, one to MaxThresholds of them. Returns
     * false and leaves `rules` untouched on anything malformed, so the caller
     * can keep the default and warn.
     */
    bool parseVeterancyThresholds(const std::string& text, VeterancyDefinition& rules);

    /**
     * Parses a `VeterancyAccuracyBuffRate` value: one whole number, zero
     * allowed (it disables the buff). Returns false and leaves `rules`
     * untouched on anything malformed.
     */
    bool parseVeterancyAccuracyBuffRate(const std::string& text, VeterancyDefinition& rules);
}
