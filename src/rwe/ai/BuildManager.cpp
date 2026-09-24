#include "BuildManager.h"
#include <rwe/ai/AiMapBounds.h>
#include <rwe/ai/BuilderSafety.h>
#include <rwe/sim/SimRandom.h>
#include <algorithm>
#include <cmath>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/movement.h>
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

        /**
         * The sites of a ring of teeth wrapped round a building: tooth-sized
         * squares hugging its footprint `gap` clear of it, corners included,
         * ordered by how squarely each faces `towards` -- the middle of that
         * face first, then its corners, then round the sides to the back.
         * Equal facings go by position, so every peer takes the same one. A
         * two-by-two tower and two-by-two teeth make the eight squares round
         * it.
         */
        std::vector<SimVector> wrapSlots(const SimVector& centre, SimScalar halfX, SimScalar halfZ, SimScalar tooth, SimScalar gap, const SimVector& towards)
        {
            const auto innerX = halfX + gap;
            const auto innerZ = halfZ + gap;
            const auto half = tooth / 2_ss;
            std::vector<SimVector> slots;
            // The two faces across z, corners and all...
            const auto countX = static_cast<int>(std::ceil(simScalarToFloat((innerX * 2_ss) / tooth) - 0.001f)) + 2;
            const auto spanX = tooth * SimScalar(static_cast<float>(countX));
            for (int i = 0; i < countX; ++i)
            {
                auto x = centre.x - (spanX / 2_ss) + half + (tooth * SimScalar(static_cast<float>(i)));
                slots.emplace_back(x, centre.y, centre.z - innerZ - half);
                slots.emplace_back(x, centre.y, centre.z + innerZ + half);
            }
            // ...and the two across x, between the corners.
            const auto countZ = static_cast<int>(std::ceil(simScalarToFloat((innerZ * 2_ss) / tooth) - 0.001f));
            const auto spanZ = tooth * SimScalar(static_cast<float>(countZ));
            for (int j = 0; j < countZ; ++j)
            {
                auto z = centre.z - (spanZ / 2_ss) + half + (tooth * SimScalar(static_cast<float>(j)));
                slots.emplace_back(centre.x - innerX - half, centre.y, z);
                slots.emplace_back(centre.x + innerX + half, centre.y, z);
            }
            const auto facing = SimVector(towards.x, 0_ss, towards.z).normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
            auto score = [&](const SimVector& slot) {
                return SimVector(slot.x - centre.x, 0_ss, slot.z - centre.z).normalizedOr(SimVector(0_ss, 0_ss, 0_ss)).dot(facing);
            };
            std::stable_sort(slots.begin(), slots.end(), [&](const SimVector& a, const SimVector& b) {
                auto sa = score(a);
                auto sb = score(b);
                if (sa != sb)
                {
                    return sa > sb;
                }
                if (a.x != b.x)
                {
                    return a.x < b.x;
                }
                return a.z < b.z;
            });
            return slots;
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

    bool BuildManager::guardRequestDisplaces(const std::optional<AiBlackboard::BuildSiteGuardRequest>& held, float siteThreat)
    {
        if (!held)
        {
            return true;
        }
        return siteThreat >= held->threat;
    }

    bool BuildManager::towerCostJustified(const AiTuningProfile& profile, const AiBlackboard& bb, const UnitDefinition& towerDef, int extractorsCovered)
    {
        if (profile.defenceValueMaxPaybackSeconds <= 0)
        {
            return true;
        }
        if (bb.metalIncome.value <= 0.0f)
        {
            return true;
        }
        auto allowanceSeconds = static_cast<float>(profile.defenceValueMaxPaybackSeconds)
            + (static_cast<float>(std::max(0, extractorsCovered)) * static_cast<float>(profile.outpostDefenceValueSecondsPerExtractor));
        return towerDef.buildCostMetal.value <= bb.metalIncome.value * allowanceSeconds;
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
        const auto& heights = sim.terrain.getHeightMap();
        const auto seaLevel = static_cast<int>(sim.terrain.getSeaLevel().value);
        int submerged = 0;
        int deepest = 0;
        for (int y = 0; y < metalGrid.getHeight(); ++y)
        {
            for (int x = 0; x < metalGrid.getWidth(); ++x)
            {
                if (metalGrid.get(x, y) > sim.surfaceMetal)
                {
                    metalPatches.emplace_back(x, y);
                    // The heightmap is one larger than the metal grid on each
                    // axis, so cell (x,y) reads its own top-left corner and
                    // is always in bounds.
                    auto depth = seaLevel - static_cast<int>(heights.get(x, y));
                    if (depth > 0)
                    {
                        ++submerged;
                        deepest = std::max(deepest, depth);
                    }
                }
            }
        }
        // Once a game, and it answers a question that cost a whole arena run
        // to ask: has the underwater extractor anything to stand on at all?
        // ARMUWMEX needs MinWaterDepth=19 and CORUWMEX 10, so a map whose
        // deepest patch is shallower than that can never use one however much
        // water it has -- and the mex search cannot say so itself, because a
        // patch it refuses lands in the same "taken or unbuildable" bucket as
        // one that simply already has an extractor on it.
        submergedMetalPatches = submerged;

        // Which deposit each cell belongs to: cells touching at an edge or a
        // corner are one deposit. A map feature lays its metal down as a
        // block of cells -- a 3x3 on Great Divide -- and the extractor search
        // decides deposit by deposit, so that one refused cell of a deposit
        // does not become an extractor on the cell beside it.
        const auto width = metalGrid.getWidth();
        std::vector<int> patchAt(static_cast<std::size_t>(width) * static_cast<std::size_t>(metalGrid.getHeight()), -1);
        for (std::size_t i = 0; i < metalPatches.size(); ++i)
        {
            patchAt[(static_cast<std::size_t>(metalPatches[i].y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(metalPatches[i].x)] = static_cast<int>(i);
        }
        metalPatchDeposit.assign(metalPatches.size(), -1);
        metalDepositCount = 0;
        std::vector<std::size_t> stack;
        for (std::size_t i = 0; i < metalPatches.size(); ++i)
        {
            if (metalPatchDeposit[i] != -1)
            {
                continue;
            }
            metalPatchDeposit[i] = metalDepositCount;
            stack.push_back(i);
            while (!stack.empty())
            {
                auto j = stack.back();
                stack.pop_back();
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        auto nx = metalPatches[j].x + dx;
                        auto ny = metalPatches[j].y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= metalGrid.getHeight())
                        {
                            continue;
                        }
                        auto k = patchAt[(static_cast<std::size_t>(ny) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(nx)];
                        if (k >= 0 && metalPatchDeposit[static_cast<std::size_t>(k)] == -1)
                        {
                            metalPatchDeposit[static_cast<std::size_t>(k)] = metalDepositCount;
                            stack.push_back(static_cast<std::size_t>(k));
                        }
                    }
                }
            }
            ++metalDepositCount;
        }

        LOG_INFO << "AI map: " << metalPatches.size() << " metal patches in " << metalDepositCount << " deposits, " << submerged
                 << " under water, deepest " << deepest;
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
            /** A factory's lane is the one that may never be given up; see collectBuildableSites. */
            bool factory{false};
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
            const SimVector& anchor,
            SimScalar radius)
        {
            std::vector<PlacementObstacle> buildings;
            auto reach = radius + radius;
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
                auto isAFactory = isFactory(defIt->second);
                buildings.push_back(PlacementObstacle{rect, isAFactory ? factoryClearanceTiles : buildingClearanceTiles, isAFactory});
            }
            return buildings;
        }

        /**
         * Does a footprint sit across a factory's exit lane?
         *
         * Asked of the crowded fallback in collectBuildableSites, where
         * every other lane has already been given up on. The factory's is
         * not given up on, for the reason factoryClearanceTiles exists: a
         * building packed against a solar collector is a tight base, and a
         * building packed across a vehicle plant's door is a plant that
         * never finishes anything again.
         */
        bool blocksAFactoryLane(const std::vector<PlacementObstacle>& buildings, const DiscreteRect& rect)
        {
            for (const auto& b : buildings)
            {
                if (!b.factory)
                {
                    continue;
                }
                if (rect.x - b.margin < b.rect.x + b.rect.width
                    && b.rect.x - b.margin < rect.x + rect.width
                    && rect.y - b.margin < b.rect.y + b.rect.height
                    && b.rect.y - b.margin < rect.y + rect.height)
                {
                    return true;
                }
            }
            return false;
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

        /**
         * canBeBuiltAt with the units taken out: would the ground, the
         * buildings and the blocking features take this footprint if
         * everything that can walk stepped off it?
         */
        bool buildableIgnoringUnits(const GameSimulation& sim, const MovementClassDefinition& mc, const UnitDefinition& def, const DiscreteRect& rect)
        {
            if (rect.x < 0 || rect.y < 0)
            {
                return false;
            }
            const auto x = static_cast<unsigned int>(rect.x);
            const auto y = static_cast<unsigned int>(rect.y);
            if (!sim.isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ))
            {
                return false;
            }
            auto region = sim.occupiedGrid.tryToRegion(DiscreteRect(rect.x, rect.y, static_cast<int>(mc.footprintX), static_cast<int>(mc.footprintZ)));
            if (!region)
            {
                return false;
            }
            auto blocked = sim.occupiedGrid.any(*region, [&](const auto& cell) {
                if (cell.buildingInfo && !cell.buildingInfo->passable)
                {
                    return true;
                }
                return cell.featureId && sim.getFeatureDefinition(sim.getFeature(*cell.featureId).featureName).blocking;
            });
            if (blocked || !isGridPointWalkable(sim.terrain, mc, x, y))
            {
                return false;
            }
            return !(def.yardMapContainsGeo && def.yardMap && !sim.containsAnyGeoMatch(*def.yardMap, x, y));
        }

        /**
         * How long a deposit waits for units to step off its heart before
         * the search settles for the best placement they leave free. A unit
         * parked there for good -- a guard, an idle kbot -- must not cost
         * the extractor for the rest of the game.
         */
        constexpr unsigned int DepositHeartWaitTicks = 60u * SimTicksPerSecond;

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
            const UnitDefinition& def,
            const SimVector& anchor,
            bool nearestRingOnly,
            SimScalar radius,
            const std::function<bool(const SimVector&)>& accept = {})
        {
            const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);
            auto footprint = sim.getFootprintXZ(def.movementCollisionInfo);
            auto spacingTiles = static_cast<float>(std::max(footprint.first, footprint.second) + 2);
            const SimScalar spacing = SimScalar(spacingTiles * MapTerrain::HeightTileWidthInWorldUnits.value);
            const int ringCount = std::max(1, static_cast<int>(radius.value / spacing.value));

            const auto standing = collectStandingBuildings(sim, anchor, radius);
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
                        if (rect.x < 0 || rect.y < 0 || !footprintInsideVisibleMap(sim.terrain, rect))
                        {
                            continue;
                        }
                        // Asked here, inside the ring walk, rather than of
                        // the finished list: nearestRingOnly breaks at the
                        // first ring with anything on it, so a caller that
                        // filtered afterwards would be handed a ring that
                        // emptied and would give up, where this walks on to
                        // the next ring instead.
                        if (accept && !accept(candidate))
                        {
                            continue;
                        }
                        // Nor on a geothermal vent, unless it is the plant
                        // that wants one. A vent blocks nothing, so the
                        // placement test is perfectly happy to put a solar
                        // collector on it -- watched in a replay -- and the
                        // 250 energy under it is then gone for good.
                        if (!def.yardMapContainsGeo)
                        {
                            // The simulation's own vent grid, a tile wider
                            // than the footprint all round.
                            bool onVent = false;
                            const int geoWidth = sim.geoGrid.getWidth();
                            const int geoHeight = sim.geoGrid.getHeight();
                            for (int gy = std::max(0, rect.y - 1); gy < std::min(geoHeight, rect.y + rect.height + 1) && !onVent; ++gy)
                            {
                                for (int gx = std::max(0, rect.x - 1); gx < std::min(geoWidth, rect.x + rect.width + 1); ++gx)
                                {
                                    // Through the vector, as the simulation reads it:
                                    // Grid<bool>::get cannot return a reference into a
                                    // vector<bool>.
                                    if (sim.geoGrid.getVector()[static_cast<std::size_t>((gy * geoWidth) + gx)])
                                    {
                                        onVent = true;
                                        break;
                                    }
                                }
                            }
                            if (onVent)
                            {
                                continue;
                            }
                        }
                        // Don't plant a building on a metal patch; mexes want
                        // those. Still checked before canBeBuiltAt, which is
                        // much the more expensive test, and still cheap -- a
                        // footprint is a few dozen cells at worst.
                        //
                        // THE WHOLE FOOTPRINT, not just its top-left cell,
                        // which is all this used to test. ARMSOLAR is 5x5, so
                        // 24 of its 25 cells went unchecked and it dodged a
                        // patch only when the patch happened to sit exactly
                        // under its corner. Observed in play: commanders
                        // planting solars across metal spots. That costs the
                        // patch for the rest of the game -- an extractor is 50
                        // metal, and a 145-metal solar parked on top of one
                        // denies it permanently -- which is where the "huge
                        // metal losses" come from. chooseMexSite was never at
                        // fault: it already takes the richest footprint it can
                        // reach. The patches were buried before it looked.
                        auto onMetalPatch = false;
                        for (int my = rect.y; my < rect.y + static_cast<int>(rect.height) && !onMetalPatch; ++my)
                        {
                            for (int mx = rect.x; mx < rect.x + static_cast<int>(rect.width); ++mx)
                            {
                                if (mx >= 0 && my >= 0 && mx < sim.metalGrid.getWidth() && my < sim.metalGrid.getHeight()
                                    && sim.metalGrid.get(mx, my) > sim.surfaceMetal)
                                {
                                    onMetalPatch = true;
                                    break;
                                }
                            }
                        }
                        if (onMetalPatch)
                        {
                            continue;
                        }
                        if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                        {
                            continue;
                        }
                        if (!clearsStandingBuildings(standing, rect, ownMargin))
                        {
                            // Crowded, which is allowed as a last resort --
                            // but not across a factory's door, which is not
                            // a last resort, it is worse than not building.
                            //
                            // Reported from a replay: "one of the core
                            // vehicle factories got blocked when trying to
                            // produce units ... there should be enough space
                            // for units to filter between structures in the
                            // base". A cramped base reaches this fallback
                            // for every building it puts up, so on a map
                            // like Crystal Maze it is the ordinary path and
                            // not the exception the comment below imagines.
                            if (blocksAFactoryLane(standing, rect))
                            {
                                continue;
                            }
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

        /**
         * Which way our buildings have actually been lost from lately,
         * weighted so a loss just now counts fully and one about to age out
         * of bb.recentLosses barely counts at all. The window is
         * LossMemoryTicks, the same one EconomyManager already prunes that
         * list to -- read again here rather than reinvented, since a
         * second, looser memory would just steer placement at a raid the
         * replace-what-was-lost rule has already stopped treating as
         * current.
         *
         * Falls back to threatDirection when there is nothing to weigh:
         * every opening, since recentLosses only exists after something has
         * been destroyed, and any later stretch where nothing has been lost
         * inside the window. threatDirection's own fallback -- the world
         * origin, when no enemy base has been seen -- is not a real
         * threat direction either, but it is at least the map's middle
         * rather than a corner nobody stood in, and it is what the radar
         * still uses, so it stays the answer for whichever of the two has
         * no better one.
         *
         * Only buildings are ever in recentLosses -- it is diffed from
         * standingBuildings, which never held anything mobile -- so a raid
         * that kills only units passing through leaves no signal here.
         * Widening that is a change to EconomyManager's loss tracking, not
         * to how a tower reads it, and is left for whoever picks that up.
         */
        SimVector defenceFacingDirection(const AiBlackboard& bb)
        {
            SimVector sum(0_ss, 0_ss, 0_ss);
            float totalWeight = 0.0f;
            for (const auto& loss : bb.recentLosses)
            {
                auto age = bb.now.value - loss.lostAt.value;
                if (age > LossMemoryTicks)
                {
                    continue;
                }
                auto weight = 1.0f - (static_cast<float>(age) / static_cast<float>(LossMemoryTicks));
                auto dir = loss.position - *bb.baseAnchor;
                sum += SimVector(dir.x, 0_ss, dir.z) * SimScalar(weight);
                totalWeight += weight;
            }
            if (totalWeight <= 0.0f)
            {
                return threatDirection(bb);
            }
            return sum.normalizedOr(threatDirection(bb));
        }

        /**
         * How many warships the map's water is worth: navalFleetSize in
         * full on a Water map, halved (and rounded down, floor of one) on a
         * Mixed map with ground of ours the base cannot reach -- half the
         * fighting there is still on land, and a full fleet's metal
         * competes with the army that does it. Zero on a Land map, zero
         * before MapIntel has run, and zero whenever navalFleetSize itself
         * is zero: that last one is the knob's documented kill switch, and
         * folding it in here means buildPriorities and planFactories only
         * ever have to ask this one question.
         */
        int navalFleetTarget(const AiTuningProfile& profile, const AiBlackboard& bb)
        {
            if (profile.navalFleetSize <= 0 || !bb.mapIntel.valid)
            {
                return 0;
            }
            switch (bb.mapIntel.character)
            {
                case MapCharacter::Water:
                    return profile.navalFleetSize;
                case MapCharacter::Mixed:
                    return bb.hasUnreachableGround ? std::max(1, profile.navalFleetSize / 2) : 0;
                case MapCharacter::Land:
                default:
                    return 0;
            }
        }
    }

    std::optional<SimVector> BuildManager::chooseBuildSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const std::string& unitType,
        const SimVector& anchor,
        std::minstd_rand& rng,
        const std::function<bool(const SimVector&)>& accept) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        // Nowhere a gun is already pointing. Asked here, inside the search,
        // rather than of the site it returns: the ring walk stops at the
        // first ring with room, so a filter applied afterwards would throw
        // that ring's one site away and give up for the pass, and the
        // planner would be offered the same place next pass and the pass
        // after. Asked here it simply walks outward to a ring that is not
        // covered. The extractor search has said this about a metal patch
        // since the Crystal Maze measurement; see siteUnderEnemyGuns.
        auto acceptable = [&](const SimVector& p) {
            return (!accept || accept(p)) && !siteUnderEnemyGuns(sim, profile, bb, p);
        };
        // Normal budget first; widen only if nothing fits at all, not even
        // a crowded site. A base with room never reaches the wide scan --
        // see buildSiteFallbackRadius for why it is conditional.
        auto sites = collectBuildableSites(sim, defIt->second, anchor, true, profile.maxMexSearchRadius, acceptable);
        if (sites.empty() && profile.buildSiteFallbackRadius > profile.maxMexSearchRadius)
        {
            sites = collectBuildableSites(sim, defIt->second, anchor, true, profile.buildSiteFallbackRadius, acceptable);
            LOG_DEBUG << "AI build: widened site search for " << unitType << " at "
                      << static_cast<int>(anchor.x.value) << "," << static_cast<int>(anchor.z.value)
                      << (sites.empty() ? " -- still nothing" : " -- found one");
            sim.eventLog.event(sim.gameTime.value, "build_site_search")
                .set("subject", unitType)
                .set("x", static_cast<double>(anchor.x.value))
                .set("z", static_cast<double>(anchor.z.value))
                .set("why", sites.empty() ? "nothing" : "found")
                .detail(sites.empty() ? "widened site search found nothing" : "widened site search found a site");
        }
        if (sites.empty())
        {
            return std::nullopt;
        }
        return sites[randomBelow(rng, static_cast<unsigned int>(sites.size()))].position;
    }

    std::optional<SimVector> BuildManager::chooseScoredBuildSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
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
        auto scored = collectBuildableSites(sim, defIt->second, anchor, false, profile.maxMexSearchRadius);
        if (scored.empty() && profile.buildSiteFallbackRadius > profile.maxMexSearchRadius)
        {
            scored = collectBuildableSites(sim, defIt->second, anchor, false, profile.buildSiteFallbackRadius);
            LOG_DEBUG << "AI build: widened scored site search for " << unitType << " at "
                      << static_cast<int>(anchor.x.value) << "," << static_cast<int>(anchor.z.value)
                      << (scored.empty() ? " -- still nothing" : " -- found one");
            sim.eventLog.event(sim.gameTime.value, "build_site_search")
                .set("subject", unitType)
                .set("x", static_cast<double>(anchor.x.value))
                .set("z", static_cast<double>(anchor.z.value))
                .set("scored", true)
                .set("why", scored.empty() ? "nothing" : "found")
                .detail(scored.empty() ? "widened scored site search found nothing" : "widened scored site search found a site");
        }
        for (const auto& site : scored)
        {
            if (accept && !accept(site.position))
            {
                continue;
            }
            // As chooseBuildSite: nowhere a gun is already pointing. This
            // walk scores every ring rather than stopping at the first, so
            // refusing one site here simply leaves the rest to compete.
            if (siteUnderEnemyGuns(sim, profile, bb, site.position))
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
            return chooseBuildSite(sim, profile, bb, unitType, *bb.baseAnchor, rng);
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
            return chooseScoredBuildSite(sim, profile, bb, unitType, *bb.baseAnchor, rng, [&](const SimVector& site, int) {
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
        // across the front. Facing follows where our buildings have
        // actually been lost from lately in preference to the enemy's base,
        // which is often unknown and always beside the point when a raid
        // has been landing somewhere else entirely -- see
        // defenceFacingDirection. Radar siting is untouched by this and
        // still uses threatDirection: a radar's job is watching the enemy's
        // side of the map, which recentLosses says nothing about.
        auto towards = profile.defenceFacesRecentLosses ? defenceFacingDirection(bb) : threatDirection(bb);
        // The first few go out on the edge of the built-up base rather than
        // among it (firstDefencesOnPerimeter): a tower beside the solar
        // collectors is already inside whatever it was meant to keep out.
        auto postDistance = profile.defenceDistanceFromBase;
        bool onPerimeter = profile.firstDefencesOnPerimeter && static_cast<int>(towers.size()) < profile.perimeterDefenceCount;
        if (onPerimeter)
        {
            SimScalar edge = 0_ss;
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.owner != aiOwner || !other.isAlive())
                {
                    continue;
                }
                auto otherDefIt = sim.unitDefinitions.find(other.unitType);
                if (otherDefIt == sim.unitDefinitions.end() || otherDefIt->second.isMobile)
                {
                    continue;
                }
                edge = rweMax(edge, flatDistance(other.position, *bb.baseAnchor));
            }
            postDistance = rweMax(postDistance, rweMin(edge + profile.perimeterDefenceMargin, profile.defendRadius));
        }
        auto post = *bb.baseAnchor + (towards * postDistance);
        // Moving the post alone would not move the tower: "facing" stops
        // counting at the post distance, and nearness to the anchor breaks
        // the tie, so the scoring would hand back the site the old rule
        // chose whatever post it was given. The clamp is what places it.
        return chooseScoredBuildSite(sim, profile, bb, unitType, post, rng, [&](const SimVector& site, int) {
            auto forward = std::min((site - *bb.baseAnchor).dot(towards), postDistance);
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
            sim, profile, bb, unitType, post, rng, [](const SimVector& site, int ring) {
                return SiteScore{-static_cast<float>(ring), site.y.value, 0.0f};
            },
            walkable);
    }

    void BuildManager::indexGeothermalVents(const GameSimulation& sim) const
    {
        if (geothermalVentsIndexed)
        {
            return;
        }
        geothermalVentsIndexed = true;
        for (const auto& [_, feature] : sim.features)
        {
            if (sim.getFeatureDefinition(feature.featureName).geothermal)
            {
                geothermalVents.push_back(feature.position);
            }
        }
    }

    std::optional<SimVector> BuildManager::chooseShipyardSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const std::string& unitType,
        std::minstd_rand& rng) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end() || !bb.baseAnchor || bb.mapIntel.shipyardSites.empty())
        {
            return std::nullopt;
        }
        const auto& def = defIt->second;
        const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);

        // Nearest to the base among the sites MapIntel nominated. Every one
        // of them already sits on real, deep-enough water -- MapIntel's own
        // depth test is the same isWaterDepthWithinBounds canBeBuiltAt
        // reaches -- so the only question left is whether a builder standing
        // at home has any hope of reaching it, and distance is what answers
        // that in practice: a site half the map away is a site the shore
        // never gets close enough to work, whatever body of water it is on.
        // With sea room round it if any such site exists within reach of the
        // nearest one; see NavalSite::open. The allowance keeps the yard from
        // being sent across the map for the sake of a tidy harbour.
        std::vector<SimVector> best;
        std::optional<SimScalar> bestDistance;
        std::optional<SimVector> bestOpen;
        std::optional<SimScalar> bestOpenDistance;
        // Our own yards, to keep clear of; see shipyardSpacing.
        std::vector<SimVector> ownYards;
        for (auto factoryId : bb.factories)
        {
            auto factoryRef = sim.tryGetUnitState(factoryId);
            if (!factoryRef)
            {
                continue;
            }
            const auto& type = factoryRef->get().unitType;
            if (type == bb.sideUnits.shipyard || (!bb.sideUnits.advancedShipyard.empty() && type == bb.sideUnits.advancedShipyard)
                || (!bb.sideUnits.seaplanePlatform.empty() && type == bb.sideUnits.seaplanePlatform))
            {
                ownYards.push_back(factoryRef->get().position);
            }
        }
        auto spacingSquared = profile.shipyardSpacing * profile.shipyardSpacing;
        for (const auto& candidate : bb.mapIntel.shipyardSites)
        {
            // The distance first, because it is the cheap question and it
            // settles most of them: a site further off than the best so far
            // can change nothing below, so there is no call to ask whether
            // it could be built on. On an open sea the list is most of the
            // map, and asking canBeBuiltAt of all of it was an 85 ms pass.
            auto distance = flatDistance(candidate.position, *bb.baseAnchor);
            bool couldBeNearest = !bestDistance || distance <= *bestDistance;
            bool couldBeNearestOpen = candidate.open && (!bestOpenDistance || distance < *bestOpenDistance);
            if (!couldBeNearest && !couldBeNearestOpen)
            {
                continue;
            }
            if (siteFailedLately(sim, candidate.position))
            {
                continue;
            }
            if (std::any_of(ownYards.begin(), ownYards.end(), [&](const SimVector& yard) { return yard.distanceSquared(candidate.position) < spacingSquared; }))
            {
                continue;
            }
            if (!footprintInsideVisibleMap(sim.terrain, DiscreteRect(candidate.tile.x, candidate.tile.y, static_cast<int>(mc.footprintX), static_cast<int>(mc.footprintZ)))
                || !sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo,
                    static_cast<unsigned int>(candidate.tile.x), static_cast<unsigned int>(candidate.tile.y)))
            {
                continue;
            }
            if (candidate.open && (!bestOpenDistance || distance < *bestOpenDistance))
            {
                bestOpenDistance = distance;
                bestOpen = candidate.position;
            }
            if (!bestDistance || distance < *bestDistance)
            {
                bestDistance = distance;
                best.clear();
                best.push_back(candidate.position);
            }
            else if (distance == *bestDistance)
            {
                best.push_back(candidate.position);
            }
        }
        if (best.empty())
        {
            return std::nullopt;
        }
        // Sixteen tiles further is a short sail and a long walk saved for
        // every hull the yard will ever launch.
        if (bestOpen && bestDistance && *bestOpenDistance <= *bestDistance + (16_ss * MapTerrain::HeightTileWidthInWorldUnits))
        {
            return bestOpen;
        }
        return best[randomBelow(rng, static_cast<unsigned int>(best.size()))];
    }

    bool BuildManager::siteClaimedByAnother(
        const GameSimulation& sim,
        PlayerId aiOwner,
        UnitId builder,
        const SimVector& site,
        SimScalar radius) const
    {
        if (radius <= 0_ss)
        {
            return false;
        }
        const auto radiusSquared = radius * radius;
        for (const auto& [otherId, other] : sim.units)
        {
            if (otherId == builder || other.owner != aiOwner || other.isDead())
            {
                continue;
            }
            for (const auto& order : other.orders)
            {
                if (auto build = std::get_if<BuildOrder>(&order); build != nullptr)
                {
                    if (build->position.distanceSquared(site) <= radiusSquared)
                    {
                        return true;
                    }
                }
                else if (auto complete = std::get_if<CompleteBuildOrder>(&order); complete != nullptr)
                {
                    auto frameRef = sim.tryGetUnitState(complete->target);
                    if (frameRef && frameRef->get().position.distanceSquared(site) <= radiusSquared)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::optional<SimVector> BuildManager::chooseMexSite(
        const GameSimulation& sim,
        const std::string& unitType,
        const SimVector& anchor,
        SimScalar radius,
        std::minstd_rand& rng,
        const std::function<bool(const SimVector&)>& accept,
        const std::function<bool(const SimVector&)>& admit,
        MexSiteTally* tally) const
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
            factories.push_back(PlacementObstacle{sim.computeFootprintRegion(unit.position, factoryDefIt->second.movementCollisionInfo), factoryClearanceTiles, true});
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

        // How squarely a footprint sits on the metal it covers: the squared
        // distance between the footprint's centre and the centre of mass of
        // the patch cells under it, both in half-cells so neither needs a
        // fraction.
        //
        // patchMetalUnder alone cannot see this. A moho is 5x5 where the
        // deposit it stands on is 3x3, so every placement centred on any of
        // the deposit's nine cells covers all nine and they all tie on
        // metal: the ring order below then took the one nearest the anchor,
        // which is the deposit's near edge, and the building came out with
        // the rock in a corner of it. Reported from a replay on Crystal
        // Maze -- a moho "completely off of a metal spot, it wasn't at all
        // central". The yield was right and the placement was not, and the
        // placement is what the rest of the base has to be laid out around.
        //
        // The raw squared distance is comparable without dividing by the
        // mass because it is only ever compared between candidates that
        // already tie on patchMetalUnder, which is the same sum.
        auto patchOffsetUnder = [&](const DiscreteRect& rect) {
            long long total = 0;
            long long sumX = 0;
            long long sumZ = 0;
            for (int y = rect.y; y < rect.y + static_cast<int>(rect.height); ++y)
            {
                for (int x = rect.x; x < rect.x + static_cast<int>(rect.width); ++x)
                {
                    if (x >= 0 && y >= 0 && x < metalGrid.getWidth() && y < metalGrid.getHeight() && metalGrid.get(x, y) > sim.surfaceMetal)
                    {
                        auto m = static_cast<long long>(metalGrid.get(x, y));
                        total += m;
                        sumX += m * ((2LL * x) + 1);
                        sumZ += m * ((2LL * y) + 1);
                    }
                }
            }
            if (total == 0)
            {
                return 0LL;
            }
            auto centreX = (2LL * rect.x) + static_cast<long long>(rect.width);
            auto centreZ = (2LL * rect.y) + static_cast<long long>(rect.height);
            auto dx = sumX - (total * centreX);
            auto dz = sumZ - (total * centreZ);
            return (dx * dx) + (dz * dz);
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
            int deposit;
        };
        std::vector<RingCell> ringCells;
        ringCells.reserve(metalPatches.size());
        for (std::size_t i = 0; i < metalPatches.size(); ++i)
        {
            const auto& patch = metalPatches[i];
            auto dx = patch.x - anchorHm.x;
            auto dz = patch.y - anchorHm.y;
            auto ring = std::max(std::abs(dx), std::abs(dz));
            if (ring > radiusInTiles)
            {
                continue;
            }
            ringCells.push_back(RingCell{ring, dz, dx, metalPatchDeposit[i]});
        }
        std::sort(ringCells.begin(), ringCells.end(), [](const RingCell& a, const RingCell& b) {
            return std::tie(a.ring, a.dz, a.dx) < std::tie(b.ring, b.dz, b.dx);
        });
        auto cellSite = [&](const RingCell& cell) {
            SimVector site = sim.terrain.heightmapIndexToWorldCenter(anchorHm.x + cell.dx, anchorHm.y + cell.dz);
            site.y = sim.terrain.getHeightAt(site.x, site.z);
            return site;
        };

        // Deposit by deposit, nearest first, each taken at its heart or not
        // at all.
        //
        // The heart: the placement with the most metal under it, nearest
        // first among equals, looked for among the deposit's cells within a
        // footprint's width of where the search first met it. A patch is one
        // metal CELL, so the first ring to turn anything up is a deposit's
        // near edge, and a 3x3 centred there hangs half off it. Extraction is
        // the sum of the metal grid under the footprint (GameSimulation's
        // resource tick), so that is real income lost, not a cosmetic offset:
        // a 4x4 deposit taken from the side gives 800 at its edge against
        // 1800 at its heart. Reported from a replay on Brain Coral as
        // extractors "not central to the metal spot". The footprint's width
        // is what bounds the look: on a map whose metal runs on for hundreds
        // of cells it is still the nearest good placement, not the best one
        // on the map.
        //
        // Or not at all. This used to be one walk over cells with a single
        // predicate asked of each, and on Great Divide that put one extractor
        // in nine off its rock (173 of 1666 orders over sixteen games, the
        // commander's nearly all of them). A predicate whose boundary ran
        // through a rock -- the commander's leash, a circle that cuts one of
        // the midfield rocks so that its centre is 1617 from the base and its
        // corner 1597 -- left only the corner, and the corner covers four of
        // the rock's nine cells. And a site dropped after a failed order is
        // remembered as a cell, so the next search took the cell beside it:
        // 101 of the 173 followed a dropped order on the same rock. Now the
        // ground rules (`admit`) pass a deposit if any of its cells passes,
        // and the site rules (`accept`) are asked only of the placement the
        // deposit would be given; refused, the deposit waits.
        const auto footprint = sim.getFootprintXZ(def.movementCollisionInfo);
        const int reach = static_cast<int>(std::max(footprint.first, footprint.second));
        std::vector<char> decided(static_cast<std::size_t>(std::max(0, metalDepositCount)), 0);
        for (std::size_t i = 0; i < ringCells.size(); ++i)
        {
            const auto deposit = ringCells[i].deposit;
            if (decided[static_cast<std::size_t>(deposit)])
            {
                continue;
            }
            decided[static_cast<std::size_t>(deposit)] = 1;
            if (tally)
            {
                ++tally->depositsInRange;
            }

            // Its cells within reach of where it was met. The list is in ring
            // order, so they are all here or later.
            const int entryRing = ringCells[i].ring;
            std::vector<std::size_t> cells;
            for (std::size_t j = i; j < ringCells.size() && ringCells[j].ring <= entryRing + reach; ++j)
            {
                if (ringCells[j].deposit == deposit)
                {
                    cells.push_back(j);
                }
            }

            if (admit && std::none_of(cells.begin(), cells.end(), [&](std::size_t j) { return admit(cellSite(ringCells[j])); }))
            {
                if (tally)
                {
                    ++tally->notAdmitted;
                }
                continue;
            }

            std::vector<SimVector> tiedCandidates;
            unsigned int bestMetal = 0;
            long long bestOffset = 0;
            int bestRing = -1;
            // The best the deposit would give if every unit stepped off it.
            unsigned int bestIgnoringUnits = 0;
            for (auto j : cells)
            {
                auto candidate = cellSite(ringCells[j]);
                auto rect = sim.computeFootprintRegion(candidate, def.movementCollisionInfo);
                if (rect.x < 0 || rect.y < 0
                    || !footprintInsideVisibleMap(sim.terrain, rect)
                    || !clearsStandingBuildings(factories, rect, 0))
                {
                    if (tally)
                    {
                        ++tally->cellsOffMap;
                    }
                    continue;
                }
                if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                {
                    if (buildableIgnoringUnits(sim, mc, def, rect))
                    {
                        bestIgnoringUnits = std::max(bestIgnoringUnits, patchMetalUnder(rect));
                        if (tally)
                        {
                            ++tally->cellsBlockedByUnit;
                        }
                    }
                    else if (tally)
                    {
                        // Told apart because they are opposite conclusions.
                        // A building standing on the rock is the AI having
                        // already taken it; ground that will not hold an
                        // extractor is a patch it can never have.
                        auto region = sim.occupiedGrid.tryToRegion(rect);
                        auto hasBuilding = region
                            && sim.occupiedGrid.any(*region, [&](const auto& cell) { return cell.buildingInfo.has_value(); });
                        ++(hasBuilding ? tally->cellsBlockedByBuilding : tally->cellsUnbuildable);
                    }
                    continue;
                }
                auto metal = patchMetalUnder(rect);
                bestIgnoringUnits = std::max(bestIgnoringUnits, metal);
                if (metal == 0)
                {
                    if (tally)
                    {
                        ++tally->cellsNoMetalUnder;
                    }
                    continue;
                }
                auto offset = patchOffsetUnder(rect);
                // The most metal, then the most squarely on it, then the
                // nearest. In ring order, so an equal find further out never
                // displaces a nearer one: ties are kept to the ring that set
                // the best, and a later ring can never beat it on distance.
                if (tiedCandidates.empty() || metal > bestMetal || (metal == bestMetal && offset < bestOffset))
                {
                    bestMetal = metal;
                    bestOffset = offset;
                    bestRing = ringCells[j].ring;
                    tiedCandidates.clear();
                    tiedCandidates.push_back(candidate);
                }
                else if (metal == bestMetal && offset == bestOffset && ringCells[j].ring == bestRing)
                {
                    tiedCandidates.push_back(candidate);
                }
            }
            // A unit standing on the heart -- a fight going over the rock, a
            // guard on the site, the army passing -- leaves only its edge
            // free, and an extractor put there earns two thirds or less for
            // the rest of the game. Measured on Great Divide once the rules
            // above were in, that was most of what was still off-centre: 126
            // of 2209, a third of them on the one midfield rock the armies
            // fight over. So the deposit waits for them to move, up to
            // DepositHeartWaitTicks, and then takes what is free.
            if (bestIgnoringUnits > bestMetal)
            {
                auto [since, fresh] = depositHeartBlockedSince.try_emplace(deposit, sim.gameTime);
                if (fresh || sim.gameTime.value - since->second.value < DepositHeartWaitTicks)
                {
                    if (tally)
                    {
                        ++tally->heartBlocked;
                    }
                    continue;
                }
            }
            else
            {
                depositHeartBlockedSince.erase(deposit);
            }
            const bool hadPlacement = !tiedCandidates.empty();
            if (accept)
            {
                tiedCandidates.erase(
                    std::remove_if(tiedCandidates.begin(), tiedCandidates.end(), [&](const SimVector& p) { return !accept(p); }),
                    tiedCandidates.end());
            }
            if (tiedCandidates.empty())
            {
                if (tally)
                {
                    // Told apart because they send the reader to different
                    // places: nothing could stand on the deposit at all, or
                    // something could and the site rules said no.
                    ++(hadPlacement ? tally->siteRefused : tally->noPlacement);
                }
                continue;
            }
            return tiedCandidates[randomBelow(rng, static_cast<unsigned int>(tiedCandidates.size()))];
        }
        return std::nullopt;
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

    bool BuildManager::siteUnderEnemyGuns(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& site) const
    {
        if (!profile.noticeProductionHarassment || profile.productionHarassRadius <= 0_ss)
        {
            return false;
        }
        auto radiusSquared = profile.productionHarassRadius * profile.productionHarassRadius;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            // A gun that cannot move keeps us off exactly what it reaches
            // (enemyGunRangeFromWeapon), which is what puts a defence of ours
            // outside a bigger one of theirs.
            if (profile.enemyGunRangeFromWeapon && enemy.isBuilding && enemy.isArmed && !enemy.isAir)
            {
                auto defIt = sim.unitDefinitions.find(enemy.unitType);
                if (defIt != sim.unitDefinitions.end())
                {
                    auto reach = weaponRange(sim, defIt->second);
                    if (reach > 0_ss)
                    {
                        reach += profile.enemyGunRangeMargin;
                        if (enemy.lastKnownPosition.distanceSquared(site) <= reach * reach)
                        {
                            return true;
                        }
                        continue;
                    }
                }
            }
            // Aircraft are excluded for the reason the extractor rule
            // excludes them: an aeroplane is over the site for a moment and
            // somewhere else by the time the builder arrives, so refusing
            // ground on account of one refuses the whole map in turn.
            if (enemy.isArmed && !enemy.isAir && enemy.lastKnownPosition.distanceSquared(site) <= radiusSquared)
            {
                return true;
            }
        }
        return false;
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
        auto outpostCap = profile.outpostDefenceCount;
        if (profile.outpostTowerIncomeStep > 0)
        {
            outpostCap = std::min(std::max(outpostCap, profile.outpostDefenceMax),
                outpostCap + static_cast<int>(bb.metalIncome.value / static_cast<float>(profile.outpostTowerIncomeStep)));
        }
        if (outpostTowers >= outpostCap)
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

    void BuildManager::watchDefences(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb)
    {
        if (!profile.fortifyWhereAttacked || bb.sideUnits.dragonsTeeth.empty())
        {
            defenceWatch.clear();
            defenceTeethOwed = false;
            return;
        }
        if (defenceWatchedAt && bb.now.value - defenceWatchedAt->value < DefenceWatchIntervalTicks)
        {
            return;
        }
        defenceWatchedAt = bb.now;

        const auto gapTicks = static_cast<unsigned int>(std::max(0, profile.fortifyAttackGapSeconds)) * SimTicksPerSecond;
        const auto memoryTicks = static_cast<unsigned int>(std::max(0, profile.fortifyAttackMemorySeconds)) * SimTicksPerSecond;
        const auto attackerRadiusSquared = profile.fortifyAttackerRadius * profile.fortifyAttackerRadius;

        for (const auto& [rawId, standing] : bb.standingBuildings)
        {
            auto defIt = sim.unitDefinitions.find(standing.unitType);
            if (defIt == sim.unitDefinitions.end() || !defIt->second.canAttack)
            {
                continue;
            }
            auto unitRef = sim.tryGetUnitState(UnitId(rawId));
            if (!unitRef)
            {
                continue;
            }
            const auto hitPoints = unitRef->get().hitPoints;
            auto [it, fresh] = defenceWatch.try_emplace(rawId);
            auto& watch = it->second;
            watch.position = standing.position;
            watch.seenAt = bb.now;
            if (!fresh && hitPoints < watch.hitPoints)
            {
                // Hit since the last look. Nothing records by whom, so it is
                // whoever armed we can see standing within reach of it --
                // on the ground, because a tooth stops nothing that flies.
                SimVector toward(0_ss, 0_ss, 0_ss);
                bool seen = false;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (!enemy.isArmed || enemy.isAir || enemy.isBuilding)
                    {
                        continue;
                    }
                    const SimVector offset(enemy.lastKnownPosition.x - standing.position.x, 0_ss, enemy.lastKnownPosition.z - standing.position.z);
                    const auto distanceSquared = offset.lengthSquared();
                    if (distanceSquared == 0_ss || distanceSquared > attackerRadiusSquared)
                    {
                        continue;
                    }
                    toward += offset.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
                    seen = true;
                }
                if (seen)
                {
                    if (watch.episodes.empty() || bb.now.value - watch.episodes.back().lastHitAt.value > gapTicks)
                    {
                        watch.episodes.push_back(AttackEpisode{toward, bb.now});
                        if (watch.episodes.size() > MaxAttackEpisodes)
                        {
                            watch.episodes.erase(watch.episodes.begin());
                        }
                    }
                    else
                    {
                        watch.episodes.back().direction += toward;
                        watch.episodes.back().lastHitAt = bb.now;
                    }
                }
            }
            watch.hitPoints = hitPoints;
        }

        // Forget what no longer stands and attacks too old to count, and
        // for each defence decide whether its attacks agree on a side: the
        // latest attack's direction, and every attack within 45 degrees of
        // it, need to number fortifyRepeatAttacks.
        const auto agreeing = SimScalar(0.7071f);
        defenceTeethOwed = false;
        for (auto it = defenceWatch.begin(); it != defenceWatch.end();)
        {
            auto& watch = it->second;
            if (watch.seenAt.value != bb.now.value)
            {
                it = defenceWatch.erase(it);
                continue;
            }
            watch.episodes.erase(
                std::remove_if(watch.episodes.begin(), watch.episodes.end(), [&](const AttackEpisode& e) { return bb.now.value - e.lastHitAt.value > memoryTicks; }),
                watch.episodes.end());
            watch.teethToward.reset();
            if (!watch.episodes.empty() && static_cast<int>(watch.episodes.size()) >= profile.fortifyRepeatAttacks)
            {
                const auto latest = watch.episodes.back().direction.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
                SimVector sum(0_ss, 0_ss, 0_ss);
                int agree = 0;
                for (const auto& episode : watch.episodes)
                {
                    const auto direction = episode.direction.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
                    if (direction.dot(latest) >= agreeing)
                    {
                        sum += direction;
                        ++agree;
                    }
                }
                if (agree >= profile.fortifyRepeatAttacks && !(sum.x == 0_ss && sum.z == 0_ss))
                {
                    watch.teethToward = sum.normalizedOr(latest);
                    defenceTeethOwed = true;
                }
            }
            ++it;
        }
    }

    std::optional<UnitId> BuildManager::chooseRepairTarget(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const SimVector& from,
        const std::function<bool(const SimVector&)>& reachable) const
    {
        if (!profile.repairStructures)
        {
            return std::nullopt;
        }
        const auto radiusSquared = profile.repairSearchRadius * profile.repairSearchRadius;

        struct Damaged
        {
            unsigned int id;
            int rank;
            SimScalar distanceSquared;
        };
        std::vector<Damaged> damaged;
        for (const auto& [rawId, standing] : bb.standingBuildings)
        {
            auto defIt = sim.unitDefinitions.find(standing.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            const auto& def = defIt->second;
            // Defences, then factories. Nothing else is worth taking a
            // builder off building for.
            int rank;
            if (def.canAttack)
            {
                rank = 0;
            }
            else if (isFactory(def))
            {
                rank = 1;
            }
            else
            {
                continue;
            }
            auto unitRef = sim.tryGetUnitState(UnitId(rawId));
            if (!unitRef || unitRef->get().isDead())
            {
                continue;
            }
            const auto& unit = unitRef->get();
            if (static_cast<long long>(unit.hitPoints) * 100 >= static_cast<long long>(def.maxHitPoints) * profile.repairStructuresBelowPercent)
            {
                continue;
            }
            const auto dx = unit.position.x - from.x;
            const auto dz = unit.position.z - from.z;
            const auto distanceSquared = (dx * dx) + (dz * dz);
            if (distanceSquared > radiusSquared || (reachable && !reachable(unit.position)))
            {
                continue;
            }
            if (!profile.repairUnderFire && siteUnderEnemyGuns(sim, profile, bb, unit.position))
            {
                continue;
            }
            damaged.push_back(Damaged{rawId, rank, distanceSquared});
        }
        if (damaged.empty())
        {
            return std::nullopt;
        }

        // Who of ours is on each already. Walked only when something is
        // damaged, which is most of the time nothing.
        std::map<unsigned int, int> repairers;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive() || unit.orders.empty())
            {
                continue;
            }
            if (auto repair = std::get_if<RepairOrder>(&unit.orders.front()); repair != nullptr)
            {
                ++repairers[repair->target.value];
            }
        }

        const Damaged* best = nullptr;
        for (const auto& candidate : damaged)
        {
            if (auto held = repairers.find(candidate.id); held != repairers.end() && held->second >= profile.repairersPerStructure)
            {
                continue;
            }
            if (!best || candidate.rank < best->rank || (candidate.rank == best->rank && candidate.distanceSquared < best->distanceSquared))
            {
                best = &candidate;
            }
        }
        if (!best)
        {
            return std::nullopt;
        }
        return UnitId(best->id);
    }

    void BuildManager::sendRepairersToCommander(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        if (!profile.repairCommander || !bb.commanderUnitId)
        {
            return;
        }
        // Every half second is plenty, and it is longer than an order takes
        // to land, so a unit sent last time is counted as on it this time
        // rather than sent again.
        if (commanderRepairCheckedAt && bb.now.value - commanderRepairCheckedAt->value < DefenceWatchIntervalTicks)
        {
            return;
        }
        commanderRepairCheckedAt = bb.now;

        auto commanderRef = sim.tryGetUnitState(*bb.commanderUnitId);
        if (!commanderRef || commanderRef->get().isDead())
        {
            return;
        }
        const auto& commander = commanderRef->get();
        const auto& commanderDef = sim.unitDefinitions.at(commander.unitType);
        if (static_cast<long long>(commander.hitPoints) * 100 >= static_cast<long long>(commanderDef.maxHitPoints) * profile.repairCommanderBelowPercent)
        {
            commanderHurtLogged = false;
            return;
        }

        // Not into a fight nothing of ours is covering: a construction unit
        // beside a commander under fire is the easier kill, and the raiders
        // take it. The commander itself is not counted as cover here -- it
        // is the one being shot.
        if (profile.builderSafety)
        {
            auto exposure = assessExposure(sim, aiOwner, bb, builderSafetyParams(profile), commander.position, *bb.commanderUnitId);
            if (exposure.exposed)
            {
                if (!commanderExposedLogged)
                {
                    commanderExposedLogged = true;
                    LOG_INFO << "AI build: nobody is sent to mend the commander at " << static_cast<int>(commander.position.x.value) << ","
                             << static_cast<int>(commander.position.z.value) << ": " << static_cast<int>(exposure.threatMetal)
                             << " metal of enemies there and " << static_cast<int>(exposure.protectionMetal) << " of cover";
                    sim.eventLog.event(sim.gameTime.value, "build_commander_mend")
                        .set("player", aiOwner.value)
                        .set("unit", bb.commanderUnitId->value)
                        .set("x", static_cast<double>(commander.position.x.value))
                        .set("z", static_cast<double>(commander.position.z.value))
                        .set("threat_metal", exposure.threatMetal)
                        .set("cover_metal", exposure.protectionMetal)
                        .set("why", "exposed")
                        .detail("nobody is sent to mend the commander, it is too exposed");
                }
                return;
            }
        }
        commanderExposedLogged = false;

        const auto radiusSquared = profile.repairCommanderRadius * profile.repairCommanderRadius;
        int onIt = 0;
        std::optional<UnitId> nearest;
        SimScalar nearestDistanceSquared = 0_ss;
        // The nearest wherever it is, for the log line alone: whether a
        // commander goes unmended because no builder is near or because
        // none is left is the question that line answers.
        std::optional<SimScalar> anyDistanceSquared;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive() || unitId == *bb.commanderUnitId)
            {
                continue;
            }
            const auto& def = sim.unitDefinitions.at(unit.unitType);
            if (!def.builder || !def.isMobile || !def.canReclamate || unit.isBeingBuilt(def))
            {
                continue;
            }
            if (!unit.orders.empty())
            {
                if (auto repair = std::get_if<RepairOrder>(&unit.orders.front()); repair != nullptr && repair->target == *bb.commanderUnitId)
                {
                    ++onIt;
                    continue;
                }
            }
            const auto dx = unit.position.x - commander.position.x;
            const auto dz = unit.position.z - commander.position.z;
            const auto distanceSquared = (dx * dx) + (dz * dz);
            if (!anyDistanceSquared || distanceSquared < *anyDistanceSquared)
            {
                anyDistanceSquared = distanceSquared;
            }
            if (distanceSquared > radiusSquared)
            {
                continue;
            }
            if (!nearest || distanceSquared < nearestDistanceSquared)
            {
                nearest = unitId;
                nearestDistanceSquared = distanceSquared;
            }
        }
        if (!commanderHurtLogged)
        {
            commanderHurtLogged = true;
            LOG_INFO << "AI build: the commander is hurt (" << commander.hitPoints << " of " << commanderDef.maxHitPoints
                     << " hit points) at " << static_cast<int>(commander.position.x.value) << "," << static_cast<int>(commander.position.z.value) << ": "
                     << onIt << " mending it, "
                     << (anyDistanceSquared ? "the nearest builder " + std::to_string(static_cast<int>(std::sqrt(simScalarToFloat(*anyDistanceSquared)))) + " away" : std::string("no builder left"));
            sim.eventLog.event(sim.gameTime.value, "build_commander_mend")
                .set("player", aiOwner.value)
                .set("unit", bb.commanderUnitId->value)
                .set("hp", commander.hitPoints)
                .set("max_hp", commanderDef.maxHitPoints)
                .set("x", static_cast<double>(commander.position.x.value))
                .set("z", static_cast<double>(commander.position.z.value))
                .set("mending", onIt)
                .set("why", "hurt")
                .detail("the commander is hurt and needs mending");
        }
        if (onIt >= profile.commanderRepairers || !nearest)
        {
            return;
        }
        LOG_INFO << "AI build: unit " << nearest->value << " leaves what it was doing to repair the commander ("
                 << commander.hitPoints << " of " << commanderDef.maxHitPoints << " hit points)";
        sim.eventLog.event(sim.gameTime.value, "build_commander_mend")
            .set("player", aiOwner.value)
            .set("unit", nearest->value)
            .set("target_id", bb.commanderUnitId->value)
            .set("hp", commander.hitPoints)
            .set("max_hp", commanderDef.maxHitPoints)
            .set("why", "repairer_dispatched")
            .detail("a builder leaves its job to repair the commander");
        outCommands.emplace_back(PlayerUnitCommand(*nearest, PlayerUnitCommand::IssueOrder(RepairOrder(*bb.commanderUnitId), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
    }

    int BuildManager::freeDepositsOnOurSide(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb) const
    {
        if (!bb.baseAnchor || bb.sideUnits.metalExtractor.empty())
        {
            return 0;
        }
        indexMetalPatches(sim);
        if (depositCentres.size() != static_cast<std::size_t>(metalDepositCount))
        {
            std::vector<SimScalar> sumX(metalDepositCount, 0_ss);
            std::vector<SimScalar> sumZ(metalDepositCount, 0_ss);
            std::vector<int> cells(metalDepositCount, 0);
            for (std::size_t i = 0; i < metalPatches.size(); ++i)
            {
                auto deposit = metalPatchDeposit[i];
                auto p = sim.terrain.heightmapIndexToWorldCenter(metalPatches[i].x, metalPatches[i].y);
                sumX[deposit] += p.x;
                sumZ[deposit] += p.z;
                ++cells[deposit];
            }
            depositCentres.clear();
            for (int d = 0; d < metalDepositCount; ++d)
            {
                auto n = SimScalar(static_cast<float>(std::max(1, cells[d])));
                depositCentres.emplace_back(sumX[d] / n, 0_ss, sumZ[d] / n);
            }
        }

        // What stands on the metal: every extractor of ours, and every one of
        // theirs we have seen.
        std::vector<SimVector> extractors;
        auto extracts = [&](const std::string& unitType) {
            auto defIt = sim.unitDefinitions.find(unitType);
            return defIt != sim.unitDefinitions.end() && defIt->second.extractsMetal.value > 0.0f;
        };
        for (const auto& [_, standing] : bb.standingBuildings)
        {
            if (extracts(standing.unitType))
            {
                extractors.push_back(standing.position);
            }
        }
        for (const auto& [_, unit] : bb.standingUnits)
        {
            if (unit.underConstruction && extracts(unit.unitType))
            {
                extractors.push_back(unit.position);
            }
        }
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (!enemy.isBuilding)
            {
                continue;
            }
            if (extracts(enemy.unitType))
            {
                extractors.push_back(enemy.lastKnownPosition);
            }
        }

        const auto radiusSquared = profile.expansionMexSearchRadius * profile.expansionMexSearchRadius;
        int free = 0;
        for (const auto& centre : depositCentres)
        {
            if (bb.baseAnchor->distanceSquared(centre) > radiusSquared)
            {
                continue;
            }
            if (profile.expansionStaysOnOurSide && bb.enemyBasePosition && bb.baseAnchor->distanceSquared(centre) > bb.enemyBasePosition->distanceSquared(centre))
            {
                continue;
            }
            bool taken = false;
            for (const auto& extractor : extractors)
            {
                if (flatDistance(extractor, centre) <= 48_ss)
                {
                    taken = true;
                    break;
                }
            }
            if (!taken && !siteUnderEnemyGuns(sim, profile, bb, centre))
            {
                ++free;
            }
        }
        return free;
    }

    BuildManager::LabShares BuildManager::counterShares(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb)
    {
        LabShares shares{profile.labRaiderShare, profile.labRocketKbotShare, profile.labArtilleryKbotShare};
        if (!profile.counterEnemyComposition)
        {
            return shares;
        }
        float staticMetal = 0.0f;
        float mobileMetal = 0.0f;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (!enemy.isArmed || enemy.isAir)
            {
                continue;
            }
            auto defIt = sim.unitDefinitions.find(enemy.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            (enemy.isBuilding ? staticMetal : mobileMetal) += defIt->second.buildCostMetal.value;
        }
        auto seen = staticMetal + mobileMetal;
        if (seen <= 0.0f)
        {
            return shares;
        }
        if (staticMetal / seen >= profile.counterShareTrigger)
        {
            shares.artilleryKbot += profile.counterShareBonus;
        }
        if (mobileMetal / seen >= profile.counterShareTrigger)
        {
            shares.rocketKbot += profile.counterShareBonus;
        }
        return shares;
    }

    bool BuildManager::incomeOutrunsSpending(const AiTuningProfile& profile, const AiBlackboard& bb)
    {
        if (!profile.spendSurplusOnCapacity || profile.capacityIncomeRatio <= 0.0f)
        {
            return false;
        }
        return bb.metalDemand.value * profile.capacityIncomeRatio < bb.metalIncome.value;
    }

    void BuildManager::keepBuildersOutOfFights(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        if (!profile.builderSafety)
        {
            builderShelteredUntil.clear();
            return;
        }
        // Twice a second: a raider closes about a hundred units in that time,
        // which the margin on its range is there to absorb.
        if (builderSafetyCheckedAt && bb.now.value - builderSafetyCheckedAt->value < DefenceWatchIntervalTicks)
        {
            return;
        }
        builderSafetyCheckedAt = bb.now;

        const auto shelterTicks = static_cast<unsigned int>(std::max(0, profile.builderShelterSeconds)) * SimTicksPerSecond;
        for (const auto& retreat : planBuilderRetreats(sim, aiOwner, bb, builderSafetyParams(profile)))
        {
            const auto& unit = sim.getUnitState(retreat.builder);
            // What it is in the middle of, and how far through. A frame
            // nearly finished is worth more than the margin backing off
            // buys: the builder stays on it (finishBuildAbovePercent).
            std::optional<UnitOrder> resume;
            if (!unit.orders.empty())
            {
                std::optional<UnitId> frameId;
                if (auto build = std::get_if<BuildOrder>(&unit.orders.front()); build != nullptr)
                {
                    // The frame it has already placed, if it got that far.
                    for (const auto& [otherId, other] : sim.units)
                    {
                        if (other.owner != aiOwner || other.isDead() || other.unitType != build->unitType)
                        {
                            continue;
                        }
                        const auto& otherDef = sim.unitDefinitions.at(other.unitType);
                        if (other.isBeingBuilt(otherDef) && other.position.distanceSquared(build->position) <= (64_ss * 64_ss))
                        {
                            frameId = otherId;
                            break;
                        }
                    }
                    resume = frameId ? UnitOrder(CompleteBuildOrder(*frameId)) : UnitOrder(*build);
                }
                else if (auto complete = std::get_if<CompleteBuildOrder>(&unit.orders.front()); complete != nullptr)
                {
                    frameId = complete->target;
                    resume = *complete;
                }
                if (frameId)
                {
                    auto frameRef = sim.tryGetUnitState(*frameId);
                    if (frameRef)
                    {
                        const auto& frameDef = sim.unitDefinitions.at(frameRef->get().unitType);
                        auto done = 100u - frameRef->get().getBuildPercentLeft(frameDef);
                        if (static_cast<int>(done) >= profile.finishBuildAbovePercent)
                        {
                            continue;
                        }
                    }
                }
            }

            // Already on its way out: the destination moves with it, so a
            // fresh one each pass would only be the same order again.
            auto sheltered = builderShelteredUntil.find(retreat.builder.value);
            if (sheltered != builderShelteredUntil.end() && bb.now.value < sheltered->second.value
                && !unit.orders.empty() && std::holds_alternative<MoveOrder>(unit.orders.front()))
            {
                continue;
            }
            LOG_INFO << "AI build: unit " << retreat.builder.value << " (" << unit.unitType << ") backs off from "
                     << static_cast<int>(retreat.exposure.threatMetal) << " metal of enemies with " << static_cast<int>(retreat.exposure.protectionMetal)
                     << " of cover, to " << static_cast<int>(retreat.destination.x.value) << "," << static_cast<int>(retreat.destination.z.value);
            sim.eventLog.event(sim.gameTime.value, "build_builder_retreat")
                .set("player", aiOwner.value)
                .set("unit", retreat.builder.value)
                .set("subject", unit.unitType)
                .set("x", static_cast<double>(retreat.destination.x.value))
                .set("z", static_cast<double>(retreat.destination.z.value))
                .set("threat_metal", retreat.exposure.threatMetal)
                .set("cover_metal", retreat.exposure.protectionMetal)
                .set("why", "exposed")
                .detail("a builder backs off from a fight it is not covered in");
            builderShelteredUntil[retreat.builder.value] = GameTime(bb.now.value + shelterTicks);
            outCommands.emplace_back(PlayerUnitCommand(retreat.builder, PlayerUnitCommand::IssueOrder(MoveOrder(retreat.destination), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            // And back to the job afterwards. An immediate order throws away
            // what the builder was doing, so without this a tower two thirds
            // of the way up is simply abandoned and the builder stands where
            // it was sent -- which is what a replay showed. The frame it had
            // started is queued behind the move as a CompleteBuildOrder; a
            // BuildOrder would try to place a second one on the same ground.
            if (profile.resumeAfterBackingOff && resume)
            {
                outCommands.emplace_back(PlayerUnitCommand(retreat.builder, PlayerUnitCommand::IssueOrder(*resume, PlayerUnitCommand::IssueOrder::IssueKind::Queued)));
            }
        }
        // The dead are forgotten.
        for (auto it = builderShelteredUntil.begin(); it != builderShelteredUntil.end();)
        {
            auto unit = sim.tryGetUnitState(UnitId(it->first));
            it = (!unit || unit->get().isDead()) ? builderShelteredUntil.erase(it) : std::next(it);
        }
    }

    void BuildManager::recordLostDefences(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb)
    {
        if (!profile.rebuildLostDefences)
        {
            lostDefenceSites.clear();
            lostDefencesReadUpTo.reset();
            return;
        }
        // Each pass's losses carry that pass's time, and a later pass a
        // later one, so reading past the newest already read counts each
        // loss exactly once.
        auto newest = lostDefencesReadUpTo;
        for (auto it = bb.recentLosses.rbegin(); it != bb.recentLosses.rend(); ++it)
        {
            const auto& loss = *it;
            if (lostDefencesReadUpTo && loss.lostAt.value <= lostDefencesReadUpTo->value)
            {
                continue;
            }
            if (!newest || loss.lostAt.value > newest->value)
            {
                newest = loss.lostAt;
            }
            auto defIt = sim.unitDefinitions.find(loss.unitType);
            if (defIt == sim.unitDefinitions.end() || defIt->second.isMobile || !defIt->second.canAttack)
            {
                continue;
            }
            auto site = std::find_if(lostDefenceSites.begin(), lostDefenceSites.end(), [&](const LostDefenceSite& s) {
                return flatDistance(s.position, loss.position) <= 32_ss;
            });
            int timesLost = 1;
            if (site != lostDefenceSites.end())
            {
                ++site->timesLost;
                site->lostAt = loss.lostAt;
                site->unitType = loss.unitType;
                timesLost = site->timesLost;
            }
            else
            {
                lostDefenceSites.push_back(LostDefenceSite{loss.unitType, loss.position, loss.lostAt, 1});
            }
            LOG_INFO << "AI build: lost the " << loss.unitType << " at " << static_cast<int>(loss.position.x.value) << ","
                     << static_cast<int>(loss.position.z.value);
            sim.eventLog.event(sim.gameTime.value, "build_defence_lost")
                .set("subject", loss.unitType)
                .set("x", static_cast<double>(loss.position.x.value))
                .set("z", static_cast<double>(loss.position.z.value))
                .set("times_lost", timesLost)
                .set("why", "destroyed")
                .detail("a defence was lost");
        }
        lostDefencesReadUpTo = newest;

        const auto memoryTicks = static_cast<unsigned int>(std::max(0, profile.lostDefenceMemorySeconds)) * SimTicksPerSecond;
        lostDefenceSites.erase(
            std::remove_if(lostDefenceSites.begin(), lostDefenceSites.end(), [&](const LostDefenceSite& s) {
                return bb.now.value - s.lostAt.value > memoryTicks || s.timesLost > profile.maxDefenceRebuilds;
            }),
            lostDefenceSites.end());
    }

    std::optional<BuildManager::DefenceRebuildPlan> BuildManager::planDefenceRebuild(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb) const
    {
        const auto delayTicks = static_cast<unsigned int>(std::max(0, profile.rebuildDelaySeconds)) * SimTicksPerSecond;
        for (const auto& lost : lostDefenceSites)
        {
            if (bb.now.value - lost.lostAt.value < delayTicks)
            {
                continue;
            }
            auto defIt = sim.unitDefinitions.find(lost.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            // Something of ours already standing there, going up, or on a
            // builder's way to it.
            bool taken = false;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != aiOwner || !unit.isAlive())
                {
                    continue;
                }
                if (!sim.unitDefinitions.at(unit.unitType).isMobile && flatDistance(unit.position, lost.position) <= 32_ss)
                {
                    taken = true;
                    break;
                }
                for (const auto& order : unit.orders)
                {
                    if (auto build = std::get_if<BuildOrder>(&order); build != nullptr && flatDistance(build->position, lost.position) <= 32_ss)
                    {
                        taken = true;
                        break;
                    }
                }
                if (taken)
                {
                    break;
                }
            }
            if (taken || siteUnderEnemyGuns(sim, profile, bb, lost.position) || siteFailedLately(sim, lost.position))
            {
                continue;
            }

            const auto& def = defIt->second;
            auto rect = sim.computeFootprintRegion(lost.position, def.movementCollisionInfo);
            if (rect.x < 0 || rect.y < 0 || !footprintInsideVisibleMap(sim.terrain, rect))
            {
                continue;
            }
            auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);
            if (sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
            {
                return DefenceRebuildPlan{lost.unitType, lost.position, std::nullopt};
            }
            // Refused. The usual reason is the tower's own wreck, which lands
            // where it stood and blocks the ground: that is reclaimed first.
            std::optional<FeatureId> wreck;
            for (const auto& [featureId, feature] : sim.features)
            {
                const auto& featureDefinition = sim.getFeatureDefinition(feature.featureName);
                if (featureDefinition.reclaimable && featureDefinition.blocking && flatDistance(feature.position, lost.position) <= 32_ss)
                {
                    wreck = featureId;
                    break;
                }
            }
            if (wreck)
            {
                return DefenceRebuildPlan{lost.unitType, lost.position, wreck};
            }
        }
        return std::nullopt;
    }

    std::optional<SimVector> BuildManager::chooseEnergyRowSite(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        const std::string& unitType,
        const SimVector& anchor,
        const SimVector& builderPosition,
        std::minstd_rand& rng,
        const std::function<bool(const SimVector&)>& accept) const
    {
        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        // The same grid collectBuildableSites lays out, so a neighbour is
        // exactly one step along a row or a column.
        auto footprint = sim.getFootprintXZ(defIt->second.movementCollisionInfo);
        const auto spacing = SimScalar(static_cast<float>(std::max(footprint.first, footprint.second) + 2) * MapTerrain::HeightTileWidthInWorldUnits.value);
        const auto slack = 8_ss;

        std::vector<SimVector> existing;
        for (const auto& [_, unit] : sim.units)
        {
            if (unit.owner == aiOwner && unit.isAlive() && unit.unitType == unitType)
            {
                existing.push_back(unit.position);
            }
        }
        // Behind the base: away from where the enemy is, or from the
        // middle of the map before anyone has seen them.
        const auto threat = bb.baseAnchor ? threatDirection(bb) : SimVector(1_ss, 0_ss, 0_ss);

        auto score = [&](const SimVector& site, int ring) {
            float beside = 0.0f;
            for (const auto& other : existing)
            {
                auto dx = rweAbs(site.x - other.x);
                auto dz = rweAbs(site.z - other.z);
                bool inRow = (rweAbs(dx - spacing) <= slack && dz <= slack) || (rweAbs(dz - spacing) <= slack && dx <= slack);
                bool diagonal = rweAbs(dx - spacing) <= slack && rweAbs(dz - spacing) <= slack;
                if (inRow)
                {
                    beside = 1.0f;
                    break;
                }
                if (diagonal)
                {
                    beside = 0.5f;
                }
            }
            auto out = SimVector(site.x - anchor.x, 0_ss, site.z - anchor.z).normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
            // From 0 straight at the enemy to 0.5 straight away, which
            // orders sites within a ring and never across rings.
            auto behind = (1.0f - simScalarToFloat(out.dot(threat))) / 4.0f;
            return SiteScore{beside, static_cast<float>(-ring) + behind, -simScalarToFloat(flatDistance(site, builderPosition)) / 10000.0f};
        };
        // A site an order was dropped at is left out of the running rather
        // than refused afterwards: the best site here is the same one every
        // pass, where the ring search draws among a ring's, so refusing it
        // after the choice would refuse every collector until it was
        // forgotten.
        auto usable = [&](const SimVector& p) {
            return (!accept || accept(p)) && !siteFailedLately(sim, p);
        };
        return chooseScoredBuildSite(sim, profile, bb, unitType, anchor, rng, score, usable);
    }

    std::optional<BuildManager::FortificationPlan> BuildManager::planFortification(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::minstd_rand& rng) const
    {
        const auto& s = bb.sideUnits;
        // Two ways in: every laser tower fortified as it stands
        // (fortifyTowers, measured and left off), or teeth only where
        // attacks have kept coming from (fortifyWhereAttacked, decided by
        // watchDefences). The second is nearly always nothing to do, and
        // says so here before anything is walked.
        // Tier two turns it on by itself (fortifyAtTierTwo): heavy towers
        // are worth teeth in front of them, and by then the metal is there.
        const bool tierTwo = !s.advancedLab.empty() && countOf(bb.ownedTotalCounts, s.advancedLab) > 0;
        const bool everyTower = (profile.fortifyTowers || (profile.fortifyAtTierTwo && tierTwo)) && !s.lightLaserTower.empty();
        const bool whereAttacked = profile.fortifyWhereAttacked && defenceTeethOwed;
        // And a third: a defence put back where one was destroyed
        // (fortifyRebuiltDefences).
        const bool rebuilt = profile.rebuildLostDefences && profile.fortifyRebuiltDefences && !lostDefenceSites.empty();
        if ((!everyTower && !whereAttacked && !rebuilt) || !bb.baseAnchor)
        {
            return std::nullopt;
        }
        const UnitDefinition* teethDef = nullptr;
        if (!s.dragonsTeeth.empty())
        {
            if (auto it = sim.unitDefinitions.find(s.dragonsTeeth); it != sim.unitDefinitions.end())
            {
                teethDef = &it->second;
            }
        }
        const bool missileTowerExists = !s.antiAirTower.empty() && sim.unitDefinitions.count(s.antiAirTower) != 0;
        const bool wantMissiles = everyTower && profile.fortifyMissileTower && missileTowerExists;
        const bool rebuiltMissiles = rebuilt && profile.reinforceTwiceLostDefences && missileTowerExists;
        if (teethDef == nullptr && !wantMissiles && !rebuiltMissiles)
        {
            return std::nullopt;
        }

        // What of ours is already there or on its way: the towers to
        // fortify, and every tooth and missile tower standing, going up or
        // ordered. An order counts because a builder takes a while to walk
        // to its site, and without it the next pass would send a second
        // builder to the same tooth.
        std::vector<std::pair<UnitId, SimVector>> towers;
        std::vector<SimVector> teeth;
        std::vector<SimVector> missiles;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }
            if (everyTower && unit.unitType == s.lightLaserTower && !unit.isBeingBuilt(sim.unitDefinitions.at(unit.unitType)))
            {
                towers.emplace_back(unitId, unit.position);
            }
            else if (teethDef != nullptr && unit.unitType == s.dragonsTeeth)
            {
                teeth.push_back(unit.position);
            }
            else if ((wantMissiles || rebuiltMissiles) && unit.unitType == s.antiAirTower)
            {
                missiles.push_back(unit.position);
            }
            for (const auto& order : unit.orders)
            {
                if (auto build = std::get_if<BuildOrder>(&order); build != nullptr)
                {
                    if (teethDef != nullptr && build->unitType == s.dragonsTeeth)
                    {
                        teeth.push_back(build->position);
                    }
                    else if ((wantMissiles || rebuiltMissiles) && build->unitType == s.antiAirTower)
                    {
                        missiles.push_back(build->position);
                    }
                }
            }
        }

        SimScalar toothWidth = 32_ss;
        if (teethDef != nullptr)
        {
            auto [footprintX, footprintZ] = sim.getFootprintXZ(teethDef->movementCollisionInfo);
            toothWidth = SimScalar(static_cast<float>(std::max(footprintX, footprintZ))) * MapTerrain::HeightTileWidthInWorldUnits;
        }
        const auto toothTaken = toothWidth / 2_ss;
        const auto wrapGap = SimScalar(static_cast<float>(std::max(0, profile.fortifyWrapGapTiles))) * MapTerrain::HeightTileWidthInWorldUnits;

        // Half the defence's footprint along each axis, from its own
        // definition: the ring is sized to what it wraps.
        auto halfFootprint = [&](UnitId towerId) -> std::pair<SimScalar, SimScalar> {
            unsigned int footprintX = 2;
            unsigned int footprintZ = 2;
            if (auto towerRef = sim.tryGetUnitState(towerId))
            {
                if (auto defIt = sim.unitDefinitions.find(towerRef->get().unitType); defIt != sim.unitDefinitions.end())
                {
                    std::tie(footprintX, footprintZ) = sim.getFootprintXZ(defIt->second.movementCollisionInfo);
                }
            }
            return {SimScalar(static_cast<float>(footprintX)) * (MapTerrain::HeightTileWidthInWorldUnits / 2_ss),
                SimScalar(static_cast<float>(footprintZ)) * (MapTerrain::HeightTileWidthInWorldUnits / 2_ss)};
        };
        // How far out a tooth of a defence's own ring can stand, corners included.
        auto wrapReach = [&](UnitId towerId) {
            auto [halfX, halfZ] = halfFootprint(towerId);
            return std::max(halfX, halfZ) + wrapGap + (toothWidth * SimScalar(1.5f)) + 8_ss;
        };
        // Whether a tooth already counts for a defence on the side `towards`:
        // the line counts what stands in front within reach of it, the ring
        // what stands in the ring on that half, the flanking sites included.
        auto toothOnSide = [&](const SimVector& tooth, const SimVector& tower, UnitId towerId, const SimVector& towards) {
            if (profile.fortifyTeethWrap)
            {
                return (tooth - tower).dot(towards) >= 0_ss - toothTaken && flatDistance(tooth, tower) <= wrapReach(towerId);
            }
            return (tooth - tower).dot(towards) > 0_ss && flatDistance(tooth, tower) <= profile.fortifyTeethDistance + (toothWidth * 2_ss);
        };

        // Whether a tooth can go down at `slot`: free of ours, not dropped
        // lately, not under a gun -- a frame is born with no hit points, and
        // a tooth put down under one is only a loss -- and ground that takes
        // it. `taken` says whether one of ours already holds it.
        auto toothSite = [&](SimVector slot, bool& taken) -> std::optional<SimVector> {
            auto mc = sim.getAdHocMovementClass(teethDef->movementCollisionInfo);
            slot.y = sim.terrain.getHeightAt(slot.x, slot.z);
            taken = false;
            for (const auto& tooth : teeth)
            {
                if (flatDistance(tooth, slot) < toothTaken)
                {
                    taken = true;
                    return std::nullopt;
                }
            }
            if (siteFailedLately(sim, slot) || siteUnderEnemyGuns(sim, profile, bb, slot))
            {
                return std::nullopt;
            }
            auto rect = sim.computeFootprintRegion(slot, teethDef->movementCollisionInfo);
            if (rect.x < 0 || rect.y < 0 || !footprintInsideVisibleMap(sim.terrain, rect)
                || !sim.canBeBuiltAt(mc, teethDef->yardMap, teethDef->yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
            {
                return std::nullopt;
            }
            return slot;
        };

        // The first free tooth of `count` for `tower`, facing `towards`.
        //
        // Wrapped (fortifyTeethWrap): round the ring from the attacked face,
        // counting what stands until `count` do. A site the ground will not
        // take is passed over and the ring carries on round, since a ring is
        // still a ring with a gap where a rock was.
        //
        // Otherwise a line laid across `towards`, fortifyTeethDistance in
        // front of `tower`, from the middle outward -- 0, +1, -1, +2, -2 --
        // so a line left half built still stands across the straight
        // approach. A site the ground will not take is passed over rather
        // than moved, because a line is only a line if its teeth touch.
        auto firstFreeTooth = [&](const SimVector& tower, UnitId towerId, const SimVector& towards, int count) -> std::optional<SimVector> {
            if (profile.fortifyTeethWrap)
            {
                auto [halfX, halfZ] = halfFootprint(towerId);
                int held = 0;
                for (const auto& slot : wrapSlots(tower, halfX, halfZ, toothWidth, wrapGap, towards))
                {
                    if (held >= count)
                    {
                        break;
                    }
                    bool taken = false;
                    if (auto site = toothSite(slot, taken))
                    {
                        return site;
                    }
                    if (taken)
                    {
                        ++held;
                    }
                }
                return std::nullopt;
            }
            const SimVector across(-towards.z, 0_ss, towards.x);
            const auto centre = tower + (towards * profile.fortifyTeethDistance);
            for (int i = 0; i < count; ++i)
            {
                const int step = ((i + 1) / 2) * ((i % 2) == 1 ? 1 : -1);
                bool taken = false;
                if (auto site = toothSite(centre + (across * (toothWidth * SimScalar(static_cast<float>(step)))), taken))
                {
                    return site;
                }
            }
            return std::nullopt;
        };
        const int reactiveTeeth = profile.fortifyTeethWrap ? profile.fortifyWrapTeeth : profile.fortifyReactiveTeeth;

        // Where attacks keep coming from, first: that is ground already
        // shown to need it. A defence whose side already has its few teeth
        // -- counted as any of ours on that side of it and within reach of
        // the line -- is left alone, so a direction that drifts a little
        // between attacks does not start a second line.
        if (whereAttacked && teethDef != nullptr && reactiveTeeth > 0)
        {
            for (const auto& [rawId, watch] : defenceWatch)
            {
                if (!watch.teethToward)
                {
                    continue;
                }
                const auto& towards = *watch.teethToward;
                int standing = 0;
                for (const auto& tooth : teeth)
                {
                    if (toothOnSide(tooth, watch.position, UnitId(rawId), towards))
                    {
                        ++standing;
                    }
                }
                if (standing >= reactiveTeeth)
                {
                    continue;
                }
                if (auto slot = firstFreeTooth(watch.position, UnitId(rawId), towards, reactiveTeeth))
                {
                    return FortificationPlan{s.dragonsTeeth, *slot, UnitId(rawId)};
                }
            }
        }

        // A defence put back where one was destroyed: teeth in front of it
        // on the side away from the base, which is the side it was lost
        // from, and once it has been lost a second time with its teeth in
        // front, a missile tower behind it as well.
        if (rebuilt)
        {
            for (const auto& lost : lostDefenceSites)
            {
                // Only once it stands again, finished.
                std::optional<UnitId> standingId;
                for (const auto& [rawId, standing] : bb.standingBuildings)
                {
                    if (flatDistance(standing.position, lost.position) > 32_ss)
                    {
                        continue;
                    }
                    auto defIt = sim.unitDefinitions.find(standing.unitType);
                    if (defIt != sim.unitDefinitions.end() && defIt->second.canAttack)
                    {
                        standingId = UnitId(rawId);
                        break;
                    }
                }
                if (!standingId)
                {
                    continue;
                }
                const auto towards = SimVector(lost.position.x - bb.baseAnchor->x, 0_ss, lost.position.z - bb.baseAnchor->z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                if (teethDef != nullptr && reactiveTeeth > 0)
                {
                    int onThatSide = 0;
                    for (const auto& tooth : teeth)
                    {
                        if (toothOnSide(tooth, lost.position, *standingId, towards))
                        {
                            ++onThatSide;
                        }
                    }
                    if (onThatSide < reactiveTeeth)
                    {
                        if (auto slot = firstFreeTooth(lost.position, *standingId, towards, reactiveTeeth))
                        {
                            return FortificationPlan{s.dragonsTeeth, *slot, *standingId};
                        }
                    }
                }
                if (rebuiltMissiles && lost.timesLost >= 2)
                {
                    bool covered = false;
                    for (const auto& missile : missiles)
                    {
                        if (flatDistance(missile, lost.position) <= profile.fortifyMissileCoverRadius)
                        {
                            covered = true;
                            break;
                        }
                    }
                    if (!covered)
                    {
                        const auto behind = lost.position - (towards * profile.fortifyMissileDistance);
                        auto site = chooseBuildSite(sim, profile, bb, s.antiAirTower, behind, rng, [&](const SimVector& p) {
                            return flatDistance(p, lost.position) <= profile.fortifyMissileCoverRadius && (p - lost.position).dot(towards) <= 0_ss;
                        });
                        if (site)
                        {
                            return FortificationPlan{s.antiAirTower, *site, *standingId};
                        }
                    }
                }
            }
        }

        if (!everyTower)
        {
            return std::nullopt;
        }
        for (const auto& [towerId, tower] : towers)
        {
            // The approach is the way the enemy is, from this tower; before
            // anyone has seen their base, it is straight out from ours.
            auto towards = bb.enemyBasePosition
                ? SimVector(bb.enemyBasePosition->x - tower.x, 0_ss, bb.enemyBasePosition->z - tower.z)
                : SimVector(tower.x - bb.baseAnchor->x, 0_ss, tower.z - bb.baseAnchor->z);
            if (towards.x == 0_ss && towards.z == 0_ss)
            {
                continue;
            }
            towards = towards.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

            if (teethDef != nullptr && profile.fortifyTeethPerTower > 0)
            {
                if (auto slot = firstFreeTooth(tower, towerId, towards, profile.fortifyTeethPerTower))
                {
                    return FortificationPlan{s.dragonsTeeth, *slot, towerId};
                }
            }

            if (wantMissiles)
            {
                bool covered = false;
                for (const auto& missile : missiles)
                {
                    if (flatDistance(missile, tower) <= profile.fortifyMissileCoverRadius)
                    {
                        covered = true;
                        break;
                    }
                }
                if (!covered)
                {
                    // Behind the tower and close to it: the laser takes what
                    // reaches the teeth, and this reaches past them.
                    const auto behind = tower - (towards * profile.fortifyMissileDistance);
                    auto site = chooseBuildSite(sim, profile, bb, s.antiAirTower, behind, rng, [&](const SimVector& p) {
                        return flatDistance(p, tower) <= profile.fortifyMissileCoverRadius && (p - tower).dot(towards) <= 0_ss;
                    });
                    if (site)
                    {
                        return FortificationPlan{s.antiAirTower, *site, towerId};
                    }
                }
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> BuildManager::buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase, const std::optional<OutpostDefencePlan>& outpost, const std::optional<FortificationPlan>& fortify, const std::optional<DefenceRebuildPlan>& rebuild, const std::string& builderType, bool enemyNavalSeen) const
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

        // Metal, of whichever kind this map's patches will take.
        //
        // Every extractor rule below used to name the dry one alone, and on a
        // map where every patch is submerged that is a rule which can never be
        // satisfied: ARMMEX is MaxWaterDepth=0, so it finds no site at all.
        // The opening then falls through to the first thing that CAN stand on
        // water -- the shipyard, and then tidal generators -- and never
        // recovers, because by the time the submerged extractor is reached at
        // the bottom of this list the starting stockpile is spent and income
        // is the metal a commander makes by itself. Watched on Brain Coral
        // (1170 patches, all 1170 of them under water) that is exactly the
        // opening the AI plays: a naval yard, then tidals, then warships that
        // idle by the yard, and 449 'cannot afford ARMUWMEX, skipping' in a
        // single game against an income of one to two a second.
        //
        // Listing the submerged one directly after the dry one costs a land
        // map nothing. The planner takes the first entry it can find a site
        // for, so wherever there is a dry patch the dry extractor still wins
        // and this is never reached -- it is the same rule that already sits
        // at the foot of this function, applied where the opening can use it.
        auto wantMetalExtractor = [&]() {
            want(s.metalExtractor);
            if (submergedMetalPatches > 0)
            {
                want(s.underwaterMetalExtractor);
            }
        };

        // Both kinds count toward the extractor targets. Counting only the dry
        // ones leaves every gate below permanently unsatisfied on a map with
        // no dry patch to take: it reads as "still no metal extractors"
        // however many are actually mining, so the opening never finishes.
        auto totalExtractors = [&]() { return total(s.metalExtractor) + total(s.underwaterMetalExtractor); };

        // A builder ferried to an island the base cannot walk to runs an
        // outpost: it takes the metal there and powers its own extractors,
        // but leaves the factories and towers to the main base.
        // The naval tech step; see targetAdvancedShipyardCount. Behind an
        // income test for the reason the land step is: 2524 metal bought on
        // four a second is the whole game spent on a building with nothing
        // to build.
        // Storage. A store found full is income thrown away, and a store of
        // a thousand cannot hold the price of anything level two sells, so
        // the first of each goes up with the tier and the rest when the
        // store is pegged. The land kind is asked for first and the
        // underwater kind after it: want() drops whichever this builder has
        // no button for, and the site search drops whichever has no site.
        auto wantStorage = [&](bool underwaterOnly) {
            auto metalStores = total(s.metalStorage) + total(s.underwaterMetalStorage);
            auto energyStores = total(s.energyStorage) + total(s.underwaterEnergyStorage);
            auto metalPegged = bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.9f;
            auto energyPegged = bb.energyStorage.value > 0.0f && bb.currentEnergy.value >= bb.energyStorage.value * 0.9f;
            auto tierTwo = total(s.advancedLab) + total(s.advancedShipyard) >= 1;
            auto hasFactory = total(s.lab) + total(s.vehiclePlant) + total(s.shipyard) >= 1;
            if (!hasFactory)
            {
                return;
            }
            if (metalStores < profile.targetMetalStorageCount && ((tierTwo && metalStores < 1) || metalPegged))
            {
                if (!underwaterOnly)
                {
                    want(s.metalStorage);
                }
                want(s.underwaterMetalStorage);
            }
            if (energyStores < profile.targetEnergyStorageCount && ((tierTwo && energyStores < 1) || energyPegged))
            {
                if (!underwaterOnly)
                {
                    want(s.energyStorage);
                }
                want(s.underwaterEnergyStorage);
            }
        };

        auto wantAdvancedShipyard = [&]() {
            if (navalFleetTarget(profile, bb) > 0 && !s.advancedShipyard.empty() && total(s.shipyard) >= 1
                && total(s.advancedShipyard) < profile.targetAdvancedShipyardCount
                && bb.metalIncome.value >= static_cast<float>(profile.navalTechMinMetalIncome))
            {
                want(s.advancedShipyard);
            }
        };

        if (!builderAtBase)
        {
            // A construction ship lands here -- the ground labelling calls
            // anything afloat stranded -- and it is the only builder with the
            // advanced shipyard's button, so the one base job it is given is
            // asked for here or never. want() drops it for a kbot on an
            // island, which has no such button. Everything else a ship might
            // do for the base it is better off not doing: measured, ships
            // that kept to extractors finished with 28.1 of them and 52.3
            // metal a second, against 18.4 and 32.6 for ships offered the
            // whole plan (navalBuildersPlanForBase).
            wantAdvancedShipyard();
            // And the advanced construction sub's: the seaplane platform,
            // which only it can build. want() drops it for everyone else.
            auto storeFull = bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.8f;
            if (profile.surplusExpansion && storeFull && !s.seaplanePlatform.empty() && total(s.airPlant) < 1
                && total(s.advancedShipyard) >= 1 && total(s.seaplanePlatform) < profile.targetSeaplanePlatformCount)
            {
                want(s.seaplanePlatform);
            }
            // The reactor that goes under the sea, which is the advanced
            // construction sub's and nobody else's. The first needs only the
            // income, as the land one does; more need the energy to be short.
            auto reactors = total(s.fusion) + total(s.underwaterFusion);
            auto energyShort = bb.energyStalled
                || (bb.energyStorage.value > 0.0f && bb.currentEnergy.value < bb.energyStorage.value * 0.25f);
            if (!s.underwaterFusion.empty() && total(s.advancedShipyard) >= 1
                && bb.metalIncome.value >= static_cast<float>(profile.navalTechMinMetalIncome)
                && total(s.underwaterFusion) < profile.targetUnderwaterFusionCount && (reactors < 1 || energyShort))
            {
                want(s.underwaterFusion);
            }
            // Storage under the water, for a side with no ground to put the
            // dry kind on. The construction ship has the buttons.
            wantStorage(true);
            wantMetalExtractor();
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
            // And the one that can stand in water, where ships matter. The
            // planner takes the first of these it finds a site for, so on dry
            // ground this changes nothing; on a map with none, the solar
            // collector finds no site for ever and the stall was never
            // answered at all. Measured on Brain Coral at twenty-five
            // minutes: metal store full at sixty a second coming in, energy
            // at 157 in and 156 out, and six tidal generators because six is
            // targetTidalCount. This is what lets the count follow the need.
            if (navalFleetTarget(profile, bb) > 0)
            {
                want(s.tidalGenerator);
            }
        }
        if (totalExtractors() < profile.openingMetalExtractorCount)
        {
            wantMetalExtractor();
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
        if (metalShort && totalExtractors() < profile.targetMetalExtractorCount)
        {
            wantMetalExtractor();
        }
        // The map says our army cannot walk to anybody, so a factory that
        // makes land units is a factory whose units have nowhere to go. The
        // tier goes to the yard and the air plant while that holds.
        //
        // The AI opened with a lab on every map, because that is what the
        // plan does on land, and nothing asked whether what it produces could
        // ever arrive. Worse, the one factory that CAN reach anybody was made
        // to wait for it: earlyShipyard tests `total(lab) >= 1`, so on a map
        // of islands the yard queued behind a factory building units for a
        // war they could not attend.
        //
        // A deferral and not a ban, which is the whole of the request this
        // came from. It lifts the moment land units stop being pointless --
        // we own a transport and can carry them over, or something armed of
        // theirs is at our own base and they are needed at home -- and it
        // never fires at all unless there is a sea factory to build instead,
        // so a mod with no shipyard, or a map whose water carries no fleet,
        // cannot end up with the AI building no factory whatever.
        //
        // landRouteToEnemy unset means the map declared too few start
        // positions to ask, and that reads as "assume a route": the ordering
        // is then exactly what it was before this existed.
        auto seaFactoryAvailable = navalFleetTarget(profile, bb) > 0 && !s.shipyard.empty();
        auto canCarryLandUnits = !bb.transports.empty();
        auto landUnitsNeededAtHome = !bb.enemiesNearBase.empty() || bb.phase == GamePhase::Defend;
        auto landFactoriesPointless = profile.seaAirFactoriesWhenIsolated > 0
            && bb.landRouteToEnemy && !*bb.landRouteToEnemy
            && seaFactoryAvailable
            && !canCarryLandUnits
            && !landUnitsNeededAtHome;
        // The kbot lab is the second step and is kept separate, because
        // measurement drew a hard line between the two: deferring the vehicle
        // plant and letting the yard go first is one thing, and taking away
        // the AI's land builder and its base defence is another. See the
        // knob's own comment and the roadmap entry.
        auto labPointless = landFactoriesPointless && profile.seaAirFactoriesWhenIsolated >= 2;

        // On an ordinary map the lab is the first factory, as it always was.
        // On one where the army has nowhere to walk it moves BELOW the yard a
        // few lines down -- the planner takes the first entry it can site and
        // afford, so the order in this list is the whole mechanism, and a lab
        // left here would win the race however the knob is set.
        if (!landFactoriesPointless && total(s.lab) < 1)
        {
            want(s.lab);
        }

        // Naval, and it goes HERE -- above the anti-air, the maker, the radar,
        // the towers, the advanced lab, the air plant and the vehicle plant --
        // because on a map where navalFleetTarget is non-zero the shipyard is
        // what the lab is on land: the factory that makes the only units able
        // to reach the enemy at all. It used to sit thirteenth in this list, so
        // the yard the entire naval plan depends on was laid down nearly last,
        // and a fleet of nine was still a fleet of five when the game ended.
        // Yard timing, not attrition and not affordability, was what was left
        // after the fleet shortfall's other two causes were fixed.
        //
        // The map gate is not new and not widened: navalFleetTarget is already
        // zero on Land and zero when navalFleetSize is the kill switch, so
        // nothing here can fire on a map where ships do not matter. What is
        // new is only WHEN it fires on a map where they do.
        //
        // The affordability bar is the OPENING economy rather than the full
        // targets. The air plant above takes the stronger reading of the same
        // idea -- airMatters skips its extractor test outright -- but a
        // 615-metal ARMSY landing on top of a 705-metal lab with nothing built
        // yet starves both, so the yard waits for the opening solars and
        // extractors and no longer.
        //
        // The late call further down is deliberately kept: want() de-duplicates
        // (see the anti-air note below), so the pair costs nothing, and the
        // late one still catches the case where this opening test has not been
        // met yet.
        if (profile.earlyShipyard && navalFleetTarget(profile, bb) > 0 && !s.shipyard.empty()
            && total(s.shipyard) < profile.targetShipyardCount
            // The yard does not queue behind the lab on a map where the
            // lab is planned after it (or not at all): waiting on something
            // deliberately placed below you holds the yard up for the whole
            // game. See landFactoriesPointless.
            && (landFactoriesPointless || total(s.lab) >= 1)
            && total(s.solar) >= profile.openingSolarCount
            && total(s.metalExtractor) >= profile.openingMetalExtractorCount)
        {
            want(s.shipyard);
        }

        // And the lab, second on such a map rather than first. Still wanted:
        // it is the AI's land builder and its base defence as much as its
        // army, and measurement was blunt about the difference between moving
        // it and removing it -- 30 games on Hundred Isles with it removed
        // decided 3 against 15, with both sides' armies collapsing. Only
        // seaAirFactoriesWhenIsolated=2 drops it, and that is not the default.
        if (landFactoriesPointless && !labPointless && total(s.lab) < 1)
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
        // One more out of the surplus itself, a maker at a time: each one
        // that comes on raises demand, so the question answers itself anew
        // on the next pass and the count settles where the energy runs out.
        auto makers = total(s.metalMaker) + total(s.floatingMetalMaker);
        auto surplusBuysAnother = profile.maxSurplusMetalMakerCount > 0 && makers < profile.maxSurplusMetalMakerCount
            && bb.energyIncome.value - bb.energyDemand.value > 75.0f;

        // And the case the flat ceiling of two was wrong for: the map has
        // run out. No free deposit left on our side, metal short, energy at
        // the cap with generation still ahead of demand -- there is nothing
        // else for a builder to do with the ground, and the energy is going
        // on the floor whether or not a maker drinks it.
        //
        // The gate is "no free deposit" rather than "few", which is what
        // keeps this off the map maxSurplusMetalMakerCount was measured and
        // rejected on: a side that still has somewhere to expand to should
        // expand. See outOfPatchesMetalMakerCount for the numbers from Dark
        // Side that asked for it.
        //
        // And the case the flat ceiling of two is wrong for: an economy
        // that has been stuck for minutes rather than stalled for a tick.
        // bb.starvedRichPasses counts the unbroken run and is kept in
        // update(), which is where the planning pass is counted.
        auto ceiling = profile.targetMetalMakerCount;
        if (profile.starvedMetalMakerCount > ceiling && metalShort && energyRich
            && bb.starvedRichPasses >= profile.starvedMetalMakerPasses)
        {
            ceiling = profile.starvedMetalMakerCount;
        }

        if ((metalShort || surplusBuysAnother) && energyRich && (makers < ceiling || surplusBuysAnother) && total(s.lab) >= 1)
        {
            want(s.metalMaker);
            // The floating one second, and counted against the same target,
            // because it is the same building with a worse price: ARMMAKR and
            // ARMFMKR both cost no metal at all and both burn sixty energy a
            // second, but ARMMAKR is 687 energy to build against 1480, and it
            // is MaxWaterDepth=0. So dry ground gets the cheap one and a base
            // with none gets the other. The planner takes the first of the
            // two it can find a site for, which is the whole mechanism.
            want(s.floatingMetalMaker);
        }
        if (total(s.radar) < profile.targetRadarCount && total(s.solar) >= profile.openingSolarCount)
        {
            want(s.radar);
        }
        // A defence we lost, put back where it stood, ahead of any new one:
        // the place has already shown it needs one. Under the same gate as
        // the fortification below.
        if (rebuild && (!metalShort || !bb.enemiesNearBase.empty()))
        {
            want(rebuild->unitType);
        }
        // Towers are a luxury while metal is short; the factory needs it more.
        if (!metalShort && total(s.lightLaserTower) < profile.targetDefenceCount && total(s.lab) >= 1)
        {
            want(s.lightLaserTower);
        }
        // And each of them fortified: teeth in front, a missile tower
        // behind. Right after the towers because it is what makes them
        // hold; a tooth is eleven metal, so this is energy and builder time
        // more than it is metal. An enemy at the door is no reason to stop.
        if (fortify && (!metalShort || !bb.enemiesNearBase.empty()))
        {
            want(fortify->unitType);
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
        // A vent, if the map has one. 250 energy that no weather touches,
        // for about the price of four solar collectors and a fraction of
        // their ground -- and neither side ever built one, because nothing
        // asked. Once there is a factory, so it does not delay the opening,
        // and as many as there are vents: the site search says when they
        // have run out or are somebody else's.
        if (!s.geothermal.empty() && !geothermalVents.empty() && (total(s.lab) >= 1 || total(s.shipyard) >= 1)
            && total(s.geothermal) < static_cast<int>(geothermalVents.size()))
        {
            want(s.geothermal);
        }

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
        // A factory and the opening's energy, of whichever kind the map
        // allows. Asked as "a lab and four solar collectors" this could never
        // be true on a map with no room for either, which is exactly the map
        // where aircraft matter most: watched over three long games on
        // Crystal Isles, neither side built one aeroplane, and neither side
        // owned anything that could have shot one down.
        auto hasFactory = total(s.lab) >= 1 || (navalFleetTarget(profile, bb) > 0 && total(s.shipyard) >= 1);
        auto openingEnergy = total(s.solar) + total(s.tidalGenerator) >= profile.openingSolarCount;
        // With vehiclePlantFirst the order of the two is turned round, unless
        // the map is one aircraft are needed to cross: there the air plant is
        // how anything arrives at all, and does not wait.
        auto vehiclePlantFirst = profile.vehiclePlantFirst && !airMatters && !landFactoriesPointless && !s.vehiclePlant.empty();
        if (vehiclePlantFirst && !metalShort && hasFactory && openingEnergy && total(s.vehiclePlant) < profile.targetVehiclePlantCount)
        {
            want(s.vehiclePlant);
        }
        if (hasFactory && total(s.airPlant) < profile.targetAirPlantCount && openingEnergy
            && airPlantAffordable
            && (!vehiclePlantFirst || total(s.vehiclePlant) >= profile.targetVehiclePlantCount)
            && (airMatters || total(s.radar) >= profile.targetRadarCount))
        {
            want(s.airPlant);
        }

        // The advanced aircraft plant, which is the gunship and nothing else
        // as far as this AI is concerned, and then the pad that makes an
        // aircraft something other than a one-way trade.
        //
        // In the plan rather than in the surplus rules below, which is where
        // this sat first. At about 2600 metal it is never affordable out of
        // an ordinary minute's income, so the surplus was the only thing
        // that could ever have bought it -- and that made a strategic
        // decision wait on a full store. The saving rule is the right
        // mechanism: it holds the metal for the thing at the top of the list
        // and lets the economy through underneath, which is how the advanced
        // lab already gets paid for. See worthTheLongWait below.
        //
        // Only the air constructor can put either up (see AiSideUnits::
        // advancedAirPlant), and want() asks the build tree, so a side with
        // none standing never takes the job.
        if (profile.techLevelTwo && incomeSupportsTech && bb.airWorthIt
            && !s.advancedAirPlant.empty() && !s.airConstructor.empty()
            && total(s.airPlant) >= 1 && total(s.airConstructor) >= 1
            && total(s.advancedAirPlant) < profile.targetAdvancedAirPlantCount)
        {
            want(s.advancedAirPlant);
        }
        if (!s.airRepairPad.empty() && total(s.advancedAirPlant) >= 1
            && total(s.airRepairPad) < profile.targetAirRepairPadCount)
        {
            // AiSideUnits has already dropped the slot unless the data
            // makes it a real pad -- IsAirBase and a worker time -- so
            // there is nothing left to test here.
            want(s.airRepairPad);
        }

        auto energyToSpare = bb.energyStorage.value > 0.0f && bb.currentEnergy.value >= bb.energyStorage.value * 0.8f
            && bb.energyIncome.value > bb.energyDemand.value;
        if (total(s.solar) < profile.targetSolarCount && !(profile.solarOnDemand && energyToSpare))
        {
            want(s.solar);
        }
        if (total(s.metalExtractor) < profile.targetMetalExtractorCount)
        {
            want(s.metalExtractor);
        }
        // A vehicle plant comes last: fast scouts and tanks once the economy is ticking over.
        if (!metalShort && !landFactoriesPointless && total(s.airPlant) >= profile.targetAirPlantCount && total(s.vehiclePlant) < profile.targetVehiclePlantCount)
        {
            want(s.vehiclePlant);
        }

        // A factory beyond the targets while the income is going unspent
        // (spendSurplusOnCapacity): the vehicle plant first, because it is
        // the cheaper of the two on both sides, then a second lab. Below the
        // extractors and the towers above on purpose -- another factory is
        // what to do with metal there is nothing else to do with, not a
        // reason to stop taking ground.
        if (spendingCapacityShort && !metalShort && hasFactory)
        {
            // Spare capacity on an island map buys another yard rather than
            // another land factory: the reasoning is the same as the opening
            // one above, and it would be odd to defer the first lab and then
            // buy the second out of surplus.
            if (landFactoriesPointless)
            {
                if (total(s.shipyard) < profile.targetShipyardCount + profile.surplusFactories)
                {
                    want(s.shipyard);
                }
            }
            else if (!s.vehiclePlant.empty() && total(s.vehiclePlant) < profile.targetVehiclePlantCount + profile.surplusFactories)
            {
                want(s.vehiclePlant);
            }
            else if (!labPointless && total(s.lab) < 1 + profile.surplusFactories)
            {
                want(s.lab);
            }
        }

        // Naval: a shipyard, once the map's water is worth a fleet.
        // navalFleetTarget folds together the map-character gate -- mirroring
        // how airMatters gates the air plant above -- and the kill switch:
        // navalFleetSize=0 makes the target always zero, so this never fires
        // and nothing downstream in planFactories does either. ARMSY/CORSY
        // sit on page two of the commander and both ordinary land
        // constructors, so no construction ship is needed first -- see
        // AiSideUnits.h and docs/ai-architecture-proposal.md S:13.2.
        if (navalFleetTarget(profile, bb) > 0 && !s.shipyard.empty() && total(s.shipyard) < profile.targetShipyardCount)
        {
            want(s.shipyard);
        }

        // The water structures, behind the same navalFleetTarget gate as the
        // yard above -- zero on a land map, zero when navalFleetSize is the
        // kill switch -- so none of this can fire where ships do not matter.
        //
        // No builder test is needed here. Only the commander's pages 3 and 4
        // and the construction ship carry these buttons; ARMCK's pages carry
        // none of them. want() filters on buildTree.canBuild, so the jobs
        // fall to whoever actually has the button.
        if (navalFleetTarget(profile, bb) > 0)
        {
            // Energy a water map can actually site. ARMSOLAR is 5x5, 145
            // metal and MaxWaterDepth=0, so every one of them competes for
            // the dry ground the base, the factories and the extractors are
            // already short of -- and on a 92% water map there is barely any.
            // ARMTIDE is 3x3 and 82 metal (CORTIDE 4x4 and 81) and stands in
            // water nothing else wants.
            if (!s.tidalGenerator.empty() && total(s.tidalGenerator) < profile.targetTidalCount)
            {
                want(s.tidalGenerator);
            }
            // The cheapest building either side owns, at 20 metal, and it
            // MAKES energy rather than costing any. It is also the only way
            // the AI can see a submarine -- it has been building its own
            // since the fleet work and has never been able to see one.
            if (!s.sonar.empty() && total(s.sonar) < profile.targetSonarCount)
            {
                want(s.sonar);
            }
            // 804 metal (CORTL 831) is a destroyer's price for something that
            // cannot move, so this waits until there is something for it to
            // shoot: an enemy hull actually seen, not merely a wet map. That
            // is the whole difference between a defence worth its cost and
            // the kind that is built because the map looked dangerous.
            if (enemyNavalSeen && !s.torpedoLauncher.empty() && total(s.torpedoLauncher) < profile.targetTorpedoLauncherCount)
            {
                want(s.torpedoLauncher);
            }
            // After the launcher, because a yard under fire wants its answer
            // before it wants a second yard.
            wantAdvancedShipyard();
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

        wantStorage(false);

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
            // The FIRST reactor needs no such reason once the income is
            // there: an advanced lab's units and a moho's appetite are both
            // energy the level-one base was never sized for, and waiting for
            // the stall means meeting it with the reactor still to pay for.
            auto firstReactorDue = incomeSupportsTech && total(s.fusion) < 1;
            if ((energyBinding || metalFull || firstReactorDue) && total(s.fusion) < profile.targetFusionCount)
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
        // The base grows with what it earns; see surplusExpansion. Only with
        // the store full, so none of this competes with a plan still being
        // paid for, and the number of factories follows the income that
        // would have to feed them.
        if (profile.surplusExpansion && metalFull)
        {
            auto step = std::max(1, profile.surplusFactoryIncomeStep);
            auto allowed = std::min(profile.surplusFactoryCap, 1 + static_cast<int>(bb.metalIncome.value) / step);
            // The tech step, whatever the knob says about choosing it: that
            // argument is about whether the tier repays its price in a short
            // game, and a store that is full has already paid it.
            if (bb.metalIncome.value >= static_cast<float>(2 * profile.techMinMetalIncome) && total(s.lab) >= 1 && total(s.advancedLab) < 1)
            {
                want(s.advancedLab);
            }
            if (navalFleetTarget(profile, bb) > 0)
            {
                if (total(s.shipyard) < allowed)
                {
                    want(s.shipyard);
                }
                // Nothing the other side builds on a water map shoots up
                // until it has seen an aircraft, so the first ones are free.
                if (total(s.airPlant) < 1)
                {
                    want(s.airPlant);
                }
            }
            else
            {
                if (total(s.lab) < allowed)
                {
                    want(s.lab);
                }
                if (total(s.vehiclePlant) < allowed)
                {
                    want(s.vehiclePlant);
                }
            }
        }
        want(s.metalExtractor);
        // And the submerged patches, once the dry ones are gone. This sits
        // directly below the land extractor because the planner takes the
        // first entry it can find a SITE for: every dry patch is tried first,
        // and this is reached exactly when there are none left -- which on a
        // 92% water map is early and permanent.
        //
        // Worth having despite being the worse buy. ARMUWMEX is 130 metal
        // against ARMMEX's 50 (CORUWMEX 125), and four times the energy, for
        // ExtractsMetal=0.001 -- the very same trickle. It is not better
        // metal, it is metal that was otherwise unreachable.
        //
        // Gated on the map actually having water rather than on
        // navalFleetTarget: a lake map can have a submerged patch worth
        // taking while wanting no navy at all, and without a gate of some
        // kind every land map would pay for a patch scan a pass to be told
        // there is nowhere to put one.
        // ...and only where there is actually metal under the water, which is
        // asked directly rather than guessed at from how wet the map is.
        //
        // Both halves of that matter, and a census of all 52 shipped maps is
        // what settled them. Wanting it unconditionally is dear: on a map
        // with no submerged patch it cost about 3500 failed site searches and
        // 4473 log lines in one game, every one asking the same question and
        // getting the same answer -- and 23 of the 52 are such maps, Hundred
        // Isles among them at 92% water with 531 patches and not one wet.
        //
        // But the water fraction is the WRONG test for it, which is worth
        // saying plainly because it was the first thing tried. Metal under
        // water has little to do with how much water there is: Crystal Maze
        // is 3% water with 36 submerged patches, Sector 410b 4% with 81, Town
        // & Country 8% with 135, Eastside Westside 9% with 261. A
        // MixedMapWaterFraction gate would have refused every one of them.
        // The count is exact where the fraction is a proxy, so the proxy goes.
        if (submergedMetalPatches > 0)
        {
            want(s.underwaterMetalExtractor);
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

    void BuildManager::planFactories(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const
    {
        const auto& s = bb.sideUnits;
        auto total = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedTotalCounts, t); };

        // Every factory that makes land combat units asks this, so it is
        // asked once: it depends on the profile and the blackboard and not
        // on which factory is being planned. Three branches below make
        // them -- the kbot lab its raiders and rocket kbots, the vehicle
        // plant its tanks, the advanced lab its assault kbots -- and a cap
        // that guarded only one of them would quietly do nothing the
        // moment a second factory type existed.
        // Unreachable ground is necessary but not sufficient. Capping the
        // land army pays on a map that is nearly all water and costs more
        // than it buys on one that is merely half water -- measured, and the
        // numbers are in isolatedLandArmyCapMinWaterFraction's own comment --
        // so how much water there is has to be part of the question. The map
        // character would be the obvious thing to ask and is the wrong one:
        // its Water threshold is 0.40, which takes in the very map where
        // capping was a regression.
        auto waterDominates = bb.mapIntel.valid
            && bb.mapIntel.waterFraction >= profile.isolatedLandArmyCapMinWaterFraction;
        auto landArmyCapped = profile.isolatedLandArmyCap > 0 && bb.hasUnreachableGround && waterDominates
            && bb.armySize >= profile.isolatedLandArmyCap;

        // Fighters to match the raid: as many as the most armed aircraft of
        // theirs ever known at once, between the standing pair and the cap.
        auto fighterTarget = std::clamp(bb.enemyArmedAirPeak, profile.targetFighterCount, std::max(profile.targetFighterCount, profile.maxReactiveFighterCount));

        // The tier-two economy reserve; see tierTwoEconomyReserve.
        auto completed = [&](const std::string& t) { return t.empty() ? 0 : countOf(bb.ownedCompletedCounts, t); };
        bool reserveHolds = false;
        if (profile.tierTwoEconomyReserve)
        {
            auto landBuilder = completed(s.advancedConstructor) >= 1;
            auto seaBuilder = completed(s.advancedConstructionSub) >= 1;
            auto mohoOwed = landBuilder && !s.mohoExtractor.empty() && completed(s.mohoExtractor) < 1;
            auto reactorOwed = ((landBuilder && !s.fusion.empty()) || (seaBuilder && !s.underwaterFusion.empty()))
                && completed(s.fusion) + completed(s.underwaterFusion) < 1;
            // The lab itself, when asked to: teching allowed and worth it for
            // this side, the income there, a constructor to build it, and
            // the game old enough that the opening army exists.
            auto labOwed = profile.tierTwoReserveCoversLabAfterSeconds > 0 && profile.techLevelTwo && !s.advancedLab.empty()
                && completed(s.advancedLab) < 1 && completed(s.lab) >= 1 && completed(s.constructor) >= 1
                && bb.advancedArmyValueRatio >= profile.techMinArmyValueRatio
                && bb.metalIncome.value >= static_cast<float>(profile.techMinMetalIncome)
                && bb.now.value >= static_cast<unsigned int>(profile.tierTwoReserveCoversLabAfterSeconds) * SimTicksPerSecond;
            if (mohoOwed || reactorOwed || labOwed)
            {
                if (!tierTwoReserveStarted)
                {
                    tierTwoReserveStarted = bb.now;
                    LOG_INFO << "AI build: holding the factories for the tier-two economy (" << (labOwed ? "lab " : "") << (mohoOwed ? "moho " : "") << (reactorOwed ? "reactor" : "") << ")";
                    sim.eventLog.event(sim.gameTime.value, "build_tier_assessment")
                        .set("player", aiOwner.value)
                        .set("lab", labOwed)
                        .set("moho", mohoOwed)
                        .set("reactor", reactorOwed)
                        .set("why", "tier_two_reserve_started")
                        .detail("holding the factories for the tier-two economy");
                }
                auto elapsed = bb.now.value - tierTwoReserveStarted->value;
                auto inTime = elapsed < static_cast<unsigned int>(std::max(0, profile.tierTwoReserveMaxSeconds)) * SimTicksPerSecond;
                auto quiet = bb.phase != GamePhase::Defend && bb.enemiesNearBase.empty();
                auto armed = bb.armySize + static_cast<int>(bb.navalCombatUnits.size()) >= profile.tierTwoReserveMinArmySize;
                reserveHolds = inTime && quiet && armed;
            }
        }

        // A hold that lasts is a state, not a transition. Log the spell when
        // it begins, when what is held changes, and when it ends, and mark it
        // seen so the sweep after the loop can find the ones that stopped.
        auto holdSubject = [](const UnitState& factory) {
            std::string subject;
            for (const auto& [type, count] : factory.buildQueue)
            {
                if (!subject.empty())
                {
                    subject += ',';
                }
                subject += type + "x" + std::to_string(count);
            }
            return subject;
        };

        auto emitFactoryHold = [&](const std::string& ev, const std::string& factoryType, UnitId factoryId, int queueSize, const std::string& subject, const std::string& why, unsigned int heldFor, const std::string& detail) {
            sim.eventLog.event(sim.gameTime.value, ev)
                .set("player", aiOwner.value)
                .set("unit", factoryId.value)
                .set("factory", factoryType)
                .set("queue", queueSize)
                .set("subject", subject)
                .set("held_for", heldFor)
                .set("why", why)
                .detail(detail);
        };

        auto trackFactoryHold = [&](const std::string& ev, const std::string& kind, const std::string& factoryType, UnitId factoryId, int queueSize, const std::string& subject) {
            auto it = factoryHoldSpells.find(factoryId.value);
            if (it == factoryHoldSpells.end())
            {
                emitFactoryHold(
                    ev, factoryType, factoryId, queueSize, subject,
                    kind == "queue" ? "queue_not_drained" : "tier_two_economy", 0,
                    kind == "queue" ? "the queue still holds something" : "holds production for the tier-two economy");
                factoryHoldSpells[factoryId.value] = FactoryHoldSpell{kind, factoryType, subject, sim.gameTime, true};
                return;
            }
            if (it->second.subject != subject || it->second.kind != kind)
            {
                emitFactoryHold(
                    ev, factoryType, factoryId, queueSize, subject,
                    kind == "queue" ? "queue_changed" : "tier_two_target_changed",
                    sim.gameTime.value - it->second.since.value,
                    kind == "queue" ? "the held queue changed" : "the held tier-two target changed");
                it->second.kind = kind;
                it->second.subject = subject;
            }
            it->second.seenThisPass = true;
        };

        for (auto factoryId : bb.factories)
        {
            const auto& factory = sim.getUnitState(factoryId);

            // A besieged factory's queue is EMPTIED, not merely left alone.
            //
            // This is the whole of the all-water soft-lock and it is why
            // declining to top the queue up was worth nothing: a factory
            // does not lose its queue entry when the frame on the slipway
            // dies. It starts the next one from the same entry, so a yard
            // holding one queued destroyer produces a frame every
            // twenty-nine ticks for the rest of the game, and the planner
            // is never consulted again because the queue is never empty.
            // Instrumented on Brain Coral, seed 3: two queue entries
            // between them, 283 and 229 ARMROY frames born and shot.
            //
            // So the queue has to be taken off it, which is the same
            // command a player cancelling a build order sends -- a negative
            // count into ModifyBuildQueue. The yard idles until the gun has
            // gone, and then the planner fills it again on the ordinary
            // path below.
            if (profile.noticeProductionHarassment && !factory.buildQueue.empty()
                && std::find(bb.besiegedFactories.begin(), bb.besiegedFactories.end(), factoryId) != bb.besiegedFactories.end())
            {
                // Walked as the queue itself is held, an ordered vector, and
                // not through getBuildQueueTotals, which is an unordered_map
                // -- this runs inside the simulation, so the order commands
                // come out in has to be the same on every peer.
                for (const auto& [queuedType, queuedCount] : factory.buildQueue)
                {
                    if (queuedCount <= 0)
                    {
                        continue;
                    }
                    LOG_INFO << "AI factory: " << factory.unitType << " " << factoryId.value
                             << " stops building " << queuedType << " (" << queuedCount
                             << " queued); an armed enemy is sitting on it and every frame dies as it is born";
                    sim.eventLog.event(sim.gameTime.value, "factory_queue_cleared")
                        .set("player", aiOwner.value)
                        .set("unit", factoryId.value)
                        .set("factory", factory.unitType)
                        .set("subject", queuedType)
                        .set("count", queuedCount)
                        .set("why", "besieged")
                        .detail("stops building, an armed enemy is sitting on it");
                    outCommands.emplace_back(PlayerUnitCommand(factoryId, PlayerUnitCommand::ModifyBuildQueue{-queuedCount, queuedType}));
                }
                continue;
            }

            if (!factory.buildQueue.empty())
            {
                // A factory is only topped up once its queue drains, so a
                // plant that queues one unit which never finishes is never
                // asked for anything again. Nothing recorded that until now.
                LOG_DEBUG << "AI factory: " << factory.unitType << " " << factoryId.value
                          << " skipped, queue holds " << factory.buildQueue.size();
                trackFactoryHold("factory_hold", "queue", factory.unitType, factoryId, static_cast<int>(factory.buildQueue.size()), holdSubject(factory));
                continue;
            }

            // Not while something is standing over it shooting.
            //
            // A nanoframe has zero hit points the tick it is born, so a
            // single gun parked off a shipyard kills every hull the yard
            // makes, one at a time, for as long as the yard keeps making
            // them. Topping the queue up there is not production, it is the
            // whole economy being handed to a hundred-metal scout ship a
            // frame at a time -- measured at 1408 destroyer frames in ten
            // games. The yard is not told to stop; it is simply not asked
            // for anything more until the gun has gone, which is what makes
            // this self-clearing rather than a permanent shutdown.
            if (profile.noticeProductionHarassment
                && std::find(bb.besiegedFactories.begin(), bb.besiegedFactories.end(), factoryId) != bb.besiegedFactories.end())
            {
                LOG_DEBUG << "AI factory: " << factory.unitType << " " << factoryId.value
                          << " not topped up, an armed enemy is sitting on it";
                sim.eventLog.event(sim.gameTime.value, "factory_refusal")
                    .set("player", aiOwner.value)
                    .set("unit", factoryId.value)
                    .set("factory", factory.unitType)
                    .set("why", "besieged")
                    .detail("not topped up, an armed enemy is sitting on it");
                continue;
            }

            // Something for a transport to carry. wantsTransport says there
            // is somewhere worth going that needs a lift; it does not say
            // anyone is waiting for one. On a map with no room for a lab
            // there is no construction kbot and no army that walks, and the
            // yard built a 900-metal transport all the same, which then lay
            // at anchor for the whole game.
            bool hasCargo = !bb.combatUnits.empty() || total(s.constructor) > 0 || total(s.advancedConstructor) > 0
                || total(s.lab) > 0 || total(s.vehiclePlant) > 0;

            std::string next;

            // Whichever type is furthest below its share of the line.
            // Cross-multiplied so it stays in integers, and strictly
            // less so that a tie goes to the first named, which is what
            // makes 2:1 come out as "raider while raiders <= 2 * rockets".
            auto pickByShare = [&](std::initializer_list<std::pair<const std::string*, int>> line) {
                const std::string* best = nullptr;
                long long bestCount = 0;
                long long bestShare = 0;
                for (const auto& [type, share] : line)
                {
                    if (type->empty() || share <= 0)
                    {
                        continue;
                    }
                    auto count = static_cast<long long>(countOf(bb.ownedTotalCounts, *type));
                    if (!best || count * bestShare < bestCount * share)
                    {
                        best = type;
                        bestCount = count;
                        bestShare = share;
                    }
                }
                return best ? *best : std::string();
            };
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
                else if (!s.airTransport.empty() && bb.wantsTransport && hasCargo && total(s.airTransport) < profile.targetTransportCount)
                {
                    next = s.airTransport;
                }
                else if (bb.enemyAirThreat && bb.enemyArmedAirPeak > 0 && !s.fighter.empty() && total(s.fighter) < profile.targetFighterCount)
                {
                    // Bombers or torpedo bombers, not a scout: the first pair
                    // of fighters comes before the constructor.
                    next = s.fighter;
                }
                else if (!s.airConstructor.empty() && total(s.airConstructor) < profile.targetAirConstructorCount)
                {
                    next = s.airConstructor;
                }
                else if (bb.enemyAirThreat && !s.fighter.empty() && total(s.fighter) < fighterTarget)
                {
                    // Fighters only once something of theirs is actually
                    // flying, for the same reason the anti-air kbot waits:
                    // cover built against nothing is metal not spent on the
                    // army.
                    next = s.fighter;
                }
                else
                {
                    // Bombers, and more of them while the store is full:
                    // an idle plant with the metal piled up is 850 metal
                    // doing nothing (surplusBomberMultiplier).
                    auto bomberTarget = profile.targetBomberCount;
                    if (profile.surplusExpansion && bb.metalStorage.value > 0.0f
                        && bb.currentMetal.value >= bb.metalStorage.value * 0.8f)
                    {
                        bomberTarget *= std::max(1, profile.surplusBomberMultiplier);
                    }
                    if (!s.bomber.empty() && total(s.bomber) < bomberTarget)
                    {
                        next = s.bomber;
                    }
                }
            }
            else if (!s.advancedAirPlant.empty() && factory.unitType == s.advancedAirPlant)
            {
                // The gunship, and nothing else. The rest of this plant's
                // page -- the level-two fighter, bomber and missile
                // aircraft -- are better versions of things the level-one
                // plant already supplies, and the AI has no rule that would
                // tell them apart from what it is already flying. The
                // gunship is the one unit on it that does something nothing
                // else the AI owns can do.
                if (!s.gunship.empty() && total(s.gunship) < profile.targetGunshipCount)
                {
                    next = s.gunship;
                }
            }
            else if (!s.vehiclePlant.empty() && factory.unitType == s.vehiclePlant)
            {
                if (!s.scoutVehicle.empty() && total(s.scoutVehicle) < profile.targetScoutVehicleCount)
                {
                    next = s.scoutVehicle;
                }
                else if (!landArmyCapped)
                {
                    // A tank cannot cross water either. The scout above is
                    // exempt for the same reason the constructor is: eyes
                    // and builders are not the raiding army.
                    next = pickByShare({{&s.tank, profile.vehicleTankShare}, {&s.missileTruck, profile.vehicleMissileTruckShare}, {&s.mediumTank, profile.vehicleMediumTankShare}});
                }
            }
            else if (!s.shipyard.empty() && factory.unitType == s.shipyard)
            {
                // Eyes first -- ARMPT/CORPT is the cheapest hull afloat, the
                // way the scout plane is the air plant's first job -- then
                // the sea transport once TransportManager actually wants one
                // (bb.wantsTransport, the same signal the air transport
                // reads), then destroyers as the fleet's generalist body,
                // and submarines only once enough of those stand that the
                // fleet can already win a surface fight without them: a
                // submarine's only weapon is a waterweapon (S:13.2), so it
                // cannot answer anything that is not afloat.
                auto fleetTarget = navalFleetTarget(profile, bb);
                // A full store means the fleet is not what is holding the
                // metal back, so build more of it; see surplusExpansion.
                auto storeFull = bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.8f;
                if (profile.surplusExpansion && storeFull)
                {
                    fleetTarget *= std::max(1, profile.surplusFleetMultiplier);
                }
                // Builders for the metal under the sea; see
                // targetConstructionShipCount. Second only to the scout, the
                // way the lab makes its constructor before any army: each one
                // is income, and the fleet is paid for out of income. Asked
                // whether or not the map wants a fleet, because a yard that
                // stands on a map with submerged metal is worth a builder
                // even where it is worth no warships.
                auto wantConstructionShip = !s.constructionShip.empty() && submergedMetalPatches > 0
                    && total(s.constructionShip) < profile.targetConstructionShipCount;
                if (fleetTarget > 0)
                {
                    auto submarineTarget = std::min(profile.targetSubmarineCount, fleetTarget);
                    auto destroyerTarget = std::max(0, fleetTarget - submarineTarget);
                    if (!s.scoutShip.empty() && total(s.scoutShip) < profile.targetScoutShipCount)
                    {
                        next = s.scoutShip;
                    }
                    else if (wantConstructionShip)
                    {
                        next = s.constructionShip;
                    }
                    else if (!s.seaTransport.empty() && bb.wantsTransport && hasCargo && total(s.seaTransport) < profile.targetSeaTransportCount)
                    {
                        next = s.seaTransport;
                    }
                    else if (!s.destroyer.empty() && total(s.destroyer) < destroyerTarget)
                    {
                        next = s.destroyer;
                    }
                    else if (!s.submarine.empty() && total(s.submarine) < submarineTarget
                        && total(s.destroyer) >= profile.submarineMinDestroyerCount)
                    {
                        next = s.submarine;
                    }
                }
                else if (wantConstructionShip)
                {
                    next = s.constructionShip;
                }
            }
            else if (!s.advancedShipyard.empty() && factory.unitType == s.advancedShipyard)
            {
                // Cover first if there is anything to cover against: a
                // torpedo bomber kills a battleship for a twentieth of its
                // price, and nothing else afloat can shoot at one. Then two
                // cruisers to every battleship, cruisers first -- they are
                // the hull with the depth charge, so they are also what keeps
                // a submarine off the 4404 metal that follows them.
                auto cruisers = total(s.cruiser);
                auto battleships = total(s.battleship);
                auto storeFull = bb.metalStorage.value > 0.0f && bb.currentMetal.value >= bb.metalStorage.value * 0.8f;
                auto fleetMultiplier = (profile.surplusExpansion && storeFull) ? std::max(1, profile.surplusFleetMultiplier) : 1;
                auto cruiserTarget = profile.targetCruiserCount * fleetMultiplier;
                auto battleshipTarget = profile.targetBattleshipCount * fleetMultiplier;
                // The construction sub, once there is an escort afloat and
                // only while it still has its one job to do: a platform to
                // build on a map that gave the air plant no ground.
                auto wantConstructionSub = profile.targetSeaplanePlatformCount > 0 && !s.advancedConstructionSub.empty()
                    && !s.seaplanePlatform.empty() && total(s.airPlant) < 1
                    && total(s.seaplanePlatform) < profile.targetSeaplanePlatformCount
                    && total(s.advancedConstructionSub) < 1 && cruisers >= 2;
                // Or a reactor to build: the underwater fusion plant is the
                // sub's alone as well, air plant or no air plant.
                auto wantSubForReactor = profile.targetUnderwaterFusionCount > 0 && !s.advancedConstructionSub.empty()
                    && !s.underwaterFusion.empty() && total(s.fusion) + total(s.underwaterFusion) < 1
                    && total(s.advancedConstructionSub) < 1 && cruisers >= 2;
                wantConstructionSub = wantConstructionSub || wantSubForReactor;
                if (bb.enemyAirThreat && !s.antiAirShip.empty() && total(s.antiAirShip) < profile.targetAntiAirShipCount)
                {
                    next = s.antiAirShip;
                }
                else if (wantConstructionSub)
                {
                    next = s.advancedConstructionSub;
                }
                else if (!s.cruiser.empty() && cruisers < cruiserTarget && cruisers < 2 * (battleships + 1))
                {
                    next = s.cruiser;
                }
                else if (!s.battleship.empty() && battleships < battleshipTarget)
                {
                    next = s.battleship;
                }
                else if (!s.cruiser.empty() && cruisers < cruiserTarget)
                {
                    next = s.cruiser;
                }
            }
            else if (!s.seaplanePlatform.empty() && factory.unitType == s.seaplanePlatform)
            {
                // Cover when something of theirs is flying, otherwise the
                // torpedo: nothing the other side floats can shoot up until
                // it has seen an aircraft and built for one.
                if (bb.enemyAirThreat && !s.seaplaneFighter.empty() && total(s.seaplaneFighter) < fighterTarget)
                {
                    next = s.seaplaneFighter;
                }
                else if (!s.torpedoSeaplane.empty() && total(s.torpedoSeaplane) < profile.targetTorpedoSeaplaneCount)
                {
                    next = s.torpedoSeaplane;
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
                else if (!landArmyCapped)
                {
                    // And neither can a Zeus or a Can, however good the
                    // metal-for-metal trade is on land. The advanced
                    // constructor above stays exempt with the other
                    // builders.
                    next = s.advancedAssault;
                }
            }
            else
            {
                auto constructors = s.constructor.empty() ? 0 : countOf(bb.ownedTotalCounts, s.constructor);
                // One more while a laser tower is still short of its teeth
                // (fortifyExtraConstructors): counted from what stands and
                // what is going up, which is all this needs to know.
                auto constructorTarget = profile.targetConstructorCount;
                // More while the income is going unspent
                // (spendSurplusOnCapacity).
                if (spendingCapacityShort)
                {
                    constructorTarget += profile.surplusConstructors;
                }
                // And more while metal lies unclaimed on our side
                // (expansionConstructors).
                if (profile.expansionConstructors > 0 && bb.baseAnchor)
                {
                    auto perBuilder = std::max(1, profile.freeDepositsPerExpansionConstructor);
                    constructorTarget += std::min(profile.expansionConstructors, freeDepositsOnOurSide(sim, profile, bb) / perBuilder);
                }
                if (profile.fortifyTowers && !s.dragonsTeeth.empty() && !s.lightLaserTower.empty())
                {
                    auto towers = countOf(bb.ownedTotalCounts, s.lightLaserTower);
                    auto teeth = countOf(bb.ownedTotalCounts, s.dragonsTeeth);
                    if (towers > 0 && teeth < towers * profile.fortifyTeethPerTower)
                    {
                        constructorTarget += profile.fortifyExtraConstructors;
                    }
                }
                if (!s.constructor.empty() && constructors < constructorTarget)
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
                else if (landArmyCapped)
                {
                    // Deliberately nothing. The army is as large as it is
                    // worth being on a map it cannot walk off, and the
                    // income is wanted by the shipyards, which are the
                    // only thing here that can reach the enemy at all.
                    //
                    // This branch sits below the constructor and anti-air
                    // cases on purpose: the cap is about the raiding army,
                    // not about stopping the base from working. A base
                    // that cannot replace a lost constructor, or cannot
                    // answer aircraft, has been capped into helplessness
                    // rather than steered.
                    //
                    // Leaving next empty is what declines the order: the
                    // guard below only queues when it is non-empty. The
                    // lab is not told to stop, it is simply not topped up,
                    // so it finishes what it holds and then idles -- and
                    // an idle factory is not spending, which is the whole
                    // point.
                }
                else
                {
                    // Two raiders for every rocket kbot, as shipped; the
                    // profile holds the ratio.
                    auto shares = counterShares(sim, profile, bb);
                    next = pickByShare({{&s.raider, shares.raider}, {&s.rocketKbot, shares.rocketKbot}, {&s.artilleryKbot, shares.artilleryKbot}});
                }
            }

            if (!next.empty() && reserveHolds)
            {
                // Builders are what the reserve is FOR, so they still come.
                auto nextDef = sim.unitDefinitions.find(next);
                if (nextDef != sim.unitDefinitions.end() && !nextDef->second.builder)
                {
                    LOG_DEBUG << "AI factory: " << factory.unitType << " " << factoryId.value << " holds " << next << " for the tier-two economy";
                    trackFactoryHold("factory_hold_t2", "tier_two", factory.unitType, factoryId, 0, next);
                    next.clear();
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
                sim.eventLog.event(sim.gameTime.value, "factory_start")
                    .set("player", aiOwner.value)
                    .set("unit", factoryId.value)
                    .set("factory", factory.unitType)
                    .set("subject", next)
                    .set("why", "planned")
                    .detail("factory starts a unit");
                outCommands.emplace_back(PlayerUnitCommand(factoryId, PlayerUnitCommand::ModifyBuildQueue{1, next}));
            }
        }

        // A hold that was not seen this pass has ended: the queue drained, the
        // reserve lifted, or the factory is gone. Emit the end with how long it
        // lasted, then forget it.
        for (auto it = factoryHoldSpells.begin(); it != factoryHoldSpells.end();)
        {
            if (it->second.seenThisPass)
            {
                it->second.seenThisPass = false;
                ++it;
                continue;
            }
            const bool tierTwo = it->second.kind == "tier_two";
            sim.eventLog.event(sim.gameTime.value, tierTwo ? "factory_hold_t2" : "factory_hold")
                .set("player", aiOwner.value)
                .set("unit", it->first)
                .set("factory", it->second.factory)
                .set("subject", it->second.subject)
                .set("held_for", sim.gameTime.value - it->second.since.value)
                .set("why", tierTwo ? "tier_two_released" : "queue_drained")
                .detail("the hold ended");
            it = factoryHoldSpells.erase(it);
        }
    }

    BuildManager::PrioritySite BuildManager::choosePrioritySite(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        const ReachabilityMap& reachability,
        std::minstd_rand& rng,
        const std::string& next,
        const UnitDefinition& nextDef,
        const BuilderContext& ctx,
        std::vector<PlayerCommand>& outCommands)
    {
        PrioritySite result;
        // A moho extractor stands on a metal patch exactly as the
        // level-one one does, and must go through the same search: the
        // ordinary site chooser deliberately refuses a patch, so a moho
        // routed through it would either find nowhere or stand somewhere
        // it produces nothing.
        if (next == ctx.sideUnits.metalExtractor
            || (!ctx.sideUnits.mohoExtractor.empty() && next == ctx.sideUnits.mohoExtractor)
            || (!ctx.sideUnits.underwaterMetalExtractor.empty() && next == ctx.sideUnits.underwaterMetalExtractor))
        {
            // The underwater extractor comes through here for the same
            // reason the moho does -- it has to stand on a patch, and the
            // ordinary site chooser deliberately refuses those -- but it
            // needs one thing they do not, below: no reachability test,
            // because a patch under twenty feet of water is not ground
            // anybody walks to. canBeBuiltAt still holds it to its own
            // MinWaterDepth, so it cannot land anywhere shallow.
            auto submerged = !ctx.sideUnits.underwaterMetalExtractor.empty() && next == ctx.sideUnits.underwaterMetalExtractor;
            // Nearby patches first; further afield if there are none.
            // Only patches the builder can walk to: islands are for the transport.
            // A builder that flies is not held to the ground's shape.
            // Applied to an air constructor this test is exactly
            // backwards: it would confine the one builder that can cross
            // water to the patches everything else can already walk to,
            // and leave the island patch -- the one nobody is contesting,
            // and the reason to own an air constructor at all -- refused.
            // siteReachable, hoisted above: the same test, and it is the
            // reasoning in this comment that it carries.
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
            auto commanderReach = profile.commanderMexSearchRadius;
            // While somebody else can take the far rocks, the commander
            // keeps to the near ones (commanderPrefersNearSites). With no
            // construction unit alive it expands as before, because a
            // side that has lost its builders must still expand.
            if (profile.commanderPrefersNearSites && profile.commanderLeashRadius > 0_ss && !bb.sideUnits.constructor.empty()
                && countOf(bb.ownedTotalCounts, bb.sideUnits.constructor) > 0)
            {
                commanderReach = rweMin(commanderReach, profile.commanderLeashRadius);
            }
            auto leashSquared = commanderReach * commanderReach;
            // The ground rules, asked of a deposit (see chooseMexSite):
            // a deposit the leash or a gun's reach cuts through is taken
            // at its heart, not on whichever edge lies inside.
            auto walkable = [&](const SimVector& p) {
                if (ctx.builderDef.commander && ctx.builderAtBase && bb.baseAnchor->distanceSquared(p) > leashSquared)
                {
                    return false;
                }
                return !underGuns(p) && (submerged || !ctx.siteReachable || ctx.siteReachable(p));
            };
            // And the site rules, asked of the one placement a deposit
            // would be given.
            auto siteFree = [&](const SimVector& p) {
                // Kept for the moho that is about to stand there.
                if (extractorUpgrade && extractorUpgrade->site.distanceSquared(p) < 64_ss * 64_ss)
                {
                    return false;
                }
                // And for the patch another builder is already walking
                // to: nothing stands there yet, so only its order says so.
                if (siteClaimedByAnother(sim, aiOwner, ctx.builderId, p, profile.claimedSiteRadius))
                {
                    return false;
                }
                return !siteFailedLately(sim, p);
            };
            result.site = chooseMexSite(sim, next, ctx.builder.position, profile.nearMexSearchRadius, rng, siteFree, walkable);
            if (!result.site && ctx.builderAtBase)
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
                auto radius = ctx.builderDef.commander ? profile.commanderMexSearchRadius : profile.expansionMexSearchRadius;

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
                // Counted from inside the predicate, so these are the calls
                // the search actually made rather than a re-walk of the
                // patches under different rules (#202).
                int refusedOffSide = 0, refusedUnknown = 0, refusedUnwalkable = 0;
                auto acceptable = [&](const SimVector& p) {
                    if (!onOurSide(p))
                    {
                        ++refusedOffSide;
                        return false;
                    }
                    if (!known(p))
                    {
                        ++refusedUnknown;
                        return false;
                    }
                    if (!walkable(p))
                    {
                        ++refusedUnwalkable;
                        return false;
                    }
                    return true;
                };
                MexSiteTally tally;
                result.site = chooseMexSite(sim, next, *bb.baseAnchor, radius, rng, siteFree, acceptable, &tally);

                if (!result.site)
                {
                    // Why not, for the log. Two tallies, and they answer
                    // different questions: the deposit tally above comes
                    // from inside the search and says what the search
                    // actually rejected, and the cell tally below re-walks
                    // the patches asking only the ground rules.
                    //
                    // Only the first can be read as a reason. The second
                    // cannot see whether a footprint fits, whether a
                    // building already stands on the rock, or whether the
                    // deposit is waiting for a unit to step off it -- and
                    // on a map where every deposit in reach was already
                    // taken it therefore reported ninety cells with nothing
                    // wrong with any of them, twice a second, for the rest
                    // of the game (#202). It is kept because "the ground
                    // rules threw out forty of ninety" is worth knowing
                    // when that is what happened, and it is now labelled
                    // for what it is.
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
                        else if (!walkable(p) || !siteFree(p))
                        {
                            ++unwalkable;
                        }
                    }
                    LOG_DEBUG << "AI build: expansion found no patch for unit " << ctx.builderId.value << " within " << radius.value
                              << ": " << tally.depositsInRange << " deposits in range, " << tally.notAdmitted << " outside the ground rules, "
                              << tally.noPlacement << " with nowhere to stand, " << tally.heartBlocked << " waiting for something to move off, "
                              << tally.siteRefused << " refused by the site rules"
                              << " (of the cells with nowhere to stand: " << tally.cellsBlockedByBuilding << " already built on, "
                              << tally.cellsUnbuildable << " on ground that will not hold one, " << tally.cellsBlockedByUnit << " under a unit)"
                              << " (ground rules over cells: " << inRange << " in range, " << offSide << " on the enemy's side, " << unknown
                              << " unexplored, " << guarded << " under enemy guns, " << unwalkable << " unreachable or beyond the leash)";
                    sim.eventLog.event(sim.gameTime.value, "build_refusal")
                        .set("player", aiOwner.value)
                        .set("unit", ctx.builderId.value)
                        .set("subject", next)
                        .set("deposits_in_range", tally.depositsInRange)
                        .set("not_admitted", tally.notAdmitted)
                        .set("refused_off_side", refusedOffSide)
                        .set("refused_unknown", refusedUnknown)
                        .set("refused_unwalkable", refusedUnwalkable)
                        .set("no_placement", tally.noPlacement)
                        .set("cells_off_map", tally.cellsOffMap)
                        .set("cells_unbuildable", tally.cellsUnbuildable)
                        .set("cells_blocked_by_building", tally.cellsBlockedByBuilding)
                        .set("cells_blocked_by_unit", tally.cellsBlockedByUnit)
                        .set("cells_no_metal_under", tally.cellsNoMetalUnder)
                        .set("submerged_patches", submergedMetalPatches)
                        .set("heart_blocked", tally.heartBlocked)
                        .set("site_refused", tally.siteRefused)
                        .set("patch_cells_in_range", inRange)
                        .set("off_side", offSide)
                        .set("unknown", unknown)
                        .set("guarded", guarded)
                        .set("unwalkable", unwalkable)
                        .set("why", "no_patch")
                        .detail("expansion found no metal patch");
                }
            }

            // The nearer patch, wet or dry. The dry extractor is listed
            // first and the planner takes the first entry with a site, so
            // a commander standing in the shallows beside a submerged
            // patch walked past it -- and past the next -- to a dry one
            // inland; watched in a replay. When the builder has both
            // buttons and the wet patch is closer, this entry stands
            // aside and the submerged extractor's own, next on the list,
            // takes it.
            if (result.site && next == ctx.sideUnits.metalExtractor && !ctx.sideUnits.underwaterMetalExtractor.empty() && submergedMetalPatches > 0
                && bb.buildTree.canBuild(ctx.builder.unitType, ctx.sideUnits.underwaterMetalExtractor))
            {
                auto wetAcceptable = [&](const SimVector& p) {
                    if (ctx.builderDef.commander && ctx.builderAtBase && bb.baseAnchor->distanceSquared(p) > leashSquared)
                    {
                        return false;
                    }
                    return !underGuns(p);
                };
                auto wet = chooseMexSite(sim, ctx.sideUnits.underwaterMetalExtractor, ctx.builder.position, profile.nearMexSearchRadius, rng, siteFree, wetAcceptable);
                if (wet && ctx.builder.position.distanceSquared(*wet) < ctx.builder.position.distanceSquared(*result.site))
                {
                    result.site.reset();
                }
            }

            // No free patch for a moho: replace an extractor instead. One
            // at a time, never while another moho is still a frame, and
            // only with enough of its price in hand that the moho follows
            // the reclaim at once -- the patch earns nothing in between.
            // Not with the lights out either: a moho draws far more
            // energy than the extractor it replaces.
            bool isMoho = !ctx.sideUnits.mohoExtractor.empty() && next == ctx.sideUnits.mohoExtractor;
            if (!result.site && isMoho && ctx.builderAtBase && profile.extractorUpgrades && !extractorUpgrade && !bb.energyStalled
                && bb.currentMetal.value >= nextDef.buildCostMetal.value * profile.extractorUpgradeMinMetalFraction)
            {
                bool mohoUnderWay = false;
                for (const auto& [unitId, unit] : sim.units)
                {
                    if (unit.owner == aiOwner && unit.isAlive() && unit.unitType == next && unit.isBeingBuilt(nextDef))
                    {
                        mohoUnderWay = true;
                        break;
                    }
                }

                // The nearest standing extractor the builder can walk to,
                // with room round it: a moho is 5x5 where the extractor
                // is 3x3, and what the larger footprint would hit cannot
                // be tested while the smaller one is still in the way.
                std::optional<UnitId> chosen;
                SimScalar chosenDistance = 0_ss;
                if (!mohoUnderWay)
                {
                    for (const auto& [unitId, unit] : sim.units)
                    {
                        if (unit.owner != aiOwner || !unit.isAlive() || unit.unitType != ctx.sideUnits.metalExtractor)
                        {
                            continue;
                        }
                        auto extractorDefIt = sim.unitDefinitions.find(unit.unitType);
                        if (extractorDefIt == sim.unitDefinitions.end() || unit.isBeingBuilt(extractorDefIt->second))
                        {
                            continue;
                        }
                        if (underGuns(unit.position) || (ctx.siteReachable && !ctx.siteReachable(unit.position)))
                        {
                            continue;
                        }
                        bool crowded = false;
                        for (const auto& [otherId, other] : sim.units)
                        {
                            if (otherId == unitId || !other.isAlive())
                            {
                                continue;
                            }
                            auto otherDefIt = sim.unitDefinitions.find(other.unitType);
                            if (otherDefIt != sim.unitDefinitions.end() && !otherDefIt->second.isMobile
                                && other.position.distanceSquared(unit.position) < 72_ss * 72_ss)
                            {
                                crowded = true;
                                break;
                            }
                        }
                        if (crowded)
                        {
                            continue;
                        }
                        auto distance = ctx.builder.position.distanceSquared(unit.position);
                        if (!chosen || distance < chosenDistance)
                        {
                            chosen = unitId;
                            chosenDistance = distance;
                        }
                    }
                }
                if (chosen)
                {
                    const auto& old = sim.getUnitState(*chosen);
                    extractorUpgrade = ExtractorUpgrade{ctx.builderId, *chosen, old.position, bb.now};
                    bb.ownReclaimTarget = *chosen;
                    LOG_INFO << "AI build: unit " << ctx.builderId.value << " reclaims its extractor at " << static_cast<int>(old.position.x.value) << ","
                             << static_cast<int>(old.position.z.value) << " to put a " << next << " there; " << bb.currentMetal.value << " metal in hand";
                    sim.eventLog.event(sim.gameTime.value, "build_order")
                        .set("player", aiOwner.value)
                        .set("unit", ctx.builderId.value)
                        .set("subject", next)
                        .set("x", static_cast<double>(old.position.x.value))
                        .set("z", static_cast<double>(old.position.z.value))
                        .set("cost", bb.currentMetal.value)
                        .set("why", "moho_extractor_upgrade")
                        .detail("reclaims its extractor to put a moho there");
                    savingFor.clear();
                    outCommands.emplace_back(PlayerUnitCommand(ctx.builderId, PlayerUnitCommand::IssueOrder(ReclaimOrder(*chosen), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                    result.stop = true;
                    return result;
                }
            }
        }
        else if (ctx.rebuild && next == ctx.rebuild->unitType)
        {
            result.site = ctx.rebuild->site;
            result.isRebuild = true;
            result.clearFirst = ctx.rebuild->wreck;
            LOG_INFO << "AI build: unit " << ctx.builderId.value << " puts back the " << next << " lost at "
                     << static_cast<int>(result.site->x.value) << "," << static_cast<int>(result.site->z.value)
                     << (result.clearFirst ? ", clearing its wreck first" : "");
            sim.eventLog.event(sim.gameTime.value, "build_order")
                .set("player", aiOwner.value)
                .set("unit", ctx.builderId.value)
                .set("subject", next)
                .set("x", static_cast<double>(result.site->x.value))
                .set("z", static_cast<double>(result.site->z.value))
                .set("clears_wreck", result.clearFirst.has_value())
                .set("why", "rebuild_defence")
                .detail("puts back a defence that was lost");
        }
        else if (ctx.fortify && next == ctx.fortify->unitType)
        {
            result.site = ctx.fortify->site;
            result.isFortification = true;
            LOG_INFO << "AI build: unit " << ctx.builderId.value << " fortifies tower " << ctx.fortify->tower.value << " with " << next << " at "
                     << static_cast<int>(result.site->x.value) << "," << static_cast<int>(result.site->z.value);
            sim.eventLog.event(sim.gameTime.value, "build_order")
                .set("player", aiOwner.value)
                .set("unit", ctx.builderId.value)
                .set("subject", next)
                .set("target_id", ctx.fortify->tower.value)
                .set("x", static_cast<double>(result.site->x.value))
                .set("z", static_cast<double>(result.site->z.value))
                .set("why", "fortify")
                .detail("fortifies a tower");
        }
        else if (next == ctx.sideUnits.lightLaserTower && ctx.outpost && (ctx.outpost->raided || countOf(bb.ownedTotalCounts, next) >= profile.targetDefenceCount))
        {
            // The outpost tower, at the cluster it is to cover. The
            // base's own towers come first unless the cluster has just
            // been raided; the count includes the outpost towers, so a
            // base rule that is still short of its target is the
            // tie-break in the base's favour.
            result.site = chooseBuildSite(sim, profile, bb, next, ctx.outpost->anchor, rng, ctx.siteReachable);
            result.isOutpostTower = true;
            if (result.site)
            {
                LOG_INFO << "AI build: unit " << ctx.builderId.value << " defends " << ctx.outpost->extractors << " extractor(s) at "
                         << static_cast<int>(ctx.outpost->anchor.x.value) << "," << static_cast<int>(ctx.outpost->anchor.z.value)
                         << (ctx.outpost->raided ? " after a raid" : "");
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", ctx.builderId.value)
                    .set("subject", next)
                    .set("x", static_cast<double>(ctx.outpost->anchor.x.value))
                    .set("z", static_cast<double>(ctx.outpost->anchor.z.value))
                    .set("extractors", ctx.outpost->extractors)
                    .set("raided", ctx.outpost->raided)
                    .set("why", "outpost_defence")
                    .detail("sites an outpost tower over uncovered extractors");
            }
        }
        else if (profile.spreadDefences && ctx.builderAtBase && (next == ctx.sideUnits.lightLaserTower || next == ctx.sideUnits.antiAirTower))
        {
            result.site = chooseDefenceSite(sim, aiOwner, profile, bb, reachability, next, rng);
        }
        else if (profile.spreadDefences && ctx.builderAtBase && next == ctx.sideUnits.radar)
        {
            result.site = chooseRadarSite(sim, profile, bb, reachability, next, rng);
        }
        else if (next == ctx.sideUnits.lightLaserTower && bb.enemyBasePosition)
        {
            // Defences go on the side of the base that faces the enemy.
            auto towards = (*bb.enemyBasePosition - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
            auto towerAnchor = *bb.baseAnchor + (towards * profile.defenceDistanceFromBase);
            result.site = chooseBuildSite(sim, profile, bb, next, towerAnchor, rng, ctx.siteReachable);
        }
        else if (!ctx.sideUnits.geothermal.empty() && next == ctx.sideUnits.geothermal)
        {
            // On a vent, and only there: the nearest one to the builder
            // that it can walk to, has looked at, and can still be built
            // on -- which is what says whether somebody has taken it.
            auto mc = sim.getAdHocMovementClass(nextDef.movementCollisionInfo);
            std::optional<SimScalar> nearest;
            for (const auto& vent : geothermalVents)
            {
                if (siteFailedLately(sim, vent) || (ctx.siteReachable && !ctx.siteReachable(vent)))
                {
                    continue;
                }
                if (!profile.cheatModeOmniscient && !sim.isExploredBy(aiOwner, vent))
                {
                    continue;
                }
                auto rect = sim.computeFootprintRegion(vent, nextDef.movementCollisionInfo);
                if (rect.x < 0 || rect.y < 0 || !footprintInsideVisibleMap(sim.terrain, rect)
                    || !sim.canBeBuiltAt(mc, nextDef.yardMap, nextDef.yardMapContainsGeo, static_cast<unsigned int>(rect.x), static_cast<unsigned int>(rect.y)))
                {
                    continue;
                }
                auto distance = ctx.builder.position.distanceSquared(vent);
                if (!nearest || distance < *nearest)
                {
                    nearest = distance;
                    result.site = vent;
                }
            }
        }
        else if (!ctx.sideUnits.torpedoLauncher.empty() && next == ctx.sideUnits.torpedoLauncher && !bb.factories.empty())
        {
            // Beside the yard, on the side the enemy comes from. It was
            // laid out round the builder like a tidal generator, and the
            // builder is the commander, on the beach: so the one defence
            // bought against a hull shelling the shipyard stood somewhere
            // along the shore, out of reach of both. A launcher reaches
            // about 500; two hundred off the yard covers the yard and the
            // water its hulls are launched into.
            std::optional<SimVector> yard;
            for (auto factoryId : bb.factories)
            {
                auto factoryRef = sim.tryGetUnitState(factoryId);
                if (factoryRef && (factoryRef->get().unitType == ctx.sideUnits.shipyard
                        || (!ctx.sideUnits.advancedShipyard.empty() && factoryRef->get().unitType == ctx.sideUnits.advancedShipyard)))
                {
                    // The yard with the fewest launchers near it, so a
                    // second launcher covers a second yard.
                    if (!yard)
                    {
                        yard = factoryRef->get().position;
                    }
                    bool covered = false;
                    for (const auto& [_, unit] : sim.units)
                    {
                        if (unit.owner == aiOwner && unit.isAlive() && unit.unitType == next
                            && unit.position.distanceSquared(factoryRef->get().position) < 400_ss * 400_ss)
                        {
                            covered = true;
                            break;
                        }
                    }
                    if (!covered)
                    {
                        yard = factoryRef->get().position;
                        break;
                    }
                }
            }
            if (yard)
            {
                std::optional<SimVector> threat;
                auto nearestThreat = 0_ss;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (!enemy.isArmed || enemy.isAir || enemy.isBuilding)
                    {
                        continue;
                    }
                    auto d = yard->distanceSquared(enemy.lastKnownPosition);
                    if (!threat || d < nearestThreat)
                    {
                        nearestThreat = d;
                        threat = enemy.lastKnownPosition;
                    }
                }
                if (!threat)
                {
                    threat = bb.enemyBasePosition;
                }
                auto towards = threat ? (*threat - *yard).normalizedOr(SimVector(1_ss, 0_ss, 0_ss))
                                      : (*yard - *bb.baseAnchor).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));
                result.site = chooseBuildSite(sim, profile, bb, next, *yard + (towards * 200_ss), rng);
            }
            if (!result.site)
            {
                result.site = chooseBuildSite(sim, profile, bb, next, ctx.anchor, rng);
            }
        }
        else if (ctx.isWaterStructure(next))
        {
            // Deliberately without siteReachable -- see where it is
            // defined. These stand in the water, so every site they have
            // is ground the builder cannot walk to, and the gate that
            // stops the commander crossing to another island would
            // otherwise refuse the lot of them.
            result.site = chooseBuildSite(sim, profile, bb, next, ctx.anchor, rng);
        }
        else if ((!ctx.sideUnits.shipyard.empty() && next == ctx.sideUnits.shipyard)
            || (!ctx.sideUnits.advancedShipyard.empty() && next == ctx.sideUnits.advancedShipyard)
            || (!ctx.sideUnits.seaplanePlatform.empty() && next == ctx.sideUnits.seaplanePlatform))
        {
            // Not a ring search: an 8x8 footprint needing
            // MinWaterDepth=30 would refuse every candidate a walk out
            // from the (dry) anchor ever offered. See chooseShipyardSite.
            result.site = chooseShipyardSite(sim, profile, bb, next, rng);
        }
        else if (profile.energyInRows && ctx.builderAtBase && !ctx.sideUnits.solar.empty() && next == ctx.sideUnits.solar)
        {
            result.site = chooseEnergyRowSite(sim, aiOwner, profile, bb, next, ctx.anchor, ctx.builder.position, rng, ctx.siteReachable);
        }
        else
        {
            result.site = chooseBuildSite(sim, profile, bb, next, ctx.anchor, rng, ctx.siteReachable);
        }
        return result;
    }

    /**
     * Whether this builder was sent to work the battlefield reclaim field,
     * in which case it has its job and the planner is done with it this
     * tick. The rules, and what happens without them, are below.
     */
    bool BuildManager::tryBattlefieldReclaim(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        AiBlackboard& bb,
        UnitId builderId,
        const UnitDefinition& builderDef,
        bool builderAtBase,
        std::vector<PlayerCommand>& outCommands)
    {
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

        // Who, if anyone, is already working the field.
        //
        // "Carrying a reclaim order" is not the test and used to be. The
        // ordinary harvest at the bottom of this pass issues reclaim orders
        // too -- rocks and wrecks within 1200 of the anchor, which is what
        // a commander spends the opening on -- so any builder clearing a
        // rock at home answered to this, and two things followed from it:
        // nobody was ever dispatched to the battlefield while it did, and
        // the recall below pulled that builder to the anchor once a pass,
        // taking it off the rock it was already standing on.
        //
        // Out on the field means out past the base's own reach. Nothing
        // else issues a reclaim that far out, so this names the builder the
        // rule below sent and no other.
        const auto baseReclaimReachSquared = SimScalar(1200.0f * 1200.0f);
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
            auto reclaim = std::get_if<ReclaimOrder>(&other.orders.front());
            if (reclaim == nullptr)
            {
                continue;
            }
            std::optional<SimVector> spoilAt;
            if (auto featureTarget = std::get_if<FeatureId>(&reclaim->target))
            {
                auto featureRef = sim.tryGetFeature(*featureTarget);
                if (featureRef)
                {
                    spoilAt = featureRef->get().position;
                }
            }
            else if (auto unitTarget = std::get_if<UnitId>(&reclaim->target))
            {
                auto targetRef = sim.tryGetUnitState(*unitTarget);
                if (targetRef)
                {
                    spoilAt = targetRef->get().position;
                }
            }
            if (!spoilAt || !bb.baseAnchor || bb.baseAnchor->distanceSquared(*spoilAt) <= baseReclaimReachSquared)
            {
                continue;
            }
            fieldPatroller = UnitId(otherId);
            break;
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
                    if (siteUnderEnemyGuns(sim, profile, bb, feature.position))
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
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", fieldPatroller->value)
                    .set("why", "battlefield_recall")
                    .detail("worker comes off the battlefield");
                outCommands.emplace_back(PlayerUnitCommand(*fieldPatroller, PlayerUnitCommand::IssueOrder(MoveOrder(*bb.baseAnchor), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
            }
        }
        else if (!fieldPatroller && fieldWorthStarting && bb.baseAnchor && builderAtBase && !builderDef.commander && profile.battlefieldReclaimEscortCount > 0)
        {
            // A queue of reclaim orders, one per wreck, and deliberately not a
            // patrol.
            //
            // The patrol was tried first, on the reasoning that a builder on
            // patrol reclaims what it passes and that this is the original's
            // only automatic reclaim. Both halves of that are true and the
            // conclusion still did not hold, because of the gate at the top of
            // findFeatureToAutoReclaim: the scan does not run at all unless one
            // of the player's two stores is under a fifth of its capacity
            // (0x405B18-0x405B54). That is faithful and stays -- but it is an
            // economy heuristic, and a builder sent to take down a wall is not
            // there for the metal. With healthy stores the patrol walked back
            // and forth over the wreckage and reclaimed none of it: measured
            // across one game, successive dispatches found 7, then 17, then
            // 43, then 95 reclaimable wrecks inside the same radius. The field
            // grew the whole time.
            //
            // A ReclaimOrder names a feature and is obeyed whatever the stores
            // say, which is what a player does when the wall is the problem
            // rather than the economy. Naming several at once answers the
            // objection that cost the patrol its place -- that one order clears
            // one corpse and the planner only comes back once a pass. The queue
            // also ends by itself, which the patrol never did: when the last
            // one is done the builder is idle and back in the pool, so the
            // recall above is a safety net rather than the only way out.
            std::vector<std::pair<SimScalar, FeatureId>> spoil;
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
                // Not under a gun. The queue outlives the wave that
                // justified it -- six orders, worked one after another --
                // so a wreck beside an armed enemy is a builder walking at
                // that enemy some seconds after the escort has moved on.
                // Reported from a replay: a construction unit finished its
                // job and "walked right towards an enemy army and got
                // destroyed".
                if (siteUnderEnemyGuns(sim, profile, bb, feature.position))
                {
                    continue;
                }
                auto d = waveCentre->distanceSquared(feature.position);
                if (d < radiusSquared)
                {
                    spoil.emplace_back(d, featureId);
                }
            }
            // Nearest the wave first, and ties by feature id, so every peer
            // builds the same queue.
            std::sort(spoil.begin(), spoil.end(), [](const auto& a, const auto& b) {
                return std::tie(a.first, a.second) < std::tie(b.first, b.second);
            });

            auto taken = std::min(spoil.size(), static_cast<std::size_t>(profile.battlefieldReclaimBatch));
            if (taken > 0)
            {
                LOG_INFO << "AI build: unit " << builderId.value << " works the battlefield at "
                         << static_cast<int>(waveCentre->x.value) << "," << static_cast<int>(waveCentre->z.value)
                         << " (" << taken << " of " << wreckCount << " reclaimable within " << profile.battlefieldReclaimRadius.value << ")";
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("x", static_cast<double>(waveCentre->x.value))
                    .set("z", static_cast<double>(waveCentre->z.value))
                    .set("taken", taken)
                    .set("wrecks", wreckCount)
                    .set("radius", profile.battlefieldReclaimRadius.value)
                    .set("why", "battlefield_reclaim")
                    .detail("builder sent to work the battlefield");
                savingFor.clear();
                for (std::size_t i = 0; i < taken; ++i)
                {
                    auto kind = i == 0 ? PlayerUnitCommand::IssueOrder::IssueKind::Immediate : PlayerUnitCommand::IssueOrder::IssueKind::Queued;
                    outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(ReclaimOrder(spoil[i].second), kind)));
                }
        return true;
            }
        }

        return false;
    }

    void BuildManager::updateAirWorthIt(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        // Are aircraft worth spending a tier on? Asked every pass, because
        // the economy streams and the answer at minute three is not the
        // answer at minute twenty.
        //
        // Three ways it comes out yes, and none of them is a list of unit
        // types. The map keeps the ground arm from reaching everything --
        // the same test buildPriorities calls airMatters, read off the map
        // the way a player reads it off the preview. They are flying, so we
        // need something that can answer them. Or they have put up enough
        // standing guns that walking at them has stopped working: a wall of
        // towers is exactly what a ground army cannot cross and an aircraft
        // does not have to, which is the argument S:16.3 made for the bomber
        // and the one the gunship inherits.
        auto airMatters = (bb.mapIntel.valid && bb.mapIntel.character != MapCharacter::Land) || bb.hasUnreachableGround;

        int enemyStaticDefences = 0;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (enemy.isBuilding && enemy.isArmed && !enemy.isAir)
            {
                ++enemyStaticDefences;
            }
        }

        auto was = bb.airWorthIt;
        bb.airWorthIt = airMatters || bb.enemyAirThreat
            || (profile.airWorthItEnemyDefences > 0 && enemyStaticDefences >= profile.airWorthItEnemyDefences);
        if (bb.airWorthIt != was)
        {
            LOG_INFO << "AI build: the air tier is " << (bb.airWorthIt ? "worth having" : "no longer worth having")
                     << " (map " << (airMatters ? "wants it" : "does not") << ", their aircraft " << (bb.enemyAirThreat ? "seen" : "not seen")
                     << ", " << enemyStaticDefences << " standing guns of theirs known)";
            sim.eventLog.event(sim.gameTime.value, "build_tier_assessment")
                .set("player", aiOwner.value)
                .set("on", bb.airWorthIt)
                .set("air_matters", airMatters)
                .set("enemy_air_threat", bb.enemyAirThreat)
                .set("enemy_guns", enemyStaticDefences)
                .set("why", bb.airWorthIt ? "air_tier_worth" : "air_tier_not_worth")
                .detail(bb.airWorthIt ? "the air tier is worth having" : "the air tier is no longer worth having");
        }
    }

    void BuildManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        AiBlackboard& bb,
        const ReachabilityMap& reachability,
        std::minstd_rand& rng,
        std::vector<PlayerCommand>& outCommands)
    {
        updateAirWorthIt(sim, aiOwner, profile, bb);
        keepBuildersOutOfFights(sim, aiOwner, profile, bb, outCommands);

        ++ticksSinceLastPlanning;
        // How long the economy has been stuck, counted once a planning pass
        // because that is the rate the answer is used at. Both halves have
        // to hold at once, and either one failing puts it back to nothing:
        // what wants measuring is an unbroken run and not an average.
        if (ticksSinceLastPlanning >= profile.buildPlannerTickInterval)
        {
            auto metalShortNow = bb.metalStalled
                || (bb.metalStorage.value > 0.0f && bb.currentMetal.value < bb.metalStorage.value * 0.1f);
            auto energyRichNow = bb.energyStorage.value > 0.0f
                && bb.currentEnergy.value >= bb.energyStorage.value * 0.8f
                && bb.energyIncome.value > bb.energyDemand.value;
            bb.starvedRichPasses = (metalShortNow && energyRichNow) ? bb.starvedRichPasses + 1 : 0;
        }
        if (ticksSinceLastPlanning < profile.buildPlannerTickInterval)
        {
            return;
        }
        ticksSinceLastPlanning = 0;

        if (!bb.baseAnchor || !bb.sideUnitsResolved)
        {
            return;
        }
        // Once a game, and cheap after that: buildPriorities reads
        // submergedMetalPatches off this object to decide whether an
        // underwater extractor has anywhere to stand, and it has no sim of
        // its own to work that out with.
        indexMetalPatches(sim);
        indexGeothermalVents(sim);
        const auto& sideUnits = bb.sideUnits;

        // Whether the income has outrun what there is to spend it with
        // (spendSurplusOnCapacity). Demand is what the running jobs draw, so
        // a base whose builders and factories are all busy reads near its
        // income and nothing is added.
        if (incomeOutrunsSpending(profile, bb))
        {
            capacityShortTicks += profile.buildPlannerTickInterval;
        }
        else
        {
            capacityShortTicks = 0;
        }
        spendingCapacityShort = capacityShortTicks >= static_cast<unsigned int>(std::max(0, profile.capacitySurplusSeconds)) * SimTicksPerSecond;

        planFactories(sim, aiOwner, profile, bb, outCommands);

        // Every pass, whether or not a builder is idle: the defences are
        // watched for where attacks come from, and a damaged commander takes
        // a construction unit off whatever it is doing.
        watchDefences(sim, profile, bb);
        recordLostDefences(sim, profile, bb);
        sendRepairersToCommander(sim, aiOwner, profile, bb, outCommands);

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

        // A commander being shot at is ArmyManager's to move, and is not
        // handed a solar collector to go and build while it runs.
        if (builderDef.commander && bb.commanderFleeing)
        {
            return;
        }
        // Nor a builder that has just backed off from a fight: a job now is
        // as likely as not the one it walked away from.
        if (auto sheltered = builderShelteredUntil.find(builderId.value); sheltered != builderShelteredUntil.end())
        {
            if (bb.now.value < sheltered->second.value)
            {
                return;
            }
            builderShelteredUntil.erase(sheltered);
        }

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
                    sim.eventLog.event(sim.gameTime.value, "build_refusal")
                        .set("player", aiOwner.value)
                        .set("unit", builderId.value)
                        .set("subject", order.unitType)
                        .set("x", static_cast<double>(order.site.x.value))
                        .set("z", static_cast<double>(order.site.z.value))
                        .set("memory_seconds", profile.failedSiteMemorySeconds)
                        .set("why", "order_dropped")
                        .detail("the build order was dropped and the site is left alone");
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

        // Which labelling answers for THIS builder. The ground layer is
        // flooded for the side's constructor, and a commander is not one:
        // ARMCOM's TANKDS2 wades to water depth 100 and climbs slope 32
        // where ARMCK's TANKSH2 stops at 12 and 15. Asking the constructor's
        // labelling about a commander calls ground unreachable that the
        // commander walks across -- and then, because the commander is
        // standing somewhere its own base supposedly cannot reach, flips
        // builderAtBase false and runs it as a stranded outpost builder,
        // which wants extractors and solars and no factory at all. It is the
        // same correction the air constructor already gets further down.
        //
        // Falls back to the ground layer whenever the commander layer was
        // never built -- which is what commanderUsesOwnReachability=false
        // leaves behind, so the knob needs no second branch here.
        auto builderReachable = [&](const SimVector& p) {
            return builderDef.commander && reachability.isCommanderValid()
                ? reachability.isCommanderReachable(sim, p)
                : reachability.isReachable(sim, p);
        };

        // A builder that cannot walk home is running an outpost: it builds
        // around itself. One that flies is always at home, because the ground
        // it happens to be over decides nothing about where it can go next.
        bool builderAtBase = builderDef.canFly || !bb.groundReachabilityValid || builderReachable(builder.position);
        auto anchor = builderAtBase ? *bb.baseAnchor : builder.position;

        // Ground the builder can actually get to, and the one test that
        // stops the commander crossing to another island to put up a single
        // solar collector. Every site chooser but this one already had it:
        // the extractor search below, chooseDefenceSite and chooseRadarSite
        // all refuse ground the builder cannot walk to, while the ordinary
        // ring walk that sites solars, labs, the air plant and the vehicle
        // plant asked nothing at all, and cheerfully returned a spot across
        // water. Observed in play, and the walk there is most of a minute of
        // the opening spent moving rather than building.
        //
        // Compared against builderAtBase rather than simply required to be
        // true, which is what makes the same predicate right for a stranded
        // outpost builder: that one wants the island it is standing on, and
        // "reachable from base" is false for every site it should take.
        //
        // A builder that flies is exempt for the reason the extractor search
        // gives at length: holding the one builder that can cross water to
        // the ground everything else can already walk to is exactly
        // backwards. The shipyard is unaffected -- it has its own chooser,
        // since an 8x8 hull needing MinWaterDepth=30 is never on ground
        // anybody walks to.
        std::function<bool(const SimVector&)> siteReachable;
        if (bb.groundReachabilityValid && !builderDef.canFly)
        {
            siteReachable = [&](const SimVector& p) { return builderReachable(p) == builderAtBase; };
        }

        // The structures that stand IN the water rather than beside it. They
        // are exempt from siteReachable for the obvious reason: a builder on
        // the shore cannot walk to any of their sites, so the test would
        // refuse every one. Nothing is lost by skipping it, because
        // canBeBuiltAt already enforces each one's own MinWaterDepth through
        // isWaterDepthWithinBounds -- the ring walk can only ever land them
        // on water that suits them.
        std::function<bool(const std::string&)> isWaterStructure = [&](const std::string& t) {
            return !t.empty()
                && ((!sideUnits.tidalGenerator.empty() && t == sideUnits.tidalGenerator)
                    || (!sideUnits.sonar.empty() && t == sideUnits.sonar)
                    || (!sideUnits.torpedoLauncher.empty() && t == sideUnits.torpedoLauncher)
                    || (!sideUnits.floatingMetalMaker.empty() && t == sideUnits.floatingMetalMaker)
                    || (!sideUnits.underwaterMetalStorage.empty() && t == sideUnits.underwaterMetalStorage)
                    || (!sideUnits.underwaterEnergyStorage.empty() && t == sideUnits.underwaterEnergyStorage)
                    || (!sideUnits.underwaterFusion.empty() && t == sideUnits.underwaterFusion));
            // The underwater extractor is NOT here. It stands in the water
            // like the rest, but it also has to stand on a metal patch, so it
            // goes through chooseMexSite with the moho extractor rather than
            // through the ring walk -- see the branch that dispatches it.
        };

        // Has the enemy actually put a hull in the water? The test is the one
        // ScoutManager already uses to tell a boat from a walker -- a movement
        // class with a minimum water depth can only float -- and buildings are
        // excluded, because a torpedo launcher of theirs is a thing in the
        // water but is not a ship.
        //
        // Worked out here rather than kept on the blackboard because this
        // scope has both sim and bb, while buildPriorities has no sim to look
        // a unit definition up with. It joins builderAtBase and outpost as
        // one more thing computed for it rather than by it.
        auto enemyNavalSeen = false;
        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            if (enemy.isAir || enemy.isBuilding)
            {
                continue;
            }
            auto enemyDefIt = sim.unitDefinitions.find(enemy.unitType);
            if (enemyDefIt == sim.unitDefinitions.end())
            {
                continue;
            }
            if (sim.getAdHocMovementClass(enemyDefIt->second.movementCollisionInfo).minWaterDepth > 0)
            {
                enemyNavalSeen = true;
                break;
            }
        }

        // Extractors and makers are exempt from the affordability test
        // below: they are what makes the next thing affordable, and a
        // stalled extractor still finishes, just later. A solar collector
        // is exempt only while energy is actually wanted; otherwise it is
        // 165 metal like anything else, and the AI's energy is nearly always
        // in surplus.
        auto energyWanted = bb.energyStalled || (bb.energyStorage.value > 0.0f && bb.currentEnergy.value < bb.energyStorage.value * 0.5f);
        auto isEconomy = [&](const std::string& t) {
            return t == sideUnits.metalExtractor || t == sideUnits.metalMaker
                || (energyWanted && (t == sideUnits.solar || t == sideUnits.tidalGenerator));
        };

        // Mending before building: a damaged defence first, then a damaged
        // factory, nearest first within its kind. Repair costs energy and
        // time but no metal, so a tower mended between attacks is a tower
        // not bought again. Only a builder whose FBI lets it repair is
        // asked; every shipped one does.
        if (builderDef.canReclamate)
        {
            if (auto target = chooseRepairTarget(sim, aiOwner, profile, bb, builder.position, siteReachable))
            {
                const auto& damaged = sim.getUnitState(*target);
                LOG_INFO << "AI build: unit " << builderId.value << " repairs " << damaged.unitType << " " << target->value << " ("
                         << damaged.hitPoints << " of " << sim.unitDefinitions.at(damaged.unitType).maxHitPoints << " hit points)";
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("target", damaged.unitType)
                    .set("target_id", target->value)
                    .set("hp", damaged.hitPoints)
                    .set("max_hp", sim.unitDefinitions.at(damaged.unitType).maxHitPoints)
                    .set("why", "repair")
                    .detail("builder sent to repair a damaged structure");
                savingFor.clear();
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(*target), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // A frame left standing comes before anything new. Its metal is
        // already half paid, the plan wanted it, and it is rotting.
        for (auto frameId : bb.orphanedFrames)
        {
            const auto& frame = sim.getUnitState(frameId);
            if (bb.groundReachabilityValid && builderReachable(frame.position) != builderAtBase)
            {
                continue;
            }
            // Not where the builder would stand in a fight it is not covered
            // in; the frame was very likely left because of that fight.
            if (profile.builderSafety && assessExposure(sim, aiOwner, bb, builderSafetyParams(profile), frame.position, builderId).exposed)
            {
                continue;
            }
            auto estimate = estimateBuild(sim.unitDefinitions.at(frame.unitType), builderDef, frame.buildTimeCompleted);
            if (!isEconomy(frame.unitType) && !canAfford(bb, estimate))
            {
                continue;
            }
            LOG_INFO << "AI build: unit " << builderId.value << " resumes abandoned " << frame.unitType << " (" << estimate.metal << " metal left)";
            sim.eventLog.event(sim.gameTime.value, "build_order")
                .set("player", aiOwner.value)
                .set("unit", builderId.value)
                .set("subject", frame.unitType)
                .set("target_id", frameId.value)
                .set("cost", estimate.metal)
                .set("why", "resume_frame")
                .detail("builder resumes an abandoned frame");
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
                if (bb.groundReachabilityValid && builderReachable(unit.position) != builderAtBase)
                {
                    continue;
                }
                LOG_INFO << "AI build: unit " << builderId.value << " assists the " << unit.unitType
                         << " frame (" << unit.getBuildPercentLeft(frameDef) << "% left)";
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("subject", unit.unitType)
                    .set("target_id", UnitId(unitId).value)
                    .set("percent_left", unit.getBuildPercentLeft(frameDef))
                    .set("why", "assist_frame")
                    .detail("builder assists a tech frame");
                savingFor.clear();
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(UnitId(unitId)), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // Whatever of ours near the base is most hurt (mendDamagedUnits):
        // nothing in TA mends itself, so a unit that came home hurt stays
        // hurt until a builder is put on it.
        if (profile.mendDamagedUnits && builderAtBase && builderDef.canReclamate
            && (profile.commanderMends || !builderDef.commander))
        {
            // One at a time: the AI issues a repair order on a mobile unit
            // nowhere else, so a builder of ours carrying one is this rule's.
            bool someoneMending = false;
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.owner != aiOwner || !other.isAlive() || other.orders.empty() || UnitId(otherId) == builderId)
                {
                    continue;
                }
                auto repair = std::get_if<RepairOrder>(&other.orders.front());
                if (repair == nullptr)
                {
                    continue;
                }
                auto target = sim.tryGetUnitState(repair->target);
                if (target && sim.unitDefinitions.at(target->get().unitType).isMobile)
                {
                    someoneMending = true;
                    break;
                }
            }
            std::optional<UnitId> worst;
            unsigned int worstShare = static_cast<unsigned int>(std::max(0, profile.mendBelowPercent));
            const auto mendSquared = profile.mendRadius * profile.mendRadius;
            for (const auto& [otherId, other] : sim.units)
            {
                if (other.owner != aiOwner || !other.isAlive() || UnitId(otherId) == builderId)
                {
                    continue;
                }
                auto otherDefIt = sim.unitDefinitions.find(other.unitType);
                // Mobile only: a damaged building is repairStructures' to
                // decide about, and it has its own rule about doing it while
                // the gun that damaged it is still there.
                if (otherDefIt == sim.unitDefinitions.end() || other.isBeingBuilt(otherDefIt->second)
                    || otherDefIt->second.maxHitPoints == 0 || !otherDefIt->second.isMobile || otherDefIt->second.commander)
                {
                    continue;
                }
                if (other.position.distanceSquared(*bb.baseAnchor) > mendSquared)
                {
                    continue;
                }
                auto share = (other.hitPoints * 100) / otherDefIt->second.maxHitPoints;
                if (share < worstShare)
                {
                    worstShare = share;
                    worst = UnitId(otherId);
                }
            }
            if (worst && !someoneMending)
            {
                LOG_INFO << "AI build: unit " << builderId.value << " mends unit " << worst->value << " (" << worstShare << "% of its hit points)";
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("target_id", worst->value)
                    .set("percent", worstShare)
                    .set("why", "mend")
                    .detail("builder mends a damaged mobile unit");
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(*worst), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // The wall of wrecks the last few waves left behind.
        //
        if (tryBattlefieldReclaim(sim, aiOwner, profile, bb, builderId, builderDef, builderAtBase, outCommands))
        {
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
        // Not the commander's walk while a construction unit is alive to
        // make it (outpostTowersLeftToConstructors): the next one planned
        // takes the outpost up instead.
        bool outpostForSomeoneElse = false;
        if (builderDef.commander && profile.outpostTowersLeftToConstructors && !sideUnits.lightLaserTower.empty())
        {
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != aiOwner || !unit.isAlive() || UnitId(unitId) == builderId)
                {
                    continue;
                }
                const auto& def = sim.unitDefinitions.at(unit.unitType);
                if (def.builder && def.isMobile && !def.commander && !unit.isBeingBuilt(def) && bb.buildTree.canBuild(unit.unitType, sideUnits.lightLaserTower))
                {
                    outpostForSomeoneElse = true;
                    break;
                }
            }
        }
        if (builderAtBase && !outpostForSomeoneElse)
        {
            outpost = planOutpostDefence(sim, aiOwner, profile, bb);
        }

        // The next piece of a laser tower's fortification, asked only of a
        // builder that has a button for one: the commander has neither the
        // teeth nor the missile tower, and walking the base for it would
        // be wasted.
        std::optional<FortificationPlan> fortify;
        if (builderAtBase
            && (profile.fortifyTowers || (profile.fortifyWhereAttacked && defenceTeethOwed)
                || (profile.rebuildLostDefences && profile.fortifyRebuiltDefences && !lostDefenceSites.empty()))
            && ((!sideUnits.dragonsTeeth.empty() && bb.buildTree.canBuild(builder.unitType, sideUnits.dragonsTeeth))
                || (!sideUnits.antiAirTower.empty() && bb.buildTree.canBuild(builder.unitType, sideUnits.antiAirTower))))
        {
            fortify = planFortification(sim, aiOwner, profile, bb, rng);
        }

        // A defence we lost, to be put back where it stood.
        std::optional<DefenceRebuildPlan> rebuild;
        if (builderAtBase && profile.rebuildLostDefences && !lostDefenceSites.empty())
        {
            rebuild = planDefenceRebuild(sim, aiOwner, profile, bb);
        }

        // An extractor upgrade in hand; see ExtractorUpgrade. Given up if it
        // has run too long or its builder is gone, and otherwise carried on
        // by the builder that began it, ahead of anything the plan might
        // offer: a patch standing empty is income lost every second.
        if (extractorUpgrade)
        {
            const auto timeoutTicks = static_cast<unsigned int>(std::max(0, profile.extractorUpgradeTimeoutSeconds)) * SimTicksPerSecond;
            auto upgrader = sim.tryGetUnitState(extractorUpgrade->builder);
            bool upgraderLives = upgrader && upgrader->get().isAlive() && upgrader->get().owner == aiOwner;
            if (!upgraderLives || bb.now.value - extractorUpgrade->at.value > timeoutTicks)
            {
                LOG_INFO << "AI build: extractor upgrade at " << static_cast<int>(extractorUpgrade->site.x.value) << ","
                         << static_cast<int>(extractorUpgrade->site.z.value) << " given up ("
                         << (upgraderLives ? "took too long" : "its builder is gone") << "); the patch is released";
                sim.eventLog.event(sim.gameTime.value, "build_refusal")
                    .set("player", aiOwner.value)
                    .set("unit", extractorUpgrade->builder.value)
                    .set("subject", sideUnits.mohoExtractor)
                    .set("x", static_cast<double>(extractorUpgrade->site.x.value))
                    .set("z", static_cast<double>(extractorUpgrade->site.z.value))
                    .set("why", upgraderLives ? "timed_out" : "builder_gone")
                    .detail("extractor upgrade given up and the patch released");
                extractorUpgrade.reset();
            }
        }
        if (extractorUpgrade && extractorUpgrade->builder == builderId)
        {
            auto old = sim.tryGetUnitState(extractorUpgrade->oldExtractor);
            // Same id, same type, same place: a unit id freed by a death may
            // be handed to something else entirely.
            bool standing = old && old->get().isAlive() && old->get().owner == aiOwner
                && old->get().unitType == sideUnits.metalExtractor
                && old->get().position.distanceSquared(extractorUpgrade->site) < 1_ss;
            if (standing)
            {
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(ReclaimOrder(extractorUpgrade->oldExtractor), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
            auto site = extractorUpgrade->site;
            extractorUpgrade.reset();
            LOG_INFO << "AI build: unit " << builderId.value << " to build " << sideUnits.mohoExtractor << " at " << site.x.value << "," << site.z.value
                     << " where its extractor stood";
            sim.eventLog.event(sim.gameTime.value, "build_order")
                .set("player", aiOwner.value)
                .set("unit", builderId.value)
                .set("subject", sideUnits.mohoExtractor)
                .set("x", static_cast<double>(site.x.value))
                .set("z", static_cast<double>(site.z.value))
                .set("why", "moho_where_extractor_stood")
                .detail("builder resumes the moho where its extractor stood");
            savingFor.clear();
            issuedOrders[builderId.value] = IssuedOrder{sideUnits.mohoExtractor, site, bb.now};
            outCommands.push_back(buildCommand(builderId, sideUnits.mohoExtractor, site));
            return;
        }

        // Whether a builder afloat plans for the base or for an outpost. The
        // ground labelling calls every ship stranded -- it is standing on
        // water no kbot can walk to -- and a stranded builder is offered an
        // extractor and a solar collector and little else. That looked like
        // a defect and measured as a virtue; see navalBuildersPlanForBase,
        // which is off. Only the PLAN would change: the siting below still
        // runs from where the ship is.
        const auto builderMc = sim.getAdHocMovementClass(builderDef.movementCollisionInfo);
        const bool builderIsShip = builderDef.isMobile && !builderDef.canFly && builderMc.minWaterDepth > 0;
        const bool builderAfloat = profile.navalBuildersPlanForBase && builderIsShip;
        auto priorities = buildPriorities(profile, bb, builderAtBase || builderAfloat, outpost, fortify, rebuild, builder.unitType, enemyNavalSeen);
        // A construction unit kept beyond targetConstructorCount for the
        // metal left lying about expands first (expansionConstructors):
        // the newest ones, in id order, past the first
        // targetConstructorCount.
        if (profile.expansionConstructors > 0 && !builderDef.commander && builder.unitType == sideUnits.constructor && builderAtBase
            && bb.buildTree.canBuild(builder.unitType, sideUnits.metalExtractor))
        {
            int older = 0;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unitId.value < builderId.value && unit.owner == aiOwner && unit.isAlive() && unit.unitType == sideUnits.constructor)
                {
                    ++older;
                }
            }
            if (older >= profile.targetConstructorCount && freeDepositsOnOurSide(sim, profile, bb) > 0)
            {
                priorities.insert(priorities.begin(), sideUnits.metalExtractor);
            }
        }
        const BuilderContext builderContext{
            builder,
            builderId,
            builderDef,
            builderAtBase,
            anchor,
            sideUnits,
            outpost,
            fortify,
            rebuild,
            isWaterStructure,
            siteReachable};
        for (const auto& next : priorities)
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
                    || (!sideUnits.seaplanePlatform.empty() && next == sideUnits.seaplanePlatform)
                    || (!sideUnits.advancedShipyard.empty() && next == sideUnits.advancedShipyard)
                    || (!sideUnits.fusion.empty() && next == sideUnits.fusion)
                    || (!sideUnits.underwaterFusion.empty() && next == sideUnits.underwaterFusion)
                    || (!sideUnits.mohoExtractor.empty() && next == sideUnits.mohoExtractor)
                    || (!sideUnits.heavyPlasmaTower.empty() && next == sideUnits.heavyPlasmaTower)
                    || (!sideUnits.heavyLaserTower.empty() && next == sideUnits.heavyLaserTower)
                    || (!sideUnits.advancedAirPlant.empty() && next == sideUnits.advancedAirPlant)
                    || (!sideUnits.airRepairPad.empty() && next == sideUnits.airRepairPad);
                // And the first air plant, while the air tier is worth
                // having at all.
                //
                // This is the one on the list that is not level two, and it
                // is here because of what it unlocks rather than what it is:
                // it is the only door to the air constructor, and the air
                // constructor is the only unit that can put up the advanced
                // plant. Judged against the ordinary minute it is 850 metal
                // the planner skips past to a solar collector every pass, so
                // on a map where the ground cannot reach everything the AI
                // could never buy the one thing that could.
                //
                // Only the first. Once one stands the capability is bought,
                // and a second is an ordinary factory competing with the
                // rest on ordinary terms.
                auto isFirstAirPlant = bb.airWorthIt && !sideUnits.airPlant.empty() && next == sideUnits.airPlant
                    && countOf(bb.ownedTotalCounts, sideUnits.airPlant) == 0;
                // So is the one answer the AI has to a hull at its shipyard.
                // The torpedo launcher is only wanted once an enemy ship has
                // actually been seen, so by the time it is on this list it is
                // a response rather than a luxury -- and at 804 metal it is as
                // far out of the ordinary minute's reach as an advanced lab.
                // Judged against that minute on Brain Coral it was skipped
                // 2140 times in ten games and never built once, while a single
                // scout ship shot every hull the yard started. The commander
                // cannot answer that ship itself: it walks the seabed there,
                // and nothing but a waterweapon fires from under the sea.
                auto isThreatAnswer = !sideUnits.torpedoLauncher.empty() && next == sideUnits.torpedoLauncher;
                auto window = (isLevelTwo || isThreatAnswer || isFirstAirPlant) ? profile.techSaveUpSeconds : profile.saveUpSeconds;
                if (!canAfford(bb, estimate))
                {
                    // Deliberately NOT gated on whether the map has room for
                    // the thing anywhere. On Brain Coral, with no dry ground,
                    // the commander saves all game for a lab and a solar
                    // collector it can never place, and that looks like a bug.
                    // It was tried as a fix and measured over the same ten
                    // seeds, and it was ruinous: 18.9 units, 14.3 buildings and
                    // 10 metal a second without the gate, against 13, 8.7 and
                    // 7.8 with it, and the torpedo launcher went from built in
                    // four games in ten to none. Saving only ever holds back
                    // what is NOT economy -- extractors and collectors are
                    // exempt, below -- so saving for the impossible makes the
                    // commander a pure economy builder, which on a map like
                    // that is exactly right. Without it the metal went on
                    // sonar and a second shipyard.
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
                            sim.eventLog.event(sim.gameTime.value, "build_saving")
                                .set("player", aiOwner.value)
                                .set("unit", builderId.value)
                                .set("subject", next)
                                .set("cost", estimate.metal)
                                .set("metal", bb.currentMetal.value)
                                .set("income", bb.metalIncome.value - bb.metalCommitted.value)
                                .set("why", "unaffordable_wait")
                                .detail("builder waits for the metal before starting");
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
                    sim.eventLog.event(sim.gameTime.value, "build_unaffordable")
                        .set("player", aiOwner.value)
                        .set("unit", builderId.value)
                        .set("subject", next)
                        .set("cost", estimate.metal)
                        .set("seconds", estimate.seconds)
                        .set("why", "cannot_afford")
                        .detail("cannot afford the build and it is not within reach");
                    continue;
                }
            }

            auto chosen = choosePrioritySite(sim, aiOwner, profile, bb, reachability, rng, next, nextDefIt->second, builderContext, outCommands);
            if (chosen.stop)
            {
                return;
            }
            std::optional<SimVector> site = std::move(chosen.site);
            bool isOutpostTower = chosen.isOutpostTower;
            bool isFortification = chosen.isFortification;
            bool isRebuild = chosen.isRebuild;
            std::optional<FeatureId> clearFirst = chosen.clearFirst;

            if (site)
            {
                // A tower's own worth: is its cost proportionate to what it
                // protects, judged against the base's current income (and,
                // for an outpost, against how much it would cover)? Not
                // asked of a raided outpost or a base with an armed enemy
                // already inside defendRadius -- those bypass the test the
                // same way they already bypass metalShort, because this
                // knob is about declining a speculative tower, not about
                // refusing to rebuild one that was just shot down.
                bool isTowerType = next == sideUnits.lightLaserTower || next == sideUnits.antiAirTower
                    || (!sideUnits.heavyLaserTower.empty() && next == sideUnits.heavyLaserTower)
                    || (!sideUnits.heavyPlasmaTower.empty() && next == sideUnits.heavyPlasmaTower);
                bool urgent = (isOutpostTower && outpost && outpost->raided) || !bb.enemiesNearBase.empty();
                if (isTowerType && !urgent && !isFortification && !isRebuild
                    && !towerCostJustified(profile, bb, nextDefIt->second, (isOutpostTower && outpost) ? outpost->extractors : 0))
                {
                    LOG_DEBUG << "AI build: " << next << " at " << static_cast<int>(site->x.value) << "," << static_cast<int>(site->z.value)
                              << " is not worth its metal against income " << bb.metalIncome.value << "/s; skipping";
                    sim.eventLog.event(sim.gameTime.value, "build_not_worth")
                        .set("player", aiOwner.value)
                        .set("unit", builderId.value)
                        .set("subject", next)
                        .set("x", static_cast<double>(site->x.value))
                        .set("z", static_cast<double>(site->z.value))
                        .set("income", bb.metalIncome.value)
                        .set("why", "tower_not_cost_justified")
                        .detail("the tower is not worth its metal against income");
                    site.reset();
                }
            }

            // Ground the enemy is working, or has just thrown us off. The
            // extractor search has always refused a patch under enemy guns;
            // nothing else did, so a builder would walk a solar collector or
            // a shipyard into the middle of the map and into whatever was
            // standing there. Remembered, so the builder is not handed the
            // same place next pass, and forgotten with the other failed sites.
            // Not for an extractor, which has a rule and a knob of its own
            // for exactly this (mexAvoidsEnemyGunsRadius, in its search).
            bool nextIsExtractor = next == sideUnits.metalExtractor
                || (!sideUnits.underwaterMetalExtractor.empty() && next == sideUnits.underwaterMetalExtractor)
                || (!sideUnits.mohoExtractor.empty() && next == sideUnits.mohoExtractor);
            if (site && !nextIsExtractor && profile.builderAvoidsContestedRadius > 0_ss)
            {
                auto radiusSquared = profile.builderAvoidsContestedRadius * profile.builderAvoidsContestedRadius;
                bool contested = false;
                for (const auto& [_, enemy] : bb.knownEnemies)
                {
                    if (enemy.isArmed && !enemy.isAir && enemy.lastKnownPosition.distanceSquared(*site) <= radiusSquared)
                    {
                        contested = true;
                        break;
                    }
                }
                // The ground we were thrown off is not contested for a
                // rebuild, which is waiting for exactly that to be over
                // and has asked for its armed enemies to be gone.
                if (!contested && !isRebuild && bb.baseAnchor && bb.baseAnchor->distanceSquared(*site) > (profile.defendRadius * profile.defendRadius))
                {
                    for (const auto& loss : bb.recentLosses)
                    {
                        if (loss.position.distanceSquared(*site) <= radiusSquared)
                        {
                            contested = true;
                            break;
                        }
                    }
                }
                if (contested)
                {
                    LOG_DEBUG << "AI build: " << next << " at " << static_cast<int>(site->x.value) << "," << static_cast<int>(site->z.value)
                              << " is contested ground; left alone";
                    sim.eventLog.event(sim.gameTime.value, "build_site_contested")
                        .set("player", aiOwner.value)
                        .set("unit", builderId.value)
                        .set("subject", next)
                        .set("x", static_cast<double>(site->x.value))
                        .set("z", static_cast<double>(site->z.value))
                        .set("why", "contested")
                        .detail("the site is contested ground and left alone");
                    auto cell = sim.terrain.worldToHeightmapCoordinate(*site);
                    failedSites[std::make_pair(cell.x, cell.y)] = bb.now;
                    site.reset();
                }
            }

            if (site && siteFailedLately(sim, *site))
            {
                // The same site the last order was dropped at. The extractor
                // search already skips these; anything else is laid out by
                // ring and would be offered the same place every pass.
                LOG_DEBUG << "AI build: " << next << " would go where an order was just dropped; skipping it this pass";
                sim.eventLog.event(sim.gameTime.value, "build_refusal")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("subject", next)
                    .set("why", "site_recently_failed")
                    .detail("the site is where an order was just dropped");
                site.reset();
            }
            if (site && siteUnderEnemyGuns(sim, profile, bb, *site))
            {
                // Not under a gun. A frame is born with no hit points at
                // all, so anything put down here dies before it is anything
                // -- and the site is still the best one by every test the
                // planner applies, so without this the builder puts the
                // same frame down again for the rest of the game. Measured
                // on Brain Coral before this: 286 and 232 units lost in one
                // game, nearly all of them tidal generators the two
                // commanders kept replacing under an enemy scout ship.
                //
                // This is the extractor search's own rule, which has said
                // exactly this about a metal patch since the Crystal Maze
                // measurement, applied to every site instead of one kind.
                LOG_DEBUG << "AI build: " << next << " would go under an enemy gun at "
                          << static_cast<int>(site->x.value) << "," << static_cast<int>(site->z.value) << "; skipping it this pass";
                sim.eventLog.event(sim.gameTime.value, "build_refusal")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("subject", next)
                    .set("x", static_cast<double>(site->x.value))
                    .set("z", static_cast<double>(site->z.value))
                    .set("why", "under_enemy_guns")
                    .detail("the site is under an enemy gun");
                site.reset();
            }
            if (site)
            {
                LOG_DEBUG << "AI build: unit " << builderId.value << " to build " << next << " at " << site->x.value << "," << site->z.value;
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("subject", next)
                    .set("x", static_cast<double>(site->x.value))
                    .set("z", static_cast<double>(site->z.value))
                    .set("why", "issued")
                    .detail("builder ordered to build at the site");
                savingFor.clear();
                issuedOrders[builderId.value] = IssuedOrder{next, *site, bb.now};
                if (clearFirst)
                {
                    outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(ReclaimOrder(*clearFirst), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                    outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(BuildOrder(next, *site), PlayerUnitCommand::IssueOrder::IssueKind::Queued)));
                }
                else
                {
                    outCommands.push_back(buildCommand(builderId, next, *site));
                }

                // Far enough from the base that the builder placing this is
                // otherwise on its own. ArmyManager reads this and detaches
                // a guard; it also owns clearing it again once the builder
                // is no longer there to protect. Only ever written here, so
                // a builder reassigned to something at home simply stops
                // refreshing it and the existing guard (if any) ages out on
                // its own terms.
                // Distance says the builder is on its own out there. It does
                // NOT say anything is coming for it, and that is the whole
                // fault measured in the guard's own audit: 69 of 81 guards
                // ended with the builder simply finished and only 6 with it
                // lost, so two units were taken off an army averaging 7.4 to
                // escort jobs nothing was threatening, and the side with the
                // guard on came out worse in both slots.
                //
                // So ask the influence map as well: is there enemy anti-ground
                // damage that can actually reach this site? buildSiteGuardThreat
                // is a DPS floor and zero switches the test off entirely,
                // restoring the old distance-only trigger exactly -- the same
                // kill-switch convention as navalFleetSize and earlyShipyard,
                // and what makes "a threat-gated guard" playable against both
                // the old guard and no guard at all.
                auto siteThreat = threatMap.antiGroundInRadius(*site, profile.buildSiteGuardThreatRadius);
                auto threatened = profile.buildSiteGuardThreat <= 0.0f || siteThreat >= profile.buildSiteGuardThreat;
                if (profile.buildSiteGuardSize > 0 && bb.baseAnchor
                    && flatDistance(*site, *bb.baseAnchor) >= profile.buildSiteGuardMinDistance
                    && threatened)
                {
                    // There is one slot, and it used to be taken by whichever
                    // qualifying build order came last. Measured over ten
                    // games at hard difficulty, 346 requests produced 55
                    // guards: most were overwritten before anybody stood
                    // anywhere, and the one that won was chosen by recency,
                    // which is not a priority at all. In the same run the four
                    // builders that actually died were at sites reading
                    // 14988, 13560, 0 and 0 -- so recency was throwing away
                    // exactly the requests worth keeping.
                    //
                    // A standing request is therefore only displaced by a site
                    // at least as threatened as it is. The comparison is >=
                    // rather than > deliberately: with buildSiteGuardThreat
                    // switched off every site reads zero, every request ties,
                    // and recency decides exactly as it did before, so the
                    // kill switch still restores the old behaviour whole. With
                    // > the slot would instead freeze on the first request
                    // until it timed out, which is a change nobody asked for.
                    if (guardRequestDisplaces(bb.buildSiteGuardRequest, siteThreat))
                    {
                        bb.buildSiteGuardRequest = AiBlackboard::BuildSiteGuardRequest{*site, builderId, bb.now, siteThreat};
                        LOG_INFO << "AI build: unit " << builderId.value << " building " << next << " "
                                 << static_cast<int>(flatDistance(*site, *bb.baseAnchor).value) << " from base wants a guard"
                                 << " (threat " << siteThreat << ")";
                        sim.eventLog.event(sim.gameTime.value, "build_guard_request")
                            .set("player", aiOwner.value)
                            .set("unit", builderId.value)
                            .set("subject", next)
                            .set("x", static_cast<double>(site->x.value))
                            .set("z", static_cast<double>(site->z.value))
                            .set("distance", flatDistance(*site, *bb.baseAnchor).value)
                            .set("threat", siteThreat)
                            .set("why", "threatened")
                            .detail("a remote build site wants a guard");
                    }
                }
                return;
            }
            LOG_DEBUG << "AI build: no site found for " << next << " near " << builder.position.x.value << "," << builder.position.z.value;
            sim.eventLog.event(sim.gameTime.value, "build_refusal")
                .set("player", aiOwner.value)
                .set("unit", builderId.value)
                .set("subject", next)
                .set("x", static_cast<double>(builder.position.x.value))
                .set("z", static_cast<double>(builder.position.z.value))
                .set("why", "no_site")
                .detail("no site found for the build");
        }

        // Nothing to build, nowhere to build it, or saving up: harvest the
        // battlefield. Wreck fields are a real economy -- the standing advice
        // is to work them even deep in enemy territory -- and so are the
        // rocks, which is what a player's commander spends the opening on
        // between extractors. Metal first, nearest first; a tree is only
        // worth the walk when energy is actually wanted, which on the maps
        // measured it hardly ever is.
        //
        // A construction ship is never "at base" -- the ground labelling
        // calls it stranded -- so it used to skip this and the factory assist
        // below, and with no patch to take it simply stood there. It works
        // from where it floats, and only what lies in water it can reach.
        // The commander lends its hands before it goes out for rocks. Its
        // nanolathe is the fastest either side has, and a frame it helps
        // with is finished in a third of the time; a rock is a walk. The
        // nearest frame of ours in reach, whoever started it, that is not a
        // unit on a factory's pad -- a factory is helped by guarding it,
        // below.
        if (builderDef.commander && builderAtBase && !saving && profile.commanderAssistRadius > 0_ss)
        {
            const auto assistSquared = profile.commanderAssistRadius * profile.commanderAssistRadius;
            // And within the leash of the base: a frame the commander can
            // reach is not worth crossing the map for (commanderLeashRadius).
            const auto leashSquared = profile.commanderLeashRadius * profile.commanderLeashRadius;
            std::optional<UnitId> nearestFrame;
            SimScalar nearestFrameDistance = 0_ss;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != aiOwner || !unit.isAlive())
                {
                    continue;
                }
                const auto& frameDef = sim.unitDefinitions.at(unit.unitType);
                if (frameDef.isMobile || !unit.isBeingBuilt(frameDef))
                {
                    continue;
                }
                auto distance = builder.position.distanceSquared(unit.position);
                if (distance > assistSquared || (siteReachable && !siteReachable(unit.position))
                    || (profile.commanderLeashRadius > 0_ss && bb.baseAnchor->distanceSquared(unit.position) > leashSquared))
                {
                    continue;
                }
                if (!nearestFrame || distance < nearestFrameDistance)
                {
                    nearestFrame = UnitId(unitId);
                    nearestFrameDistance = distance;
                }
            }
            if (nearestFrame)
            {
                const auto& frame = sim.getUnitState(*nearestFrame);
                LOG_INFO << "AI build: the commander helps with the " << frame.unitType << " frame " << nearestFrame->value
                         << " (" << frame.getBuildPercentLeft(sim.unitDefinitions.at(frame.unitType)) << "% left)";
                sim.eventLog.event(sim.gameTime.value, "build_order")
                    .set("player", aiOwner.value)
                    .set("unit", builderId.value)
                    .set("subject", frame.unitType)
                    .set("target_id", nearestFrame->value)
                    .set("percent_left", frame.getBuildPercentLeft(sim.unitDefinitions.at(frame.unitType)))
                    .set("why", "commander_assist_frame")
                    .detail("the commander helps with a frame");
                outCommands.emplace_back(PlayerUnitCommand(builderId, PlayerUnitCommand::IssueOrder(RepairOrder(*nearestFrame), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        if (builderAtBase || builderIsShip)
        {
            const auto reachSquared = SimScalar(1200.0f * 1200.0f);
            const auto& reclaimCentre = builderIsShip ? builder.position : *bb.baseAnchor;
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
                auto distanceSquared = reclaimCentre.distanceSquared(feature.position);
                if (distanceSquared >= reachSquared)
                {
                    continue;
                }
                if (builderIsShip && sim.terrain.getHeightAt(feature.position.x, feature.position.z) >= sim.terrain.getSeaLevel())
                {
                    continue;
                }
                // The same rule every build site has followed since the
                // Crystal Maze measurement, which reclaim never did: a
                // builder sent to a wreck under an armed enemy is a builder
                // walked into that enemy. Within 1200 of the anchor is
                // still well inside the reach of anything that has come to
                // raid the base.
                if (siteUnderEnemyGuns(sim, profile, bb, feature.position))
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
        //
        // The nearest one that is actually building something, of the kind
        // this builder can get to: a ship helps a yard and a kbot helps a
        // plant on its own ground. It used to be the first factory on the
        // list whatever it was doing, so with the first lab idle every spare
        // builder stood idle beside it while the second one worked alone.
        if (!bb.factories.empty() && (builderAtBase || builderIsShip) && !saving)
        {
            std::optional<UnitId> busiest;
            SimScalar busiestDistance = 0_ss;
            for (auto candidateId : bb.factories)
            {
                auto candidateRef = sim.tryGetUnitState(candidateId);
                if (!candidateRef || candidateRef->get().buildQueue.empty())
                {
                    continue;
                }
                const auto& candidate = candidateRef->get();
                auto candidateDefIt = sim.unitDefinitions.find(candidate.unitType);
                if (candidateDefIt == sim.unitDefinitions.end())
                {
                    continue;
                }
                auto candidateAfloat = sim.getAdHocMovementClass(candidateDefIt->second.movementCollisionInfo).minWaterDepth > 0;
                if (candidateAfloat != builderIsShip && !builderDef.canFly)
                {
                    continue;
                }
                auto distance = builder.position.distanceSquared(candidate.position);
                if (!busiest || distance < busiestDistance)
                {
                    busiest = candidateId;
                    busiestDistance = distance;
                }
            }
            if (!busiest)
            {
                return;
            }
            auto factoryId = *busiest;
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
