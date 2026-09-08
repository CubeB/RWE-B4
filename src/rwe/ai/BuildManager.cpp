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
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/util/rwe_string.h>
#include <tuple>
#include <variant>

namespace rwe
{
    namespace
    {
        int countOf(const std::map<std::string, int>& counts, const std::string& unitType)
        {
            auto it = counts.find(unitType);
            return it == counts.end() ? 0 : it->second;
        }

        /** The longest reach of any weapon the definition names; zero for an unarmed one. */
        SimScalar weaponRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            SimScalar best = 0_ss;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                auto it = weaponName.empty() ? sim.weaponDefinitions.end() : sim.weaponDefinitions.find(weaponName);
                if (it != sim.weaponDefinitions.end())
                {
                    best = std::max(best, it->second.maxRange);
                }
            }
            return best;
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

    float BuildManager::unitCombatValuePerMetal(const GameSimulation& sim, const std::string& unitType)
    {
        if (unitType.empty())
        {
            return 0.0f;
        }
        auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return 0.0f;
        }
        const auto& def = defIt->second;
        auto metal = def.buildCostMetal.value;
        if (metal <= 0.0f || def.maxHitPoints == 0)
        {
            return 0.0f;
        }

        // The best damage a second any one of its weapons manages, against
        // the default armour class. Not the sum: a unit rarely brings two
        // weapons to bear on the same target, and taking the best keeps a
        // token anti-air gun from flattering a tank.
        float bestDps = 0.0f;
        for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
        {
            if (weaponName.empty())
            {
                continue;
            }
            // Both the weapon table and the damage classes within it are
            // keyed upper case by the loader, while the FBI's own spelling
            // is whatever the unit's author typed.
            auto weaponIt = sim.weaponDefinitions.find(toUpper(weaponName));
            if (weaponIt == sim.weaponDefinitions.end())
            {
                continue;
            }
            const auto& weapon = weaponIt->second;
            auto damageIt = weapon.damage.find("DEFAULT");
            if (damageIt == weapon.damage.end())
            {
                continue;
            }
            auto reload = weapon.reloadTime.value;
            if (reload <= 0.0f)
            {
                continue;
            }
            auto shots = static_cast<float>(std::max(1, weapon.burst));
            bestDps = std::max(bestDps, static_cast<float>(damageIt->second) * shots / reload);
        }
        if (bestDps <= 0.0f)
        {
            return 0.0f;
        }
        return static_cast<float>(def.maxHitPoints) * bestDps / metal;
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

    namespace
    {
        /**
         * Tiles of clear ground between buildings. Two is the lane the ring
         * pitch below already leaves -- wide enough for a commander to walk
         * through -- but that pitch only spaces a building from others of its
         * own size: the spacing is computed from the footprint of the thing
         * being placed, so every size gets a different grid off the same
         * anchor and a small building can land hard against a large one. This
         * is the same lane, tested against what is actually standing.
         */
        constexpr int buildingClearanceTiles = 2;

        /**
         * And what a factory wants. A unit a factory finishes is given a
         * BuggerOffOrder out of the factory's own footprint; until it is out,
         * the pad is occupied and trySpawnUnit refuses to place the next one,
         * so the factory does not stall for a moment, it stalls for the rest
         * of the game. In a play-test one commander planted a factory across
         * the front of another and stopped it dead.
         */
        constexpr int factoryClearanceTiles = 3;

        /** A factory is the immobile thing that builds: the only building whose exit has to stay open. */
        bool isFactory(const UnitDefinition& def)
        {
            return !def.isMobile && def.builder;
        }

        /** Something already standing or going up, and the lane it wants kept clear around it. */
        struct PlacementObstacle
        {
            DiscreteRect rect;
            int margin{0};
        };

        /**
         * Every building near the anchor, with the lane each wants around it.
         *
         * Collected once per site search rather than once per candidate: the
         * ring scan asks about a few hundred places and this is a few dozen
         * rectangles. Owner is not consulted -- a lane blocked by somebody
         * else's building is just as blocked.
         */
        std::vector<PlacementObstacle> collectStandingBuildings(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const SimVector& anchor)
        {
            std::vector<PlacementObstacle> buildings;
            auto reach = profile.maxMexSearchRadius + profile.maxMexSearchRadius;
            for (const auto& [_, unit] : sim.units)
            {
                if (!unit.isAlive())
                {
                    continue;
                }
                auto defIt = sim.unitDefinitions.find(unit.unitType);
                if (defIt == sim.unitDefinitions.end() || defIt->second.isMobile)
                {
                    continue;
                }
                auto dx = unit.position.x - anchor.x;
                auto dz = unit.position.z - anchor.z;
                if (((dx * dx) + (dz * dz)) > (reach * reach))
                {
                    continue;
                }
                auto rect = sim.computeFootprintRegion(unit.position, defIt->second.movementCollisionInfo);
                buildings.push_back(PlacementObstacle{rect, isFactory(defIt->second) ? factoryClearanceTiles : buildingClearanceTiles});
            }
            return buildings;
        }

        /**
         * Does a footprint keep its distance from everything already standing?
         * The wider of the two lanes wins, so a solar collector next to a
         * factory is held off at the factory's distance.
         */
        bool clearsStandingBuildings(const std::vector<PlacementObstacle>& buildings, const DiscreteRect& rect, int ownMargin)
        {
            for (const auto& b : buildings)
            {
                auto margin = std::max(b.margin, ownMargin);
                if (rect.x - margin < b.rect.x + b.rect.width
                    && b.rect.x - margin < rect.x + rect.width
                    && rect.y - margin < b.rect.y + b.rect.height
                    && b.rect.y - margin < rect.y + rect.height)
                {
                    return false;
                }
            }
            return true;
        }

        /** A site a building fits on, and which ring around the anchor it was found on. */
        struct BuildableSite
        {
            SimVector position;
            int ring;
        };

        /**
         * Every site on the square rings around the anchor where the
         * building fits: nearest ring first, scan order within a ring, out
         * to the base radius -- or only the nearest ring that has room, when
         * asked for that. Buildings are laid out on a grid with a two-tile
         * lane between them, wide enough for a commander to walk through, so
         * nothing gets walled in.
         */
        std::vector<BuildableSite> collectBuildableSites(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const UnitDefinition& def,
            const SimVector& anchor,
            bool nearestRingOnly)
        {
            const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);
            auto footprint = sim.getFootprintXZ(def.movementCollisionInfo);
            auto spacingTiles = static_cast<float>(std::max(footprint.first, footprint.second) + 2);
            const SimScalar spacing = SimScalar(spacingTiles * MapTerrain::HeightTileWidthInWorldUnits.value);
            const SimScalar radius = profile.maxMexSearchRadius;
            const int ringCount = std::max(1, static_cast<int>(radius.value / spacing.value));

