#include "AiSideUnits.h"
#include <cctype>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    namespace
    {
        bool isDefined(const GameSimulation& sim, const std::string& unitType)
        {
            return !unitType.empty() && sim.unitDefinitions.find(unitType) != sim.unitDefinitions.end();
        }
    }

    AiSideUnits resolveAiSideUnits(const GameSimulation& sim, const std::string& side)
    {
        std::string upper;
        for (auto c : side)
        {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }

        AiSideUnits units;
        if (upper == "CORE")
        {
            units = AiSideUnits{"CORMEX", "CORSOLAR", "CORLAB", "CORCK", "CORAK", "CORSTORM", "CORLLT", "CORRAD", "CORMAKR",
                "CORAP", "CORFINK", "CORVALK", "CORVP", "CORFAV", "CORGATOR"};
        }
        else
        {
            units = AiSideUnits{"ARMMEX", "ARMSOLAR", "ARMLAB", "ARMCK", "ARMPW", "ARMROCK", "ARMLLT", "ARMRAD", "ARMMAKR",
                "ARMAP", "ARMPEEP", "ARMATLAS", "ARMVP", "ARMFAV", "ARMFLASH"};
        }

        auto check = [&](std::string& name) {
            if (!isDefined(sim, name))
            {
                name.clear();
            }
        };
        check(units.metalExtractor);
        check(units.solar);
        check(units.lab);
        check(units.constructor);
        check(units.raider);
        check(units.rocketKbot);
        check(units.lightLaserTower);
        check(units.radar);
        check(units.metalMaker);
        check(units.airPlant);
        check(units.scoutPlane);
        check(units.airTransport);
        check(units.vehiclePlant);
        check(units.scoutVehicle);
        check(units.tank);
        return units;
    }

    bool isAiScoutType(const AiSideUnits& units, const std::string& unitType)
    {
        return !unitType.empty() && (unitType == units.scoutPlane || unitType == units.scoutVehicle);
    }
}
