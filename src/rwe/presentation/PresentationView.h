#pragma once

#include <optional>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct PlayerVisibility;

    /**
     * MOD-10, docs/MODDING-SDK-FINDINGS.md MOD-SDK-013.
     *
     * The filtered player view a presentation mod is given instead of the
     * simulation. Everything here is already subject to the viewer's fog,
     * radar/sonar picture and cloak rules; the view holds no way to reach
     * `GameSimulation::units`, `projectiles`, `playerVisibility`, `explored`
     * or `rng`, and every list it returns is a copy of public state.
     *
     * RWE-original: the original has no presentation mod surface, so there is
     * no byte-tactics counterpart for the API itself. The filters it applies
     * are the decoded ones (`canSeeUnitInVisibility`, `isExploredBy`,
     * `effectIsVisibleToPlayer`), which is what keeps it conformance-shaped.
     */

    /** One unit the viewer is allowed to see in full. */
    struct PresentationUnit
    {
        UnitId id;
        std::string type;
        PlayerId owner;
        std::optional<int> teamId;
        std::optional<PlayerColorIndex> color;
        SimVector position;
        SimAngle rotation;
        unsigned int hitPoints{0};
        unsigned int maxHitPoints{0};

        /** True when the unit belongs to the viewer. */
        bool owned{false};

        /** True when the owner is an ally of the viewer, including the viewer. */
        bool allied{false};

        /** The type's display name from its FBI, when the data has one. */
        std::string displayName;
    };

    /**
     * A unit known only as a radar or sonar contact. Deliberately carries no
     * type, name, health or owner: the original's contact gives a dot and a
     * prefix, nothing more (BT-141).
     */
    struct PresentationSensorContact
    {
        UnitId id;
        SimVector position;

        /** True for a sonar contact, false for radar. */
        bool sonar{false};
    };

    /** A map feature on ground the viewer has explored. */
    struct PresentationFeature
    {
        FeatureId id;
        std::string type;
        SimVector position;

        /** True when the ground under it is in live sight, false when remembered. */
        bool visible{false};
    };

    /** A round in the air over ground the viewer can currently see. */
    struct PresentationProjectile
    {
        ProjectileId id;
        SimVector position;
    };

    /** What the viewer's own stockpile reads. Allied stockpiles are not exposed. */
    struct PresentationResources
    {
        Metal metal;
        Energy energy;
        Metal maxMetal;
        Energy maxEnergy;
        Metal metalProduced;
        Energy energyProduced;
        bool metalStalled{false};
        bool energyStalled{false};
    };

    /** Static description of a unit type, for build menus and labels. */
    struct PresentationUnitDefinition
    {
        std::string name;
        std::string displayName;
        std::string description;
        Metal buildCostMetal;
        Energy buildCostEnergy;
        unsigned int buildTime{0};
        unsigned int maxHitPoints{0};
        bool isBuilder{false};
        bool isMobile{false};
        bool canMove{false};
    };

    /**
     * The only route from a presentation mod to the simulation.
     *
     * Constructed per query, or held for a frame; it borrows the simulation
     * and the visibility picture and never writes either. The caller must
     * keep both alive for the view's lifetime, which in practice means the
     * view is a stack object in a frame callback.
     */
    class PlayerView
    {
    public:
        PlayerView(
            const GameSimulation& simulation,
            PlayerId viewer,
            const PlayerVisibility& visibility,
            bool exploredByAnyGroup = false);

        PlayerId viewer() const { return viewer_; }

        /** Own units and every foreign unit the viewer can see in full. */
        std::vector<PresentationUnit> units() const;

        /** The same filter, for one id. Nothing if the viewer cannot see it. */
        std::optional<PresentationUnit> unit(UnitId id) const;

        /** Contacts the viewer has no full sight of; visible units are excluded. */
        std::vector<PresentationSensorContact> sensorContacts() const;

        /** Features on explored ground; `visible` distinguishes sight from memory. */
        std::vector<PresentationFeature> features() const;

        /** Rounds over live-sight ground. */
        std::vector<PresentationProjectile> projectiles() const;

        std::optional<PresentationResources> resources() const;

        std::optional<PresentationUnitDefinition> unitDefinition(const std::string& type) const;

        /** Whether the ground at `position` is in live sight now. */
        bool isVisibleAt(const SimVector& position) const;

        /** Whether the ground at `position` has ever been explored. */
        bool isExploredAt(const SimVector& position) const;

        /** The current simulation tick. A presentation clock, not a wall clock. */
        unsigned int tick() const;

    private:
        const GameSimulation& simulation_;
        PlayerId viewer_;
        const PlayerVisibility& visibility_;
        bool exploredByAnyGroup_{false};
    };
}
