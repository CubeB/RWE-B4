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

        // One UnloadOrder sets down exactly one passenger -- the crane
        // stows itself between each unit and VTOL_Unload does the same one
        // hook at a time (UnitBehaviorService::handleUnloadOrder) -- so a
        // group ferry needs one order per head, not one for the whole
        // party. firstKind lets a caller reissuing from a clean order queue
        // send the first Immediate and the rest Queued, exactly as the
        // matching load commands do.
        void queueUnloads(std::vector<PlayerCommand>& outCommands, UnitId transport, const SimVector& at, std::size_t count, IssueKind firstKind)
        {
            for (std::size_t i = 0; i < std::max<std::size_t>(count, 1); ++i)
            {
                outCommands.push_back(unloadCommand(transport, at, i == 0 ? firstKind : IssueKind::Queued));
            }
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
                sim.eventLog.event(sim.gameTime.value, "transport_ferry")
                    .set("player", transport.owner.value)
                    .set("unit", transportId.value)
                    .set("passengers", ferry.passengers.size())
                    .set("carried", transport.carriedUnits.size())
                    .set("why", overdue ? "overdue" : "called_off")
                    .detail(overdue ? "ferry called off (overdue)" : "ferry called off");
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
                    // Loaded but the unload order got lost: set them all down
                    // where they were going, one order per head aboard.
                    queueUnloads(outCommands, transportId, ferry.destination, transport.carriedUnits.size(), IssueKind::Immediate);
                }
                else if (ferry.loaded)
                {
                    // Set down and idle again: the trip is over.
                    LOG_DEBUG << "AI transport " << transportId.value << ": ferry complete at " << ferry.destination.x.value << "," << ferry.destination.z.value;
                    sim.eventLog.event(sim.gameTime.value, "transport_ferry")
                        .set("player", transport.owner.value)
                        .set("unit", transportId.value)
                        .set("x", static_cast<double>(ferry.destination.x.value))
                        .set("z", static_cast<double>(ferry.destination.z.value))
                        .set("why", "complete")
                        .detail("ferry complete");
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
                    queueUnloads(outCommands, transportId, ferry.destination, ferry.passengers.size(), IssueKind::Queued);
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
        expansionSite = build.chooseMexSite(sim, bb.sideUnits.metalExtractor, *bb.baseAnchor, radius, rng, nullptr, [&](const SimVector& p) {
            return !reachability.isReachable(sim, p) && reachability.isWalkable(sim, p);
        });
    }

    /**
     * The walk-back both landing searches share, with the test that tells
     * them apart handed in.
     *
     * A template rather than a std::function because the predicate for the
     * naval case probes the terrain eight times per candidate, and this runs
     * once per ferry per tactical pass -- but mostly because that is what
     * ThreatMap does for the same reason, and one convention is better than
     * two.
     */
    template <typename Usable>
    std::optional<SimVector> searchLandingAlongRay(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        const SimVector& target,
        const SimVector& from,
        TransportManager::LandingSearchTally& tally,
        Usable&& usable)
    {
        auto back = (from - target);
        back.y = 0_ss;
        auto direction = back.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        // A FAN rather than a single ray, and this is the part that decides
        // whether the search finds anything at all.
        //
        // Walking straight back towards the ferry's origin assumes the cargo's
        // shore lies on that line. Measured on Coast To Coast it usually does
        // not: a refusal logged at 514s reported `steps: 21, wet: 21,
        // refused: 0` -- every one of the twenty-one points that stayed on the
        // map was open sea, because the objective sat on a headland and the
        // line home left it over water immediately. Widening that ray from 576
        // to 1152 units changed the refusal count in one seed of ten, which is
        // what a wrong direction looks like when you make it longer.
        //
        // So each radius is tried at nine bearings spread through a half-turn
        // about the direction home, nearest-to-home first. Fixed unit vectors
        // and a complex multiply, not std::cos -- this chooses where units are
        // put down, so it is sim arithmetic and has to be the same on every
        // machine (see updateNavy's bearings table and CLAUDE.md's
        // determinism rules).
        static const float fan[9][2] = {
            {1.0f, 0.0f},
            {0.9239f, 0.3827f},
            {0.9239f, -0.3827f},
            {0.7071f, 0.7071f},
            {0.7071f, -0.7071f},
            {0.3827f, 0.9239f},
            {0.3827f, -0.9239f},
            {0.0f, 1.0f},
            {0.0f, -1.0f}};

        std::optional<SimVector> best;
        float bestThreat = 0.0f;
        SimScalar bestHomeward(0_ss);

        auto lastStep = std::max(4, profile.ferryLandingSearchSteps);
        for (int step = 4; step <= lastStep; ++step)
        {
            auto radius = SimScalar(static_cast<float>(step) * 48.0f);
            auto bearingCount = profile.ferryLandingFan ? static_cast<int>(std::size(fan)) : 1;
            for (int bearingIndex = 0; bearingIndex < bearingCount; ++bearingIndex)
            {
                const auto& b = fan[bearingIndex];
                // direction rotated by the bearing, as a complex product.
                SimVector bearing(
                    (direction.x * SimScalar(b[0])) - (direction.z * SimScalar(b[1])),
                    0_ss,
                    (direction.x * SimScalar(b[1])) + (direction.z * SimScalar(b[0])));
                auto candidate = target + (bearing * radius);

                // tryGetHeightAt is the terrain's own answer to "is this point
                // on the map", and it is asked here rather than compared
                // against the width because world space is CENTRED: x runs
                // from -width/2 to +width/2. Testing against 0..width, as this
                // did, is a window shifted by half a map. It is a `continue`
                // and not a `break` now that there is a fan: one bearing
                // leaving the map says nothing about the next.
                auto height = sim.terrain.tryGetHeightAt(candidate.x, candidate.z);
                if (!height)
                {
                    continue;
                }
                ++tally.stepsTried;
                candidate.y = *height;
                if (candidate.y < sim.terrain.getSeaLevel())
                {
                    ++tally.wet;
                    continue;
                }
                if (!usable(candidate))
                {
                    ++tally.refused;
                    continue;
                }

                if (!profile.ferryLandingAvoidsThreat)
                {
                    return candidate;
                }

                // The quietest wins; among equally quiet points, the one
                // nearest home. That second half is not decoration. The
                // threat field already accounts for weapon reach, so a
                // defended shore has a wide skirt and the quiet points are
                // all some way out -- and without a tie-break the first zero
                // the ring happens to reach wins, which on the test map was a
                // bearing of ninety degrees: out of everything's range, and
                // exactly as far from home as the objective it was supposed
                // to be short of.
                //
                // Comparing floats for equality is safe for the case that
                // matters, which is nought against nought.
                auto threat = threatMap.antiGroundInRadius(candidate, profile.ferryLandingThreatRadius);
                auto homeward = candidate.distanceSquared(from);
                if (!best || threat < bestThreat || (threat == bestThreat && homeward < bestHomeward))
                {
                    best = candidate;
                    bestThreat = threat;
                    bestHomeward = homeward;
                }
            }
            if (best && bestThreat <= 0.0f)
            {
                // This ring has a landing nothing can shoot at, so there is
                // no reason to look further out. The whole ring is scored
                // first, though, and the ring runs nearest-the-way-home
                // outwards: returning on the first zero found took a bearing
                // of ninety degrees -- a point abreast of the objective, no
                // nearer home than the objective is -- over one at
                // forty-five that was just as quiet and genuinely short of
                // it. That is the bug this whole search exists to avoid,
                // rediscovered one loop in.
                break;
            }
        }
        if (best)
        {
            return best;
        }

        // getHeightAt would answer 0 for a point off the map, which on a map
        // at sea level 0 passes the test below and lands the cargo nowhere.
        auto fallbackHeight = sim.terrain.tryGetHeightAt(target.x, target.z);
        if (!fallbackHeight)
        {
            return std::nullopt;
        }
        SimVector fallback(target.x, *fallbackHeight, target.z);
        if (fallback.y >= sim.terrain.getSeaLevel() && usable(fallback))
        {
            return fallback;
        }
        return std::nullopt;
    }

    std::optional<SimVector> TransportManager::landingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const AiTuningProfile& profile, const ThreatMap& threatMap, const SimVector& target, const SimVector& from, LandingSearchTally& tally) const
    {
        // Walk back from the target towards home until there is ground to set
        // down on that is not in the enemy's lap -- which used to mean the
        // first dry cell it came to, and now means the quietest.
        return searchLandingAlongRay(sim, profile, threatMap, target, from, tally, [&](const SimVector& p) {
            // Walkable, and not somewhere the army could have walked to by
            // itself -- a ferry that sets its cargo down on our own side of
            // the water has carried it nowhere. The old search was bounded at
            // 576 units and could not reach back across a channel; widening
            // it made this an ordinary case rather than an impossible one,
            // and the air-ferry test caught it on the first run.
            return reachability.isWalkable(sim, p) && !reachability.isReachable(sim, p);
        });
    }

    std::optional<SimVector> TransportManager::navalLandingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const AiTuningProfile& profile, const ThreatMap& threatMap, const SimVector& target, const SimVector& from, LandingSearchTally& tally) const
    {
        // A ship cannot cross the strip of dry land landingNear would happily
        // land an Atlas on: the drop point still has to be dry ground the
        // cargo can stand on, but there also has to be water right beside it
        // that our own navy -- not just any navy -- can actually reach, or
        // the hull will approach the shore and stick there, forever out of
        // crane range. isNavalReachable is what tests "our own": the naval
        // layer is homed on our base the same way the ground one is.
        if (!reachability.isNavalValid())
        {
            // Nothing was even looked at: no naval labelling exists, so
            // "no landing" here means "no navy", which is a different
            // complaint entirely.
            return std::nullopt;
        }

        auto back = (from - target);
        back.y = 0_ss;
        auto direction = back.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        // Reachability's components are labelled by a footprint's top-left
        // corner, not by whichever point of it a query happens to land on
        // (ReachabilityMap::labelComponents), so a probe within one hull
        // width of the shoreline can read as unreachable even though the
        // hull could genuinely float there -- its own footprint just could
        // not START at that exact tile without running onto the bank. A
        // point safely inside the water, clear of that shoreline margin, is
        // what actually answers "is there water here our navy can reach";
        // eight steps covers hulls a good deal wider than anything the game
        // ships without ever being asked to search past a channel a couple
        // of hundred units wide.
        auto hasReachableWaterNearby = [&](const SimVector& dry) {
            for (float stepsTowardsHome : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f})
            {
                auto probe = dry + (direction * SimScalar(stepsTowardsHome * 48.0f));
                auto probeHeight = sim.terrain.tryGetHeightAt(probe.x, probe.z);
                if (!probeHeight)
                {
                    continue;
                }
                probe.y = *probeHeight;
                if (probe.y < sim.terrain.getSeaLevel() && reachability.isNavalReachable(sim, probe))
                {
                    return true;
                }
            }
            return false;
        };

        return searchLandingAlongRay(sim, profile, threatMap, target, from, tally, [&](const SimVector& p) {
            // See landingNear for why unreachable-on-foot is part of the test.
            return reachability.isWalkable(sim, p) && !reachability.isReachable(sim, p) && hasReachableWaterNearby(p);
        });
    }

    void TransportManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ReachabilityMap& reachability,
        const ThreatMap& threatMap,
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

        // Whether a ferry is WANTED is a different question from where to
        // send one, and the difference is a horizon. enemyAcrossWater answers
        // the second: it needs bb.attackTarget, which ArmyManager rebuilds
        // from scratch every tactical pass and leaves unset outside the Attack
        // phase and whenever nothing is currently remembered. Asked of a build
        // decision that takes 223 seconds to carry out, it flaps with
        // visibility and the hull never gets made.
        //
        // So the build decision reads a LATCH -- the last answer this question
        // actually had a target to ask about -- and falls back on the map only
        // before there has ever been one.
        //
        // Nothing below may dereference bb.attackTarget on the strength of
        // armyNeedsFerry: it can be true with no target at all, which is the
        // entire point of it.
        if (bb.attackTarget && bb.groundReachabilityValid)
        {
            lastArmyFerryAnswer = enemyAcrossWater;
        }

        bool armyNeedsFerry = enemyAcrossWater;
        if (profile.armyFerryWantFromMap)
        {
            if (lastArmyFerryAnswer)
            {
                armyNeedsFerry = *lastArmyFerryAnswer;
            }
            else if (bb.landRouteToEnemy && !*bb.landRouteToEnemy)
            {
                // Nothing has ever been seen, so there is no latched answer
                // and the map is all there is. landRouteToEnemy is "can the
                // army walk to ANY other declared start position", which on a
                // map that deals ten seats is usually yes even when the enemy
                // it actually drew is across water -- so this only ever opens
                // the question on a genuinely isolated map, and never closes
                // it. Measured: on Coast To Coast it is true from tick 1 and
                // this branch does nothing at all, which is why the latch and
                // not the map is what makes this work.
                armyNeedsFerry = true;
            }
        }

        bb.hasExpansionSite = expansionSite.has_value();
        bb.enemyAcrossWater = enemyAcrossWater;
        bb.armyNeedsFerry = armyNeedsFerry;
        bb.wantsTransport = expansionSite.has_value() || armyNeedsFerry;

        if (armyNeedsFerry && bb.phase == GamePhase::Attack && bb.transports.empty())
        {
            LOG_DEBUG << "AI transport: army ferry wanted, but nothing is classified as a transport";
            sim.eventLog.event(sim.gameTime.value, "transport_refusal")
                .set("player", aiOwner.value)
                .set("why", "no_transport")
                .detail("army ferry wanted, but nothing is classified as a transport");
        }

        for (auto transportId : bb.transports)
        {
            if (ferries.count(transportId.value) > 0)
            {
                continue;
            }
            const auto& transport = sim.getUnitState(transportId);
            if (!transport.orders.empty() || !transport.carriedUnits.empty())
            {
                if (enemyAcrossWater && bb.phase == GamePhase::Attack)
                {
                    LOG_DEBUG << "AI transport " << transportId.value << ": army ferry blocked, transport busy ("
                              << transport.orders.size() << " orders, " << transport.carriedUnits.size() << " aboard)";
                    sim.eventLog.event(sim.gameTime.value, "transport_refusal")
                        .set("player", aiOwner.value)
                        .set("unit", transportId.value)
                        .set("orders", transport.orders.size())
                        .set("carried", transport.carriedUnits.size())
                        .set("why", "transport_busy")
                        .detail("army ferry blocked, transport busy");
                }
                continue;
            }
            const auto& transportDef = sim.unitDefinitions.at(transport.unitType);
            // Mirrors GameSimulation::canLoadUnitIntoTransport, which caps an
            // air transport at exactly one passenger whatever its FBI says:
            // the original's VTOL pickup aborts while anything is already
            // attached, which is why the Atlas's transportcapacity=5 has
            // never meant five. Booking a second passenger onto an aircraft
            // got it refused by the simulation and left it marked as a ferry
            // passenger regardless -- out of the army, and waiting for a lift
            // that was never coming.
            auto capacity = static_cast<std::size_t>(
                transportDef.canFly ? 1u : transportDef.effectiveTransportCapacity());

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
                    sim.eventLog.event(sim.gameTime.value, "transport_dispatch")
                        .set("player", aiOwner.value)
                        .set("unit", transportId.value)
                        .set("passenger", builder->value)
                        .set("x", static_cast<double>(expansionSite->x.value))
                        .set("z", static_cast<double>(expansionSite->z.value))
                        .set("why", "builder_ferry")
                        .detail("ferrying a builder to a fresh metal patch");
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
                // Walk back towards where the army STANDS, not towards where
                // the game began. Both searches step from the attack target
                // back along this direction looking for a shore to put the
                // cargo on, so the origin decides which side of the target
                // they look at -- and on a map where the commander walked off
                // its spawn island, baseAnchor names an island the AI has not
                // owned since its opening minutes. Measured before this: 1494
                // blocked landings in four games on Hundred Isles, 1426 of
                // them the same point, while the same runs found landings
                // perfectly well for other targets. See
                // AiBlackboard::groundAnchor.
                const SimVector& ferryOrigin = bb.groundAnchor ? *bb.groundAnchor : *bb.baseAnchor;
                LandingSearchTally tally;
                auto landing = transportDef.canFly
                    ? landingNear(sim, reachability, profile, threatMap, *bb.attackTarget, ferryOrigin, tally)
                    : navalLandingNear(sim, reachability, profile, threatMap, *bb.attackTarget, ferryOrigin, tally);
                if (!landing)
                {
                    LOG_DEBUG << "AI transport " << transportId.value << ": army ferry blocked, no landing near "
                              << static_cast<int>(bb.attackTarget->x.value) << "," << static_cast<int>(bb.attackTarget->z.value)
                              << (transportDef.canFly ? " (air)" : " (sea)");
                    sim.eventLog.event(sim.gameTime.value, "transport_refusal")
                        .set("player", aiOwner.value)
                        .set("unit", transportId.value)
                        .set("x", static_cast<double>(bb.attackTarget->x.value))
                        .set("z", static_cast<double>(bb.attackTarget->z.value))
                        .set("air", transportDef.canFly)
                        .set("from_x", static_cast<double>(ferryOrigin.x.value))
                        .set("from_z", static_cast<double>(ferryOrigin.z.value))
                        .set("steps", tally.stepsTried)
                        .set("wet", tally.wet)
                        .set("refused", tally.refused)
                        .set("naval_layer", reachability.isNavalValid())
                        .set("why", "no_landing")
                        .detail("army ferry blocked, no landing near the attack target");
                    continue;
                }
                std::vector<UnitId> passengers;
                // Why each candidate was turned away, counted so the blocked
                // message below can name the gate rather than only the tally.
                // Measured on Hundred Isles, sixteen combat units were refused
                // against a capacity of twenty, 152 times in ten games, and
                // which of these gates did it could not be told from the log
                // at all -- the same silent gap every other blocked path in
                // this file already avoids by saying what stopped it.
                int refusedScout = 0;
                int refusedCarried = 0;
                int refusedUnreachable = 0;
                int refusedBooked = 0;
                int refusedCantBeTransported = 0;
                int refusedFootprint = 0;
                int refusedNeedsWater = 0;
                // Where the first unreachable candidate was standing. The
                // refusal counts said the gate; they cannot say WHY it fired,
                // and the three candidate causes want three different fixes:
                // the army standing off its own island (the rally point is
                // 220 units seaward, and a ~360 tile island is only about 300
                // world units across), the query disagreeing with the
                // labelling for a position that is genuinely on it, or the
                // unit sitting in the two-tile margin the components grid
                // drops by being heightmap-minus-footprint in size.
                std::optional<SimVector> firstUnreachable;
                for (auto id : bb.combatUnits)
                {
                    if (passengers.size() >= capacity)
                    {
                        break;
                    }
                    if (bb.scoutUnitId && *bb.scoutUnitId == id)
                    {
                        ++refusedScout;
                        continue;
                    }
                    const auto& unit = sim.getUnitState(id);
                    const auto& def = sim.unitDefinitions.at(unit.unitType);
                    // Split out of one compound condition purely so each can be
                    // counted; the behaviour is unchanged.
                    if (unit.carriedBy)
                    {
                        ++refusedCarried;
                        continue;
                    }
                    if (!reachability.isReachable(sim, unit.position))
                    {
                        ++refusedUnreachable;
                        if (!firstUnreachable)
                        {
                            firstUnreachable = unit.position;
                        }
                        continue;
                    }
                    if (bb.ferryPassengers.count(id.value) > 0)
                    {
                        ++refusedBooked;
                        continue;
                    }
                    // A unit the simulation will refuse to load must not be
                    // booked onto a ferry either: the transport would fly out,
                    // hover over it and never pick it up.
                    if (def.cantBeTransported)
                    {
                        continue;
                    }
                    // The size gate is the passenger's FOOTPRINT X against
                    // transportsize -- that is what
                    // GameSimulation::canLoadUnitIntoTransport tests, and it
                    // is how an air transport refuses ships, every one of
                    // which is wider than transportsize 3. This used to
                    // compare the passenger's own transportSize field, which
                    // is 0 for anything that is not itself a transport, so
                    // the gate passed everything and the simulation did the
                    // refusing a tick later -- the same shape of bug as the
                    // water-depth one below.
                    if (transportDef.transportSize > 0
                        && sim.getFootprintXZ(def.movementCollisionInfo).first > transportDef.transportSize)
                    {
                        continue;
                    }
                    // Mirrors GameSimulation::canLoadUnitIntoTransport: a sea
                    // or hover transport refuses anything that needs water
                    // under it. Without this a ship gets a load order the
                    // simulation declines outright, and the ferry sits
                    // there -- loaded with nothing -- until it times out.
                    if (!transportDef.canFly)
                    {
                        auto passengerMovement = sim.getAdHocMovementClass(def.movementCollisionInfo);
                        if (passengerMovement.minWaterDepth > 0)
                        {
                            continue;
                        }
                    }
                    passengers.push_back(id);
                }
                if (passengers.empty())
                {
                    int factoriesReachable = 0;
                    for (auto factoryId : bb.factories)
                    {
                        if (reachability.isReachable(sim, sim.getUnitState(factoryId).position))
                        {
                            ++factoriesReachable;
                        }
                    }
                    LOG_DEBUG << "AI transport " << transportId.value << ": army ferry blocked, no eligible passenger out of "
                              << bb.combatUnits.size() << " combat units (capacity " << capacity << ")"
                              << " refused: scout=" << refusedScout
                              << " carried=" << refusedCarried
                              << " unreachable=" << refusedUnreachable
                              << " booked=" << refusedBooked
                              << " cantBeTransported=" << refusedCantBeTransported
                              << " footprint=" << refusedFootprint
                              << " needsWater=" << refusedNeedsWater
                              << " anchorReachable=" << (bb.baseAnchor ? (reachability.isReachable(sim, *bb.baseAnchor) ? 1 : 0) : -1)
                              << " anchorAt=" << (bb.baseAnchor ? static_cast<int>(bb.baseAnchor->x.value) : 0)
                              << "," << (bb.baseAnchor ? static_cast<int>(bb.baseAnchor->z.value) : 0)
                              << " firstRefusedAt=" << (firstUnreachable ? static_cast<int>(firstUnreachable->x.value) : 0)
                              << "," << (firstUnreachable ? static_cast<int>(firstUnreachable->z.value) : 0)
                              // Walkable-but-not-home means the labelling split a
                              // region the unit demonstrably walked across, which
                              // points at connectivity (the flood fills 4-way while
                              // the pathfinder moves octile). Not walkable at all
                              // means the footprint-origin margin instead. The two
                              // want different fixes, and the refusal counts cannot
                              // tell them apart.
                              << " firstRefusedWalkable=" << (firstUnreachable ? (reachability.isWalkable(sim, *firstUnreachable) ? 1 : 0) : -1)
                              // How much of our own BASE is on home ground.
                              //
                              // Counted across every factory rather than read
                              // off the first one, because bb.factories is
                              // "builder, not mobile, not commander" and that
                              // includes the SHIPYARD -- which floats at
                              // MinWaterDepth=30 where a land constructor can
                              // never stand, so a single unreachable factory
                              // proves nothing at all. The type of the first is
                              // logged for the same reason.
                              //
                              // anchorWalkable is the other half: setAnchor has
                              // a lenient branch for an anchor tile carrying no
                              // component, which claims whatever its four
                              // orthogonal neighbours belong to, and isReachable
                              // returns true for the anchor tile itself
                              // regardless. So anchorReachable=1 can be that
                              // special case firing rather than a healthy home,
                              // and the claimed component can be a neighbouring
                              // piece that is not where the base actually is.
                              << " factories=" << factoriesReachable << "/" << bb.factories.size()
                              << " firstFactory=" << (bb.factories.empty() ? std::string("-") : sim.getUnitState(bb.factories.front()).unitType)
                              << " anchorWalkable=" << (bb.baseAnchor ? (reachability.isWalkable(sim, *bb.baseAnchor) ? 1 : 0) : -1)
                              // The commander wades to depth 100 where the
                              // constructor the ground layer is labelled for
                              // stops at 12. If the COMMANDER layer reaches our
                              // own lab while the ground layer does not, the base
                              // was built on an island the army can never leave,
                              // and baseAnchor -- set from homePosition the first
                              // tick the commander is seen, and never revised --
                              // is still pointing at the spawn tile.
                              << " cmdrValid=" << (reachability.isCommanderValid() ? 1 : 0)
                              << " factoryCmdrReachable=" << (bb.factories.empty() ? -1 : (reachability.isCommanderReachable(sim, sim.getUnitState(bb.factories.front()).position) ? 1 : 0))
                              // Plain coordinates rather than a distance: anchorAt
                              // is right there on the same line, and a subtraction
                              // the reader can do needs no <cmath> here and no
                              // arithmetic of mine to get wrong.
                              << " factoryAt=" << (bb.factories.empty() ? 0 : static_cast<int>(sim.getUnitState(bb.factories.front()).position.x.value))
                              << "," << (bb.factories.empty() ? 0 : static_cast<int>(sim.getUnitState(bb.factories.front()).position.z.value));
                    sim.eventLog.event(sim.gameTime.value, "transport_refusal")
                        .set("player", aiOwner.value)
                        .set("unit", transportId.value)
                        .set("combat_units", bb.combatUnits.size())
                        .set("capacity", capacity)
                        .set("refused_scout", refusedScout)
                        .set("refused_carried", refusedCarried)
                        .set("refused_unreachable", refusedUnreachable)
                        .set("refused_booked", refusedBooked)
                        .set("refused_cant_be_transported", refusedCantBeTransported)
                        .set("refused_footprint", refusedFootprint)
                        .set("refused_needs_water", refusedNeedsWater)
                        .set("factories_reachable", factoriesReachable)
                        .set("why", "no_passenger")
                        .detail("army ferry blocked, no eligible passenger");
                    continue;
                }
                Ferry ferry{passengers, *landing, sim.gameTime, false};
                LOG_DEBUG << "AI transport " << transportId.value << ": ferrying " << passengers.size() << " units to " << landing->x.value << "," << landing->z.value;
                sim.eventLog.event(sim.gameTime.value, "transport_dispatch")
                    .set("player", aiOwner.value)
                    .set("unit", transportId.value)
                    .set("passengers", passengers.size())
                    .set("x", static_cast<double>(landing->x.value))
                    .set("z", static_cast<double>(landing->z.value))
                    .set("why", "army_ferry")
                    .detail("ferrying the army to an enemy it cannot walk to");
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
