#pragma once

#include <rwe/util/OpaqueId.h>

namespace rwe
{
    // Logical group of units assigned to a shared mission.
    // Phase 1 does not yet use platoons; declared here for API stability
    // because the Sorian-style platoon abstraction lands in Phase 3.
    struct PlatoonIdTag;
    using PlatoonId = OpaqueId<unsigned int, PlatoonIdTag>;

    // A pending build instruction (unitType + position) that the BuildManager
    // is trying to assign to an idle builder unit.
    struct BuildJobIdTag;
    using BuildJobId = OpaqueId<unsigned int, BuildJobIdTag>;

    // A higher-level "thing the AI is trying to do" (mission, expansion, scout).
    // Phase 1 does not actually emit any AiTaskIds; declared here so future phases
    // do not have to retrofit IDs into existing storage.
    struct AiTaskIdTag;
    using AiTaskId = OpaqueId<unsigned int, AiTaskIdTag>;
}
