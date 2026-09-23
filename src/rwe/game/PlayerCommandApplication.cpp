#include "PlayerCommandApplication.h"

#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>

namespace rwe
{
    void feedAiCommands(GameSimulation& simulation, PlayerCommandService& playerCommandService, unsigned int bufferDepth)
    {
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            PlayerId id(static_cast<unsigned int>(i));
            if (simulation.players[i].type != GamePlayerType::Computer)
            {
                continue;
            }

            auto bufferedCount = playerCommandService.bufferedCommandCount(id);
            if (bufferedCount <= bufferDepth)
            {
                auto aiCommands = simulation.takeAiCommandsForPlayer(id);
                playerCommandService.pushCommands(id, aiCommands);
                ++bufferedCount;
            }

            // A frame may dispatch several ticks -- catching up after a slow
            // one, or any game speed above 1x -- and each tick pops a set from
            // every player's buffer. One set queued would leave the second
            // tick of such a frame with an empty buffer, and the whole
            // simulation blocked on it.
            for (; bufferedCount < bufferDepth; ++bufferedCount)
            {
                playerCommandService.pushCommands(id, std::vector<PlayerCommand>());
            }
        }
    }

    namespace
    {
        void issueOrder(GameSimulation& simulation, UnitId unitId, const UnitOrder& order)
        {
            auto unit = simulation.tryGetUnitState(unitId);
            if (!unit)
            {
                return;
            }

            // Whatever it was doing (building, reclaiming) stops now, so the
            // arm is stowed and the nano spray ends; a later order to the same
            // target starts cleanly with StartBuilding.
            UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            unit->get().clearOrders();
            unit->get().addOrder(order);
        }

        void enqueueOrder(GameSimulation& simulation, UnitId unitId, const UnitOrder& order)
        {
            auto unit = simulation.tryGetUnitState(unitId);
            if (!unit)
            {
                return;
            }

            // An idle unit has nothing to queue behind, so this order starts
            // straight away -- which means an aircraft part-way through setting
            // down has to break off and get back in the air for it.
            if (unit->get().orders.empty())
            {
                UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            }
            unit->get().addOrder(order);
        }

        void cancelBuildOrderAt(GameSimulation& simulation, UnitId unitId, const SimVector& position)
        {
            auto unit = simulation.tryGetUnitState(unitId);
            if (!unit)
            {
                return;
            }
            auto cell = simulation.terrain.worldToHeightmapCoordinate(position);
            auto& orders = unit->get().orders;
            for (auto it = orders.begin(); it != orders.end(); ++it)
            {
                auto buildOrder = std::get_if<BuildOrder>(&*it);
                if (!buildOrder)
                {
                    continue;
                }
                const auto& definition = simulation.unitDefinitions.at(buildOrder->unitType);
                auto rect = simulation.computeFootprintRegion(buildOrder->position, definition.movementCollisionInfo);
                if (cell.x >= rect.x && cell.x < rect.x + static_cast<int>(rect.width) && cell.y >= rect.y && cell.y < rect.y + static_cast<int>(rect.height))
                {
                    // Only the plan is dropped; a building already started stays.
                    if (it == orders.begin() && std::holds_alternative<UnitBehaviorStateBuilding>(unit->get().behaviourState))
                    {
                        return;
                    }
                    orders.erase(it);
                    return;
                }
            }
        }
    }

    bool applyUnitCommandToSimulation(GameSimulation& simulation, const PlayerUnitCommand& unitCommand)
    {
        // A command is about half a second old by the time it lands -- it waits
        // out the command buffer like everybody else's -- and the unit it names
        // can have died in the meantime. Asked once here, of the simulation's
        // own state, so every peer drops the same command on the same tick.
        if (!simulation.unitExists(unitCommand.unit))
        {
            return false;
        }

        match(
            unitCommand.command,
            [&](const PlayerUnitCommand::IssueOrder& c) {
                switch (c.issueKind)
                {
                    case PlayerUnitCommand::IssueOrder::IssueKind::Immediate:
                        issueOrder(simulation, unitCommand.unit, c.order);
                        break;
                    case PlayerUnitCommand::IssueOrder::IssueKind::Queued:
                        enqueueOrder(simulation, unitCommand.unit, c.order);
                        break;
                }
            },
            [&](const PlayerUnitCommand::ModifyBuildQueue& c) {
                if (auto unit = simulation.tryGetUnitState(unitCommand.unit))
                {
                    unit->get().modifyBuildQueue(c.unitType, c.count);
                }
            },
            [&](const PlayerUnitCommand::ModifyStockpile& c) {
                simulation.modifyStockpileQueue(unitCommand.unit, c.count);
            },
            [&](const PlayerUnitCommand::Stop&) {
                if (auto unit = simulation.tryGetUnitState(unitCommand.unit))
                {
                    UnitBehaviorService(&simulation).interruptCurrentTask(unitCommand.unit);
                    unit->get().clearOrders();
                }
            },
            [&](const PlayerUnitCommand::SetFireOrders& c) {
                if (auto unit = simulation.tryGetUnitState(unitCommand.unit))
                {
                    unit->get().setFireOrders(c.orders);
                }
            },
            [&](const PlayerUnitCommand::SetMovementOrders& c) {
                if (simulation.tryGetUnitState(unitCommand.unit))
                {
                    simulation.setMoveOrders(unitCommand.unit, c.orders);
                }
            },
            [&](const PlayerUnitCommand::SetOnOff& c) {
                if (c.on)
                {
                    simulation.activateUnit(unitCommand.unit);
                }
                else
                {
                    simulation.deactivateUnit(unitCommand.unit);
                }
            },
            [&](const PlayerUnitCommand::SetCloak& c) {
                // This only records what the unit is asking for. Whether it
                // actually cloaks is settled a second at a time by the energy
                // and by how close the nearest enemy is standing.
                if (simulation.tryGetUnitState(unitCommand.unit))
                {
                    simulation.setCloakRequested(unitCommand.unit, c.cloaked);
                }
            },
            [&](const PlayerUnitCommand::CancelBuildOrder& c) {
                cancelBuildOrderAt(simulation, unitCommand.unit, c.position);
            },
            [&](const PlayerUnitCommand::SelfDestruct&) {
                // Starts the countdown, or cancels it if pressed again.
                simulation.toggleSelfDestruct(unitCommand.unit);
            });

        return true;
    }
}
