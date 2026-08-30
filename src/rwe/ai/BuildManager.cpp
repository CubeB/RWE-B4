#include "BuildManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/MapTerrain.h>
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
        std::uniform_int_distribution<std::size_t> dist(0, tiedCandidates.size() - 1);
        return tiedCandidates[dist(rng)];
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
        std::uniform_int_distribution<std::size_t> dist(0, tiedCandidates.size() - 1);
        return tiedCandidates[dist(rng)];
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
        // Out of patches but swimming in energy: turn energy into metal.
        auto energyRich = bb.energyStorage.value > 0.0f && bb.currentEnergy.value >= bb.energyStorage.value * 0.8f;
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
        // An air plant for scout planes, and for transports when there is
        // ground to reach that no one can walk to. Metal is nearly always
        // short on a poor map, so this is not gated on it: a blind AI is
        // worth less than a slow one.
        if (total(s.lab) >= 1 && total(s.airPlant) < profile.targetAirPlantCount && total(s.solar) >= profile.openingSolarCount && total(s.radar) >= profile.targetRadarCount)
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
        (void)aiOwner;
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

        // A builder that cannot walk home is running an outpost: it builds around itself.
        bool builderAtBase = !bb.groundReachabilityValid || reachability.isReachable(sim, builder.position);
        auto anchor = builderAtBase ? *bb.baseAnchor : builder.position;

        for (const auto& next : buildPriorities(profile, bb, builderAtBase))
        {
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
                site = chooseMexSite(sim, next, builder.position, profile.maxMexSearchRadius, rng, walkable);
                if (!site && builderAtBase)
                {
                    site = chooseMexSite(sim, next, *bb.baseAnchor, profile.expansionMexSearchRadius, rng, walkable);
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
                outCommands.push_back(buildCommand(builderId, next, *site));
                return;
            }
            LOG_DEBUG << "AI build: no site found for " << next << " near " << builder.position.x.value << "," << builder.position.z.value;
        }

        // Nothing to build (or nowhere to build it): lend a hand at the factory.
        if (!bb.factories.empty() && builderAtBase)
        {
            const auto& factory = sim.getUnitState(bb.factories.front());
            if (!factory.buildQueue.empty())
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(GuardOrder(bb.factories.front()), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
    }
}
