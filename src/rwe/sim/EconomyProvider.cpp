#include "EconomyProvider.h"

namespace rwe
{
    SettledResourceAccount NativeEconomyProvider::settle(
        const ResourceAccount& account, float incomeMultiplier) const
    {
        return settleResourceAccount(account, incomeMultiplier);
    }
}
