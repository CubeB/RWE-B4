#include "ResourceSettler.h"

namespace rwe
{
    ResourceSettlement settleResourcePool(float supply, float debt, float requested)
    {
        // The original never reaches here with a negative supply, because the
        // stockpile it feeds in is the previous second's remainder and that is
        // floored at zero. Say so explicitly rather than divide by a debt of
        // nothing on the way to finding out.
        if (supply < 0.0f)
        {
            supply = 0.0f;
        }

        ResourceSettlement result{};

        float afterDebt;
        if (debt <= supply)
        {
            result.debtFraction = 1.0f;
            afterDebt = supply - debt;
        }
        else
        {
            result.debtFraction = supply / debt;
            afterDebt = 0.0f;
        }

        if (requested <= afterDebt)
        {
            result.requestFraction = 1.0f;
            result.remaining = afterDebt - requested;
        }
        else
        {
            result.requestFraction = afterDebt / requested;
            result.remaining = 0.0f;
        }

        result.stalled = result.debtFraction < 1.0f || result.requestFraction < 1.0f;
        return result;
    }

    SettledResourceAccount settleResourceAccount(const ResourceAccount& account, float incomeMultiplier)
    {
        SettledResourceAccount result{};

        // The handicap is applied to the income before it is tallied or spent,
        // so a cheating computer player's chart figure and the money it
        // actually has to spend are the same number.
        result.produced = account.production * incomeMultiplier;
        result.lifetimeProduced = account.lifetimeProduced + result.produced;

        auto settlement = settleResourcePool(
            account.stockpile + result.produced,
            account.pooledDebt,
            account.pooledRequest);

        result.stockpile = settlement.remaining;
        result.requestFraction = settlement.requestFraction;
        result.debtFraction = settlement.debtFraction;
        result.stalled = settlement.stalled;

        // "Excess" is what the cap takes off the top here. A player whose
        // storage is full is throwing its whole income away, and the chart is
        // where that shows.
        if (result.stockpile > account.maxStorage)
        {
            result.lifetimeExcess = account.lifetimeExcess + (result.stockpile - account.maxStorage);
            result.stockpile = account.maxStorage;
        }
        else
        {
            result.lifetimeExcess = account.lifetimeExcess;
        }

        // Only the player's own block rolls its debt; the pooled figures were
        // there to size the fractions, and the units each carry their own.
        result.debt = account.ownRequest * (1.0f - settlement.requestFraction) + account.ownDebt * (1.0f - settlement.debtFraction);

        return result;
    }
}
