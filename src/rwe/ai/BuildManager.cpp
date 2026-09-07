#include "BuildManager.h"
#include <rwe/sim/SimRandom.h>
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <tuple>

namespace rwe
{
    namespace
    {
        int countOf(const std::map<std::string, int>& counts, const std::string& unitType)
        {
            auto it = counts.find(unitType);
            return it == counts.end() ? 0 : it->second;
        }

        PlayerCommand buildCommand(UnitId builder, const std::string& unitType, const SimVector& site)
        {
            return PlayerUnitCommand(builder, PlayerUnitCommand::IssueOrder(BuildOrder(unitType, site), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
        }
    }

    BuildManager::BuildEstimate BuildManager::estimateBuild(const UnitDefinition& target, const UnitDefinition& builder, unsigned int alreadyBuilt)
    {
        // A builder adds workerTimePerTick to the frame each tick and the
        // frame is done at buildTime, paying for itself pro rata as it goes
        // (UnitState::getBuildCostInfo). So what is left to pay is the
        // unbuilt fraction of the price, and the time is the unbuilt work
        // at the builder's rate.
        auto total = std::max(1u, target.buildTime);
        auto remaining = total - std::min(alreadyBuilt, total);
        auto fraction = static_cast<float>(remaining) / static_cast<float>(total);
        auto rate = std::max(1u, builder.workerTimePerTick);
        BuildEstimate estimate;
        estimate.metal = target.buildCostMetal.value * fraction;
        estimate.seconds = static_cast<float>(remaining) / static_cast<float>(rate) / static_cast<float>(SimTicksPerSecond);
        return estimate;
    }

    bool BuildManager::canAfford(const AiBlackboard& bb, const BuildEstimate& estimate, int extraSeconds)
    {
        if (bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.9f)
        {
            return true;
        }
        auto net = bb.metalIncome.value - bb.metalCommitted.value;
        return bb.currentMetal.value + (net * (estimate.seconds + static_cast<float>(extraSeconds))) >= estimate.metal;
    }

    void BuildManager::indexMetalPatches(const GameSimulation& sim) const
    {
        if (metalPatchesIndexed)
        {
            return;
        }
        metalPatchesIndexed = true;
        const auto& metalGrid = sim.metalGrid;
        for (int y = 0; y < metalGrid.getHeight(); ++y)
        {
            for (int x = 0; x < metalGrid.getWidth(); ++x)
            {
                if (metalGrid.get(x, y) > sim.surfaceMetal)
                {
                    metalPatches.emplace_back(x, y);
                }
            }
        }
    }

    std::optional<SimVector> BuildManager::chooseBuildSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const std::string& unitType,
        const SimVector& anchor,
        std::minstd_rand& rng) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        const auto& def = defIt->second;
        const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);

        // Lay buildings out on a grid with a two-tile lane between them, wide
        // enough for a commander to walk through, so nothing gets walled in.
        auto footprint = sim.getFootprintXZ(def.movementCollisionInfo);
        auto spacingTiles = static_cast<float>(std::max(footprint.first, footprint.second) + 2);
        const SimScalar spacing = SimScalar(spacingTiles * MapTerrain::HeightTileWidthInWorldUnits.value);
        const SimScalar radius = profile.maxMexSearchRadius;
        const int ringCount = std::max(1, static_cast<int>(radius.value / spacing.value));

        std::vector<SimVector> tiedCandidates;
        for (int ring = 1; ring <= ringCount; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                    {
                        continue;
                    }

                    SimVector candidate(anchor.x + (SimScalar(static_cast<float>(dx)) * spacing), anchor.y, anchor.z + (SimScalar(static_cast<float>(dz)) * spacing));
                    candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);

