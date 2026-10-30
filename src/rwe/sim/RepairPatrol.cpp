#include "RepairPatrol.h"
#include <algorithm>
#include <cmath>
#include <rwe/sim/FlatRange.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/match.h>
#include <vector>

namespace rwe
{
    namespace
    {
        // The double 0.2 at 0x4FC950 (ground) and 0x4FCC58 (air).
        constexpr double RepairPatrolStoreFraction = 0.2;

        /**
         * The fraction of storage a patrol threshold stands for. Outside the
         * Community Patch that is the original's fixed fifth. Under it
         * (TADR 2026.10.6, "Patrol reclaim thresholds") it is the player's own
         * whole-percentage level, read live so that it tracks the storage as
         * the storage grows.
         */
        double reclaimThresholdFraction(const GameSimulation& sim, const GamePlayerInfo& player, bool metal)
        {
            if (!sim.communityPatchMode)
            {
                return RepairPatrolStoreFraction;
            }
            return static_cast<double>(metal ? player.reclaimMetalPercent : player.reclaimEnergyPercent) / 100.0;
        }

        // The scan's sample spacing, 0x30 in whole world units.
        constexpr int ScanStep = 48;

        // The cell the unit query (0x47E890) walks: 128 world units.
        constexpr float QueryCellSize = 128.0f;

        struct ReclaimSample
        {
            FeatureId feature;
            float amount;
        };
    }

    unsigned int repairPatrolPeriod(bool flies)
    {
        // TOTALA-EXE-ECONOMY.md S:131: VTOL_RepairPatrol's timer is 45, the
        // ground routine's 60.
        return flies ? 45u : 60u;
    }

    unsigned int drawIndexBelow(GameSimulation& sim, unsigned int n)
    {
        if (n < 2)
        {
            return 0;
        }
        return static_cast<unsigned int>(sim.rng() % n);
    }

