#include "PlayerCommandApplication.h"

#include <algorithm>
#include <rwe/game/OrderDispatch.h>
#include <rwe/game/ReplayFile.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>

namespace rwe
{
    void applyPlayerDropToSimulation(GameSimulation& simulation, const PlayerDroppedCommand& command, unsigned int sceneTick)
    {
        auto& player = simulation.getPlayer(command.player);
        if (player.status == GamePlayerStatus::Alive && !player.removeUnitsAt)
        {
            // sceneTick names the tick about to run; gameTime still names the
            // completed tick. A loaded game's two clocks have different bases.
            auto delay = command.fromTick > sceneTick ? command.fromTick - sceneTick : 0u;
            player.removeUnitsAt = simulation.gameTime + GameTime(delay + 1u);
        }
    }

    void feedReplayCommands(GameSimulation& simulation, PlayerCommandService& playerCommandService,
        const Replay& replay, unsigned int tick, bool rejoining)
    {
        auto tickIt = replay.commands.find(tick);
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            // A rejoining peer runs the same controllers from the beginning.
            // Feeding their recorded output instead leaves their generated
            // orders undrained and loses the seven-tick live command pipeline.
            if (rejoining && simulation.players[i].type == GamePlayerType::Computer)
            {
                continue;
            }
            if (!playerCommandService.needsCommandsForTick(PlayerId(i), tick + 1))
            {
                continue;
            }
            std::vector<PlayerCommand> commands;
            if (tickIt != replay.commands.end())
            {
                auto playerIt = tickIt->second.find(static_cast<unsigned int>(i));
                if (playerIt != tickIt->second.end())
                {
                    commands = playerIt->second;
                }
            }
            // Recorded pauses and speed changes must not control playback.
            commands.erase(
                std::remove_if(commands.begin(), commands.end(), [](const PlayerCommand& c) {
                    return std::holds_alternative<PlayerPauseGameCommand>(c)
                        || std::holds_alternative<PlayerUnpauseGameCommand>(c)
                        || std::holds_alternative<PlayerSetGameSpeedCommand>(c);
                }),
                commands.end());
            playerCommandService.pushCommands(PlayerId(i), commands);
        }
        if (rejoining)
        {
            feedAiCommands(simulation, playerCommandService, aiCommandBufferDepth());
        }
    }

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

                // Resource sharing is a human's preference (the automatic
                // pass skips computer players altogether), and the AI has no
                // business toggling it. Dropped here, before it reaches a
                // command set, so that every peer drops the same ones.
                aiCommands.erase(
                    std::remove_if(aiCommands.begin(), aiCommands.end(), [](const PlayerCommand& c) {
                        return std::holds_alternative<PlayerShareCommand>(c);
                    }),
                    aiCommands.end());

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

        /**
         * The queued-order toggle's "same order" test (0x43AFC0). A feature
         * reclaim has a position and no unit target, so it matches on type
         * and the sixteen-unit box around the feature, and two features
         * within sixteen units are one order (BT-86). RWE's order holds the
         * feature rather than the clicked point, so the box is taken between
         * the two features' positions. Everything else goes through
         * ordersMatchForQueueToggle.
         */
        bool queuedOrdersMatch(const GameSimulation& simulation, const UnitOrder& existing, const UnitOrder& requested)
        {
            const auto* oldReclaim = std::get_if<ReclaimOrder>(&existing);
            const auto* newReclaim = std::get_if<ReclaimOrder>(&requested);
            if (oldReclaim != nullptr && newReclaim != nullptr)
            {
                const auto* oldFeature = std::get_if<FeatureId>(&oldReclaim->target);
                const auto* newFeature = std::get_if<FeatureId>(&newReclaim->target);
                if (oldFeature != nullptr && newFeature != nullptr)
                {
                    auto a = simulation.tryGetFeature(*oldFeature);
                    auto b = simulation.tryGetFeature(*newFeature);
                    if (a && b)
                    {
                        return queuePositionsMatch(a->get().position, b->get().position);
                    }
                    return *oldFeature == *newFeature;
                }
            }
            return ordersMatchForQueueToggle(existing, requested);
        }

        void enqueueOrder(GameSimulation& simulation, UnitId unitId, const UnitOrder& order)
        {
            auto unit = simulation.tryGetUnitState(unitId);
            if (!unit)
            {
                return;
            }

            // TOTALA-EXE-INTERFACE S:136: remove the first matching queued
            // mission, interrupting the active task only when it is the head.
            auto& orders = unit->get().orders;
            auto found = std::find_if(orders.begin(), orders.end(), [&](const UnitOrder& old) {
                return queuedOrdersMatch(simulation, old, order);
            });
            if (found != orders.end())
            {
                if (found == orders.begin())
                {
                    UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
                    unit->get().buildOrderUnitId.reset();
                }
                orders.erase(found);
                return;
            }

            // BT-79: a new primary order, a Shift-queued one included, removes
            // the leading run of nodes the unit made for itself from idle
            // (the loop at 0x43AE7C in AddOrder). Engagements made in front
            // of a real order are unmarked and stay.
            auto isAutomatic = [](const UnitOrder& o) {
                if (const auto* attack = std::get_if<AttackOrder>(&o))
                {
                    return attack->automatic;
                }
                if (const auto* move = std::get_if<MoveOrder>(&o))
                {
                    return move->automatic;
                }
                return false;
            };
            if (!orders.empty() && isAutomatic(orders.front()))
            {
                UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
                unit->get().buildOrderUnitId.reset();
                while (!orders.empty() && isAutomatic(orders.front()))
                {
                    orders.pop_front();
                }
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

                // A repair or help-build is accepted where the order is chosen
                // (0x43F0E0), and the water reach test is part of that choice:
                // a flier that is not amphibious cannot be sent to a unit whose
                // top is under the surface, and a ground or sea repairer not to
                // one deeper than it can wade (BT-13). A refusal leaves the
                // queue alone.
                // The attack arm can answer "no order" (0x43F154): a unit that
                // cannot carry the attack out keeps its queue. The interface
                // asks the same question before it sends the command; the
                // rule belongs here too, so that the computer player and every
                // peer see the same refusal (BT-63).
                if (const auto* attack = std::get_if<AttackOrder>(&issue->order); attack != nullptr)
                {
                    const auto& attacker = simulation.getUnitState(unitCommand.unit);
                    if (const auto* targetUnit = std::get_if<UnitId>(&attack->target))
                    {
                        auto target = simulation.tryGetUnitState(*targetUnit);
                        if (target && !attackOrderIsAccepted(simulation, simulation.unitDefinitions.at(attacker.unitType), attacker, target->get()))
                        {
                            return "that unit cannot carry out the attack";
                        }
                    }
                    else if (!attackGroundOrderIsAccepted(simulation, attacker))
                    {
                        return "that unit cannot carry out the attack";
                    }
                }

                if (const auto* repair = std::get_if<RepairOrder>(&issue->order); repair != nullptr)
                {
                    auto target = simulation.tryGetUnitState(repair->target);
                    const auto& repairer = simulation.getUnitState(unitCommand.unit);
                    if (target && !repairWaterReachAllows(simulation, simulation.unitDefinitions.at(repairer.unitType), target->get()))
                    {
                        return "the target is under the water beyond that repairer's reach";
                    }
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
                    auto& queue = unit->get().buildQueue;
                    bool cancelActive = false;
                    if (c.count < 0 && !queue.empty() && queue.front().first == c.unitType)
                    {
                        // S:137 removes from the back. Only deletion of the
                        // active node cancels the product (S:134).
                        cancelActive = -c.count >= unit->get().getBuildQueueTotal(c.unitType);
                    }
                    unit->get().modifyBuildQueue(c.unitType, c.count);
                    if (cancelActive)
                    {
                        UnitBehaviorService(&simulation).clearBuild(simulation.getUnitInfo(unitCommand.unit));
                    }
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
            },
            [&](const PlayerUnitCommand::SetBuildQueueRepeat& c) {
                // Off is restored exactly, so a factory nobody has toggled
                // behaves as it always did. The queue itself is left alone
                // either way: the toggle only decides what happens to an
                // entry once it has been worked off, and a player who turns
                // it off gets the drain-the-queue behaviour back mid-queue
                // rather than losing what is already in it.
                if (auto unit = simulation.tryGetUnitState(unitCommand.unit))
                {
                    unit->get().repeatBuildQueue = c.repeat;
                }
            });

        return true;
    }
}
