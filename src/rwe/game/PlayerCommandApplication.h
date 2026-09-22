#pragma once

#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
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
     */
    bool applyUnitCommandToSimulation(GameSimulation& simulation, const PlayerUnitCommand& unitCommand);
}
