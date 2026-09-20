#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/ResourceSettler.h>

namespace rwe
{
    // These tests read the settle from its numbers alone. Nothing here builds a
    // GameSimulation, a unit or a map: if a case needs one of those to say what
    // it means, it belongs in economy.test.cpp instead.
    TEST_CASE("the handicap scales the income before it is tallied or spent", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.production = 50.0f;
        account.maxStorage = 1000.0f;

        auto r = settleResourceAccount(account, 2.0f);

        REQUIRE(r.produced == Catch::Approx(100.0f));
        REQUIRE(r.lifetimeProduced == Catch::Approx(100.0f));
        REQUIRE(r.stockpile == Catch::Approx(100.0f));
    }

    TEST_CASE("the storage cap throws away the overshoot and counts it", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.production = 100.0f;
        account.maxStorage = 50.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.stockpile == Catch::Approx(50.0f));
        REQUIRE(r.lifetimeExcess == Catch::Approx(50.0f));
        REQUIRE(r.lifetimeProduced == Catch::Approx(100.0f));
    }

    TEST_CASE("lifetime tallies accumulate rather than replace", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.production = 100.0f;
        account.maxStorage = 1000.0f;
        account.lifetimeProduced = 1000.0f;
        account.lifetimeExcess = 25.0f;
        account.stockpile = 900.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.lifetimeProduced == Catch::Approx(1100.0f));
        REQUIRE(r.lifetimeExcess == Catch::Approx(25.0f));
    }

    TEST_CASE("the pooled ask sizes the fraction but only the player's own becomes its debt", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.stockpile = 50.0f;
        account.maxStorage = 1000.0f;
        account.ownRequest = 10.0f;
        account.pooledRequest = 110.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.requestFraction == Catch::Approx(50.0f / 110.0f));
        REQUIRE(r.stockpile == Catch::Approx(0.0f));
        REQUIRE(r.debt == Catch::Approx(10.0f * (1.0f - 50.0f / 110.0f)));
    }

    TEST_CASE("a debt the settle pays in full clears to exactly zero", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.stockpile = 1000.0f;
        account.maxStorage = 1000.0f;
        account.ownDebt = 30.0f;
        account.ownRequest = 30.0f;
        account.pooledDebt = 30.0f;
        account.pooledRequest = 30.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.debtFraction == 1.0f);
        REQUIRE(r.requestFraction == 1.0f);
        REQUIRE_FALSE(r.stalled);
        REQUIRE(r.debt == 0.0f);
    }

    TEST_CASE("debt is paid before anything new, and a shortfall starves the request", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.stockpile = 25.0f;
        account.maxStorage = 1000.0f;
        account.ownDebt = 100.0f;
        account.ownRequest = 50.0f;
        account.pooledDebt = 100.0f;
        account.pooledRequest = 50.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.debtFraction == Catch::Approx(0.25f));
        REQUIRE(r.requestFraction == Catch::Approx(0.0f));
        REQUIRE(r.stalled);
        REQUIRE(r.stockpile == Catch::Approx(0.0f));
        // The whole of the player's own ask goes unpaid, and three quarters of
        // its debt, so both carry forward: 50 * 1 + 100 * 0.75.
        REQUIRE(r.debt == Catch::Approx(125.0f));
    }

    TEST_CASE("an idle account is not stalled and keeps its stockpile", "[economy][resourcesettler]")
    {
        ResourceAccount account{};
        account.stockpile = 500.0f;
        account.maxStorage = 1000.0f;

        auto r = settleResourceAccount(account, 1.0f);

        REQUIRE(r.stockpile == Catch::Approx(500.0f));
        REQUIRE_FALSE(r.stalled);
        REQUIRE(r.debt == 0.0f);
    }
}
