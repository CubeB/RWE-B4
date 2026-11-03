#include "PresentationView.h"

#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/GamePlayerInfo.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitVisibility.h>

namespace rwe
{
    namespace
    {
        PresentationUnit makeUnit(
            const GameSimulation& simulation,
            PlayerId viewer,
            UnitId id,
            const UnitState& unit,
            const UnitDefinition& definition)
        {
            PresentationUnit view{};
            view.id = id;
            view.type = unit.unitType;
            view.owner = unit.owner;
            view.position = unit.position;
            view.rotation = unit.rotation;
            view.hitPoints = unit.hitPoints;
            view.maxHitPoints = definition.maxHitPoints;
            view.owned = unit.isOwnedBy(viewer);
            view.allied = simulation.arePlayersAllied(viewer, unit.owner);
            view.displayName = definition.unitName;

            if (unit.owner.value < simulation.players.size())
            {
                const auto& player = simulation.players.at(unit.owner.value);
                view.teamId = player.teamId;
                view.color = player.color;
            }

            return view;
        }
    }

    PlayerView::PlayerView(
        const GameSimulation& simulation,
        PlayerId viewer,
        const PlayerVisibility& visibility,
        bool exploredByAnyGroup)
        : simulation_(simulation), viewer_(viewer), visibility_(visibility), exploredByAnyGroup_(exploredByAnyGroup)
    {
    }

    std::vector<PresentationUnit> PlayerView::units() const
    {
        std::vector<PresentationUnit> result;

        for (const auto& [id, unit] : simulation_.units)
        {
            if (!canSeeUnitInVisibility(simulation_, viewer_, id, visibility_))
            {
                continue;
            }

            const auto definition = simulation_.unitDefinitions.find(unit.unitType);
            if (definition == simulation_.unitDefinitions.end())
            {
                continue;
            }

            result.push_back(makeUnit(simulation_, viewer_, id, unit, definition->second));
        }

        return result;
    }

    std::optional<PresentationUnit> PlayerView::unit(UnitId id) const
    {
        const auto unitState = simulation_.tryGetUnitState(id);
        if (!unitState)
        {
            return std::nullopt;
        }

        // The same gate the world draw and the unit pick use. A presentation
        // mod must not be able to ask directly for an id it should not know.
        if (!canSeeUnitInVisibility(simulation_, viewer_, id, visibility_))
        {
            return std::nullopt;
        }

        const auto& unit = unitState->get();
        const auto definition = simulation_.unitDefinitions.find(unit.unitType);
        if (definition == simulation_.unitDefinitions.end())
        {
            return std::nullopt;
        }

        return makeUnit(simulation_, viewer_, id, unit, definition->second);
    }

    std::vector<PresentationSensorContact> PlayerView::sensorContacts() const
    {
        std::vector<PresentationSensorContact> result;

        // A contact the viewer can also see in full is a unit, not a contact:
        // the original's contact sets only matter where full sight fails.
        auto addContacts = [&](const std::unordered_set<UnitId>& contacts, bool sonar) {
            for (auto id : contacts)
            {
                if (canSeeUnitInVisibility(simulation_, viewer_, id, visibility_))
                {
                    continue;
                }

                const auto unit = simulation_.tryGetUnitState(id);
                if (!unit)
                {
                    continue;
                }

                // No type, name, health or owner crosses this boundary. The
                // original's dot says that something is there and nothing
                // else (BT-141).
                result.push_back(PresentationSensorContact{id, unit->get().position, sonar});
            }
        };

        addContacts(visibility_.radarContacts, false);
        addContacts(visibility_.sonarContacts, true);

        return result;
    }

    std::vector<PresentationFeature> PlayerView::features() const
    {
        std::vector<PresentationFeature> result;

        for (const auto& [id, feature] : simulation_.features)
        {
            const auto explored = exploredByAnyGroup_
                ? simulation_.isExploredByAnyGroup(feature.position)
                : simulation_.isExploredBy(viewer_, feature.position);
            if (!explored)
            {
                continue;
            }

            PresentationFeature view{};
            view.id = id;
            view.position = feature.position;
            view.visible = isVisibleAt(feature.position);

            const auto definition = simulation_.featureDefinitions.tryGet(feature.featureName);
            view.type = definition ? definition->get().name : std::string();

            result.push_back(std::move(view));
        }

        return result;
    }

    std::vector<PresentationProjectile> PlayerView::projectiles() const
    {
        std::vector<PresentationProjectile> result;

        for (const auto& [id, projectile] : simulation_.projectiles)
        {
            // The same question the renderer asks of a round: is the ground
            // under it in live sight now? Radar reveals no ground, and a
            // remembered cell is not live sight.
            if (!isVisibleAt(projectile.position))
            {
                continue;
            }

            result.push_back(PresentationProjectile{id, projectile.position});
        }

        return result;
    }

    std::optional<PresentationResources> PlayerView::resources() const
    {
        if (viewer_.value >= simulation_.players.size())
        {
            return std::nullopt;
        }

        const auto& player = simulation_.players.at(viewer_.value);

        PresentationResources view{};
        view.metal = player.metal;
        view.energy = player.energy;
        view.maxMetal = player.maxMetal;
        view.maxEnergy = player.maxEnergy;
        view.metalProduced = player.metalProduced;
        view.energyProduced = player.energyProduced;
        view.metalStalled = player.metalStalled;
        view.energyStalled = player.energyStalled;
        return view;
    }

    std::optional<PresentationUnitDefinition> PlayerView::unitDefinition(const std::string& type) const
    {
        const auto definition = simulation_.unitDefinitions.find(type);
        if (definition == simulation_.unitDefinitions.end())
        {
            return std::nullopt;
        }

        const auto& unit = definition->second;

        PresentationUnitDefinition view{};
        view.name = unit.unitName;
        view.displayName = unit.unitName;
        view.description = unit.unitDescription;
        view.buildCostMetal = unit.buildCostMetal;
        view.buildCostEnergy = unit.buildCostEnergy;
        view.buildTime = unit.buildTime;
        view.maxHitPoints = unit.maxHitPoints;
        view.isBuilder = unit.builder;
        view.isMobile = unit.isMobile;
        view.canMove = unit.canMove;
        return view;
    }

    bool PlayerView::isVisibleAt(const SimVector& position) const
    {
        return visibility_.isVisible(simulation_.visionCellAt(position));
    }

    bool PlayerView::isExploredAt(const SimVector& position) const
    {
        return exploredByAnyGroup_
            ? simulation_.isExploredByAnyGroup(position)
            : simulation_.isExploredBy(viewer_, position);
    }

    unsigned int PlayerView::tick() const
    {
        return simulation_.gameTime.value;
    }
}
