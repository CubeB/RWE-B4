#include "BuildManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    namespace
    {
        int countOf(const std::map<std::string, int>& counts, const std::string& unitType)
        {
            auto it = counts.find(unitType);
            return it == counts.end() ? 0 : it->second;
        }

        bool isDefined(const GameSimulation& sim, const std::string& unitType)
        {
            return !unitType.empty() && sim.unitDefinitions.find(unitType) != sim.unitDefinitions.end();
        }

        PlayerCommand buildCommand(UnitId builder, const std::string& unitType, const SimVector& site)
        {
            return PlayerUnitCommand(builder, PlayerUnitCommand::IssueOrder(BuildOrder(unitType, site), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
        }
    }

    void BuildManager::resolveSide(const GameSimulation& sim, PlayerId aiOwner)
    {
        if (sideResolved)
        {
            return;
        }
        sideResolved = true;

        auto side = sim.getPlayer(aiOwner).side;
        std::string upper;
        for (auto c : side)
        {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }

        if (upper == "CORE")
        {
            sideUnits = AiSideUnits{"CORMEX", "CORSOLAR", "CORLAB", "CORCK", "CORAK", "CORSTORM", "CORLLT", "CORRAD", "CORMAKR"};
        }
        else
        {
            sideUnits = AiSideUnits{"ARMMEX", "ARMSOLAR", "ARMLAB", "ARMCK", "ARMPW", "ARMROCK", "ARMLLT", "ARMRAD", "ARMMAKR"};
        }

        // Anything the game data does not define is simply never built.
        auto check = [&](std::string& name) {
            if (!isDefined(sim, name))
            {
                name.clear();
            }
        };
        check(sideUnits.metalExtractor);
        check(sideUnits.solar);
        check(sideUnits.lab);
        check(sideUnits.constructor);
        check(sideUnits.raider);
        check(sideUnits.rocketKbot);
        check(sideUnits.lightLaserTower);
        check(sideUnits.radar);
        check(sideUnits.metalMaker);
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
                    if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                    {
                        continue;
                    }
                    // Don't plant a building on a metal patch; mexes want those.
                    if (rect.x < sim.metalGrid.getWidth() && rect.y < sim.metalGrid.getHeight()
                        && sim.metalGrid.get(rect.x, rect.y) > sim.surfaceMetal)
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
        std::minstd_rand& rng) const
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

        // Take the richest buildable patch on the nearest ring that has one.
        std::vector<SimVector> tiedCandidates;
        unsigned int bestMetal = 0;
        for (int ring = 0; ring <= radiusInTiles; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                    {
                        continue;
                    }
                    const int gx = anchorHm.x + dx;
                    const int gz = anchorHm.y + dz;
                    if (gx < 0 || gz < 0 || gx >= metalGrid.getWidth() || gz >= metalGrid.getHeight())
                    {
                        continue;
                    }
                    if (metalGrid.get(gx, gz) <= sim.surfaceMetal)
                    {
                        continue;
                    }

                    SimVector candidate = sim.terrain.heightmapIndexToWorldCenter(gx, gz);
                    candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);
                    auto rect = sim.computeFootprintRegion(candidate, def.movementCollisionInfo);
                    if (rect.x < 0 || rect.y < 0)
                    {
                        continue;
                    }
                    if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                    {
                        continue;
                    }
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

    std::vector<std::string> BuildManager::buildPriorities(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb) const
    {
        (void)sim;
        const auto& s = sideUnits;
        // Count what exists or is already going up, so we don't double up.
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        std::vector<std::string> wanted;
        auto want = [&](const std::string& t) {
            if (!t.empty() && std::find(wanted.begin(), wanted.end(), t) == wanted.end())
            {
                wanted.push_back(t);
            }
        };

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
        if (total(s.solar) < profile.targetSolarCount)
        {
            want(s.solar);
        }
        if (total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            want(s.metalExtractor);
        }
        return wanted;
    }

    void BuildManager::planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const
    {
        const auto& s = sideUnits;
        for (auto factoryId : bb.factories)
        {
            const auto& factory = sim.getUnitState(factoryId);
            if (!factory.buildQueue.empty())
            {
                continue;
            }

            auto constructors = s.constructor.empty() ? 0 : countOf(bb.ownedTotalCounts, s.constructor);
            std::string next;
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
        std::minstd_rand& rng,
        std::vector<PlayerCommand>& outCommands)
    {
        ++ticksSinceLastPlanning;
        if (ticksSinceLastPlanning < profile.buildPlannerTickInterval)
        {
            return;
        }
        ticksSinceLastPlanning = 0;

        if (!bb.baseAnchor)
        {
            return;
        }
        resolveSide(sim, aiOwner);

        planFactories(sim, profile, bb, outCommands);

        // One job per planning pass keeps counts honest: the next pass sees
        // the nanoframe in ownedTotalCounts and moves on to the next need.
        if (bb.idleBuilders.empty())
        {
            return;
        }
        auto builderId = bb.idleBuilders.front();
        const auto& builder = sim.getUnitState(builderId);

        for (const auto& next : buildPriorities(sim, profile, bb))
        {
            std::optional<SimVector> site;
            if (next == sideUnits.metalExtractor)
            {
                // Nearby patches first; further afield if there are none.
                site = chooseMexSite(sim, next, builder.position, profile.maxMexSearchRadius, rng);
                if (!site)
                {
                    site = chooseMexSite(sim, next, *bb.baseAnchor, profile.expansionMexSearchRadius, rng);
                }
            }
            else if (next == sideUnits.lightLaserTower && bb.enemyBasePosition)
            {
                // Defences go on the side of the base that faces the enemy.
                auto towards = (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                auto anchor = *bb.baseAnchor + (towards * profile.defenceDistanceFromBase);
                site = chooseBuildSite(sim, profile, next, anchor, rng);
            }
            else
            {
                site = chooseBuildSite(sim, profile, next, *bb.baseAnchor, rng);
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
        if (!bb.factories.empty())
        {
            const auto& factory = sim.getUnitState(bb.factories.front());
            if (!factory.buildQueue.empty())
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(GuardOrder(bb.factories.front()), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
    }
}
