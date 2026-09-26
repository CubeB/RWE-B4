#pragma once

#include <rwe/game/PlayerCommand.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    /**
     * Takes what every computer player asked for on the tick just run and
     * queues it, keeping each one's buffer topped up to `bufferDepth`.
     *
     * **Call this once per simulation tick and never once per frame.** The
     * buffer is drained a set per player per tick, so the depth a set is
     * pushed at is the number of ticks before the simulation sees it -- and if
     * the feeding runs per frame while the draining runs per tick, that number
     * becomes a function of how many ticks the last frame happened to
     * dispatch. On one machine that is merely untidy. Between two peers of a
     * network game it is a desync: each peer runs the same AI over the same
     * simulation and gets the same commands, then applies them on different
     * ticks, and the sync hashes part company with nothing whatever wrong with
     * the AI. See ROADMAP.md, Phase 2.
     *
     * The depth is deliberately not the human players' RTT-derived one for the
     * same reason: it must be a constant every peer agrees on, and a round trip
     * time is neither. A computer player's orders never cross the network --
     * every peer's own copy of the AI issues them -- so there is no latency
     * here to cover for. What the depth buys is the delay between the AI
     * deciding and the simulation acting, which is part of how the AI plays and
     * so is held where it was.
     */
    void feedAiCommands(GameSimulation& simulation, PlayerCommandService& playerCommandService, unsigned int bufferDepth);

    /**
     * The simulation half of a unit command: orders, build queues, stockpile,
     * on/off, cloak, self-destruct. GameScene adds the interface bookkeeping
     * that hangs off the same command (the build-button totals, the fire-order
     * toggle); the headless arena has no interface and calls this directly, so
     * the two paths share one definition rather than drifting apart.
     *
     * Returns false if the unit the command names no longer exists, which every
     * caller must drop the command on -- including, and especially, the
     * interface side, which would otherwise refresh a panel for a dead unit.
     *
     * Also false, and nothing applied, for a command the issuing player may
     * not give: one to a unit it does not own, one naming a unit type the game
     * has no definition for, or a queue change out of range. `issuingPlayer`
     * is where the command came from -- the network endpoint, the computer
     * player, the recording -- never anything the command claims. Issue #75.
     */
    bool applyUnitCommandToSimulation(GameSimulation& simulation, PlayerId issuingPlayer, const PlayerUnitCommand& unitCommand);
}
