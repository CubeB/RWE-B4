#include "InterfaceRules.h"
#include <cmath>

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

    bool pressIsClick(unsigned int pressMillis, unsigned int releaseMillis, float worldDeltaX, float worldDeltaZ)
    {
        // Each reading is scaled before the difference is taken, as the
        // original's clock is.
        auto pressClock = static_cast<unsigned int>((static_cast<unsigned long long>(pressMillis) * 30ull) / 1000ull);
        auto releaseClock = static_cast<unsigned int>((static_cast<unsigned long long>(releaseMillis) * 30ull) / 1000ull);
        return releaseClock - pressClock < 25u && std::fabs(worldDeltaX) < 32.0f && std::fabs(worldDeltaZ) < 32.0f;
    }

    bool sweepSelectsUnit(bool underConstruction, bool carried)
    {
        return !underConstruction && !carried;
    }

    std::vector<std::size_t> selfDestructRecipients(const std::vector<bool>& counting)
    {
        std::vector<std::size_t> result;
        for (std::size_t i = 0; i < counting.size(); ++i)
        {
            if (counting[i])
            {
                result.push_back(i);
            }
        }
        if (!result.empty())
        {
            return result;
        }
        for (std::size_t i = 0; i < counting.size(); ++i)
        {
            result.push_back(i);
        }
        return result;
    }

    unsigned int endGameFadeStep(unsigned int elapsedTicks)
    {
        auto taken = elapsedTicks / EndGameFadeTicksPerStep;
        return taken < EndGameFadeSteps ? taken : EndGameFadeSteps;
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
