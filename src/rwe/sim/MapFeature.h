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
         * How the feature is moving, in world units per tick. Zero for all but
         * one thing: wreckage dropped into water sinks to the sea bed.
         *
         * The original spawns a corpse at the dying unit's exact height, land
         * or sea alike, and then gives it a downward velocity when the ground
         * beneath is at or below sea level (0x486416). A per-tick sweep spends
         * it: fall, stop on the bottom, hold a fixed terminal speed while
         * submerged, otherwise accelerate under the map's gravity. The
         * constant it writes appears exactly twice in the whole binary, and
         * the only other writes to a feature's velocity are the zeroing, so
         * this physics exists solely to sink wreckage.
         */
        SimVector velocity{0_ss, 0_ss, 0_ss};

        /**
         * Reclaim work applied so far, in worker-time units.
         * Lives on the feature so several units can reclaim it together.
         * The feature is removed once this reaches computeFeatureReclaimWork().
         */
        unsigned int reclaimProgress{0};

        /**
         * Hit points. Initialised from the definition's `damage` value (TA stores
         * a feature's health under that key) when the feature is placed. A blast
         * takes them away unless the feature is indestructible, and at zero the
         * feature breaks down to its featureDead form. computeFeatureReclaimWork
         * reads what is left, which is why a boulder takes longer to salvage than
         * a shrub of the same value, and a shelled wreck clears quicker.
         */
        unsigned int hitPoints{0};

        /** Set while the feature is on fire: the tick it burns out and is replaced by its burnt form. */
        std::optional<GameTime> burningUntil;

        /** While burning, the next tick the fire tries to spread to its neighbours. */
        GameTime nextSpark{0};
    };
}
