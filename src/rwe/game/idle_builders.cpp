#include "idle_builders.h"

#include <algorithm>
#include <iterator>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    std::vector<UnitId> playerIdleBuilders(const GameSimulation& simulation, PlayerId playerId)
    {
        std::vector<UnitId> builders;

        for (const auto& [unitId, unit] : simulation.units)
        {
            // The same three the selection helpers skip, and for the same
            // reason: a unit a mission script holds is not the player's to
            // count or to send anywhere (0x48BF30), and a dead one is not idle,
            // it is gone.
            if (!unit.isAlive() || !unit.isOwnedBy(playerId) || unit.heldByMission)
            {
                continue;
            }

            // A unit whose type this simulation has no definition for cannot
            // be a builder, and asking anyway would throw -- once a frame, into
            // the middle of drawing the HUD. selectAllWhere can afford .at()
            // because a keypress is rare; this cannot.
            auto definition = simulation.unitDefinitions.find(unit.unitType);
            if (definition == simulation.unitDefinitions.end())
            {
                continue;
            }

            if (!definition->second.builder || !definition->second.isMobile)
            {
                continue;
            }

            if (!unit.orders.empty())
            {
                continue;
            }

            builders.push_back(unitId);
        }

        // Sorted by id. simulation.units is a slot vector with a free list, and
        // an id is the slot number in its high bits with a generation in its
        // low ones, so walking the map in slot order does already come out in
        // id order -- by the accident of that layout rather than by anything
        // the map promises. The rotation the sign walks should not depend on
        // an accident, so the order is stated here instead of inferred. This
        // sorts the vector this function has just built and hands that back;
        // the simulation is not touched.
        std::sort(builders.begin(), builders.end());

        return builders;
    }

    std::size_t nextIdleBuilderIndex(const std::vector<UnitId>& builders, const std::optional<UnitId>& lastSelected)
    {
        if (builders.empty())
        {
            return 0;
        }

        if (!lastSelected)
        {
            return 0;
        }

        auto it = std::find(builders.begin(), builders.end(), *lastSelected);
        if (it == builders.end())
        {
            // The unit last shown is no longer idle, so there is no position in
            // the rotation to carry on from.
            return 0;
        }

        return (static_cast<std::size_t>(std::distance(builders.begin(), it)) + 1) % builders.size();
    }
}
