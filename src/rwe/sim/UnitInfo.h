#pragma once

#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct UnitState;
    struct UnitDefinition;

    struct UnitInfo
    {
        const UnitId id;
        UnitState* const state;
        const UnitDefinition* const definition;

        UnitInfo(UnitId id, UnitState* state, const UnitDefinition* definition)
            : id(id), state(state), definition(definition) {}
    };

    struct ConstUnitInfo
    {
        const UnitId id;
        const UnitState* const state;
        const UnitDefinition* const definition;

        ConstUnitInfo(UnitId id, const UnitState* state, const UnitDefinition* definition)
            : id(id), state(state), definition(definition) {}

        ConstUnitInfo(UnitInfo unitInfo)
            : id(unitInfo.id), state(unitInfo.state), definition(unitInfo.definition) {}
    };
}
