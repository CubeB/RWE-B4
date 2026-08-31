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
}
