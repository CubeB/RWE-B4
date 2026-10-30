#include "UnitDefinition.h"
#include <rwe/util/rwe_string.h>

namespace rwe
{
    bool categoryListContains(const std::string& categoryList, const std::string& category)
    {
        if (category.empty())
        {
            return false;
        }

        auto wanted = toUpper(category);
        for (const auto& token : split(categoryList, {' ', '\t'}))
        {
            if (!token.empty() && toUpper(token) == wanted)
            {
                return true;
            }
        }

        return false;
    }

    const std::string& unitDeathExplosionWeapon(const UnitDefinition& definition, bool carried, bool selfDestruct, bool communityPatch)
    {
        if (selfDestruct)
        {
            if (communityPatch && carried && !definition.transportedSelfDestructAs.empty())
            {
                return definition.transportedSelfDestructAs;
            }
            return definition.selfDestructAs.empty() ? definition.explodeAs : definition.selfDestructAs;
        }

        if (communityPatch && carried && !definition.transportedExplodeAs.empty())
        {
            return definition.transportedExplodeAs;
        }
        return definition.explodeAs;
    }
}
