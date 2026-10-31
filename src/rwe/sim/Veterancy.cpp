#include "Veterancy.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

namespace rwe
{
    namespace
    {
        /** Strict whole-number parse: the whole token, no sign, no overflow. */
        bool parseWholeNumber(const std::string& token, unsigned int& out)
        {
            if (token.empty())
            {
                return false;
            }

            std::uint64_t value = 0;
            for (auto c : token)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                value = (value * 10u) + static_cast<unsigned int>(c - '0');
                if (value > std::numeric_limits<unsigned int>::max())
                {
                    return false;
                }
            }

            out = static_cast<unsigned int>(value);
            return true;
        }

        /** Splits on whitespace, the way TADR's reader does. */
        std::vector<std::string> splitTokens(const std::string& text)
        {
            std::vector<std::string> tokens;
            std::string current;
            for (auto c : text)
            {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    if (!current.empty())
                    {
                        tokens.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!current.empty())
            {
                tokens.push_back(current);
            }
            return tokens;
        }
    }

    const VeterancyDefinition& defaultVeterancy()
    {
        static const VeterancyDefinition rules{};
        return rules;
    }

    const VeterancyDefinition& veterancyRules(const VeterancyDefinition& unitRules, bool communityPatch)
    {
        return communityPatch ? unitRules : defaultVeterancy();
    }

    unsigned int veterancyLevel(const VeterancyDefinition& rules, unsigned int kills)
    {
        // `thresholdCount` is always at least one and the list is strictly
        // increasing, which the parsers and the default guarantee. Count how
        // many thresholds the kill count has reached.
        auto count = std::min(rules.thresholdCount, VeterancyDefinition::MaxThresholds);
        auto end = rules.thresholds.begin() + count;
        return static_cast<unsigned int>(std::distance(
            rules.thresholds.begin(),
            std::upper_bound(rules.thresholds.begin(), end, kills)));
    }

    unsigned int veterancyLevelUnbounded(const VeterancyDefinition& rules, unsigned int kills)
    {
        auto count = std::max(1u, std::min(rules.thresholdCount, VeterancyDefinition::MaxThresholds));
        auto level = veterancyLevel(rules, kills);

        auto last = rules.thresholds[count - 1];
        if (kills <= last)
        {
            return level;
        }

        // TADR's extrapolation: continue with the step between the last two
        // thresholds, or with the single threshold when there is only one.
        // A zero step is impossible for two thresholds (strictly increasing)
        // and is floored to one for the one-threshold case, where TADR would
        // otherwise divide by zero on a threshold of nought.
        std::uint64_t step = count >= 2
            ? static_cast<std::uint64_t>(last - rules.thresholds[count - 2])
            : static_cast<std::uint64_t>(std::max(1u, last));

        auto extra = static_cast<std::uint64_t>(kills - last) / step;
        auto unbounded = static_cast<std::uint64_t>(level) + extra;
        return static_cast<unsigned int>(
            std::min<std::uint64_t>(unbounded, std::numeric_limits<unsigned int>::max()));
    }

    unsigned int veterancyAccuracyDivisor(const VeterancyDefinition& rules, unsigned int kills)
    {
        if (rules.accuracyBuffRate == 0)
        {
            return 0;
        }

        // The original zero-extends the kill count from a word before the
        // divide, so keep the same truncation.
        return static_cast<unsigned int>(static_cast<std::uint16_t>(kills)) / rules.accuracyBuffRate;
    }

    bool parseVeterancyThresholds(const std::string& text, VeterancyDefinition& rules)
    {
        auto tokens = splitTokens(text);
        if (tokens.empty() || tokens.size() > VeterancyDefinition::MaxThresholds)
        {
            return false;
        }

        VeterancyDefinition parsed = rules;
        parsed.thresholdCount = static_cast<unsigned int>(tokens.size());
        parsed.thresholds.fill(0u);

        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            unsigned int value = 0;
            if (!parseWholeNumber(tokens[i], value))
            {
                return false;
            }
            if (i > 0 && value <= parsed.thresholds[i - 1])
            {
                return false;
            }
            parsed.thresholds[i] = value;
        }

        rules = parsed;
        return true;
    }

    bool parseVeterancyAccuracyBuffRate(const std::string& text, VeterancyDefinition& rules)
    {
        unsigned int value = 0;
        if (!parseWholeNumber(text, value))
        {
            return false;
        }

        rules.accuracyBuffRate = value;
        return true;
    }
}
