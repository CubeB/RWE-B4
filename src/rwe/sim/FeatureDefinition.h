#pragma once

#include <optional>
#include <rwe/sim/FeatureDefinitionId.h>
#include <rwe/sim/SimScalar.h>
#include <string>
#include <vector>

namespace rwe
{
    struct FeatureDefinition
    {
        std::string name;

        int footprintX;
        int footprintZ;
        SimScalar height;

        bool reclaimable;
        bool autoreclaimable;
        std::optional<FeatureDefinitionId> featureReclamate;
        unsigned int metal;
        unsigned int energy;

        bool flamable;
        std::optional<FeatureDefinitionId> featureBurnt;
        unsigned int burnMin;
        unsigned int burnMax;
        unsigned int sparkTime;
        unsigned int spreadChance;
        std::string burnWeapon;

        /** BT-44/255: play-once burn timings, loaded identically without graphics. */
        std::vector<unsigned int> burnFrameTicks{};
        bool hasModel{false};

        bool geothermal;


        unsigned int reproduce;
        unsigned int reproduceArea;

        bool noDisplayInfo;

        bool permanent;

        bool blocking;

        bool indestructible;
        unsigned int damage;
        std::optional<FeatureDefinitionId> featureDead;

        bool isStanding() const;
    };
}
