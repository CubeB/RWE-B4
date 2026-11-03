#pragma once

#include <rwe/sim/ResourceSettler.h>
#include <string>

namespace rwe
{
    // MOD-11, docs/MODDING-PRESENTATION-SUBSYSTEM-MAP.md. The first bounded
    // subsystem contract: one player's once-a-second resource settle. Values
    // only; no pointers, entities or RNG cross this boundary.
    class EconomyProvider
    {
    public:
        virtual ~EconomyProvider() = default;
        // Recorded in saves and the hash, so a replacement cannot be
        // silently swapped for the native arithmetic.
        virtual std::string id() const = 0;
        virtual SettledResourceAccount settle(const ResourceAccount& account, float incomeMultiplier) const = 0;
    };

    // The default provider is the existing arithmetic, unchanged. It exists
    // so the native path and a replacement use the same call.
    class NativeEconomyProvider final : public EconomyProvider
    {
    public:
        std::string id() const override;
        SettledResourceAccount settle(const ResourceAccount& account, float incomeMultiplier) const override;
    };
}
