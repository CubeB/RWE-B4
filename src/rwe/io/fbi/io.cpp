#include "io.h"
#include <rwe/util/OpaqueId_io.h>

namespace rwe
{
    UnitFbi parseUnitFbi(const TdfBlock& tdf)
    {
        auto infoBlock = tdf.findBlock("UNITINFO");
        if (!infoBlock)
        {
            throw std::runtime_error("FBI missing UNITINFO block");
        }

        return parseUnitInfoBlock(*infoBlock);
    }

    UnitFbi parseUnitInfoBlock(const TdfBlock& tdf)
    {
        UnitFbi u;

        tdf.read("UnitName", u.unitName);
        tdf.read("Objectname", u.objectName);
        tdf.read("SoundCategory", u.soundCategory);

        tdf.readOrDefault("MovementClass", u.movementClass);

        tdf.readOrDefault("Name", u.name);
        tdf.readOrDefault("Description", u.description);

        // AI-relevant classification fields. Defaults (empty string / 0)
        // mean "not present in the FBI"; consumer code (UnitClassifier,
        // future LOS system) must treat empty/0 as "fall back to other
        // signals" rather than as a meaningful zero.
        tdf.readOrDefault("TEDClass", u.tedClass);
        tdf.readOrDefault("Category", u.category);

        // Target preferences. The original also accepts an unprefixed
        // BadTargetCategory in three quarters of the shipped units, but that
        // key exists nowhere in TotalA.exe — only the three slot-prefixed
        // forms are read — so neither do we.
        tdf.readOrDefault("wpri_badTargetCategory", u.wpriBadTargetCategory);
        tdf.readOrDefault("wsec_badTargetCategory", u.wsecBadTargetCategory);
        tdf.readOrDefault("wspe_badTargetCategory", u.wspeBadTargetCategory);
        tdf.readOrDefault("NoChaseCategory", u.noChaseCategory);
        tdf.readOrDefault("ShootMe", u.shootMe);
        tdf.readOrDefault("SightDistance", u.sightDistance, 0u);
        tdf.readOrDefault("RadarDistance", u.radarDistance, 0u);
        tdf.readOrDefault("SonarDistance", u.sonarDistance, 0u);

        tdf.readOrDefault("TurnRate", u.turnRate);
        tdf.readOrDefault("MaxVelocity", u.maxVelocity);
        tdf.readOrDefault("Acceleration", u.acceleration);
        tdf.readOrDefault("BrakeRate", u.brakeRate);

        tdf.readOrDefault("FootprintX", u.footprintX);
        tdf.readOrDefault("FootprintZ", u.footprintZ);

        tdf.readOrDefault("MaxSlope", u.maxSlope, 255u);
        tdf.readOrDefault("MaxWaterSlope", u.maxWaterSlope, u.maxSlope);
        tdf.readOrDefault("MinWaterDepth", u.minWaterDepth);
        tdf.readOrDefault("MaxWaterDepth", u.maxWaterDepth);
        if (u.maxWaterDepth < u.minWaterDepth)
        {
            // Ships and shipyards give only MinWaterDepth; TA treats the
            // missing maximum as "any depth", not zero.
            u.maxWaterDepth = 255u;
        }

        tdf.readOrDefault("CanAttack", u.canAttack);
        tdf.readOrDefault("CanMove", u.canMove);
        tdf.readOrDefault("CanGuard", u.canGuard);
        tdf.readOrDefault("CanCapture", u.canCapture);
        tdf.readOrDefault("Cloakable", u.cloakable);

        tdf.readOrDefault("MobileStandOrders", u.mobileStandOrders, false);
        tdf.readOrDefault("FireStandOrders", u.fireStandOrders, false);
        tdf.readOrDefault("StandingMoveOrder", u.standingMoveOrder, 2u);
        tdf.readOrDefault("StandingFireOrder", u.standingFireOrder, 2u);

        tdf.readOrDefault("Commander", u.commander);

        tdf.readOrDefault("MaxDamage", u.maxDamage);
        tdf.readOrDefault("DamageModifier", u.damageModifier, 1.0f);

        tdf.readOrDefault("BMCode", u.bmCode);

        tdf.readOrDefault("Floater", u.floater);
        tdf.readOrDefault("CanHover", u.canHover);

        tdf.readOrDefault("CanFly", u.canFly);
        tdf.readOrDefault("TransportCapacity", u.transportCapacity);
        tdf.readOrDefault("TransportSize", u.transportSize);

        tdf.readOrDefault("CruiseAlt", u.cruiseAlt);
        tdf.readOrDefault("HoverAttack", u.hoverAttack);
        tdf.readOrDefault("AttackRunLength", u.attackRunLength);
        tdf.readOrDefault("ManeuverLeashLength", u.maneuverLeashLength);
        // The original defaults this to one, so an aircraft whose FBI is
        // silent still banks like an aircraft.
        tdf.readOrDefault("BankScale", u.bankScale, 1.0f);

        tdf.readOrDefault("Weapon1", u.weapon1);
        tdf.readOrDefault("Weapon2", u.weapon2);
        tdf.readOrDefault("Weapon3", u.weapon3);

        tdf.readOrDefault("ExplodeAs", u.explodeAs);
        tdf.readOrDefault("SelfDestructAs", u.selfDestructAs);

        tdf.readOrDefault("Builder", u.builder);

        tdf.readOrDefault("BuildTime", u.buildTime);
        tdf.readOrDefault("BuildCostEnergy", u.buildCostEnergy);
        tdf.readOrDefault("BuildCostMetal", u.buildCostMetal);

        tdf.readOrDefault("WorkerTime", u.workerTime);
        tdf.readOrDefault("Builddistance", u.buildDistance);
        tdf.readOrDefault("BuildAngle", u.buildAngle, 0u);

        tdf.readOrDefault("onoffable", u.onOffable);
        tdf.readOrDefault("ActivateWhenBuilt", u.activateWhenBuilt);

        tdf.readOrDefault("EnergyMake", u.energyMake);
        tdf.readOrDefault("MetalMake", u.metalMake);
        tdf.readOrDefault("EnergyUse", u.energyUse);
        tdf.readOrDefault("MetalUse", u.metalUse);
        tdf.readOrDefault("ExtractsMetal", u.extractsMetal);
        tdf.readOrDefault("EnergyStorage", u.energyStorage);
        tdf.readOrDefault("MetalStorage", u.metalStorage);
        tdf.readOrDefault("WindGenerator", u.windGenerator);

        tdf.readOrDefault("MakesMetal", u.makesMetal);

        tdf.readOrDefault("HideDamage", u.hideDamage, false);
        tdf.readOrDefault("ShowPlayerName", u.showPlayerName, false);

        tdf.readOrDefault("YardMap", u.yardMap);

        tdf.readOrDefault("Corpse", u.corpse);

        return u;
    }
}
