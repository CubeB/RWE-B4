#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/EconomyProvider.h>

namespace rwe
{
    namespace
    {
        struct DoublingProvider final : EconomyProvider
        {
            std::string id() const override { return "test.doubling@1"; }
            SettledResourceAccount settle(const ResourceAccount& account, float incomeMultiplier) const override
            {
                auto result = settleResourceAccount(account, incomeMultiplier);
                result.stockpile *= 2.0f;
                return result;
            }
        };
    }

    TEST_CASE("the native economy provider is exactly the existing settle", "[modsdk][economyprovider]")
    {
        ResourceAccount account;
        account.stockpile = 100.0f;
        account.production = 50.0f;
        account.maxStorage = 1000.0f;
        account.ownRequest = 80.0f;
        account.pooledRequest = 80.0f;

        NativeEconomyProvider native;
        const auto expected = settleResourceAccount(account, 1.0f);
        const auto provided = native.settle(account, 1.0f);
        CHECK(provided.stockpile == expected.stockpile);
        CHECK(provided.produced == expected.produced);
        CHECK(provided.debt == expected.debt);
        CHECK(provided.requestFraction == expected.requestFraction);
        CHECK(provided.debtFraction == expected.debtFraction);
        CHECK(provided.stalled == expected.stalled);
    }

    TEST_CASE("a replacement economy provider overrides only the settle output", "[modsdk][economyprovider]")
    {
        ResourceAccount account;
        account.stockpile = 10.0f;
        account.production = 5.0f;
        account.maxStorage = 1000.0f;

        DoublingProvider replacement;
        const auto nativeResult = NativeEconomyProvider().settle(account, 1.0f);
        const auto replaced = replacement.settle(account, 1.0f);
        CHECK(replaced.stockpile == nativeResult.stockpile * 2.0f);
        CHECK(replaced.produced == nativeResult.produced);
    }
}
