#include "TransportManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        using IssueKind = PlayerUnitCommand::IssueOrder::IssueKind;

        PlayerCommand loadCommand(UnitId transport, UnitId passenger, IssueKind kind)
        {
            return PlayerUnitCommand(transport, PlayerUnitCommand::IssueOrder(LoadOrder(passenger), kind));
        }

        PlayerCommand unloadCommand(UnitId transport, const SimVector& at, IssueKind kind)
        {
            return PlayerUnitCommand(transport, PlayerUnitCommand::IssueOrder(UnloadOrder(at), kind));
        }

        PlayerCommand stopCommand(UnitId unit)
        {
            return PlayerUnitCommand(unit, PlayerUnitCommand::Stop());
        }

        bool isAliveUnit(const GameSimulation& sim, UnitId id)
        {
            auto unit = sim.tryGetUnitState(id);
            return unit && unit->get().isAlive();
        }

        // How often to look for a patch across the water.
        const unsigned int ExpansionSiteRefreshTicks = 10u * static_cast<unsigned int>(SimTicksPerSecond);
    }

    void TransportManager::bookPassengers(AiBlackboard& bb, const Ferry& ferry)
    {
        for (auto id : ferry.passengers)
        {
            bb.ferryPassengers.insert(id.value);
        }
    }

    void TransportManager::tendFerries(const GameSimulation& sim, const AiTuningProfile& profile, AiBlackboard& bb, std::vector<PlayerCommand>& outCommands)
    {
        bb.ferryPassengers.clear();
        auto timeout = GameTime(static_cast<unsigned int>(std::max(1, profile.ferryTimeoutSeconds)) * static_cast<unsigned int>(SimTicksPerSecond));

        for (auto it = ferries.begin(); it != ferries.end();)
        {
            auto transportId = UnitId(it->first);
            auto& ferry = it->second;
            auto transportRef = sim.tryGetUnitState(transportId);
            if (!transportRef || transportRef->get().isDead())
            {
                it = ferries.erase(it);
                continue;
            }
            const auto& transport = transportRef->get();

            // Passengers that died on the way are struck off.
            ferry.passengers.erase(
                std::remove_if(ferry.passengers.begin(), ferry.passengers.end(), [&](UnitId id) { return !isAliveUnit(sim, id); }),
                ferry.passengers.end());

            bool carryingAny = !transport.carriedUnits.empty();
            if (!ferry.loaded && carryingAny)
            {
                // Everyone booked is aboard once the transport has all of them.
                bool allAboard = std::all_of(ferry.passengers.begin(), ferry.passengers.end(), [&](UnitId id) {
                    return std::find(transport.carriedUnits.begin(), transport.carriedUnits.end(), id) != transport.carriedUnits.end();
                });
                if (allAboard)
                {
                    ferry.loaded = true;
                }
            }

            bool overdue = sim.gameTime >= ferry.startedAt + timeout;
            if (ferry.passengers.empty() || (overdue && !carryingAny))
            {
                // Nothing left to carry, or the pickup never happened: call it off.
                LOG_DEBUG << "AI transport " << transportId.value << ": ferry called off" << (overdue ? " (overdue)" : "");
                if (!transport.orders.empty())
                {
                    outCommands.push_back(stopCommand(transportId));
                }
                it = ferries.erase(it);
                continue;
            }

            if (transport.orders.empty())
            {
                if (carryingAny)
                {
                    // Loaded but the unload order got lost: set them down where they were going.
                    outCommands.push_back(unloadCommand(transportId, ferry.destination, IssueKind::Immediate));
                }
                else if (ferry.loaded)
                {
                    // Set down and idle again: the trip is over.
                    LOG_DEBUG << "AI transport " << transportId.value << ": ferry complete at " << ferry.destination.x.value << "," << ferry.destination.z.value;
                    it = ferries.erase(it);
                    continue;
                }
                else
                {
                    // Never picked anyone up and has nothing to do: try again from the top.
                    outCommands.push_back(loadCommand(transportId, ferry.passengers.front(), IssueKind::Immediate));
                    for (std::size_t i = 1; i < ferry.passengers.size(); ++i)
                    {
                        outCommands.push_back(loadCommand(transportId, ferry.passengers[i], IssueKind::Queued));
                    }
                    outCommands.push_back(unloadCommand(transportId, ferry.destination, IssueKind::Queued));
                }
            }

            bookPassengers(bb, ferry);
            ++it;
        }
    }

    void TransportManager::refreshExpansionSite(const GameSimulation& sim, const ReachabilityMap& reachability, const BuildManager& build, const AiBlackboard& bb, std::minstd_rand& rng)
    {
        if (!bb.groundReachabilityValid || !bb.baseAnchor || bb.sideUnits.metalExtractor.empty())
        {
            expansionSite.reset();
            expansionSiteSearched = false;
            return;
        }
        // On a map where everything can be walked to there is nothing for a
        // ferry to reach, so there is no point looking at all.
        if (!bb.hasUnreachableGround)
        {
            expansionSite.reset();
            return;
        }
        // Look at most once every refresh interval whether or not the last
        // look found anything: an empty result used to fall through this
        // guard and search the map again on the very next pass.
        if (expansionSiteSearched && sim.gameTime < expansionSiteCheckedAt + GameTime(ExpansionSiteRefreshTicks))
        {
            return;
        }
        expansionSiteSearched = true;
        expansionSiteCheckedAt = sim.gameTime;

        // The nearest rich patch on ground the base cannot walk to, anywhere on the map.
        auto radius = rweMax(sim.terrain.getWidthInWorldUnits(), sim.terrain.getHeightInWorldUnits());
        expansionSite = build.chooseMexSite(sim, bb.sideUnits.metalExtractor, *bb.baseAnchor, radius, rng, [&](const SimVector& p) {
            return !reachability.isReachable(sim, p) && reachability.isWalkable(sim, p);
        });
    }

    std::optional<SimVector> TransportManager::landingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const SimVector& target, const SimVector& from) const
    {
        // Walk back from the target towards home until there is ground to set
        // down on that is not in the enemy's lap.
        auto back = (from - target);
        back.y = 0_ss;
        auto direction = back.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
        for (int step = 4; step <= 12; ++step)
        {
            auto candidate = target + (direction * SimScalar(static_cast<float>(step) * 48.0f));
            if (candidate.x < 0_ss || candidate.z < 0_ss || candidate.x >= sim.terrain.getWidthInWorldUnits() || candidate.z >= sim.terrain.getHeightInWorldUnits())
            {
                break;
            }
            candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);
            if (candidate.y >= sim.terrain.getSeaLevel() && reachability.isWalkable(sim, candidate))
            {
                return candidate;
            }
        }
        SimVector fallback(target.x, sim.terrain.getHeightAt(target.x, target.z), target.z);
        if (fallback.y >= sim.terrain.getSeaLevel() && reachability.isWalkable(sim, fallback))
        {
            return fallback;
        }
        return std::nullopt;
    }

    void TransportManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ReachabilityMap& reachability,
        const BuildManager& build,
        AiBlackboard& bb,
        std::minstd_rand& rng,
        std::vector<PlayerCommand>& outCommands)
    {
        (void)aiOwner;
        ++ticksSinceLastUpdate;
        if (ticksSinceLastUpdate < profile.tacticalTickInterval)
        {
            // Keep the bookings visible to the other managers between passes.
            bb.ferryPassengers.clear();
            for (const auto& [_, ferry] : ferries)
            {
                bookPassengers(bb, ferry);
            }
            return;
        }
        ticksSinceLastUpdate = 0;

        tendFerries(sim, profile, bb, outCommands);
        refreshExpansionSite(sim, reachability, build, bb, rng);

        // Is there anywhere worth going that needs a lift?
        bool enemyAcrossWater = bb.groundReachabilityValid && bb.attackTarget && !reachability.isReachable(sim, *bb.attackTarget);
        bb.wantsTransport = expansionSite.has_value() || enemyAcrossWater;

        for (auto transportId : bb.transports)
        {
            if (ferries.count(transportId.value) > 0)
            {
                continue;
            }
            const auto& transport = sim.getUnitState(transportId);
            if (!transport.orders.empty() || !transport.carriedUnits.empty())
            {
                continue;
            }
            const auto& transportDef = sim.unitDefinitions.at(transport.unitType);
            auto capacity = static_cast<std::size_t>(transportDef.effectiveTransportCapacity());

            // Ferrying a builder to fresh metal comes first.
            if (expansionSite && !bb.idleBuilders.empty())
            {
                std::optional<UnitId> builder;
                for (auto id : bb.idleBuilders)
                {
                    const auto& unit = sim.getUnitState(id);
                    const auto& def = sim.unitDefinitions.at(unit.unitType);
                    if (def.commander || !def.isMobile || def.canFly || def.floater || unit.carriedBy)
                    {
                        continue;
                    }
                    if (bb.ferryPassengers.count(id.value) > 0)
                    {
                        continue;
                    }
                    // Only builders on the home ground need the lift.
                    if (!reachability.isReachable(sim, unit.position))
                    {
                        continue;
                    }
                    builder = id;
                    break;
                }
                if (builder)
                {
                    Ferry ferry{{*builder}, *expansionSite, sim.gameTime, false};
                    LOG_DEBUG << "AI transport " << transportId.value << ": ferrying builder " << builder->value << " to " << expansionSite->x.value << "," << expansionSite->z.value;
                    outCommands.push_back(loadCommand(transportId, *builder, IssueKind::Immediate));
                    outCommands.push_back(unloadCommand(transportId, *expansionSite, IssueKind::Queued));
                    bookPassengers(bb, ferry);
                    ferries[transportId.value] = std::move(ferry);
                    // The patch is spoken for; look for another next time
                    // rather than waiting out the refresh interval.
                    expansionSite.reset();
                    expansionSiteSearched = false;
                    continue;
                }
            }

            // Otherwise, carry the army over to an enemy it cannot walk to.
            if (enemyAcrossWater && bb.phase == GamePhase::Attack && bb.baseAnchor)
            {
                auto landing = landingNear(sim, reachability, *bb.attackTarget, *bb.baseAnchor);
                if (!landing)
                {
                    continue;
                }
                std::vector<UnitId> passengers;
                for (auto id : bb.combatUnits)
                {
                    if (passengers.size() >= capacity)
                    {
                        break;
                    }
                    if (bb.scoutUnitId && *bb.scoutUnitId == id)
                    {
                        continue;
                    }
                    const auto& unit = sim.getUnitState(id);
                    const auto& def = sim.unitDefinitions.at(unit.unitType);
                    if (unit.carriedBy || !reachability.isReachable(sim, unit.position) || bb.ferryPassengers.count(id.value) > 0)
                    {
                        continue;
                    }
                    // A unit the simulation will refuse to load must not be
                    // booked onto a ferry either: the transport would fly out,
                    // hover over it and never pick it up.
                    if (def.cantBeTransported)
                    {
                        continue;
                    }
                    if (transportDef.transportSize > 0 && def.transportSize > transportDef.transportSize)
                    {
                        continue;
                    }
                    passengers.push_back(id);
                }
                if (passengers.empty())
                {
                    continue;
                }
                Ferry ferry{passengers, *landing, sim.gameTime, false};
                LOG_DEBUG << "AI transport " << transportId.value << ": ferrying " << passengers.size() << " units to " << landing->x.value << "," << landing->z.value;
                outCommands.push_back(loadCommand(transportId, passengers.front(), IssueKind::Immediate));
                for (std::size_t i = 1; i < passengers.size(); ++i)
                {
                    outCommands.push_back(loadCommand(transportId, passengers[i], IssueKind::Queued));
                }
                outCommands.push_back(unloadCommand(transportId, *landing, IssueKind::Queued));
                bookPassengers(bb, ferry);
                ferries[transportId.value] = std::move(ferry);
            }
        }
    }
}