                    auto rect = sim.computeFootprintRegion(candidate, def.movementCollisionInfo);
                    if (rect.x < 0 || rect.y < 0)
                    {
                        continue;
                    }
                    // Don't plant a building on a metal patch; mexes want
                    // those. Checked before canBeBuiltAt, which walks the
                    // whole footprint and is much the more expensive test.
                    if (rect.x < sim.metalGrid.getWidth() && rect.y < sim.metalGrid.getHeight()
                        && sim.metalGrid.get(rect.x, rect.y) > sim.surfaceMetal)
                    {
                        continue;
                    }
                    if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                    {
                        continue;
                    }
                    tiedCandidates.push_back(candidate);
                }
            }
            if (!tiedCandidates.empty())
            {
                break;
            }
        }

        if (tiedCandidates.empty())
        {
            return std::nullopt;
        }
        return tiedCandidates[randomBelow(rng, static_cast<unsigned int>(tiedCandidates.size()))];
    }

    std::optional<SimVector> BuildManager::chooseMexSite(
        const GameSimulation& sim,
        const std::string& unitType,
        const SimVector& anchor,
        SimScalar radius,
        std::minstd_rand& rng,
        const std::function<bool(const SimVector&)>& accept) const
    {
        const auto& metalGrid = sim.metalGrid;
        const auto anchorHm = sim.terrain.worldToHeightmapCoordinate(anchor);
        const int radiusInTiles = std::max(1, static_cast<int>(radius.value / MapTerrain::HeightTileWidthInWorldUnits.value));

        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        const auto& def = defIt->second;
        const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);

        // Metal under a footprint placed at a cell; only patches count, not the
        // map's ordinary surface metal.
        auto patchMetalUnder = [&](const DiscreteRect& rect) {
            unsigned int total = 0;
            for (int y = rect.y; y < rect.y + static_cast<int>(rect.height); ++y)
            {
                for (int x = rect.x; x < rect.x + static_cast<int>(rect.width); ++x)
                {
                    if (x >= 0 && y >= 0 && x < metalGrid.getWidth() && y < metalGrid.getHeight() && metalGrid.get(x, y) > sim.surfaceMetal)
                    {
                        total += metalGrid.get(x, y);
                    }
                }
            }
            return total;
        };

        // Only cells that actually hold metal can win, and the map's patches
        // never move, so walk the known patches ring by ring instead of every
        // cell out to the radius. Sorting by (ring, dz, dx) visits them in
        // exactly the order the old whole-map ring scan did, which keeps the
        // tie-breaking - and therefore the RNG draw below - unchanged.
        indexMetalPatches(sim);
        struct RingCell
        {
            int ring;
            int dz;
            int dx;
        };
        std::vector<RingCell> ringCells;
        ringCells.reserve(metalPatches.size());
        for (const auto& patch : metalPatches)
        {
            auto dx = patch.x - anchorHm.x;
            auto dz = patch.y - anchorHm.y;
            auto ring = std::max(std::abs(dx), std::abs(dz));
            if (ring > radiusInTiles)
            {
                continue;
            }
            ringCells.push_back(RingCell{ring, dz, dx});
        }
        std::sort(ringCells.begin(), ringCells.end(), [](const RingCell& a, const RingCell& b) {
            return std::tie(a.ring, a.dz, a.dx) < std::tie(b.ring, b.dz, b.dx);
        });

        // Take the richest buildable patch on the nearest ring that has one.
        std::vector<SimVector> tiedCandidates;
        unsigned int bestMetal = 0;
        for (std::size_t i = 0; i < ringCells.size(); ++i)
        {
            const int gx = anchorHm.x + ringCells[i].dx;
            const int gz = anchorHm.y + ringCells[i].dz;

            SimVector candidate = sim.terrain.heightmapIndexToWorldCenter(gx, gz);
            candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);
            bool usable = !accept || accept(candidate);
            if (usable)
            {
                auto rect = sim.computeFootprintRegion(candidate, def.movementCollisionInfo);
                usable = rect.x >= 0 && rect.y >= 0
                    && sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y));
                if (usable)
                {
                    auto metal = patchMetalUnder(rect);
                    if (metal > bestMetal)
                    {
                        bestMetal = metal;
                        tiedCandidates.clear();
                        tiedCandidates.push_back(candidate);
                    }
                    else if (metal == bestMetal && metal > 0)
                    {
                        tiedCandidates.push_back(candidate);
                    }
                }
            }

            // Stop at the end of the first ring that turned something up.
            bool ringEnds = (i + 1 == ringCells.size()) || ringCells[i + 1].ring != ringCells[i].ring;
            if (ringEnds && !tiedCandidates.empty())
            {
                break;
            }
        }

        if (tiedCandidates.empty())
        {
            return std::nullopt;
        }
        return tiedCandidates[randomBelow(rng, static_cast<unsigned int>(tiedCandidates.size()))];
    }

    std::vector<std::string> BuildManager::buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase) const
    {
        const auto& s = bb.sideUnits;
        // Count what exists or is already going up, so we don't double up.
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        std::vector<std::string> wanted;
        auto want = [&](const std::string& t) {
            if (!t.empty() && std::find(wanted.begin(), wanted.end(), t) == wanted.end())
            {
                wanted.push_back(t);
            }
        };

        // A builder ferried to an island the base cannot walk to runs an
        // outpost: it takes the metal there and powers its own extractors,
        // but leaves the factories and towers to the main base.
        if (!builderAtBase)
        {
            want(s.metalExtractor);
            want(s.solar);
            return wanted;
        }

        // Energy first if the lights are out, otherwise metal first: every
        // later build is paced by metal income, and the starting stockpile
        // covers the opening. Each need is listed in turn; the planner takes
        // the first one it can find a site for, so an opening without a
        // third metal patch nearby still gets its solars and lab.
        if (bb.energyStalled)
        {
            want(s.solar);
        }
        if (total(s.metalExtractor) < profile.openingMetalExtractorCount)
        {
            want(s.metalExtractor);
        }
        if (total(s.solar) < profile.openingSolarCount)
        {
            want(s.solar);
        }
        // The stall flag flickers off for a moment whenever a build finishes,
        // so judge metal by the stockpile as well: under a tenth of storage is short.
        auto metalShort = bb.metalStalled || (bb.metalStorage.value > 0.0f && bb.currentMetal.value < bb.metalStorage.value * 0.1f);

        // Don't sink the commander into a factory while metal is short; more
        // extractors first, as long as there are patches to take.
        if (metalShort && total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            want(s.metalExtractor);
        }
        if (total(s.lab) < 1)
        {
            want(s.lab);
        }

        // Anti-air, and it goes here -- above the radar, the towers and the
        // second factory -- whenever aircraft are actually in the picture.
        // The AI had no answer to air at all before this: nothing it built
        // was chosen for it and nothing it owned was kept back for it, so a
        // single bomber could work through a base unopposed. A Defender is 79
        // metal against a bomber that costs several times that, and the
        // exchange only gets better the longer the bomber keeps coming back.
        //
        // Deliberately not gated on metalShort. Saving up while being bombed
        // is how a base ends up with neither the metal nor the buildings.
        auto antiAirWanted = bb.enemyAirThreat ? profile.reactiveAntiAirTowerCount : profile.baseAntiAirTowerCount;
        if (bb.enemyAirThreat && total(s.antiAirTower) < antiAirWanted)
        {
            want(s.antiAirTower);
        }
        // Out of patches but swimming in energy: turn energy into metal.
        // A full tank is not the same as a surplus, and telling them apart is
        // the difference between an economy and a trap. The AI used to build a
        // maker whenever storage happened to be near full, which on six solar
        // collectors it briefly is -- and each maker then draws sixty energy a
        // second for ever, so two of them ate the whole generation and left
        // nothing to build with. Ten minutes into an arena game it sat at zero
        // energy with demand at 191 against income of 138.
        //
        // So: near-full storage AND production actually running ahead of
        // demand. MetalMakerManager switches the ones we have off when that
        // stops being true, which is also what stops this rule seeing a full
        // tank that only looks full because nothing can afford to spend it.
        auto energySurplus = bb.energyIncome.value > bb.energyDemand.value;
        auto energyRich = bb.energyStorage.value > 0.0f && bb.currentEnergy.value >= bb.energyStorage.value * 0.8f && energySurplus;
        if (metalShort && energyRich && total(s.metalMaker) < profile.targetMetalMakerCount && total(s.lab) >= 1)
        {
            want(s.metalMaker);
        }
        if (total(s.radar) < profile.targetRadarCount && total(s.solar) >= profile.openingSolarCount)
        {
            want(s.radar);
        }
        // Towers are a luxury while metal is short; the factory needs it more.
        if (!metalShort && total(s.lightLaserTower) < profile.targetDefenceCount && total(s.lab) >= 1)
        {
            want(s.lightLaserTower);
        }
        // The standing anti-air, for the case where nothing has flown over
        // yet. The first bombing run arrives before anyone has scouted the
        // airfield that launched it, so waiting for proof is waiting too
        // long -- but one tower, down here with the other luxuries, is all
        // that buys. (A dedupe in want() means the reactive rule above keeps
        // its higher position when both fire.)
        if (!metalShort && total(s.antiAirTower) < antiAirWanted && total(s.lab) >= 1)
        {
            want(s.antiAirTower);
        }
        // An air plant for scout planes, and for transports when there is
        // ground to reach that no one can walk to. Metal is nearly always
        // short on a poor map, so this is not gated on it: a blind AI is
        // worth less than a slow one.
        //
        // On a map where the ground runs out, the air plant stops being a
        // convenience and becomes the only way anything crosses at all, so it
        // comes forward: no waiting on the radar first. That is read off the
        // map before the game starts, the way a player reads it off the
        // preview, and confirmed later by the reachability pass finding
        // ground it cannot walk to.
        auto airMatters = (bb.mapIntel.valid && bb.mapIntel.character != MapCharacter::Land) || bb.hasUnreachableGround;

        // On a map where the ground does not run out, an air plant is 850
        // metal that buys one 40-metal scout and then stands idle for the
        // rest of the game -- planFactories has nothing else to give it,
        // since the transport is only wanted when there is water to cross.
        // Measured over three seeds it was 21% of all the metal the AI
        // committed in ten minutes, and it owned the commander for two to
        // five minutes at the point the economy most needed building. So on
        // land it waits until the extractors it is competing with are up.
        auto airPlantAffordable = airMatters || total(s.metalExtractor) >= profile.targetMetalExtractorCount;
        if (total(s.lab) >= 1 && total(s.airPlant) < profile.targetAirPlantCount && total(s.solar) >= profile.openingSolarCount
            && airPlantAffordable
            && (airMatters || total(s.radar) >= profile.targetRadarCount))
        {
            want(s.airPlant);
        }
        if (total(s.solar) < profile.targetSolarCount)
        {
            want(s.solar);
        }
        if (total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            want(s.metalExtractor);
        }
        // A vehicle plant comes last: fast scouts and tanks once the economy is ticking over.
        if (!metalShort && total(s.airPlant) >= profile.targetAirPlantCount && total(s.vehiclePlant) < profile.targetVehiclePlantCount)
        {
            want(s.vehiclePlant);
        }
        // Replace what was just destroyed before getting on with the plan.
        //
        // Without this a razed base is rebuilt in generic priority order,
        // which is the order a base is built in from nothing -- so an AI that
        // has just lost its radar and two solars to a raid goes back to the
        // top of the list and works down, and may not reach the radar for a
        // long time. Anything lost lately that is still wanted is moved to
        // the front, most recently lost first, and the rest of the plan
        // follows behind it unchanged.
        if (!bb.recentLosses.empty())
        {
            std::vector<std::string> urgent;
            for (const auto& loss : bb.recentLosses)
            {
                auto it = std::find(wanted.begin(), wanted.end(), loss.unitType);
                if (it == wanted.end())
                {
                    // Already replaced, or not something we want any more.
                    continue;
                }
                if (std::find(urgent.begin(), urgent.end(), loss.unitType) != urgent.end())
                {
                    continue;
                }
                urgent.push_back(loss.unitType);
                wanted.erase(it);
            }
            wanted.insert(wanted.begin(), urgent.begin(), urgent.end());
        }

        return wanted;
    }

    void BuildManager::planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const
    {
        const auto& s = bb.sideUnits;
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        for (auto factoryId : bb.factories)
        {
            const auto& factory = sim.getUnitState(factoryId);
            if (!factory.buildQueue.empty())
            {
                continue;
            }

            std::string next;
            if (!s.airPlant.empty() && factory.unitType == s.airPlant)
            {
                // Eyes first, then lift when it is needed. Otherwise the plant waits.
                if (!s.scoutPlane.empty() && total(s.scoutPlane) < profile.targetScoutPlaneCount)
                {
                    next = s.scoutPlane;
                }
                else if (!s.airTransport.empty() && bb.wantsTransport && total(s.airTransport) < profile.targetTransportCount)
                {
                    next = s.airTransport;
                }
            }
            else if (!s.vehiclePlant.empty() && factory.unitType == s.vehiclePlant)
            {
                if (!s.scoutVehicle.empty() && total(s.scoutVehicle) < profile.targetScoutVehicleCount)
                {
                    next = s.scoutVehicle;
                }
                else
                {
                    next = s.tank;
                }
            }
            else
            {
                auto constructors = s.constructor.empty() ? 0 : countOf(bb.ownedTotalCounts, s.constructor);
                if (!s.constructor.empty() && constructors < profile.targetConstructorCount)
                {
                    next = s.constructor;
                }
                else if (bb.enemyAirThreat && !s.antiAirKbot.empty() && total(s.antiAirKbot) < profile.antiAirMobileCount)
                {
                    // Ahead of the raiders: an anti-air kbot is level 1 and
                    // this lab can already build it, so the answer to being
                    // bombed does not need a second factory or a tech step.
                    next = s.antiAirKbot;
                }
                else if (!s.raider.empty() && !s.rocketKbot.empty())
                {
                    // Two raiders for every rocket kbot.
                    auto raiders = countOf(bb.ownedTotalCounts, s.raider);
                    auto rockets = countOf(bb.ownedTotalCounts, s.rocketKbot);
                    next = raiders <= rockets * 2 ? s.raider : s.rocketKbot;
                }
                else if (!s.raider.empty())
                {
                    next = s.raider;
                }
                else if (!s.rocketKbot.empty())
                {
                    next = s.rocketKbot;
                }
            }

            if (!next.empty())
            {
                outCommands.emplace_back(PlayerUnitCommand(factoryId, PlayerUnitCommand::ModifyBuildQueue{1, next}));
            }
        }
    }

    void BuildManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const ReachabilityMap& reachability,
        std::minstd_rand& rng,
        std::vector<PlayerCommand>& outCommands)
    {
        ++ticksSinceLastPlanning;
        if (ticksSinceLastPlanning < profile.buildPlannerTickInterval)
        {
            return;
        }
        ticksSinceLastPlanning = 0;

        if (!bb.baseAnchor || !bb.sideUnitsResolved)
        {
            return;
        }
        const auto& sideUnits = bb.sideUnits;

        planFactories(sim, profile, bb, outCommands);

        // One job per planning pass keeps counts honest: the next pass sees
        // the nanoframe in ownedTotalCounts and moves on to the next need.
        if (bb.idleBuilders.empty())
        {
            return;
        }
        auto builderId = bb.idleBuilders.front();
        const auto& builder = sim.getUnitState(builderId);
        const auto& builderDef = sim.unitDefinitions.at(builder.unitType);

        // A builder that cannot walk home is running an outpost: it builds around itself.
        bool builderAtBase = !bb.groundReachabilityValid || reachability.isReachable(sim, builder.position);
        auto anchor = builderAtBase ? *bb.baseAnchor : builder.position;

        // Extractors and makers are exempt from the affordability test
        // below: they are what makes the next thing affordable, and a
        // stalled extractor still finishes, just later. A solar collector
        // is exempt only while energy is actually wanted; otherwise it is
        // 165 metal like anything else, and the AI's energy is nearly always
        // in surplus.
        auto energyWanted = bb.energyStalled || (bb.energyStorage.value > 0.0f && bb.currentEnergy.value < bb.energyStorage.value * 0.5f);
        auto isEconomy = [&](const std::string& t) {
            return t == sideUnits.metalExtractor || t == sideUnits.metalMaker || (energyWanted && t == sideUnits.solar);
        };

        // A frame left standing comes before anything new. Its metal is
        // already half paid, the plan wanted it, and it is rotting.
        for (auto frameId : bb.orphanedFrames)
        {
            const auto& frame = sim.getUnitState(frameId);
            if (bb.groundReachabilityValid && reachability.isReachable(sim, frame.position) != builderAtBase)
            {
                continue;
            }
            auto estimate = estimateBuild(sim.unitDefinitions.at(frame.unitType), builderDef, frame.buildTimeCompleted);
            if (!isEconomy(frame.unitType) && !canAfford(bb, estimate))
            {
                continue;
            }
            LOG_INFO << "AI build: unit " << builderId.value << " resumes abandoned " << frame.unitType << " (" << estimate.metal << " metal left)";
            savingFor.clear();
            outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(frameId), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            return;
        }

        // Set when the builder is holding off for the stockpile to catch up
        // with the price of the thing it wants; it reclaims meanwhile and
        // does not lend a hand at the factory, since that would spend what
        // it is saving.
        bool saving = false;

        for (const auto& next : buildPriorities(profile, bb, builderAtBase))
        {
            auto nextDefIt = sim.unitDefinitions.find(next);
            if (nextDefIt == sim.unitDefinitions.end())
            {
                continue;
            }
            if (!isEconomy(next))
            {
                auto estimate = estimateBuild(nextDefIt->second, builderDef);
                if (!canAfford(bb, estimate))
                {
                    if (canAfford(bb, estimate, profile.saveUpSeconds))
                    {
                        // Within reach: wait for it rather than spend the
                        // money on something further down the list, which is
                        // how the expensive things never get built.
                        if (savingFor != next)
                        {
                            savingFor = next;
                            LOG_INFO << "AI build: unit " << builderId.value << " saving for " << next << ": " << estimate.metal << " metal, have " << bb.currentMetal.value
                                     << ", net " << (bb.metalIncome.value - bb.metalCommitted.value) << "/s";
                        }
                        saving = true;
                        break;
                    }
                    LOG_DEBUG << "AI build: cannot afford " << next << " (" << estimate.metal << " metal over " << estimate.seconds << " s), skipping";
                    continue;
                }
            }

            std::optional<SimVector> site;
            if (next == sideUnits.metalExtractor)
            {
                // Nearby patches first; further afield if there are none.
                // Only patches the builder can walk to: islands are for the transport.
                std::function<bool(const SimVector&)> walkable;
                if (bb.groundReachabilityValid)
                {
                    walkable = [&](const SimVector& p) { return reachability.isReachable(sim, p) == builderAtBase; };
                }
                site = chooseMexSite(sim, next, builder.position, profile.nearMexSearchRadius, rng, walkable);
                if (!site && builderAtBase)
                {
                    // Expanding, as opposed to filling in around the base,
                    // takes ground we have actually looked at. Metal shows on
                    // a player's map only where that player has explored, so
                    // an AI reading the whole metal grid is claiming patches
                    // it has no business knowing about.
                    //
                    // The near search above is deliberately left alone. What
                    // is inside maxMexSearchRadius of a builder standing in
                    // its own base is ground that base can see, and gating it
                    // as well starves the opening: an AI that has not built a
                    // scout yet then cannot expand at all, and an AI with no
                    // metal never builds the scout.
                    auto exploredAndWalkable = [&](const SimVector& p) {
                        if (!profile.cheatModeOmniscient && !sim.isExploredBy(aiOwner, p))
                        {
                            return false;
                        }
                        return !walkable || walkable(p);
                    };
                    site = chooseMexSite(sim, next, *bb.baseAnchor, profile.expansionMexSearchRadius, rng, exploredAndWalkable);
                }
            }
            else if (next == sideUnits.lightLaserTower && bb.enemyBasePosition)
            {
                // Defences go on the side of the base that faces the enemy.
                auto towards = (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                auto towerAnchor = *bb.baseAnchor + (towards * profile.defenceDistanceFromBase);
                site = chooseBuildSite(sim, profile, next, towerAnchor, rng);
            }
            else
            {
                site = chooseBuildSite(sim, profile, next, anchor, rng);
            }

            if (site)
            {
                LOG_DEBUG << "AI build: unit " << builderId.value << " to build " << next << " at " << site->x.value << "," << site->z.value;
                savingFor.clear();
                outCommands.push_back(buildCommand(builderId, next, *site));
                return;
            }
            LOG_DEBUG << "AI build: no site found for " << next << " near " << builder.position.x.value << "," << builder.position.z.value;
        }

        // Nothing to build, nowhere to build it, or saving up: harvest the
        // battlefield. Wreck fields are a real economy -- the standing advice
        // is to work them even deep in enemy territory -- and so are the
        // rocks, which is what a player's commander spends the opening on
        // between extractors. Metal first, nearest first; a tree is only
        // worth the walk when energy is actually wanted, which on the maps
        // measured it hardly ever is.
        if (builderAtBase)
        {
            const auto reachSquared = SimScalar(1200.0f * 1200.0f);
            std::optional<FeatureId> best;
            bool bestHasMetal = false;
            auto bestDistanceSquared = reachSquared;
            for (const auto& [featureId, feature] : sim.features)
            {
                const auto& featureDefinition = sim.getFeatureDefinition(feature.featureName);
                if (!featureDefinition.reclaimable)
                {
                    continue;
                }
                bool hasMetal = featureDefinition.metal > 0;
                if (!hasMetal && !(energyWanted && featureDefinition.energy > 0))
                {
                    continue;
                }
                // A wreck we have never had eyes on is not ours to know
                // about. Explored rather than visible, because a player keeps
                // seeing wreckage in ground they have already uncovered.
                if (!profile.cheatModeOmniscient && !sim.isExploredBy(aiOwner, feature.position))
                {
                    continue;
                }
                auto distanceSquared = bb.baseAnchor->distanceSquared(feature.position);
                if (distanceSquared >= reachSquared)
                {
                    continue;
                }
                if (!best || std::make_pair(!hasMetal, distanceSquared) < std::make_pair(!bestHasMetal, bestDistanceSquared))
                {
                    bestDistanceSquared = distanceSquared;
                    bestHasMetal = hasMetal;
                    best = featureId;
                }
            }
            if (best)
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(ReclaimOrder(*best), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // Otherwise lend a hand at the factory.
        if (!bb.factories.empty() && builderAtBase && !saving)
        {
            const auto& factory = sim.getUnitState(bb.factories.front());
            if (!factory.buildQueue.empty())
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(GuardOrder(bb.factories.front()), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
    }
}