    std::optional<UnitId> pickRepairPatrolRepairTarget(GameSimulation& sim, ConstUnitInfo unitInfo)
    {
        // S:131: "only if player.energy >= 0.2 * player.energyCap".
        // TADR 2026.10.6: under the Community Patch the energy level is the
        // player's own, and it also decides when a patrol assists; at or above
        // the level it assists, below it it does not.
        const auto& player = sim.getPlayer(unitInfo.state->owner);
        const auto energyFraction = reclaimThresholdFraction(sim, player, false);
        if (static_cast<double>(player.energy.value) < energyFraction * static_cast<double>(player.maxEnergy.value))
        {
            return std::nullopt;
        }

        // Units this player is already reclaiming are refused by the
        // callback (cand+0xF4 is the player and cand+0xF5 is 5).
        std::vector<UnitId> beingReclaimed;
        for (const auto& [id, other] : sim.units)
        {
            if (!other.isOwnedBy(unitInfo.state->owner))
            {
                continue;
            }
            if (auto reclaiming = std::get_if<UnitBehaviorStateReclaiming>(&other.behaviourState))
            {
                if (auto targetUnit = std::get_if<UnitId>(&reclaiming->target))
                {
                    beingReclaimed.push_back(*targetUnit);
                }
            }
        }

        // The query is flat, in whole world units, each square floored
        // separately (S:127).
        const auto sight = static_cast<float>(unitInfo.definition->sightDistance);
        const auto& position = unitInfo.state->position;

        struct Found
        {
            int cellZ;
            int cellX;
            UnitId id;
        };
        std::vector<Found> found;

        for (const auto& [id, candidate] : sim.units)
        {
            if (id == unitInfo.id || candidate.isDead())
            {
                continue;
            }

            // A non-zero alliance byte: own or allied.
            if (!sim.arePlayersAllied(unitInfo.state->owner, candidate.owner))
            {
                continue;
            }

            // cand+0x110 bits 0-1 equal to 1: standing on the ground.
            if (isFlying(candidate.physics))
            {
                continue;
            }

            const auto& candidateDefinition = sim.unitDefinitions.at(candidate.unitType);
            if (candidate.hitPoints >= candidateDefinition.maxHitPoints && !candidate.isBeingBuilt(candidateDefinition))
            {
                continue;
            }

            if (std::find(beingReclaimed.begin(), beingReclaimed.end(), id) != beingReclaimed.end())
            {
                continue;
            }

            if (!withinFlatRange(position, candidate.position, SimScalar(sight)))
            {
                continue;
            }

            found.push_back(Found{
                static_cast<int>(std::floor(candidate.position.z.value / QueryCellSize)),
                static_cast<int>(std::floor(candidate.position.x.value / QueryCellSize)),
                id});
        }

        if (found.empty())
        {
            return std::nullopt;
        }

        // Outer z ascending, inner x ascending, and within a cell the chain
        // order, which is not known; unit id stands in for it. [open]
        std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
            if (a.cellZ != b.cellZ)
            {
                return a.cellZ < b.cellZ;
            }
            if (a.cellX != b.cellX)
            {
                return a.cellX < b.cellX;
            }
            return a.id.value < b.id.value;
        });

        return found[drawIndexBelow(sim, static_cast<unsigned int>(found.size()))].id;
    }

    std::optional<FeatureId> pickRepairPatrolReclaimTarget(GameSimulation& sim, ConstUnitInfo unitInfo, SimScalar side)
    {
        const auto& position = unitInfo.state->position;
        const auto half = side.value / 2.0f;

        // 0x47EA40: samples every 48 units from position - half while
        // <= position + half, z outer and x inner.
        const auto count = static_cast<int>(std::floor((2.0f * half) / static_cast<float>(ScanStep))) + 1;

        std::vector<ReclaimSample> metalList;
        std::vector<ReclaimSample> energyList;

        for (int zi = 0; zi < count; ++zi)
        {
            for (int xi = 0; xi < count; ++xi)
            {
                SimVector sample(
                    SimScalar(position.x.value - half + static_cast<float>(xi * ScanStep)),
                    0_ss,
                    SimScalar(position.z.value - half + static_cast<float>(zi * ScanStep)));

                auto heightmapSpace = sim.terrain.worldToHeightmapSpace(sample);
                if (heightmapSpace.x.value < 0.0f || heightmapSpace.z.value < 0.0f)
                {
                    continue;
                }

                auto cell = sim.terrain.worldToHeightmapCoordinate(sample);
                auto occupied = sim.occupiedGrid.tryGet(cell);
                if (!occupied || !occupied->get().featureId)
                {
                    continue;
                }

                auto featureId = *occupied->get().featureId;
                auto feature = sim.tryGetFeature(featureId);
                if (!feature)
                {
                    continue;
                }

                // Bits 7 and 8 of the flag word: reclaimable, autoreclaimable.
                const auto& definition = sim.getFeatureDefinition(feature->get().featureName);
                if (!definition.reclaimable || !definition.autoreclaimable)
                {
                    continue;
                }

                if (definition.energy > 0)
                {
                    energyList.push_back(ReclaimSample{featureId, static_cast<float>(definition.energy)});
                }
                if (definition.metal > 0)
                {
                    metalList.push_back(ReclaimSample{featureId, static_cast<float>(definition.metal)});
                }
            }
        }

        // The best of three draws, with replacement, keeping a draw only if
        // its amount is strictly greater than the best so far (initially 0.0,
        // index 0). The order the two lists are drawn in is not given. [open]
        auto bestOfThree = [&](const std::vector<ReclaimSample>& list) -> std::optional<ReclaimSample> {
            if (list.empty())
            {
                return std::nullopt;
            }
            float bestAmount = 0.0f;
            std::size_t bestIndex = 0;
            for (int draw = 0; draw < 3; ++draw)
            {
                auto index = drawIndexBelow(sim, static_cast<unsigned int>(list.size()));
                if (list[index].amount > bestAmount)
                {
                    bestAmount = list[index].amount;
                    bestIndex = index;
                }
            }
            return list[bestIndex];
        };

        auto metal = bestOfThree(metalList);
        auto energy = bestOfThree(energyList);

        // The decision ladder, in order. Under the Community Patch the first
        // two rungs use the player's own levels instead of the fixed fifth
        // (TADR 2026.10.6); the two "still fits" rungs are unchanged.
        const auto& player = sim.getPlayer(unitInfo.state->owner);
        const auto metalStored = static_cast<double>(player.metal.value);
        const auto metalCap = static_cast<double>(player.maxMetal.value);
        const auto energyStored = static_cast<double>(player.energy.value);
        const auto energyCap = static_cast<double>(player.maxEnergy.value);
        const auto metalFraction = reclaimThresholdFraction(sim, player, true);
        const auto energyFraction = reclaimThresholdFraction(sim, player, false);

        if (metal && metalStored < metalFraction * metalCap)
        {
            return metal->feature;
        }
        if (energy && energyStored < energyFraction * energyCap)
        {
            return energy->feature;
        }
        if (metal && metalStored + static_cast<double>(metal->amount) <= metalCap)
        {
            return metal->feature;
        }
        if (energy && energyStored + static_cast<double>(energy->amount) <= energyCap)
        {
            return energy->feature;
        }

        return std::nullopt;
    }
}
