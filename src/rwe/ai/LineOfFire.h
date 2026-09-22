#pragma once

#include <optional>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    struct GameSimulation;
    class UnitState;

    /**
     * Where a shot leaves a unit and where it has to arrive, measured up
     * from the feet. Neither end of a shot is at ground level: a weapon sits
     * somewhere up the model and a hull is a target along its whole height.
     * One number for both ends, because the alternative is reading a firing
     * piece out of the COB script for a question that only wants to know
     * whether a ridge is in the way.
     */
    constexpr SimScalar MuzzleHeightAboveFeet = 24_ss;

    /**
     * Whether a straight shot from `from` to `to` passes over the ground
     * between them rather than into it.
     *
     * There is no line-of-fire test anywhere in the simulation, and the
     * original has none either -- a unit will happily empty itself into a
     * rock, and RWE matches it (TOTALA-EXE.md and S:16.5 of the AI
     * architecture note). That is the simulation's business and it is not
     * changed here. This is the computer player asking a question a human
     * player answers by looking at the screen: are my shots arriving, or am
     * I firing into a hillside?
     *
     * Deterministic: SimScalar throughout, a fixed sample count, and no
     * floating-point state carried between calls. It runs inside
     * GameSimulation::tick like the rest of the AI, so it has to be.
     */
    bool shotClearsTerrain(const GameSimulation& sim, const SimVector& from, const SimVector& to);

    /**
     * Whether `unit` firing at `target` from where it stands would put its
     * round into the ground.
     *
     * False for a unit whose reaching weapon lobs -- a ballistic shell or a
     * bomb goes over a ridge by design, and asking a straight line about it
     * would answer a question nobody posed. False, too, for an unarmed unit
     * and for anything in the air, which has no ridge to be behind.
     */
    bool terrainBlocksShot(const GameSimulation& sim, const UnitState& unit, const UnitState& target);

    /**
     * Where to stand to get the shot, when the ground is in the way.
     *
     * Walks in from the unit towards the target and returns the first place
     * along that line with a clear shot -- the least ground given up for a
     * round that arrives. Nothing when the unit can already hit it from
     * where it stands. The target's own position when no point on the line
     * works at all, which on a maze map is the right answer and not a
     * surrender: the straight line runs into a cliff, so the pathfinder is
     * asked for a route round it instead of the unit standing still.
     */
    std::optional<SimVector> positionForClearShot(const GameSimulation& sim, const UnitState& unit, const UnitState& target);
}
