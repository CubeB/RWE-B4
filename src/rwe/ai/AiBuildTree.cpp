#include "AiBuildTree.h"
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    bool AiBuildTree::knows(const std::string& builderType) const
    {
        return buildableBy.find(toUpper(builderType)) != buildableBy.end();
    }

    bool AiBuildTree::canBuild(const std::string& builderType, const std::string& unitType) const
    {
        if (!missionAllows(unitType))
        {
            return false;
        }
        auto it = buildableBy.find(toUpper(builderType));
        if (it == buildableBy.end())
        {
            // Nothing known about this builder, so no grounds to refuse it.
            return true;
        }
        return it->second.count(toUpper(unitType)) != 0;
    }

    bool AiBuildTree::missionAllows(const std::string& type) const
    {
        auto rule = missionRules.find(toUpper(type));
        if (rule == missionRules.end())
        {
            return true;
        }
        auto count = missionCommitted.find(toUpper(type));
        const int committed = count == missionCommitted.end() ? 0 : count->second;
        return rule->second.multiplier > 0 && (rule->second.limit == -1 || committed < rule->second.limit);
    }

    int AiBuildTree::missionWeight(const std::string& type) const
    {
        auto rule = missionRules.find(toUpper(type));
        return rule == missionRules.end() ? 100 : rule->second.multiplier;
    }

    AiBuildTree buildTreeFromBuilderGuis(const BuilderGuisDatabase& guis, const std::set<std::string>& unitTypes)
    {
        // Everything is compared upper case. The unit definitions are keyed
        // that way and the build menus are not: the loader files a unit's
        // pages under the raw name out of its FBI, and the expansion data
        // spells a few of those in mixed case (Armvulc, Corbuzz), so a
        // straight lookup would miss them.
        std::set<std::string> upperUnitTypes;
        for (const auto& type : unitTypes)
        {
            upperUnitTypes.insert(toUpper(type));
        }

        AiBuildTree tree;
        for (const auto& [builderName, pages] : guis.builderGuisMap)
        {
            std::set<std::string> buildable;
            for (const auto& page : pages)
            {
                for (const auto& entry : page)
                {
                    auto name = toUpper(entry.common.name);
                    if (upperUnitTypes.count(name) != 0)
                    {
                        buildable.insert(name);
                    }
                }
            }
            if (!buildable.empty())
            {
                tree.buildableBy.emplace(toUpper(builderName), std::move(buildable));
            }
        }
        return tree;
    }
}
