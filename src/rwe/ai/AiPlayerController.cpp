#include "AiPlayerController.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>

namespace rwe
{
    namespace
    {
        // A pass slower than this is worth a line of its own in the log.
        const double AiProfileSpikeThresholdMs = 2.0;

        // How often the accumulated totals are dumped.
        const unsigned int AiProfileReportIntervalTicks = 30u * static_cast<unsigned int>(SimTicksPerSecond);
    }

    bool AiProfiler::enabled()
    {
        // Read once: the answer cannot change, and getenv is not cheap.
        static const bool on = std::getenv("RWE_AI_PROFILE") != nullptr;
        return on;
    }

    void AiProfiler::record(const char* pass, double milliseconds, PlayerId player, GameTime now)
    {
        auto& stats = passes[pass];
        stats.totalMs += milliseconds;
        stats.worstMs = std::max(stats.worstMs, milliseconds);
        ++stats.calls;
        windowMs += milliseconds;
        if (milliseconds >= AiProfileSpikeThresholdMs)
        {
            ++stats.spikes;
            LOG_INFO << "AI profile: player " << player.value << " pass " << pass << " took " << milliseconds << " ms at tick " << now.value;
        }
    }

    void AiProfiler::report(PlayerId player, GameTime now)
    {
        if (passes.empty())
        {
            return;
        }
        std::string line;
        for (const auto& [name, stats] : passes)
        {
            line += " " + name + "=" + std::to_string(stats.totalMs) + "ms/" + std::to_string(stats.calls) + " (worst " + std::to_string(stats.worstMs) + "ms, " + std::to_string(stats.spikes) + " spikes)";
        }
        LOG_INFO << "AI profile summary: player " << player.value << " at tick " << now.value << ", total " << windowMs << " ms:" << line;
        passes.clear();
        windowMs = 0.0;
    }

    AiPlayerController::AiPlayerController(
        PlayerId playerId,
        AiTuningProfile profile,
        std::uint64_t rngSeed,
        MapIntel mapIntel,
        AiBuildTree buildTree)
        : playerId(playerId),
          profile(std::move(profile)),
          rng(static_cast<std::uint_fast32_t>(rngSeed))
    {
        // Read once from map data that cannot change, before the first tick,
        // and never touched again -- so it sits on the blackboard beside the
        // per-tick state rather than being threaded through every manager.
        blackboard.mapIntel = std::move(mapIntel);

        // Likewise the build tree, which describes the game's data rather
        // than the game. A default-constructed one knows no builder and so
        // permits everything, which is what the tests and the harnesses get.
        blackboard.buildTree = std::move(buildTree);
    }

