#include "InterfaceRules.h"

namespace rwe
{
    bool armedCaptureAccepts(bool ordererCanCapture, PlayerId ordererOwner, PlayerId targetOwner)
    {
        return ordererCanCapture && ordererOwner != targetOwner;
    }

    bool buildClickTakesUnit(bool isBuilder, bool isMobile)
    {
        return isBuilder && isMobile;
    }

    BuildMenuPosition stepBuildMenu(BuildMenuPosition position, unsigned int pageCount, bool forward, bool toggle)
    {
        if (pageCount == 0)
        {
            return position;
        }

        if (toggle)
        {
            if (!position.open)
            {
                return BuildMenuPosition{true, forward ? 0u : pageCount - 1u};
            }
            if (forward ? position.page + 1 >= pageCount : position.page == 0)
            {
                return BuildMenuPosition{false, position.page};
            }
            return BuildMenuPosition{true, forward ? position.page + 1 : position.page - 1};
        }

        return BuildMenuPosition{true, forward ? (position.page + 1) % pageCount : (position.page + pageCount - 1) % pageCount};
    }
}
