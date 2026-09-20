#pragma once

namespace rwe
{
    /**
     * The streaming economy's arithmetic, on its own. Nothing here knows about
     * units, map data or the simulation: the whole interface is numbers in and
     * numbers out, which is what lets the once-a-second settle be read and
     * tested without building a game around it.
     *
     * See TOTALA-EXE.md section 23 for the throttle and section 111 for the
     * settle re-read: one phase for every player, on the same tick, with the
     * fractions the same for every consumer of a resource.
     */

    /** What one second's settle decided for one resource. */
    struct ResourceSettlement
    {
        /** The share of this second's requests that could be paid, 0 to 1. */
        float requestFraction;
        /** The share of the debt carried in from earlier seconds that could be paid, 0 to 1. */
        float debtFraction;
        /** What is left in the stockpile once both have been paid. */
        float remaining;
        /** True when either share fell short of the whole. */
        bool stalled;
    };

    /**
     * Divides a second's supply between what is already owed and what has been
     * asked for since. Debt is paid first and in preference: if it cannot be
     * paid in full then nothing new gets anything at all this second. Whatever
     * fraction comes back is the same for every consumer of that resource, so a
     * player who can afford two thirds of its outgoings has every builder,
     * every metal maker and every cloak working at two thirds rather than a
     * lucky two thirds of them working and the rest stopped.
     *
     * This is TotalA.exe 0x401A4D, run once for energy and once for metal.
     */
    ResourceSettlement settleResourcePool(float supply, float debt, float requested);

    /**
     * One player's position in one resource going into a settle: its own
     * stockpile and block, the storage this second rebuilt, the lifetime
     * tallies, and the same two figures pooled over every unit it owns. The
     * pooled figures decide the fractions -- what the whole player is asking
     * for -- while only the player's own block rolls its debt forward; each
     * unit rolls its own against the fractions that come back.
     */
    struct ResourceAccount
    {
        /** The stockpile carried out of last second (player+0x8C / +0x98). */
        float stockpile{0.0f};
        /** This second's income, before the computer player's handicap. */
        float production{0.0f};
        /** Everything ever earned, for the end-of-game chart. */
        float lifetimeProduced{0.0f};
        /** Everything ever thrown away at the storage cap. */
        float lifetimeExcess{0.0f};
        /** The storage cap this same settle rebuilt. */
        float maxStorage{0.0f};

        /** The player's own block, which is the one that rolls its debt forward. */
        float ownDebt{0.0f};
        float ownRequest{0.0f};

        /**
         * The player's own block and every owned unit's, pooled. The caller
         * accumulates these in the original's order -- player first, then each
         * unit -- so the sum is bit for bit the one the original adds up, and
         * it is only ever used to size the fractions.
         */
        float pooledDebt{0.0f};
        float pooledRequest{0.0f};
    };

    /** What one player's settle decided for one resource. */
    struct SettledResourceAccount
    {
        /** The stockpile after the debt, the requests and the cap. */
        float stockpile{0.0f};
        /** This second's income after the handicap, which is what the display copy shows. */
        float produced{0.0f};
        /** The lifetime income, with `produced` added. */
        float lifetimeProduced{0.0f};
        /** The lifetime waste, with this second's overshoot added. */
        float lifetimeExcess{0.0f};
        /** The player's own debt carried forward. */
        float debt{0.0f};

        float requestFraction{1.0f};
        float debtFraction{1.0f};
        bool stalled{false};
    };

    /**
     * One resource's whole once-a-second settle for one player, 0x401360 steps
     * 4 through 7 plus the display tallies: apply the handicap to the income,
     * add the pooled requests to the player's own, run the throttle, clamp the
     * leftover to storage and count the overshoot, then roll the player's own
     * debt forward against its own ask. `incomeMultiplier` is the computer
     * player's production handicap, 1 for everyone else.
     */
    SettledResourceAccount settleResourceAccount(const ResourceAccount& account, float incomeMultiplier);
}
