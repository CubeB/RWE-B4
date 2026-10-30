#pragma once

#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * Applies one resource-sharing command from `issuer`: a toggle of its
     * metal or energy sharing flag, a change of its reserve, a gift to
     * another player, or a Community Patch patrol reclaim threshold.
     * TOTALA-EXE-COMMANDS.md section 124; the thresholds are TADR 2026.10.6,
     * "Patrol reclaim thresholds".
     *
     * This is the one definition of what those commands do. The live game,
     * the replay player and the headless arena all call it, so they cannot
     * drift apart the way three hand-written copies would.
     *
     * Returns whether anything was applied. A command is refused (and
     * nothing changes) when:
     *
     *  - the issuer is not a player of this game, or is dead (a gift only;
     *    a dead player's toggles harmlessly flip, as nothing reads them);
     *  - a gift names no player, the issuer itself, or a player that does not
     *    exist;
     *  - a gift is negative, zero after capping, or not a finite number;
     *  - a reserve is not a finite number;
     *  - the issuer or a gift's recipient is not simulated on this machine,
     *    because a Remote player's account belongs to the machine that owns
     *    its units;
     *  - a patrol reclaim threshold arrives while communityPatchMode is off,
     *    or is not a finite number.
     *
     * Two deliberate departures from the original, both hardenings:
     *
     *  - **A negative gift is ignored.** The original does not clamp the
     *    amount, so a negative one reverses the transfer and takes the
     *    recipient's stock; there it is a peer's own client typing it, here a
     *    peer's packet can say anything and a gift is not a way to reach into
     *    another player's account.
     *  - **A gift to or from a Remote player is not applied**, where the
     *    original would send a packet; RWE does not speak that packet.
     *
     * Nothing here consults the alliance table: a gift is to anyone, as in the
     * original. Only the automatic pass is for allies.
     *
     * A gift debits the donor as a spend and credits the receiver as income,
     * through the player's own request and production buffers, so it settles
     * with the next economy second like any reclaim credit. It does not call
     * recordDesire: a gift is not consumption and must not inflate the
     * consumption statistics. A computer-player receiver's handicap is
     * applied by the settle itself, which multiplies the whole production
     * buffer by `resourceBonusFor`, so the credit is not scaled a second time
     * here. (The automatic pass is different, per BT-27: it takes the amount
     * out of the donor's stockpile at once.)
     */
    bool applyPlayerShareCommand(GameSimulation& sim, PlayerId issuer, const PlayerShareCommand& command);

    /**
     * The automatic sharing pass. Called from GameSimulation::tick as the last
     * thing in the tick (BT-27), so sizes are taken on the balances the tick
     * leaves and the gift is paid out by the next settle; does nothing unless
     * the tick is a multiple of sixty (every two seconds) and the economy is
     * not frozen by a mission.
     *
     * For each player in index order that this machine simulates, that is
     * alive, human and has a sharing flag set, and for metal and then energy
     * independently: if the player holds more than its reserve, find the
     * recipient and offer it one third (metal) or one half (energy) of the
     * surplus, capped at the recipient's free storage. A reserve set with a
     * trailing `%` is that percentage of current storage and tracks it; an
     * absolute reserve stays absolute.
     *
     * The recipient is the LAST player, in index order, that is alive, human,
     * simulated here, allied to the donor and holds less of the resource than
     * the donor. There is no comparison between candidates, so the poorest
     * ally is not what is chosen; section 124 is explicit that this is what
     * the original does, and a later slot with no room starves an earlier one
     * with plenty. A computer player is never a recipient.
     */
    void runAutomaticSharing(GameSimulation& sim);
}
