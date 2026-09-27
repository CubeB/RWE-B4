#include "PlayerCommandApplication.h"

#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
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

            // A patrol keeps a move the unit is walking as the route's first
            // waypoint instead of replacing the queue, so a move then a patrol
            // patrols between the two points. UnitState::addOrder turns the
            // move into a patrol node. Every other order replaces the queue.
            std::optional<MoveOrder> move;
            if (std::holds_alternative<PatrolOrder>(order) && !unit->get().orders.empty())
            {
                if (auto m = std::get_if<MoveOrder>(&unit->get().orders.back()))
                {
                    move = *m;
                }
            }

            unit->get().clearOrders();
            if (move)
            {
                unit->get().addOrder(*move);
            }
            unit->get().addOrder(order);
            if (!move && std::holds_alternative<PatrolOrder>(order))
            {
                // A fresh patrol loops between the clicked point and where the
                // unit stands now. One waypoint alone would park it.
                unit->get().addOrder(PatrolOrder(unit->get().position));
            }
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
            auto wasIdle = unit->get().orders.empty();
            if (wasIdle)
            {
                UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            }
            unit->get().addOrder(order);
            if (wasIdle && std::holds_alternative<PatrolOrder>(order))
            {
                // A patrol queued on an idle unit is a fresh patrol, as in
                // issueOrder: it loops between the point and where the unit
                // stands, since one waypoint alone would park it.
                unit->get().addOrder(PatrolOrder(unit->get().position));
            }
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

    namespace
    {
        /**
         * The largest single change to a build queue or a stockpile a command
         * may ask for. The interface's own steps are one, five and twenty; this
         * is only here so that a count from the wire cannot overflow the
         * queue's arithmetic.
         */
        constexpr int MaxQueueChange = 1000;

        bool isKnownUnitType(const GameSimulation& simulation, const std::string& unitType)
        {
            return simulation.unitDefinitions.find(unitType) != simulation.unitDefinitions.end();
        }

        /** Off the mission's unit list (issue #381); the original has no such unit to build. */
        bool isExcludedByMission(const GameSimulation& simulation, const std::string& unitType)
        {
            auto it = simulation.unitDefinitions.find(unitType);
            return it != simulation.unitDefinitions.end() && it->second.excludedByMission;
        }

        /**
         * Why a command has to be refused before any of it reaches the
         * simulation, or nothing if it may go ahead.
         *
         * Every peer runs this over the same command and the same state, so
         * a refusal is the same refusal everywhere and cannot desync. Nothing
         * an honest interface sends is refused. Issue #75.
         */
        const char* refusalOf(const GameSimulation& simulation, PlayerId issuingPlayer, const PlayerUnitCommand& unitCommand)
        {
            // Only a unit's owner commands it. The issuing player is where the
            // command came from -- the network endpoint, the computer player
            // or the recording -- and never anything the command says of
            // itself. Without this any peer could self-destruct, stop or walk
            // off every unit in the game.
            if (simulation.getUnitState(unitCommand.unit).owner != issuingPlayer)
            {
                return "the unit belongs to another player";
            }

            // A unit type the game has no definition for reaches
            // unitDefinitions.at() on every peer the tick the order runs, and
            // in the renderer before that.
            if (const auto* issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand.command))
            {
                if (const auto* build = std::get_if<BuildOrder>(&issue->order); build != nullptr && !isKnownUnitType(simulation, build->unitType))
                {
                    return "it names an unknown unit type";
                }
                if (const auto* build = std::get_if<BuildOrder>(&issue->order); build != nullptr && isExcludedByMission(simulation, build->unitType))
                {
                    return "the mission does not offer that unit";
                }
            }
            if (const auto* queue = std::get_if<PlayerUnitCommand::ModifyBuildQueue>(&unitCommand.command))
            {
                if (!isKnownUnitType(simulation, queue->unitType))
                {
                    return "it names an unknown unit type";
                }
                if (queue->count > 0 && isExcludedByMission(simulation, queue->unitType))
                {
                    return "the mission does not offer that unit";
                }
                if (queue->count < -MaxQueueChange || queue->count > MaxQueueChange)
                {
                    return "the count is out of range";
                }
            }
            if (const auto* stockpile = std::get_if<PlayerUnitCommand::ModifyStockpile>(&unitCommand.command))
            {
                if (stockpile->count < -MaxQueueChange || stockpile->count > MaxQueueChange)
                {
                    return "the count is out of range";
                }
            }
            return nullptr;
        }
    }

    bool applyUnitCommandToSimulation(GameSimulation& simulation, PlayerId issuingPlayer, const PlayerUnitCommand& receivedCommand)
    {
        // A command is about half a second old by the time it lands -- it waits
        // out the command buffer like everybody else's -- and the unit it names
        // can have died in the meantime. Asked once here, of the simulation's
        // own state, so every peer drops the same command on the same tick.
        if (!simulation.unitExists(receivedCommand.unit))
        {
            return false;
        }

        if (const auto* refusal = refusalOf(simulation, issuingPlayer, receivedCommand))
        {
            LOG_WARN << "Refusing a command from player " << issuingPlayer.value << " to unit " << receivedCommand.unit.value << ": " << refusal;
            return false;
        }

        // A mission unit its script still holds takes no orders from anyone:
        // no selection reaches it (0x487E69 cleared the bit every selection
        // path tests), and the computer's AI does not take it on (0x408830).
        // Refused here, on every peer alike, whoever sent it.
        if (simulation.getUnitState(receivedCommand.unit).heldByMission)
        {
            return false;
        }

        // An order starts from nothing. A resurrection keeps its progress on
        // the order, as the original does, and the wire can carry it because
        // the same message type is used where progress is real; taken from a
        // command, it would let a peer finish one in a tick.
        auto unitCommand = receivedCommand;
        if (auto* issue = std::get_if<PlayerUnitCommand::IssueOrder>(&unitCommand.command))
        {
            if (auto* resurrect = std::get_if<ResurrectOrder>(&issue->order))
            {
                resurrect->remainingTicks.reset();
            }
        }

        match(
            unitCommand.command,
            [&](const PlayerUnitCommand::IssueOrder& c) {
                switch (c.issueKind)
                {
                    case PlayerUnitCommand::IssueOrder::IssueKind::Immediate:
                        // An order that replaces the unit's own replaces what
                        // is left of its mission list with them: in the
                        // original the list is the unit's order list.
                        if (simulation.missionScripts)
                        {
                            simulation.missionScripts->scripts.erase(unitCommand.unit.value);
                        }
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
                // Stop empties the unit's list in the original, and what is
                // left of a mission list is part of it.
                if (simulation.missionScripts)
                {
                    simulation.missionScripts->scripts.erase(unitCommand.unit.value);
                }
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