    void AiPlayerController::tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands)
    {
        // An Idle opponent does nothing: no economy pass, no perception, no
        // commands. Bailing here rather than at each manager means an idle
        // player also costs nothing, which is the point when the reason for
        // switching it off was to measure something else.
        if (profile.idle)
        {
            return;
        }

        // Times a pass when RWE_AI_PROFILE is set, and simply runs it otherwise.
        // Nothing measured here feeds back into the sim, so this is invisible
        // to determinism.
        const bool profiling = AiProfiler::enabled();
        auto timed = [&](const char* name, auto&& pass) {
            if (!profiling)
            {
                pass();
                return;
            }
            auto start = std::chrono::steady_clock::now();
            pass();
            auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            profiler.record(name, elapsed, playerId, sim.gameTime);
        };

        // 1. What do we own, and how is the economy doing?
        timed("economy", [&] { economy.refresh(sim, playerId, profile, blackboard); });

        // 2. What do we know about the enemy?
        timed("perception", [&] { perception.refresh(sim, playerId, profile, blackboard); });

        // 3. Influence map, once a second.
        ++ticksSinceThreatRebuild;
        if (threatMap.isEmpty() || ticksSinceThreatRebuild >= profile.threatMapTickInterval)
        {
            ticksSinceThreatRebuild = 0;
            timed("threatMap", [&] { threatMap.rebuild(sim, playerId, blackboard, profile.cheatModeOmniscient); });
        }

        // 3b. Where can our ground units walk to? Rebuilt now and then; the ground does not change.
        ++ticksSinceReachabilityRebuild;
        if (blackboard.baseAnchor && blackboard.sideUnitsResolved && (!reachability.isValid() || ticksSinceReachabilityRebuild >= 20 * SimTicksPerSecond))
        {
            ticksSinceReachabilityRebuild = 0;
            std::string mover = blackboard.sideUnits.constructor.empty() ? (blackboard.commanderUnitId ? sim.getUnitState(*blackboard.commanderUnitId).unitType : std::string()) : blackboard.sideUnits.constructor;
            auto moverDef = sim.unitDefinitions.find(mover);
            if (moverDef != sim.unitDefinitions.end())
            {
                timed("reachability", [&] {
                    reachability.rebuild(sim, moverDef->second.movementCollisionInfo, *blackboard.baseAnchor);

                    // Home on the base we ACTUALLY have, not on the tile the
                    // commander happened to spawn on.
                    //
                    // baseAnchor is homePosition, which EconomyManager sets the
                    // first tick it sees the commander and never revises. The
                    // commander's TANKDS2 wades to water depth 100 and climbs
                    // slope 32 where the constructor's TANKSH2 -- which this
                    // layer is labelled for -- stops at 12 and 15, so it walks
                    // off its spawn island and builds the whole base on ground
                    // no kbot it produces can ever leave. Nothing bounds that:
                    // commanderMexSearchRadius leashes only the mex search.
                    //
                    // Measured on Hundred Isles: anchor at 2128,-1600, the AI's
                    // own lab at 592,-1216, both factories unreachable on this
                    // layer and the lab reachable on the commander's. Every unit
                    // the AI owns is born out there, so TransportManager refused
                    // every passenger it was ever offered -- 168,076 refusals in
                    // six games and not one ferry, army or builder -- and
                    // hasUnreachableGround, enemyAcrossWater, the land-army cap
                    // and tower siting were all answering about an island the AI
                    // abandoned in its opening minutes.
                    //
                    // Only fires when NOT ONE factory is reachable, which is the
                    // unambiguous case: the base is somewhere this labelling
                    // cannot see. Where the base really is on the anchor's
                    // island the test passes on the first factory and nothing
                    // changes. The shipyard excludes itself without a special
                    // case, floating at MinWaterDepth=30 where no land mover can
                    // stand. baseAnchor itself is deliberately left alone -- the
                    // rally point, the defence facing and the mex leash keep
                    // their present meaning; moving those is a bigger change and
                    // is kept separate.
                    if (!blackboard.factories.empty())
                    {
                        auto reachableFactory = std::any_of(
                            blackboard.factories.begin(),
                            blackboard.factories.end(),
                            [&](UnitId id) { return reachability.isReachable(sim, sim.getUnitState(id).position); });
                        if (!reachableFactory)
                        {
                            for (auto factoryId : blackboard.factories)
                            {
                                const auto& factoryPosition = sim.getUnitState(factoryId).position;
                                if (reachability.isWalkable(sim, factoryPosition))
                                {
                                    reachability.rebuild(sim, moverDef->second.movementCollisionInfo, factoryPosition);
                                    break;
                                }
                            }
                        }
                    }

                    blackboard.groundReachabilityValid = reachability.isValid();
                    blackboard.hasUnreachableGround = reachability.walkableTileCount() > reachability.reachableTileCount() + 64;
                    // A reachable count of 0 or 1 means setAnchor found no
                    // component at the base and none beside it either, so
                    // homeComponents is empty and EVERY position on the map
                    // reads unreachable except the anchor tile itself. That is
                    // indistinguishable from "the enemy is across water" at
                    // every call site that asks, and it would switch off every
                    // ferry the AI ever wants without saying so anywhere.
                    // Logged because the alternative -- a healthy home
                    // component with the army standing off it -- wants a
                    // completely different fix, and the two cannot be told
                    // apart from any figure the AI currently reports.
                    LOG_INFO << "AI player " << playerId.value << ": ground reachability for " << mover
                             << " walkable=" << reachability.walkableTileCount()
                             << " reachable=" << reachability.reachableTileCount();
                });

                // 3b-ii. And where can the COMMANDER walk? Not the same
                // question, and the ground layer above cannot answer it: it
                // is labelled for the constructor, and ARMCOM's TANKDS2
                // wades to depth 100 and climbs slope 32 where ARMCK's
                // TANKSH2 stops at 12 and 15.
                //
                // Measured on Hundred Isles before this existed: 504 of the
                // 531 patch cells in range were refused as unreachable,
                // leaving three extractors and a commander idle for two
                // thirds of the game. Worse, on the side that did walk out
                // -- the shipyard being the one errand whose site search is
                // not bounded by a radius -- the commander ended up standing
                // on ground its own base supposedly could not reach, which
                // flips builderAtBase false and runs it as a stranded
                // outpost builder, whose want-list is extractors and solars
                // and no factory at all. That is why neither side built a
                // single factory in twenty games on that map.
                //
                // One more flood per game, not one per rebuild: rebuildLayer
                // relabels only when the movement class changes, and a
                // side's commander does not change class mid-game.
                if (profile.commanderUsesOwnReachability && blackboard.commanderUnitId)
                {
                    const auto& commanderUnit = sim.getUnitState(*blackboard.commanderUnitId);
                    auto commanderDefIt = sim.unitDefinitions.find(commanderUnit.unitType);
                    if (commanderDefIt != sim.unitDefinitions.end())
                    {
                        timed("commanderReachability", [&] {
                            reachability.rebuildCommander(sim, commanderDefIt->second.movementCollisionInfo, *blackboard.baseAnchor);
                        });
                    }
                }
            }

            // 3c. Where can our navy float? Same gate and the same reset of
            // ticksSinceReachabilityRebuild as the ground layer just above,
            // so a naval flood never happens more often than a ground one --
            // flooding the whole heightmap a second time for a second
            // movement class would be the expensive half of what rebuild()
            // exists to amortise. Skipped outright on a map with no
            // navigable water, or when nothing that could float has a
            // resolved type to build the labelling from -- a mod that
            // defines no sea transport and no other hull leaves the naval
            // layer untouched (isNavalValid() stays false) rather than
            // flooding for a movement class nothing will ever use.
            if (blackboard.mapIntel.valid && blackboard.mapIntel.character != MapCharacter::Land)
            {
                const std::string& navalMover = !blackboard.sideUnits.seaTransport.empty() ? blackboard.sideUnits.seaTransport
                    : !blackboard.sideUnits.destroyer.empty()                              ? blackboard.sideUnits.destroyer
                    : !blackboard.sideUnits.submarine.empty()                              ? blackboard.sideUnits.submarine
                    : !blackboard.sideUnits.scoutShip.empty()                              ? blackboard.sideUnits.scoutShip
                                                                                            : blackboard.sideUnits.constructionShip;
                auto navalMoverDef = navalMover.empty() ? sim.unitDefinitions.end() : sim.unitDefinitions.find(navalMover);
                if (navalMoverDef != sim.unitDefinitions.end())
                {
                    // Anchor on our own shipyard if we have built one;
                    // failing that, the nearest site a shipyard could stand
                    // -- the best water access MapIntel already knows about
                    // near our base -- rather than the base anchor itself,
                    // which is dry land more often than not and would label
                    // almost nothing as home.
                    SimVector navalAnchor = *blackboard.baseAnchor;
                    bool haveShipyardAnchor = false;
                    if (!blackboard.sideUnits.shipyard.empty())
                    {
                        for (auto id : blackboard.factories)
                        {
                            if (sim.getUnitState(id).unitType == blackboard.sideUnits.shipyard)
                            {
                                navalAnchor = sim.getUnitState(id).position;
                                haveShipyardAnchor = true;
                                break;
                            }
                        }
                    }
                    if (!haveShipyardAnchor && !blackboard.mapIntel.shipyardSites.empty())
                    {
                        const NavalSite* nearest = nullptr;
                        SimScalar bestDistanceSquared(0_ss);
                        for (const auto& site : blackboard.mapIntel.shipyardSites)
                        {
                            auto d = site.position.distanceSquared(*blackboard.baseAnchor);
                            if (!nearest || d < bestDistanceSquared)
                            {
                                nearest = &site;
                                bestDistanceSquared = d;
                            }
                        }
                        if (nearest)
                        {
                            // The site's own top-left tile, not its centre:
                            // components are labelled by footprint top-left,
                            // and the shipyard's 8x8 is wider than plenty of
                            // hulls that might anchor here, so the centre can
                            // land on a column a narrower footprint could
                            // never start from. The top-left is the tile the
                            // site was actually validated at, which is
                            // walkable for anything no bigger.
                            auto corner = sim.terrain.heightmapIndexToWorldCorner(nearest->tile);
                            navalAnchor = SimVector(corner.x, sim.terrain.getSeaLevel(), corner.z);
                        }
                    }
                    timed("navalReachability", [&] {
                        reachability.rebuildNaval(sim, navalMoverDef->second.movementCollisionInfo, navalAnchor);
                    });
                }
            }
        }

        // Worth a line: it is the one thing the AI reacts to rather than
        // plans for, so a play-test log that shows a base full of Defenders
        // should also show when it decided it needed them.
        if (blackboard.enemyAirThreat != loggedAirThreat)
        {
            loggedAirThreat = blackboard.enemyAirThreat;
            LOG_INFO << "AI player " << playerId.value << ": enemy air threat "
                     << (loggedAirThreat ? "detected" : "expired")
                     << " at tick " << sim.gameTime.value
                     << ", aircraft known " << blackboard.knownEnemyAirCount;
        }

        // 4. Which phase of the game are we in?
        auto previousPhase = blackboard.phase;
        strategic.update(profile, blackboard);
        if (blackboard.phase != previousPhase)
        {
            LOG_INFO << "AI player " << playerId.value << " (" << profile.name << "): " << gamePhaseName(previousPhase) << " -> " << gamePhaseName(blackboard.phase)
                     << " at tick " << sim.gameTime.value << ", army " << blackboard.armySize << ", known enemies " << blackboard.knownEnemies.size();
        }

        // Periodic status line for play-test logs.
        if (sim.gameTime.value % (30u * SimTicksPerSecond) == 0)
        {
            std::string counts;
            for (const auto& [type, count] : blackboard.ownedTotalCounts)
            {
                counts += " " + type + "x" + std::to_string(count);
            }
            std::string commanderDoing = "no commander";
            if (blackboard.commanderUnitId)
            {
                const auto& commander = sim.getUnitState(*blackboard.commanderUnitId);
                commanderDoing = "commander at " + std::to_string(static_cast<int>(commander.position.x.value)) + "," + std::to_string(static_cast<int>(commander.position.z.value));
                if (commander.orders.empty())
                {
                    commanderDoing += " idle";
                }
                else
                {
                    commanderDoing += match(
                        commander.orders.front(),
                        [](const BuildOrder& o) { return " building " + o.unitType + " at " + std::to_string(static_cast<int>(o.position.x.value)) + "," + std::to_string(static_cast<int>(o.position.z.value)); },
                        [](const MoveOrder&) { return std::string(" moving"); },
                        [](const GuardOrder&) { return std::string(" guarding"); },
                        [](const auto&) { return std::string(" on another order"); });
                    commanderDoing += std::holds_alternative<NavigationStateMoving>(commander.navigationState.state) ? " (walking)" : " (not walking)";
                    commanderDoing += commander.buildOrderUnitId ? " nanoframe placed" : " no nanoframe";
                }
            }
            const auto& player = sim.getPlayer(playerId);
            LOG_INFO << "AI player " << playerId.value << " status: phase " << gamePhaseName(blackboard.phase)
                     << ", metal " << blackboard.currentMetal.value << (blackboard.metalStalled ? "(stalled)" : "")
                     << " (+" << player.previousMetalProductionBuffer.value << "/-" << player.previousDesiredMetalConsumptionBuffer.value << " per s)"
                     << ", energy " << blackboard.currentEnergy.value << (blackboard.energyStalled ? "(stalled)" : "")
                     << " (+" << player.previousEnergyProductionBuffer.value << "/-" << player.previousDesiredEnergyConsumptionBuffer.value << " per s)"
                     << ", idle builders " << blackboard.idleBuilderCount << ", army " << blackboard.armySize
                     << ", known enemies " << blackboard.knownEnemies.size()
                     // Both gate whether a ferry can run at all: hasUnreachableGround
                     // gates refreshExpansionSite, wantsTransport is what BuildManager
                     // reads before building one. Each was computed every pass and
                     // discarded, so a ferry that never fired left nothing to read.
                     << ", unreachable ground " << (blackboard.hasUnreachableGround ? "yes" : "no")
                     << ", wants transport " << (blackboard.wantsTransport ? "yes" : "no")
                     << " (expansion site " << (blackboard.hasExpansionSite ? "yes" : "no")
                     << ", enemy across water " << (blackboard.enemyAcrossWater ? "yes" : "no") << ")"
                     << ", naval scout " << (blackboard.navalScoutUnitId ? "yes" : "no")
                     << ", units:" << counts << "; " << commanderDoing;
        }

        // 5. Economy and production.
        //
        // The makers go first: what they are switched to decides how much
        // energy the build pass below has to spend.
        timed("makers", [&] { metalMakers.update(sim, profile, blackboard, outCommands); });
        timed("build", [&] { build.update(sim, playerId, profile, blackboard, reachability, rng, outCommands); });

        // 6. Eyes, lift and fists.
        timed("scout", [&] { scout.update(sim, profile, threatMap, reachability, blackboard, outCommands); });
        timed("transport", [&] { transport.update(sim, playerId, profile, reachability, build, blackboard, rng, outCommands); });
        timed("army", [&] { army.update(sim, playerId, profile, threatMap, blackboard, outCommands); });
        timed("air", [&] { air.update(sim, playerId, profile, threatMap, blackboard, outCommands); });

        if (profiling && sim.gameTime.value % AiProfileReportIntervalTicks == 0)
        {
            profiler.report(playerId, sim.gameTime);
        }
    }
}
