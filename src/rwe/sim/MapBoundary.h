#pragma once

namespace rwe
{
    struct GameSimulation;

    // BOUNDARY-001, docs/PLAYTEST-MAP-BOUNDARY-FINDINGS.md. Called after
    // initial features and lava preparation, before movement-class caches.
    void initializeMapBoundaryExclusions(GameSimulation& simulation);
}
