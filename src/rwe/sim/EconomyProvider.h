#pragma once

#include <rwe/sim/ResourceSettler.h>

namespace rwe
{
    // MOD-11, docs/MODDING-PRESENTATION-SUBSYSTEM-MAP.md. The first bounded
    // subsystem contract: one player's once-a-second resource settle. Values
    // only; no pointers, entities or RNG cross this boundary.
    class EconomyProvider
    {
    public:
        virtual ~EconomyProvider() = default;
        virtual SettledResourceAccount settle(const ResourceAccount& account, float incomeMultiplier) const = 0;
    };

    // The default provider is the existing arithmetic, unchanged. It exists
    // so the native path and a replacement use the same call.
    class NativeEconomyProvider final : public EconomyProvider
    {
    public:
        SettledResourceAccount settle(const ResourceAccount& account, float incomeMultiplier) const override;
    };
}
