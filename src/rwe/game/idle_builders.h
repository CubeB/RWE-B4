#pragma once

#include <cstddef>
#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>
#include <vector>

namespace rwe
{
    class GameSimulation;

    /**
     * The named player's own construction units that have nothing to do right
     * now, in id order.
     *
     * The order is stated rather than inherited. `simulation.units` is a slot
     * vector with a free list, and an id carries its slot in the high bits and
     * a generation in the low ones, so walking the map already comes out in id
     * order -- by the accident of that layout rather than by anything the map
     * promises. The sign walks a rotation over this list, so the order is
     * sorted here explicitly. It sorts the vector it has just built and hands
     * that back; the simulation is not touched.
     *
     * This is the whole of what the side panel's idle-builder sign reports, and
     * it is deliberately a *read*. Every field it looks at -- the unit's owner,
     * its order queue, the `builder` and `isMobile` on its definition -- is
     * state the simulation already hashes, saves and dumps, so the count on the
     * HUD is a fact about the simulation rather than a new piece of it, and
     * nothing here is ever written back. A frame that asks this question
     * changes no unit, and a HUD widget that is not on screen changes none
     * either. See UiIdleBuilderSign.
     *
     * The test for idle is the AI's own (EconomyManager): a builder with an
     * empty order queue. An immobile builder is left out, because a factory
     * that is not building anything is not something a player can send
     * anywhere, and a commander is left in, because a commander with nothing
     * to do is exactly what a player is looking for.
     */
    std::vector<UnitId> playerIdleBuilders(const GameSimulation& simulation, PlayerId playerId);

    /**
     * Which entry of `builders` a click on the sign moves to, given the unit
     * the last click landed on.
     *
     * A rotation, so a second click reaches a second builder: the entry after
     * the one last shown, wrapping round at the end. The first click has no
     * previous entry and starts at the front, and so does any click after the
     * unit last shown has stopped being idle -- a builder that picked up work
     * or died is no longer in the list, and the rotation restarts rather than
     * resuming at a stale index that now names a different unit.
     *
     * With nothing idle there is no entry to move to and 0 is returned, which
     * the caller must not index; the sign is disabled in that case anyway.
     */
    std::size_t nextIdleBuilderIndex(const std::vector<UnitId>& builders, const std::optional<UnitId>& lastSelected);
}
