#include "BuildManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    namespace
    {
        // Phase 1 hardcoded unit name table. Future phases swap this for the
        // BuildOrderTemplate library described in docs §6.
        struct SidePhase1Names
        {
            std::string mexUnitType;
            std::string solarUnitType;
        };

        // Resolve the side's phase-1 build names from the commander's unit
        // type. We cannot ask GamePlayerInfo for the side directly because
        // the legacy `side` string is "ARM"/"CORE" lower-case in some places
        // and the canonical place to read the canonical answer is the
        // commander unit's prefix.
        SidePhase1Names resolvePhase1Names(const std::string& commanderUnitType)
        {
            SidePhase1Names n;
            // Commander unit names are conventionally "ARMCOM" / "CORCOM".
            // Strip the suffix and produce "<PREFIX>MEX" / "<PREFIX>SOLAR".
            const std::string suffix = "COM";
            std::string prefix = commanderUnitType;
            if (prefix.size() > suffix.size()
                && prefix.compare(prefix.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                prefix.erase(prefix.size() - suffix.size());
            }
            else
            {
                // Non-standard commander; fall back to ARM names. Phase 2 will
                // replace this with a side-data lookup.
                prefix = "ARM";
            }
            n.mexUnitType = prefix + "MEX";
            n.solarUnitType = prefix + "SOLAR";
            return n;
        }
    }

    void BuildManager::resolveSide(const GameSimulation& sim, const AiBlackboard& bb)
    {
        if (sideResolved)
        {
            return;
        }

        std::string commanderUnitType = "ARMCOM"; // safe default
        if (bb.commanderUnitId)
        {
            const auto& unit = sim.getUnitState(*bb.commanderUnitId);
            commanderUnitType = unit.unitType;
        }

        const auto names = resolvePhase1Names(commanderUnitType);
        mexUnitType = names.mexUnitType;
        solarUnitType = names.solarUnitType;
        sideResolved = true;
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

        // Sweep a square grid centred on `anchor`, in deterministic order.
        // Spiral iteration would be marginally more "human" but ordered
        // x-then-z makes the test much easier to reason about.
        const SimScalar spacing = profile.buildSiteGridSpacing;
        const SimScalar radius = profile.maxMexSearchRadius;

        // Convert the search radius to integer ring counts to avoid float
        // comparisons in the inner loop.
        const int ringCount = std::max(1, static_cast<int>(radius.value / spacing.value));

        // Tiebreak: among all valid sites, pick the one closest to anchor;
        // if multiple have the same integer-distance, use rng to pick.
        std::optional<SimVector> bestSite;
        int bestRing = ringCount + 1;

        std::vector<SimVector> tiedCandidates;

        for (int ring = 0; ring <= ringCount; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    // Only emit cells on the *current* ring perimeter, so
                    // we genuinely visit closest-first.
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                    {
                        continue;
                    }

                    const SimScalar offX = SimScalar(static_cast<float>(dx)) * spacing;
                    const SimScalar offZ = SimScalar(static_cast<float>(dz)) * spacing;
                    SimVector candidate(anchor.x + offX, anchor.y, anchor.z + offZ);
                    candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);

                    auto rect = sim.computeFootprintRegion(candidate, def.movementCollisionInfo);
                    // computeFootprintRegion returns int rect; cast to unsigned
                    // for the canBeBuiltAt API.
                    if (rect.x < 0 || rect.y < 0)
                    {
                        continue;
                    }
                    const auto x = static_cast<unsigned int>(rect.x);
                    const auto y = static_cast<unsigned int>(rect.y);

                    if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, x, y))
                    {
                        continue;
                    }

                    if (ring < bestRing)
                    {
                        bestRing = ring;
                        bestSite = candidate;
                        tiedCandidates.clear();
                        tiedCandidates.push_back(candidate);
                    }
                    else if (ring == bestRing)
                    {
                        tiedCandidates.push_back(candidate);
                    }
                }
            }

            // Closest-ring-first short-circuit: if we found anything in this
            // ring, stop searching. We still pick from the tied set inside.
            if (bestSite && bestRing == ring)
            {
                break;
            }
        }

        if (!bestSite || tiedCandidates.empty())
        {
            return std::nullopt;
        }

        // Deterministic tiebreak: use the AI sub-RNG.
        std::uniform_int_distribution<std::size_t> dist(0, tiedCandidates.size() - 1);
        return tiedCandidates[dist(rng)];
    }

    std::optional<SimVector> BuildManager::chooseMexSite(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const std::string& unitType,
        const SimVector& anchor,
        std::minstd_rand& rng) const
    {
        // Walk the metal grid radially around the anchor in heightmap-space
        // and pick the first cell with non-zero metal that yields a buildable
        // mex placement.
        const auto& metalGrid = sim.metalGrid;
        const auto anchorHm = sim.terrain.worldToHeightmapCoordinate(anchor);

        const SimScalar radius = profile.maxMexSearchRadius;
        const int radiusInTiles = std::max(
            1,
            static_cast<int>(radius.value / MapTerrain::HeightTileWidthInWorldUnits.value));

        const auto defIt = sim.unitDefinitions.find(unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return std::nullopt;
        }
        const auto& def = defIt->second;
        const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);

        std::vector<SimVector> tiedCandidates;
        int bestRing = radiusInTiles + 1;

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
                    if (gx < 0 || gz < 0
                        || gx >= static_cast<int>(metalGrid.getWidth())
                        || gz >= static_cast<int>(metalGrid.getHeight()))
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
                    const auto rx = static_cast<unsigned int>(rect.x);
                    const auto ry = static_cast<unsigned int>(rect.y);

                    if (!sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, rx, ry))
                    {
                        continue;
                    }

                    if (ring < bestRing)
                    {
                        bestRing = ring;
                        tiedCandidates.clear();
                        tiedCandidates.push_back(candidate);
                    }
                    else if (ring == bestRing)
                    {
                        tiedCandidates.push_back(candidate);
                    }
                }
            }

            if (bestRing == ring && !tiedCandidates.empty())
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

    void BuildManager::update(
        const GameSimulation& sim,
        PlayerId aiOwner,
        const AiTuningProfile& profile,
        const AiBlackboard& bb,
        std::minstd_rand& rng,
        std::vector<PlayerCommand>& outCommands)
    {
        (void)aiOwner;

        // Cadence: only act every `buildPlannerTickInterval` ticks. This is
        // both a rate-limit (avoid spamming orders that the sim can't keep
        // up with) and a deduplication shield (a previously-issued order is
        // still being walked to by the commander).
        ++ticksSinceLastPlanning;
        if (ticksSinceLastPlanning < profile.buildPlannerTickInterval)
        {
            return;
        }
        ticksSinceLastPlanning = 0;

        if (!bb.baseAnchor)
        {
            // Cannot plan without a base location.
            return;
        }

        // Lazily resolve which unit names to use for this AI's side.
        resolveSide(sim, bb);

        // Phase 1 only ever issues orders to the commander — there is no
        // factory/cons-kbot logic yet. If the commander is dead, we have
        // nothing to do.
        if (!bb.commanderUnitId)
        {
            return;
        }

        // Don't pile new orders onto the commander if it's already busy.
        // The commander's order queue is the simplest possible busy check.
        const auto& commander = sim.getUnitState(*bb.commanderUnitId);
        if (!commander.orders.empty())
        {
            return;
        }

        const auto mexCountIt = bb.ownedTotalCounts.find(mexUnitType);
        const int mexCount = mexCountIt == bb.ownedTotalCounts.end() ? 0 : mexCountIt->second;

        const auto solarCountIt = bb.ownedTotalCounts.find(solarUnitType);
        const int solarCount = solarCountIt == bb.ownedTotalCounts.end() ? 0 : solarCountIt->second;

        // Step 1: prioritise mexes.
        if (mexCount < profile.openingMetalExtractorCount)
        {
            if (auto site = chooseMexSite(sim, profile, mexUnitType, *bb.baseAnchor, rng))
            {
                outCommands.emplace_back(
                    PlayerUnitCommand(
                        *bb.commanderUnitId,
                        PlayerUnitCommand::IssueOrder(
                            BuildOrder(mexUnitType, *site),
                            PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
            // No mex site reachable — fall through to solar so the AI is
            // not stuck if the map has no metal patches near base.
        }

        // Step 2: solar collectors.
        if (solarCount < profile.openingSolarCount)
        {
            if (auto site = chooseBuildSite(sim, profile, solarUnitType, *bb.baseAnchor, rng))
            {
                outCommands.emplace_back(
                    PlayerUnitCommand(
                        *bb.commanderUnitId,
                        PlayerUnitCommand::IssueOrder(
                            BuildOrder(solarUnitType, *site),
                            PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
                return;
            }
        }

        // Quotas met (or no buildable sites). Phase 1 deliberately ends here
        // — Phase 2 will start chaining factories, units, and reactive
        // build steps.
    }
}
