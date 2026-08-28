#pragma once

#include <optional>
#include <rwe/sim/FeatureDefinitionId.h>
#include <rwe/sim/GameTime.h>
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

        /** Set while the feature is on fire: the tick it burns out and is replaced by its burnt form. */
        std::optional<GameTime> burningUntil;

        /** While burning, the next tick the fire tries to spread to its neighbours. */
        GameTime nextSpark{0};
    };
}
