#pragma once

#include <rwe/sim/FeatureDefinitionId.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    struct MapFeature
    {
        FeatureDefinitionId featureName;
        SimVector position;
        SimAngle rotation{0};

        /**
         * Reclaim work applied so far, in worker-time units.
         * Lives on the feature so several units can reclaim it together.
         * The feature is removed once this reaches computeFeatureReclaimWork().
         */
        unsigned int reclaimProgress{0};
    };
}
