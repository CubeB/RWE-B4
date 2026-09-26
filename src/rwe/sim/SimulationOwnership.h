#pragma once

#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * Whether this machine decides the player's units.
     *
     * The gate for every system that makes a decision for a player rather than
     * applying one: orders, weapon fire, local damage, the economy settle and
     * the AI. It is not hash state, so it never moves a lockstep sync hash.
     */
    bool simulatesLocally(const GameSimulation& sim, PlayerId player);

    /** Whether any player's units are decided elsewhere, which a save cannot hold. */
    bool hasRemotePlayer(const GameSimulation& sim);

    /**
     * Applies damage recorded from the owner of a Remote player's unit.
     *
     * The local damage path refuses a Remote victim, so this is how the stream
     * takes health off one; it kills the unit when the damage exceeds what is
     * left, matching applyDamage's own health effect.
     */
    void applyRemoteDamage(GameSimulation& sim, UnitId unitId, unsigned int damagePoints);
}
