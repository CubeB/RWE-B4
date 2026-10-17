#include "ClassicAiScoring.h"

#include <algorithm>

namespace rwe
{
    namespace
    {
        int clampInt(int v, int lo, int hi)
        {
            return std::max(lo, std::min(hi, v));
        }

        int truncToInt(double v)
        {
            if (v > 2147483000.0)
            {
                return 2147483000;
            }
            if (v < -2147483000.0)
            {
                return -2147483000;
            }
            return static_cast<int>(v);
        }

        int clampDoubleToInt(double v, int lo, int hi)
        {
            if (v < static_cast<double>(lo))
            {
                return lo;
            }
            if (v > static_cast<double>(hi))
            {
                return hi;
            }
            return static_cast<int>(v);
        }
    }

    bool classicLowWind(int maxWindSpeed)
    {
        // Half of the 5000 at global +0x37EC8, rounded toward zero.
        constexpr int HalfGenerationDenominator = 5000 / 2;
        return maxWindSpeed < HalfGenerationDenominator;
    }

    int classicWeaponScore(bool canAttack, const std::vector<ClassicWeaponFacts>& weapons)
    {
        int score = canAttack ? 11 : 1;
        for (const auto& w : weapons)
        {
            // Integer divisions, one per term: S:120 "adds damage / 40 + range / 100 + 5".
            int damageTerm = static_cast<int>(w.damage / 40u);
            int rangeTerm = truncToInt(w.range) / 100;
            score += damageTerm + rangeTerm + 5;
        }
        return clampInt(score, -100, 100);
    }

    int classicTypeValue(const ClassicTypeFacts& facts, int weaponScore)
    {
        int value = facts.extractor ? 11 : 1;
        if (facts.converter)
        {
            value += 10;
        }
        if (facts.netEnergyUse() < 0.0)
        {
            value += 10;
        }
        // The source converts at each term, so these are two truncations and
        // not one cast of the sum.
        value += truncToInt(facts.metalCost * 0.01);
        value += truncToInt(facts.energyCost * 0.002);
        value += weaponScore;
        return clampInt(value, -128, 127);
    }

    ClassicRatings classicRatings(const ClassicTypeFacts& facts, int typeValue, const ClassicRatingContext& context)
    {
        ClassicRatings r;

        // Normal. TOTALA-EXE-COMPUTER-PLAYER.md S:120, "The three ratings".
        int normal = facts.canAttack ? 21 : 1;
        if (facts.builder && context.completedOfType < 3)
        {
            normal += 30;
        }
        if (facts.netEnergyUse() < 0.0)
        {
            normal += 50;
        }
        if (facts.extractor)
        {
            normal += 50;
        }
        if (facts.converter)
        {
            normal += 25;
        }
        if (facts.canFly)
        {
            normal += 40;
        }
        if (facts.sonar)
        {
            normal += 15;
        }
        if (facts.radar)
        {
            normal += 5;
        }
        normal += clampDoubleToInt(facts.energyMake, 0, 30);

        // [open] The three multipliers are applied one after the other, which
        // is how the source reads, so a type with none standing and the
        // water-depth word gets 12.
        if (context.completedOfType == 0)
        {
            normal *= 4;
        }
        else if (context.completedOfType == 1)
        {
            normal *= 2;
        }
        if (facts.waterDepthWord)
        {
            normal *= 3;
        }

        if (context.completedTotal > (context.unitLimit >> 1))
        {
            normal += typeValue / 2;
        }

        if (facts.canLoad || facts.isFeature || (facts.windGenerator && context.lowWind))
        {
            normal = 0;
        }
        r.normal = std::min(normal, 100);

        // Metal.
        {
            double v = -0.02 * facts.metalCost + (facts.converter ? 25.0 : 0.0) + (facts.extractor ? 100.0 : 0.0);
            r.metal = clampDoubleToInt(v, 0, 100);
        }

        // Energy.
        {
            double v = -0.0025 * facts.energyCost - 5.0 * facts.netEnergyUse();
            r.energy = clampDoubleToInt(v, 0, 100);
        }

        return r;
    }

    ClassicShortage classicShortage(const ClassicEconomy& economy)
    {
        const int energyCap = std::min(1000, truncToInt(economy.energyStorage));
        const int metalCap = std::min(500, truncToInt(economy.metalStorage));

        int e = truncToInt(std::max(0.0, (static_cast<double>(energyCap) - economy.energy) * 0.125));
        int m = truncToInt(std::max(0.0, (static_cast<double>(metalCap) - economy.metal) * 0.25));

        if (economy.netEnergy < 1.0)
        {
            e += 20;
        }
        if (economy.netMetal < 1.0)
        {
            m += 20;
        }
        if (economy.energyIncome < 50.0)
        {
            e += 100;
        }
        else if (economy.energyIncome < 200.0)
        {
            e += 10;
        }
        if (economy.metalIncome < 3.0)
        {
            m += 100;
        }
        else if (economy.metalIncome < 5.0)
        {
            m += 20;
        }

        ClassicShortage s;
        s.metalWeight = clampInt(m, 0, 100);
        s.energyWeight = clampInt(e - s.metalWeight, 0, 100);
        s.normalWeight = std::max(100 - s.metalWeight - s.energyWeight, 0);
        return s;
    }

    int classicFinalRating(const ClassicRatings& ratings, const ClassicEconomy& economy, int multiplier, int limit, int completedOfType)
    {
        if (economy.energy < 50.0 || economy.metal < 25.0)
        {
            return 0;
        }
        if (limit != -1 && completedOfType >= limit)
        {
            return 0;
        }
        auto s = classicShortage(economy);
        long long sum = static_cast<long long>(s.normalWeight) * ratings.normal
            + static_cast<long long>(s.metalWeight) * ratings.metal
            + static_cast<long long>(s.energyWeight) * ratings.energy;
        long long scaled = sum * static_cast<long long>(multiplier) / 10000;
        return static_cast<int>(scaled);
    }

    int classicWeightedDraw(const std::vector<int>& ratings, ClassicRandom& rng)
    {
        int chosen = -1;
        std::uint32_t total = 0;
        for (std::size_t i = 0; i < ratings.size(); ++i)
        {
            if (ratings[i] <= 0)
            {
                continue;
            }
            auto r = static_cast<std::uint32_t>(ratings[i]);
            total += r;
            if (rng.below(total) < r)
            {
                chosen = static_cast<int>(i);
            }
        }
        return chosen;
    }
}
