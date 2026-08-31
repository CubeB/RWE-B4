#pragma once

#include <rwe/cob/CobAngle.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    CobAngle toCobAngle(SimAngle angle);

    CobSpeed toCobSpeed(SimScalar speed);

    void handlePieceCommand(GameSimulation& sim, const CobEnvironment& env, UnitId unitId, const CobEnvironment::PieceCommandStatus& result);

    int handleQuery(GameSimulation& sim, const CobEnvironment& env, UnitId unitId, const CobEnvironment::QueryStatus& result);

    void handleSetQuery(GameSimulation& sim, const CobEnvironment& env, UnitId unitId, const CobEnvironment::SetQueryStatus& result);

    void runUnitCobScripts(GameSimulation& simulation, UnitId unitId);
}
