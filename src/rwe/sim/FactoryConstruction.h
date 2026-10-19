#pragma once
#include <rwe/sim/UnitId.h>
namespace rwe
{
    struct GameSimulation;
    // BT-266; call before cargo cleanup and the factory death explosion.
    void cancelFactoryProductOnDeath(GameSimulation& sim, UnitId factoryId);
}
