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
            sideUnits = AiSideUnits{"CORMEX", "CORSOLAR", "CORLAB", "CORCK", "CORAK", "CORSTORM", "CORLLT", "CORRAD"};
        }
        else
        {
            sideUnits = AiSideUnits{"ARMMEX", "ARMSOLAR", "ARMLAB", "ARMCK", "ARMPW", "ARMROCK", "ARMLLT", "ARMRAD"};
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

        std::vector<SimVector> tiedCandidates;
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
                    if (metalGrid.get(gx, gz) == 0)
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

    std::optional<std::string> BuildManager::chooseNextBuilding(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb) const
    {
        (void)sim;
        const auto& s = sideUnits;
        // Count what exists or is already going up, so we don't double up.
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        // Keep the lights on first.
        if (!s.solar.empty() && (bb.energyStalled || total(s.solar) < profile.openingSolarCount))
        {
            return s.solar;
        }
        if (!s.metalExtractor.empty() && total(s.metalExtractor) < profile.openingMetalExtractorCount)
        {
            return s.metalExtractor;
        }
        if (!s.lab.empty() && total(s.lab) < 1)
        {
            return s.lab;
        }
        if (!s.metalExtractor.empty() && bb.metalStalled && total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            return s.metalExtractor;
        }
        if (!s.radar.empty() && total(s.radar) < profile.targetRadarCount && total(s.solar) >= profile.openingSolarCount)
        {
            return s.radar;
        }
        if (!s.lightLaserTower.empty() && total(s.lightLaserTower) < profile.targetDefenceCount && total(s.lab) >= 1)
        {
            return s.lightLaserTower;
        }
        if (!s.solar.empty() && total(s.solar) < profile.targetSolarCount)
        {
            return s.solar;
        }
        if (!s.metalExtractor.empty() && total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            return s.metalExtractor;
        }
        return std::nullopt;
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

        auto next = chooseNextBuilding(sim, profile, bb);
        if (!next)
        {
            // Nothing to build: lend a hand at the factory.
            if (!bb.factories.empty())
            {
                const auto& factory = sim.getUnitState(bb.factories.front());
                if (!factory.buildQueue.empty())
                {
                    outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(GuardOrder(bb.factories.front()), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                }
            }
            return;
        }

        std::optional<SimVector> site;
        if (*next == sideUnits.metalExtractor)
        {
            auto radius = bb.phase == GamePhase::Opening ? profile.maxMexSearchRadius : profile.expansionMexSearchRadius;
            site = chooseMexSite(sim, *next, builder.position, radius, rng);
        }
        else if (*next == sideUnits.lightLaserTower && bb.enemyBasePosition)
        {
            // Defences go on the side of the base that faces the enemy.
            auto towards = (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            auto anchor = *bb.baseAnchor + (towards * profile.defenceDistanceFromBase);
            site = chooseBuildSite(sim, profile, *next, anchor, rng);
        }
        else
        {
            site = chooseBuildSite(sim, profile, *next, *bb.baseAnchor, rng);
        }

        if (site)
        {
            LOG_DEBUG << "AI build: unit " << builderId.value << " to build " << *next << " at " << site->x.value << "," << site->z.value;
            outCommands.push_back(buildCommand(builderId, *next, *site));
        }
        else
        {
            LOG_DEBUG << "AI build: no site found for " << *next << " near " << builder.position.x.value << "," << builder.position.z.value;
        }
    }
}
