#include "EconomyProvider.h"

namespace rwe
{
    std::string NativeEconomyProvider::id() const
    {
        return "native.economy@1";
    }

    SettledResourceAccount NativeEconomyProvider::settle(
        const ResourceAccount& account, float incomeMultiplier) const
    {
        return settleResourceAccount(account, incomeMultiplier);
    }
}
