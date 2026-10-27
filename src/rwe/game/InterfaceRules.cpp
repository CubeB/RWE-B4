#include "InterfaceRules.h"
#include <algorithm>
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

    bool bandboxFilterAdmits(const BandboxFilter& filter, bool isMobile, bool isArmed, bool isBuilder)
    {
        if (!filter.armedMobile && !filter.constructors && !filter.factories)
        {
            return true;
        }
        return (filter.armedMobile && isMobile && isArmed)
            || (filter.constructors && isMobile && isBuilder)
            || (filter.factories && !isMobile && isBuilder);
    }

    bool isIdleFactory(bool isMobile, bool isBuilder, bool buildQueueEmpty)
    {
        return !isMobile && isBuilder && buildQueueEmpty;
    }

    bool isIdleBuilder(bool isMobile, bool isBuilder, bool ordersEmpty)
    {
        return isMobile && isBuilder && ordersEmpty;
    }

    std::optional<UnitId> nextInCycle(std::vector<UnitId> candidates, std::optional<UnitId> cursor)
    {
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::sort(candidates.begin(), candidates.end());
        if (cursor)
        {
            auto it = std::upper_bound(candidates.begin(), candidates.end(), *cursor);
            if (it != candidates.end())
            {
                return *it;
            }
        }
        return candidates.front();
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

    bool keyDismissesEndScreen(SDL_Keycode key)
    {
        switch (key)
        {
            case SDLK_LSHIFT:
            case SDLK_RSHIFT:
            case SDLK_LCTRL:
            case SDLK_RCTRL:
            case SDLK_LALT:
            case SDLK_RALT:
            case SDLK_LGUI:
            case SDLK_RGUI:
            case SDLK_CAPSLOCK:
            case SDLK_NUMLOCKCLEAR:
            case SDLK_SCROLLLOCK:
            case SDLK_UNKNOWN:
                return false;
            default:
                return true;
        }
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