            const auto standing = collectStandingBuildings(sim, profile, anchor);
            const auto ownMargin = isFactory(def) ? factoryClearanceTiles : buildingClearanceTiles;

            std::vector<BuildableSite> sites;
            // Sites that fit but crowd something already up. Kept only as a
            // last resort: a base with nowhere left to put anything is worse
            // off refusing to build than it is packed tight.
            std::vector<BuildableSite> crowded;
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
                        if (!clearsStandingBuildings(standing, rect, ownMargin))
                        {
                            crowded.push_back(BuildableSite{candidate, ring});
                            continue;
                        }
                        sites.push_back(BuildableSite{candidate, ring});
                    }
                }
                if (nearestRingOnly && !sites.empty())
                {
                    break;
                }
            }
            return sites.empty() ? crowded : sites;
        }

        /** Distance across the ground, ignoring height: sites sit on the terrain and anchors do not. */
        SimScalar flatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }

        /**
         * Which way trouble comes from: the enemy base once one has been
         * seen, and the middle of the map until then -- the world origin is
         * the map's centre, and the centre is where the two sides meet.
         */
        SimVector threatDirection(const AiBlackboard& bb)
        {
            auto target = bb.enemyBasePosition ? *bb.enemyBasePosition : SimVector(0_ss, 0_ss, 0_ss);
            auto towards = target - *bb.baseAnchor;
            return SimVector(towards.x, 0_ss, towards.z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
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
        auto sites = collectBuildableSites(sim, profile, defIt->second, anchor, true);
        if (sites.empty())
        {
            return std::nullopt;
        }
        return sites[randomBelow(rng, static_cast<unsigned int>(sites.size()))].position;
    }

    std::optional<SimVector> BuildManager::chooseScoredBuildSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const std::string& unitType,
        const SimVector& anchor,
        std::minstd_rand& rng,
        const std::function<SiteScore(const SimVector&, int)>& score,
        const std::function<bool(const SimVector&)>& accept) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        std::vector<SimVector> best;
        std::optional<SiteScore> bestScore;
        for (const auto& site : collectBuildableSites(sim, profile, defIt->second, anchor, false))
        {
            if (accept && !accept(site.position))
            {
                continue;
            }
            auto siteScore = score(site.position, site.ring);
            if (!bestScore || siteScore > *bestScore)
            {
                bestScore = siteScore;
                best.clear();
                best.push_back(site.position);
            }
            else if (siteScore == *bestScore)
            {
                best.push_back(site.position);
            }
        }
        if (best.empty())
        {
            return std::nullopt;
        }
        return best[randomBelow(rng, static_cast<unsigned int>(best.size()))];
    }

    std::optional<SimVector> BuildManager::chooseDefenceSite(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const ReachabilityMap& reachability,
        const std::string& unitType,
        std::minstd_rand& rng) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end() || !bb.baseAnchor)
        {
            return std::nullopt;
        }
        std::function<bool(const SimVector&)> walkable;
        if (bb.groundReachabilityValid)
        {
            walkable = [&](const SimVector& p) { return reachability.isReachable(sim, p); };
        }
        // The spacing is the weapon's reach, read from the weapon table: a
        // Defender reaches 700 and a light laser tower 300, and two towers
        // 64 units apart -- which is where the nearest-ring rule put them --
        // cover the same ground twice.
        const auto range = weaponRange(sim, defIt->second);
        if (range <= 0_ss)
        {
            return chooseBuildSite(sim, profile, unitType, *bb.baseAnchor, rng);
        }
        const auto rangeSquared = range * range;

        // Towers of this type standing, going up, or on a builder's way --
        // an order still being walked to is as good as placed, or the next
        // builder plans a tower beside it -- and everything else of ours
        // that stands still, which is what the towers are for. From the
        // unit list in id order, so the choice is the same on every peer.
        std::vector<SimVector> towers;
        std::vector<SimVector> buildings;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }
            const auto& unitDef = sim.unitDefinitions.at(unit.unitType);
            if (unit.unitType == unitType)
            {
                towers.push_back(unit.position);
            }
            else if (!unitDef.isMobile)
            {
                buildings.push_back(unit.position);
            }
            if (unitDef.builder && unitDef.isMobile)
            {
                for (const auto& order : unit.orders)
                {
                    if (auto build = std::get_if<BuildOrder>(&order); build && build->unitType == unitType)
                    {
                        towers.push_back(build->position);
                    }
                }
            }
        }

        // How far the nearest tower of the type is, up to the range: beyond
        // that the two no longer overlap and further apart is no better.
        auto spread = [&](const SimVector& site) {
            auto nearest = range;
            for (const auto& tower : towers)
            {
                nearest = std::min(nearest, flatDistance(tower, site));
            }
            return nearest.value;
        };

        if (unitType == bb.sideUnits.antiAirTower)
        {
            // Coverage: what a tower here would take under its umbrella that
            // no tower yet covers. The first goes wherever reaches the most
            // of the base, which is its middle; the next goes where the
            // umbrella runs out, towards whatever stands beyond it.
            std::vector<SimVector> uncovered;
            for (const auto& building : buildings)
            {
                bool covered = false;
                for (const auto& tower : towers)
                {
                    if (tower.distanceSquared(building) <= rangeSquared)
                    {
                        covered = true;
                        break;
                    }
                }
                if (!covered)
                {
                    uncovered.push_back(building);
                }
            }
            return chooseScoredBuildSite(sim, profile, unitType, *bb.baseAnchor, rng, [&](const SimVector& site, int) {
                int newlyCovered = 0;
                for (const auto& building : uncovered)
                {
                    if (building.distanceSquared(site) <= rangeSquared)
                    {
                        ++newlyCovered;
                    }
                }
                return SiteScore{static_cast<float>(newlyCovered), spread(site), -flatDistance(site, *bb.baseAnchor).value};
            },
                walkable);
        }

        // Approaches: the post is on the side that faces the enemy, and
        // "facing" stops counting beyond the post, so that the second tower
        // -- a range from the first, at least as far forward as the post,
        // and then as near the base as it can be -- lands beside the first
        // rather than out in front of it or behind it. That makes a line
        // across the front.
        auto towards = threatDirection(bb);
        auto post = *bb.baseAnchor + (towards * profile.defenceDistanceFromBase);
        return chooseScoredBuildSite(sim, profile, unitType, post, rng, [&](const SimVector& site, int) {
            auto forward = std::min((site - *bb.baseAnchor).dot(towards), profile.defenceDistanceFromBase);
            return SiteScore{spread(site), forward.value, -flatDistance(site, *bb.baseAnchor).value};
        },
            walkable);
    }

    std::optional<SimVector> BuildManager::chooseRadarSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const ReachabilityMap& reachability,
        const std::string& unitType,
        std::minstd_rand& rng) const
    {
        if (!bb.baseAnchor)
        {
            return std::nullopt;
        }
        std::function<bool(const SimVector&)> walkable;
        if (bb.groundReachabilityValid)
        {
            walkable = [&](const SimVector& p) { return reachability.isReachable(sim, p); };
        }
        // Nearest ring to the post, and the highest ground on it.
        auto post = *bb.baseAnchor + (threatDirection(bb) * profile.radarDistanceFromBase);
        return chooseScoredBuildSite(
            sim, profile, unitType, post, rng, [](const SimVector& site, int ring) {
                return SiteScore{-static_cast<float>(ring), site.y.value, 0.0f};
            },
            walkable);
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

        // An extractor goes where the metal is, so it never sits on the
        // base's grid at all and can land squarely across a factory's apron.
        // A stalled factory costs more than a patch does.
        std::vector<PlacementObstacle> factories;
        for (const auto& [_, unit] : sim.units)
        {
            if (!unit.isAlive())
            {
                continue;
            }
            auto factoryDefIt = sim.unitDefinitions.find(unit.unitType);
            if (factoryDefIt == sim.unitDefinitions.end() || !isFactory(factoryDefIt->second))
            {
                continue;
            }
            factories.push_back(PlacementObstacle{sim.computeFootprintRegion(unit.position, factoryDefIt->second.movementCollisionInfo), factoryClearanceTiles});
        }

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
                    && clearsStandingBuildings(factories, rect, 0)
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

    bool BuildManager::siteFailedLately(const GameSimulation& sim, const SimVector& site) const
    {
        if (failedSites.empty())
        {
            return false;
        }
        auto cell = sim.terrain.worldToHeightmapCoordinate(site);
        return failedSites.count(std::make_pair(cell.x, cell.y)) != 0;
    }

    std::optional<BuildManager::OutpostDefencePlan> BuildManager::planOutpostDefence(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb) const
    {
        const auto& s = bb.sideUnits;
        if (!bb.baseAnchor || s.lightLaserTower.empty() || s.metalExtractor.empty() || profile.outpostDefenceCount <= 0)
        {
            return std::nullopt;
        }
        auto towerDefIt = sim.unitDefinitions.find(s.lightLaserTower);
        if (towerDefIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        // The tower covers what its own weapon reaches, read from the weapon
        // table rather than assumed: a mod's tower reaches what it reaches.
        const auto range = weaponRange(sim, towerDefIt->second);
        if (range <= 0_ss)
        {
            return std::nullopt;
        }
        const auto rangeSquared = range * range;
        const auto baseRadiusSquared = profile.defendRadius * profile.defendRadius;

        // Our extractors, and every tower of ours standing or going up. A
        // frame counts as cover so a second tower is not planned for the
        // cluster the first is still being built at. In id order, from the
        // unit list, so the choice below is the same on every peer.
        std::vector<SimVector> extractors;
        std::vector<SimVector> towers;
        int outpostTowers = 0;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }
            if (unit.unitType == s.lightLaserTower)
            {
                towers.push_back(unit.position);
                if (unit.position.distanceSquared(*bb.baseAnchor) > baseRadiusSquared)
                {
                    ++outpostTowers;
                }
            }
            else if (unit.unitType == s.metalExtractor && !unit.isBeingBuilt(sim.unitDefinitions.at(unit.unitType)))
            {
                extractors.push_back(unit.position);
            }
        }
        if (outpostTowers >= profile.outpostDefenceCount)
        {
            return std::nullopt;
        }

        // Uncovered: beyond the base's own defence, which the army answers
        // (defendRadius is what puts it in Defend), and outside every tower.
        std::vector<SimVector> uncovered;
        for (const auto& position : extractors)
        {
            if (position.distanceSquared(*bb.baseAnchor) <= baseRadiusSquared)
            {
                continue;
            }
            bool covered = false;
            for (const auto& tower : towers)
            {
                if (tower.distanceSquared(position) <= rangeSquared)
                {
                    covered = true;
                    break;
                }
            }
            if (!covered)
            {
                uncovered.push_back(position);
            }
        }
        // Where extractors of ours were destroyed lately, beyond the base
        // and outside every tower. recentLosses has carried these positions
        // since the loss tracking went in and nothing built defences from
        // them before; a tower where the raid came is what a player puts
        // up, and it goes up even where the raid took the last extractor,
        // since that is the site the plan is about to rebuild on.
        std::vector<SimVector> raids;
        std::vector<SimVector> losses;
        for (const auto& loss : bb.recentLosses)
        {
            if (loss.unitType == s.metalExtractor)
            {
                losses.push_back(loss.position);
            }
        }
        for (const auto& [cell, _] : raidedSites)
        {
            losses.push_back(sim.terrain.heightmapIndexToWorldCenter(cell.first, cell.second));
        }
        for (const auto& position : losses)
        {
            if (position.distanceSquared(*bb.baseAnchor) <= baseRadiusSquared)
            {
                continue;
            }
            bool covered = false;
            for (const auto& tower : towers)
            {
                if (tower.distanceSquared(position) <= rangeSquared)
                {
                    covered = true;
                    break;
                }
            }
            if (!covered)
            {
                raids.push_back(position);
            }
        }
        if (uncovered.empty() && raids.empty())
        {
            return std::nullopt;
        }

        // The cluster is whatever one tower's reach takes in around a seed
        // -- an uncovered extractor, or the place one was lost. The best
        // seed is the one with the most standing company, a raid within
        // reach counting for the minimum on its own; the first in id order
        // (extractors before losses, losses most recent first) on a tie.
        std::vector<SimVector> seeds(uncovered);
        seeds.insert(seeds.end(), raids.begin(), raids.end());
        auto companyOf = [&](const SimVector& seed) {
            int company = 0;
            for (const auto& other : uncovered)
            {
                if (other.distanceSquared(seed) <= rangeSquared)
                {
                    ++company;
                }
            }
            return company;
        };
        auto raidedAt = [&](const SimVector& seed) {
            for (const auto& raid : raids)
            {
                if (raid.distanceSquared(seed) <= rangeSquared)
                {
                    return true;
                }
            }
            return false;
        };
        std::optional<std::size_t> bestSeed;
        int bestScore = 0;
        for (std::size_t i = 0; i < seeds.size(); ++i)
        {
            auto score = companyOf(seeds[i]) + (raidedAt(seeds[i]) ? profile.outpostDefenceMinExtractors : 0);
            if (score > bestScore)
            {
                bestScore = score;
                bestSeed = i;
            }
        }
        if (!bestSeed || bestScore < profile.outpostDefenceMinExtractors)
        {
            return std::nullopt;
        }

        // Sited in the middle of what it covers; at the loss itself when
        // nothing of ours is left standing there.
        OutpostDefencePlan plan;
        plan.raided = raidedAt(seeds[*bestSeed]);
        SimVector sum(0_ss, 0_ss, 0_ss);
        for (const auto& other : uncovered)
        {
            if (other.distanceSquared(seeds[*bestSeed]) <= rangeSquared)
            {
                sum += other;
                ++plan.extractors;
            }
        }
        if (plan.extractors == 0)
        {
            plan.anchor = seeds[*bestSeed];
            return plan;
        }
        auto n = SimScalar(static_cast<float>(plan.extractors));
        plan.anchor = SimVector(sum.x / n, sum.y / n, sum.z / n);
        return plan;
    }

    std::vector<std::string> BuildManager::buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase, const std::optional<OutpostDefencePlan>& outpost, const std::string& builderType) const
    {
        const auto& s = bb.sideUnits;
        // Count what exists or is already going up, so we don't double up.
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        std::vector<std::string> wanted;
        auto want = [&](const std::string& t) {
            if (t.empty() || !bb.buildTree.canBuild(builderType, t))
            {
                // Not something this builder has a button for. Every rule
                // below is written as what the BASE wants, and the filter
                // here is what turns that into what this builder can do
                // about it -- so the commander stops putting up Defenders,
                // which it has no button for and the AI did anyway, and the
                // advanced constructor is the only thing that reaches a
                // fusion plant. See AiBuildTree.h.
                return;
            }
            if (std::find(wanted.begin(), wanted.end(), t) == wanted.end())
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
        // Holding what was taken. An expansion is a place, not a mex, and
        // the AI used to defend nothing but the base: measured over thirty
        // minutes one side lost fifty-four extractors and kept ordering the
        // same raided sites again. A cluster of ours beyond the base's
        // cover gets a tower of its own, sited at the cluster (update()
        // reads the plan for where). A raided one gets it even while metal
        // is short, for the same reason the reactive anti-air is not gated:
        // saving up while being raided is how the extractors and the metal
        // are both lost.
        if (outpost && total(s.lab) >= 1 && (!metalShort || outpost->raided))
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
        // The tech step, and it goes above the air plant and the vehicle
        // plant because for the side that wants it, it is worth more than
        // either. Below them it was ordered six minutes after the income
        // could carry it -- minute 18 against minute 12 -- and the Cans it
        // eventually bought arrived in the last five minutes of the game.
        //
        // Whether this side wants it at all is a question about its units,
        // not about the plan: Core's Can is worth 9.4 times an A.K. per
        // metal and Arm's Zeus 1.44 times a Peewee, so the same rule techs
        // for one and declines for the other. §15.7.
        auto incomeSupportsTech = bb.metalIncome.value >= static_cast<float>(profile.techMinMetalIncome);
        auto tierWorthIt = bb.advancedArmyValueRatio >= profile.techMinArmyValueRatio;
        if (profile.techLevelTwo && tierWorthIt && incomeSupportsTech && total(s.lab) >= 1 && total(s.advancedLab) < profile.targetAdvancedLabCount)
        {
            want(s.advancedLab);
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

        // Growth, once the plan above is satisfied. The targets are where
        // the base starts, not where it stops: the AI used to build its
        // eighth extractor and never another, and its income sat at ten a
        // second from the seventh minute to the thirtieth in every game
        // measured, with patches to spare. An extractor is fifty metal
        // that pays for itself in half a minute; there is no count at which
        // it stops being worth having. And when the store is full, income
        // is being thrown away, and another lab is what turns it into an
        // army.
        auto metalFull = bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.8f;

        // Level two, and it goes above the surplus lab because a full store
        // buys more as an advanced lab than as a third level-one one.
        //
        // The trigger needs no machinery of its own: a store this near the
        // cap is income being thrown away, and canAfford already says yes to
        // anything at all when the stockpile is within a tenth of full. What
        // level two really costs is energy -- the advanced constructor is 300
        // metal against 5784 energy -- and energy is the resource the AI has
        // been measured sitting on, storage pegged at the cap for a third to
        // two thirds of every game. So this spends the surplus that exists
        // rather than competing for the metal that does not. §15.3.
        // What the tier is for, and in the order a player spends it: the
        // extractor upgrade first, since a moho is three times the yield of
        // the extractor beside it and is the whole reason the tier pays for
        // itself; then the reactor; then the standing defences. Something
        // actually coming outranks all three.
        //
        // This asks only whether an advanced lab stands, not whether the tech
        // knob is on. Deciding to tech is one question and spending a tier
        // already bought is another, and gating the second on the first is
        // how a finished advanced constructor was left with an empty list and
        // sent to nanolathe Peewees for the rest of the game.
        if (total(s.advancedLab) >= 1)
        {
            auto energyBinding = bb.energyStalled
                || (bb.energyStorage.value > 0.0f && bb.currentEnergy.value < bb.energyStorage.value * 0.25f);
            // Imminent, meaning armed enemies at the base now or buildings
            // lost lately -- not the standing garrison, which is further down.
            auto underAttack = !bb.enemiesNearBase.empty() || !bb.recentLosses.empty();
            if (underAttack && total(s.heavyLaserTower) < profile.heavyDefenceCount)
            {
                want(s.heavyLaserTower);
            }
            if (incomeSupportsTech)
            {
                want(s.mohoExtractor);
            }
            if (total(s.advancedRadar) < profile.targetAdvancedRadarCount)
            {
                want(s.advancedRadar);
            }
            // 5130 metal is several minutes of the whole economy, so this
            // wants a reason: energy is genuinely the thing running out, or
            // the metal store is full and the income is being thrown away --
            // and it sits below the advanced radar because 125 metal that is
            // affordable now beats 5130 that is not -- above it, saving for
            // the reactor suppresses everything cheaper and the radar never
            // gets built.
            if ((energyBinding || metalFull) && total(s.fusion) < profile.targetFusionCount)
            {
                want(s.fusion);
            }
            if (!metalShort && total(s.heavyLaserTower) < profile.heavyDefenceCount)
            {
                want(s.heavyLaserTower);
            }
            if (incomeSupportsTech && total(s.heavyPlasmaTower) < profile.heavyDefenceCount)
            {
                want(s.heavyPlasmaTower);
            }
        }

        // A surplus buys a second level-one lab only once teching is done
        // with, or impossible. Otherwise this rule quietly eats the tech
        // step: the commander is planned first and has no button for the
        // advanced lab, so it spent every full store on another Peewee lab
        // and the one builder that could tech never found the store full.
        // Measured, that was the whole of it -- eight games, three extra
        // level-one labs, and not one advanced lab ordered.
        auto techWanted = profile.techLevelTwo && !s.advancedLab.empty() && total(s.advancedLab) < profile.targetAdvancedLabCount;
        if (metalFull && !techWanted && total(s.lab) >= 1 && total(s.lab) < 1 + profile.surplusLabCount)
        {
            want(s.lab);
        }
        want(s.metalExtractor);
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
                // Eyes first, then lift when it is needed, and then the
                // reason the plant is worth 850 metal at all.
                //
                // Before this it built one 40-metal scout and stood idle for
                // the rest of the game: the AI's entire air force was a
                // single unarmed aeroplane, and a side quietly mining a whole
                // flank behind a wall of towers was never troubled by
                // anything, because nothing the AI owned could reach past the
                // wall.
                //
                // The air constructor comes before the aircraft because it is
                // the one builder the ground cannot stop, and the patch a
                // walking constructor cannot get to is exactly the patch
                // nobody is contesting.
                if (!s.scoutPlane.empty() && total(s.scoutPlane) < profile.targetScoutPlaneCount)
                {
                    next = s.scoutPlane;
                }
                else if (!s.airTransport.empty() && bb.wantsTransport && total(s.airTransport) < profile.targetTransportCount)
                {
                    next = s.airTransport;
                }
                else if (!s.airConstructor.empty() && total(s.airConstructor) < profile.targetAirConstructorCount)
                {
                    next = s.airConstructor;
                }
                else if (bb.enemyAirThreat && !s.fighter.empty() && total(s.fighter) < profile.targetFighterCount)
                {
                    // Fighters only once something of theirs is actually
                    // flying, for the same reason the anti-air kbot waits:
                    // cover built against nothing is metal not spent on the
                    // army.
                    next = s.fighter;
                }
                else if (!s.bomber.empty() && total(s.bomber) < profile.targetBomberCount)
                {
                    next = s.bomber;
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
            else if (!s.advancedLab.empty() && factory.unitType == s.advancedLab)
            {
                // The advanced constructor first, for the same reason the
                // level-one lab makes its constructor first: it is the only
                // thing that unlocks the rest of the tier, and at 300 metal
                // it is the cheapest thing this lab builds. Then assault
                // kbots for the waves -- a Zeus is 267 metal against a
                // Peewee's 65 and beats a great many of them, and the Can is
                // 2800 hit points where the level-one line is 300.
                if (!s.advancedConstructor.empty() && total(s.advancedConstructor) < profile.targetAdvancedConstructorCount)
                {
                    next = s.advancedConstructor;
                }
                else
                {
                    next = s.advancedAssault;
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
                // One line per unit a factory takes on. A factory is only
                // topped up when its queue has emptied -- the guard at the
                // head of the loop -- so this is one line per unit produced
                // and not per planning pass.
                //
                // Worth having because until it was added there was no record
                // anywhere of what the factories built. Builder orders are
                // logged and production was not, so a log could show an
                // aircraft plant going up and say nothing about whether it
                // ever produced a bomber, which is exactly the question S:16.4
                // and S:17.2 were about.
                LOG_INFO << "AI factory: " << factory.unitType << " " << factoryId.value << " starts " << next;
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
        // Whose job it is rotates, rather than always falling to the front of
        // the list, which in id order means the commander.
        if (bb.idleBuilders.empty())
        {
            return;
        }
        if (plannerCursor >= bb.idleBuilders.size())
        {
            plannerCursor = 0;
        }
        auto builderId = bb.idleBuilders[plannerCursor];
        ++plannerCursor;
        const auto& builder = sim.getUnitState(builderId);
        const auto& builderDef = sim.unitDefinitions.at(builder.unitType);

        // Did this builder's last order come to anything? It is idle again;
        // if nothing of ours stands where it was sent, the order was
        // dropped -- unreachable, or the site occupied when it arrived --
        // and the site is remembered so it is not handed straight back.
        // (A dead builder never reaches here, and one that finished its
        // job left a frame or a building behind.)
        const auto memoryTicks = static_cast<unsigned int>(std::max(0, profile.failedSiteMemorySeconds)) * SimTicksPerSecond;
        if (auto issued = issuedOrders.find(builderId.value); issued != issuedOrders.end())
        {
            const auto& order = issued->second;
            if (bb.now.value - order.at.value <= memoryTicks)
            {
                bool somethingThere = false;
                for (const auto& [unitId, unit] : sim.units)
                {
                    if (unitId != builderId && unit.owner == aiOwner && unit.isAlive() && unit.position.distanceSquared(order.site) <= (48_ss * 48_ss))
                    {
                        somethingThere = true;
                        break;
                    }
                }
                if (!somethingThere)
                {
                    auto cell = sim.terrain.worldToHeightmapCoordinate(order.site);
                    failedSites[std::make_pair(cell.x, cell.y)] = bb.now;
                    LOG_INFO << "AI build: unit " << builderId.value << " dropped its order for " << order.unitType << " at "
                             << static_cast<int>(order.site.x.value) << "," << static_cast<int>(order.site.z.value)
                             << "; the site is left alone for " << profile.failedSiteMemorySeconds << " s";
                }
            }
            issuedOrders.erase(issued);
        }
        for (auto it = failedSites.begin(); it != failedSites.end();)
        {
            it = (bb.now.value - it->second.value > memoryTicks) ? failedSites.erase(it) : std::next(it);
        }

        // Raids on our extractors, remembered for longer than the
        // blackboard keeps them: the outpost tower that answers one needs a
        // builder to come free first.
        const auto raidTicks = static_cast<unsigned int>(std::max(0, profile.outpostRaidMemorySeconds)) * SimTicksPerSecond;
        for (const auto& loss : bb.recentLosses)
        {
            if (loss.unitType == sideUnits.metalExtractor)
            {
                auto cell = sim.terrain.worldToHeightmapCoordinate(loss.position);
                raidedSites[std::make_pair(cell.x, cell.y)] = loss.lostAt;
            }
        }
        for (auto it = raidedSites.begin(); it != raidedSites.end();)
        {
            it = (bb.now.value - it->second.value > raidTicks) ? raidedSites.erase(it) : std::next(it);
        }

        // A builder that cannot walk home is running an outpost: it builds
        // around itself. One that flies is always at home, because the ground
        // it happens to be over decides nothing about where it can go next.
        bool builderAtBase = builderDef.canFly || !bb.groundReachabilityValid || reachability.isReachable(sim, builder.position);
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

        // Everyone lends a hand on the tech step.
        //
        // One builder on a 2007-metal frame is the whole reason level two
        // never arrived. Measured on Core, whose Can is worth nine times an
        // A.K. per metal and ought to be worth any price: the lab was
        // started at minute 20 and in three games of four it was STILL a
        // frame when the game ended half an hour in, the metal sunk and not
        // one Can ever fielded. A player does not watch a lone constructor
        // do that; every spare builder piles onto it, which is what a
        // repair order on a frame means. The commander can help even though
        // it could not have started the lab itself, which is exactly the
        // asymmetry that made this so slow. §15.7.
        if (!bb.sideUnits.advancedLab.empty())
        {
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != aiOwner || !unit.isAlive() || unit.unitType != bb.sideUnits.advancedLab)
                {
                    continue;
                }
                const auto& frameDef = sim.unitDefinitions.at(unit.unitType);
                if (!unit.isBeingBuilt(frameDef))
                {
                    continue;
                }
                if (bb.groundReachabilityValid && reachability.isReachable(sim, unit.position) != builderAtBase)
                {
                    continue;
                }
                LOG_INFO << "AI build: unit " << builderId.value << " assists the " << unit.unitType
                         << " frame (" << unit.getBuildPercentLeft(frameDef) << "% left)";
                savingFor.clear();
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(UnitId(unitId)), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // The wall of wrecks the last few waves left behind.
        //
        // A corpse blocks movement and absorbs every shot fired at whatever
        // stands behind it, so an army that reaches one stops at it and fires
        // into it -- and so does the army on the other side. Neither can
        // advance, the wall thickens with every wave, and both sides spend
        // the rest of the game shooting rubbish while their factories fill
        // the rally points behind them. That is the original's behaviour and
        // not something to correct in the simulation: projectiles collide
        // with blocking features and there is no line-of-fire test anywhere,
        // deliberately. Wrecks can be shot away -- GameSimulation gates blast
        // damage on the weapon's damagesFeatures and a feature's `damage` key
        // is its hit points -- but LoadingScene_util clears that flag for
        // render types 0, 5 and 7, which is every laser, and the early armies
        // on both sides are almost entirely laser-armed. So their shots stop
        // on the wall and cannot mark it. What a player does is reclaim the
        // field, which takes the wall down and pays for the next wave twice
        // over.
        //
        // This has to sit above the build priorities rather than below them,
        // where it was first put. Below, it is reached only by a builder with
        // nothing to build, and there is always another extractor to want:
        // measured over six games the AI logged "idle builders 0" in every
        // status line and the fallback never once ran.
        //
        // Three gates keep the dose right. A wave has to be out and standing
        // there, which is what makes the walk survivable; there has to be a
        // wall rather than a single corpse; and no other builder of ours may
        // already be at it, so this takes one builder off the economy and not
        // all of them. A fourth rule brings that builder back, which matters
        // more here than it looks: the order it is given never completes.

        // Who, if anyone, is already working the field. The AI issues a
        // patrol nowhere else, so a builder of ours carrying one is this
        // rule's and no other's.
        std::optional<UnitId> fieldPatroller;
        for (const auto& [otherId, other] : sim.units)
        {
            if (other.owner != aiOwner || !other.isAlive() || other.orders.empty())
            {
                continue;
            }
            auto otherDefIt = sim.unitDefinitions.find(other.unitType);
            if (otherDefIt == sim.unitDefinitions.end() || !otherDefIt->second.builder)
            {
                continue;
            }
            if (std::get_if<PatrolOrder>(&other.orders.front()) != nullptr)
            {
                fieldPatroller = UnitId(otherId);
                break;
            }
        }

        // Where the wave is standing, and how much rubbish is standing with
        // it. Both are wanted whether or not anyone is about to be sent: the
        // same two numbers decide when to send a builder and when to call one
        // home.
        std::optional<SimVector> waveCentre;
        int wreckCount = 0;
        if (!bb.attackGroup.empty())
        {
            float sumX = 0.0f;
            float sumZ = 0.0f;
            int counted = 0;
            for (auto id : bb.combatUnits)
            {
                if (bb.attackGroup.count(id.value) == 0)
                {
                    continue;
                }
                const auto& p = sim.getUnitState(id).position;
                sumX += p.x.value;
                sumZ += p.z.value;
                ++counted;
            }
            if (counted >= profile.battlefieldReclaimEscortCount)
            {
                auto divisor = static_cast<float>(counted);
                waveCentre = SimVector(SimScalar(sumX / divisor), 0_ss, SimScalar(sumZ / divisor));
                auto radiusSquared = profile.battlefieldReclaimRadius * profile.battlefieldReclaimRadius;
                for (const auto& [featureId, feature] : sim.features)
                {
                    const auto& featureDefinition = sim.getFeatureDefinition(feature.featureName);
                    if (!featureDefinition.reclaimable || !(featureDefinition.metal > 0))
                    {
                        continue;
                    }
                    if (!profile.cheatModeOmniscient && !sim.isExploredBy(aiOwner, feature.position))
                    {
                        continue;
                    }
                    if (waveCentre->distanceSquared(feature.position) < radiusSquared)
                    {
                        ++wreckCount;
                    }
                }
            }
        }

        // A wall, not a corpse. Four is the smallest number that cannot be
        // walked around by accident.
        //
        // Two thresholds and not one, which is what stops the builder being
        // sent and recalled in the same breath. The count is taken around the
        // wave's centre, and the wave moves: with a single threshold, a wave
        // drifting a few hundred units takes the count across four and the
        // builder is recalled the second after it was dispatched. Measured
        // over four recorded games, eleven of twenty-seven dispatches were
        // reversed inside a second. Going out wants a wall; coming back wants
        // the field to be genuinely finished.
        auto fieldWorthStarting = waveCentre.has_value() && wreckCount >= 4;
        auto fieldWorthFinishing = waveCentre.has_value() && wreckCount >= 2;

        // The exit condition, which the first version of this rule did not
        // have. A patrol never ends by itself, so a builder left on one is a
        // builder gone from the economy for good -- exactly how the factory
        // guard order swallowed every builder that ever ran out of work in
        // S:16.1. When the wave has moved on or the field is clear, it comes
        // home, and an empty order queue puts it back in the pool.
        if (fieldPatroller && !fieldWorthFinishing)
        {
            if (bb.baseAnchor)
            {
                LOG_INFO << "AI build: unit " << fieldPatroller->value << " comes off the battlefield";
                outCommands.emplace_back(PlayerUnitCommand(*fieldPatroller, PlayerUnitCommand::IssueOrder(MoveOrder(*bb.baseAnchor), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
        else if (!fieldPatroller && fieldWorthStarting && bb.baseAnchor && builderAtBase && !builderDef.commander && profile.battlefieldReclaimEscortCount > 0)
        {
            // A patrol, not a reclaim order naming one wreck. A builder on
            // patrol reclaims whatever it passes, and that is the original's
            // only automatic reclaim -- the area scan behind RepairPatrol,
            // which handlePatrolOrder reproduces. One order clears a field
            // and goes on clearing it; a ReclaimOrder names a single corpse
            // and has to be reissued for the next, which the planner reaches
            // once a pass at best.
            //
            // The route runs across the wall rather than at it. Wreckage
            // lies in a band athwart the approach, so a line square to the
            // base-to-field axis sweeps along the band instead of poking
            // through it.
            auto axis = *waveCentre - *bb.baseAnchor;
            auto along = SimVector(axis.x, 0_ss, axis.z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            SimVector across(along.z, 0_ss, -along.x);
            auto reach = profile.battlefieldReclaimRadius / 2_ss;
            auto legA = *waveCentre + (across * reach);
            auto legB = *waveCentre - (across * reach);
            LOG_INFO << "AI build: unit " << builderId.value << " patrols the battlefield at "
                     << static_cast<int>(waveCentre->x.value) << "," << static_cast<int>(waveCentre->z.value)
                     << " (" << wreckCount << " reclaimable within " << profile.battlefieldReclaimRadius.value << ")";
            savingFor.clear();
            outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(PatrolOrder(legA), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(PatrolOrder(legB), PlayerUnitCommand::IssueOrder::IssueKind::Queued)));
            return;
        }

        // Set when the builder is holding off for the stockpile to catch up
        // with the price of the thing it wants; it reclaims meanwhile and
        // does not lend a hand at the factory, since that would spend what
        // it is saving.
        bool saving = false;

        // Whether an extractor cluster of ours is standing undefended, and
        // where. Worked out once per pass; the priorities say whether a
        // tower is wanted for it and the site choice below says where.
        std::optional<OutpostDefencePlan> outpost;
        if (builderAtBase)
        {
            outpost = planOutpostDefence(sim, aiOwner, profile, bb);
        }

        for (const auto& next : buildPriorities(profile, bb, builderAtBase, outpost, builder.unitType))
        {
            auto nextDefIt = sim.unitDefinitions.find(next);
            if (nextDefIt == sim.unitDefinitions.end())
            {
                continue;
            }
            if (!isEconomy(next))
            {
                if (saving)
                {
                    // Holding for something dearer higher up the list, and
                    // everything from here down is cheaper than that: buying
                    // any of it is how the saving never completes.
                    continue;
                }
                auto estimate = estimateBuild(nextDefIt->second, builderDef);
                // A level-two building is worth waiting longer for. An
                // advanced lab is 2007 metal, three or four minutes of the
                // AI's income, and judged against the ordinary minute it is
                // simply unaffordable for ever -- the planner skips past it
                // to something cheap every pass and the AI never techs.
                auto isLevelTwo = (!sideUnits.advancedLab.empty() && next == sideUnits.advancedLab)
                    || (!sideUnits.fusion.empty() && next == sideUnits.fusion)
                    || (!sideUnits.mohoExtractor.empty() && next == sideUnits.mohoExtractor)
                    || (!sideUnits.heavyPlasmaTower.empty() && next == sideUnits.heavyPlasmaTower)
                    || (!sideUnits.heavyLaserTower.empty() && next == sideUnits.heavyLaserTower);
                auto window = isLevelTwo ? profile.techSaveUpSeconds : profile.saveUpSeconds;
                if (!canAfford(bb, estimate))
                {
                    if (canAfford(bb, estimate, window))
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
                        // Carry on down the list rather than stopping here.
                        // Extractors and collectors are exempt from the
                        // affordability test because they are what pays for
                        // the thing being saved for, and a builder that
                        // stood idle through four minutes of saving was
                        // giving up the very income that ends the wait.
                        saving = true;
                        continue;
                    }
                    LOG_DEBUG << "AI build: cannot afford " << next << " (" << estimate.metal << " metal over " << estimate.seconds << " s), skipping";
                    continue;
                }
            }

            std::optional<SimVector> site;
            // A moho extractor stands on a metal patch exactly as the
            // level-one one does, and must go through the same search: the
            // ordinary site chooser deliberately refuses a patch, so a moho
            // routed through it would either find nowhere or stand somewhere
            // it produces nothing.
            if (next == sideUnits.metalExtractor || (!sideUnits.mohoExtractor.empty() && next == sideUnits.mohoExtractor))
            {
                // Nearby patches first; further afield if there are none.
                // Only patches the builder can walk to: islands are for the transport.
                // A builder that flies is not held to the ground's shape.
                // Applied to an air constructor this test is exactly
                // backwards: it would confine the one builder that can cross
                // water to the patches everything else can already walk to,
                // and leave the island patch -- the one nobody is contesting,
                // and the reason to own an air constructor at all -- refused.
                std::function<bool(const SimVector&)> reachable;
                if (bb.groundReachabilityValid && !builderDef.canFly)
                {
                    reachable = [&](const SimVector& p) { return reachability.isReachable(sim, p) == builderAtBase; };
                }
                // Not under the enemy's guns. The nearest free patch stays
                // the nearest free patch after the frame on it is shot, so
                // without this the builder puts the same frame down again:
                // measured, a commander ordered one site 232 times in five
                // hundred seconds, each frame living a second or two.
                auto gunsSquared = profile.mexAvoidsEnemyGunsRadius * profile.mexAvoidsEnemyGunsRadius;
                auto underGuns = [&](const SimVector& p) {
                    if (profile.mexAvoidsEnemyGunsRadius <= 0_ss)
                    {
                        return false;
                    }
                    for (const auto& [_, enemy] : bb.knownEnemies)
                    {
                        if (enemy.isArmed && !enemy.isAir && enemy.lastKnownPosition.distanceSquared(p) <= gunsSquared)
                        {
                            return true;
                        }
                    }
                    return false;
                };
                // The commander stays within reach of home in both searches.
                // The near search is from wherever the builder stands, so a
                // commander that had just built at the edge of the base
                // could chain from there to the next patch and the next:
                // measured, one was at the midfield building extractors 3900
                // from its start when it was caught, and the game with it.
                auto leashSquared = profile.commanderMexSearchRadius * profile.commanderMexSearchRadius;
                auto walkable = [&](const SimVector& p) {
                    if (builderDef.commander && builderAtBase && bb.baseAnchor->distanceSquared(p) > leashSquared)
                    {
                        return false;
                    }
                    return !underGuns(p) && !siteFailedLately(sim, p) && (!reachable || reachable(p));
                };
                site = chooseMexSite(sim, next, builder.position, profile.nearMexSearchRadius, rng, walkable);
                if (!site && builderAtBase)
                {
                    // Expanding, as opposed to filling in around the base:
                    // the nearest free patch to the base, so the base grows
                    // outward, out to the builder's radius. The commander
                    // has a shorter one -- it is the game, and it is planned
                    // first -- so the far patches fall to the constructors.
                    //
                    // Measured before this, the extractor count sat at eight
                    // to ten for the middle third of every game: the ring of
                    // patches past 2048 was out of reach, and the ring
                    // inside it was gated on ground the AI had explored, which
                    // with no scout until the eighth extractor meant nothing
                    // until the plane flew over. Both are knobs now, and the
                    // exploration gate is off: a player is shown every metal
                    // spot on the map from the start.
                    auto radius = builderDef.commander ? profile.commanderMexSearchRadius : profile.expansionMexSearchRadius;

                    // Ours to take: nearer our base than the enemy's, once
                    // the enemy has been found. Not, before that, nearer
                    // than any other start position the map declares --
                    // that was tried, and Crystal Maze declares ten of them,
                    // so it threw out 405 of the 468 patch cells in reach
                    // and the AI sat on twelve extractors while the other
                    // side took fifty. A player in a two-player game takes
                    // the empty starts' metal; so does this.
                    auto onOurSide = [&](const SimVector& p) {
                        if (!profile.expansionStaysOnOurSide || !bb.enemyBasePosition)
                        {
                            return true;
                        }
                        return bb.baseAnchor->distanceSquared(p) <= bb.enemyBasePosition->distanceSquared(p);
                    };
                    // Ground we have looked at, when the knob asks for it:
                    // explored, or under a radar of ours, which is how a
                    // player would know a patch was still free.
                    auto known = [&](const SimVector& p) {
                        if (!profile.expansionNeedsExploredGround || profile.cheatModeOmniscient)
                        {
                            return true;
                        }
                        return sim.isExploredBy(aiOwner, p) || sim.isOnRadarOf(aiOwner, p);
                    };
                    auto acceptable = [&](const SimVector& p) {
                        return onOurSide(p) && known(p) && walkable(p);
                    };
                    site = chooseMexSite(sim, next, *bb.baseAnchor, radius, rng, acceptable);

                    if (!site)
                    {
                        // Why not, for the log: which test threw out the
                        // patches within reach. This is what showed the
                        // plateau was the radius and the exploration gate
                        // rather than a shortage of patches.
                        indexMetalPatches(sim);
                        int inRange = 0, offSide = 0, unknown = 0, guarded = 0, unwalkable = 0;
                        auto radiusSquared = radius * radius;
                        for (const auto& patch : metalPatches)
                        {
                            auto p = sim.terrain.heightmapIndexToWorldCenter(patch.x, patch.y);
                            if (bb.baseAnchor->distanceSquared(p) > radiusSquared)
                            {
                                continue;
                            }
                            ++inRange;
                            if (!onOurSide(p))
                            {
                                ++offSide;
                            }
                            else if (!known(p))
                            {
                                ++unknown;
                            }
                            else if (underGuns(p))
                            {
                                ++guarded;
                            }
                            else if (!walkable(p))
                            {
                                ++unwalkable;
                            }
                        }
                        LOG_DEBUG << "AI build: expansion found no patch for unit " << builderId.value << " within " << radius.value
                                  << ": " << inRange << " patch cells in range, " << offSide << " on the enemy's side, " << unknown << " unexplored, "
                                  << guarded << " under enemy guns, " << unwalkable << " unreachable or beyond the commander's leash, the rest taken or unbuildable";
                    }
                }
            }
            else if (next == sideUnits.lightLaserTower && outpost && (outpost->raided || countOf(bb.ownedTotalCounts, next) >= profile.targetDefenceCount))
            {
                // The outpost tower, at the cluster it is to cover. The
                // base's own towers come first unless the cluster has just
                // been raided; the count includes the outpost towers, so a
                // base rule that is still short of its target is the
                // tie-break in the base's favour.
                site = chooseBuildSite(sim, profile, next, outpost->anchor, rng);
                if (site)
                {
                    LOG_INFO << "AI build: unit " << builderId.value << " defends " << outpost->extractors << " extractor(s) at "
                             << static_cast<int>(outpost->anchor.x.value) << "," << static_cast<int>(outpost->anchor.z.value)
                             << (outpost->raided ? " after a raid" : "");
                }
            }
            else if (profile.spreadDefences && builderAtBase && (next == sideUnits.lightLaserTower || next == sideUnits.antiAirTower))
            {
                site = chooseDefenceSite(sim, aiOwner, profile, bb, reachability, next, rng);
            }
            else if (profile.spreadDefences && builderAtBase && next == sideUnits.radar)
            {
                site = chooseRadarSite(sim, profile, bb, reachability, next, rng);
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

            if (site && siteFailedLately(sim, *site))
            {
                // The same site the last order was dropped at. The extractor
                // search already skips these; anything else is laid out by
                // ring and would be offered the same place every pass.
                LOG_DEBUG << "AI build: " << next << " would go where an order was just dropped; skipping it this pass";
                site.reset();
            }
            if (site)
            {
                LOG_DEBUG << "AI build: unit " << builderId.value << " to build " << next << " at " << site->x.value << "," << site->z.value;
                savingFor.clear();
                issuedOrders[builderId.value] = IssuedOrder{next, *site, bb.now};
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
            auto factoryId = bb.factories.front();
            const auto& factory = sim.getUnitState(factoryId);
            // Already helping there. Now that an assisting builder is offered
            // to the planner again, re-issuing the order every pass would
            // restart the walk and put a command on the wire for nothing.
            auto alreadyGuarding = !builder.orders.empty()
                && std::holds_alternative<GuardOrder>(builder.orders.front())
                && std::get<GuardOrder>(builder.orders.front()).target == factoryId;
            if (!factory.buildQueue.empty() && !alreadyGuarding)
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(GuardOrder(factoryId), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
    }
}
