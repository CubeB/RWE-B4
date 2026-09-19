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

        // Written out field by field rather than as a positional aggregate.
        // The list had grown to seventeen strings of the same type in a row,
        // where transposing two is silent and would only show up as an AI
        // building the wrong thing, and the level-two block below makes it
        // twenty-five.
        AiSideUnits units;
        if (upper == "CORE")
        {
            units.metalExtractor = "CORMEX";
            units.solar = "CORSOLAR";
            units.lab = "CORLAB";
            units.constructor = "CORCK";
            units.raider = "CORAK";
            units.rocketKbot = "CORSTORM";
            units.lightLaserTower = "CORLLT";
            units.radar = "CORRAD";
            units.metalMaker = "CORMAKR";
            units.airPlant = "CORAP";
            units.scoutPlane = "CORFINK";
            units.airTransport = "CORVALK";
            units.fighter = "CORVENG";
            units.bomber = "CORSHAD";
            units.airConstructor = "CORCA";
            units.vehiclePlant = "CORVP";
            units.scoutVehicle = "CORFAV";
            units.tank = "CORGATOR";
            units.missileTruck = "CORMIST";
            units.mediumTank = "CORRAID";
            units.artilleryKbot = "CORTHUD";
            units.dragonsTeeth = "CORDRAG";
            units.antiAirTower = "CORRL";
            units.antiAirKbot = "CORCRASH";
            units.advancedLab = "CORALAB";
            units.advancedConstructor = "CORACK";
            units.advancedAssault = "CORCAN";
            units.heavyLaserTower = "CORHLT";
            units.heavyPlasmaTower = "CORPUN";
            units.advancedRadar = "CORARAD";
            units.mohoExtractor = "CORMOHO";
            units.fusion = "CORFUS";
            units.shipyard = "CORSY";
            units.constructionShip = "CORCS";
            units.scoutShip = "CORPT";
            units.destroyer = "CORROY";
            units.seaTransport = "CORTSHIP";
            units.submarine = "CORSUB";
            units.advancedShipyard = "CORASY";
            units.cruiser = "CORCRUS";
            units.battleship = "CORBATS";
            units.antiAirShip = "CORARCH";
            units.advancedConstructionSub = "CORACSUB";
            units.seaplanePlatform = "CORPLAT";
            units.seaplaneFighter = "CORSFIG";
            units.torpedoSeaplane = "CORSEAP";
            units.tidalGenerator = "CORTIDE";
            units.sonar = "CORSONAR";
            units.torpedoLauncher = "CORTL";
            units.underwaterMetalExtractor = "CORUWMEX";
            units.floatingMetalMaker = "CORFMKR";
            units.geothermal = "CORGEO";
            units.metalStorage = "CORMSTOR";
            units.energyStorage = "CORESTOR";
            units.underwaterMetalStorage = "CORUWMS";
            units.underwaterEnergyStorage = "CORUWES";
            units.underwaterFusion = "CORUWFUS";
        }
        else
        {
            units.metalExtractor = "ARMMEX";
            units.solar = "ARMSOLAR";
            units.lab = "ARMLAB";
            units.constructor = "ARMCK";
            units.raider = "ARMPW";
            units.rocketKbot = "ARMROCK";
            units.lightLaserTower = "ARMLLT";
            units.radar = "ARMRAD";
            units.metalMaker = "ARMMAKR";
            units.airPlant = "ARMAP";
            units.scoutPlane = "ARMPEEP";
            units.airTransport = "ARMATLAS";
            units.fighter = "ARMFIG";
            units.bomber = "ARMTHUND";
            units.airConstructor = "ARMCA";
            units.vehiclePlant = "ARMVP";
            units.scoutVehicle = "ARMFAV";
            units.tank = "ARMFLASH";
            units.missileTruck = "ARMSAM";
            units.mediumTank = "ARMSTUMP";
            units.artilleryKbot = "ARMHAM";
            units.dragonsTeeth = "ARMDRAG";
            units.antiAirTower = "ARMRL";
            units.antiAirKbot = "ARMJETH";
            units.advancedLab = "ARMALAB";
            units.advancedConstructor = "ARMACK";
            units.advancedAssault = "ARMZEUS";
            units.heavyLaserTower = "ARMHLT";
            units.heavyPlasmaTower = "ARMGUARD";
            units.advancedRadar = "ARMARAD";
            units.mohoExtractor = "ARMMOHO";
            units.fusion = "ARMFUS";
            units.shipyard = "ARMSY";
            units.constructionShip = "ARMCS";
            units.scoutShip = "ARMPT";
            units.destroyer = "ARMROY";
            units.seaTransport = "ARMTSHIP";
            units.submarine = "ARMSUB";
            units.advancedShipyard = "ARMASY";
            units.cruiser = "ARMCRUS";
            units.battleship = "ARMBATS";
            units.antiAirShip = "ARMAAS";
            units.advancedConstructionSub = "ARMACSUB";
            units.seaplanePlatform = "ARMPLAT";
            units.seaplaneFighter = "ARMSFIG";
            units.torpedoSeaplane = "ARMSEAP";
            units.tidalGenerator = "ARMTIDE";
            units.sonar = "ARMSONAR";
            units.torpedoLauncher = "ARMTL";
            units.underwaterMetalExtractor = "ARMUWMEX";
            units.floatingMetalMaker = "ARMFMKR";
            units.geothermal = "ARMGEO";
            units.metalStorage = "ARMMSTOR";
            units.energyStorage = "ARMESTOR";
            units.underwaterMetalStorage = "ARMUWMS";
            units.underwaterEnergyStorage = "ARMUWES";
            units.underwaterFusion = "ARMUWFUS";
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
        check(units.fighter);
        check(units.bomber);
        check(units.airConstructor);
        check(units.vehiclePlant);
        check(units.scoutVehicle);
        check(units.tank);
        check(units.missileTruck);
        check(units.mediumTank);
        check(units.artilleryKbot);
        check(units.dragonsTeeth);
        check(units.antiAirTower);
        check(units.antiAirKbot);
        check(units.advancedLab);
        check(units.advancedConstructor);
        check(units.advancedAssault);
        check(units.heavyLaserTower);
        check(units.heavyPlasmaTower);
        check(units.advancedRadar);
        check(units.mohoExtractor);
        check(units.fusion);
        check(units.shipyard);
        check(units.constructionShip);
        check(units.scoutShip);
        check(units.destroyer);
        check(units.seaTransport);
        check(units.submarine);
        check(units.advancedShipyard);
        check(units.cruiser);
        check(units.battleship);
        check(units.antiAirShip);
        check(units.advancedConstructionSub);
        check(units.seaplanePlatform);
        check(units.seaplaneFighter);
        check(units.torpedoSeaplane);
        check(units.tidalGenerator);
        check(units.sonar);
        check(units.torpedoLauncher);
        check(units.underwaterMetalExtractor);
        check(units.floatingMetalMaker);
        check(units.geothermal);
        check(units.metalStorage);
        check(units.energyStorage);
        check(units.underwaterMetalStorage);
        check(units.underwaterEnergyStorage);
        check(units.underwaterFusion);
        return units;
    }

    bool isAiScoutType(const AiSideUnits& units, const std::string& unitType)
    {
        return !unitType.empty() && (unitType == units.scoutPlane || unitType == units.scoutVehicle);
    }

    bool isAiAntiAirType(const AiSideUnits& units, const std::string& unitType)
    {
        return !unitType.empty() && unitType == units.antiAirKbot;
    }
}
