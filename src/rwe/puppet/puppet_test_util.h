#pragma once

#include <memory>
#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

namespace rwe
{
    /** The three types a puppet fixture records: a walker, a builder, a product. */
    inline std::vector<std::string> puppetTestLoadOrder()
    {
        return {"KBOT", "SOLAR", "TANK"};
    }

    inline UnitDefinition makePuppetWalkerDef()
    {
        UnitDefinition d{};
        d.objectName = "model";
        d.isMobile = true;
        d.canMove = true;
        d.maxVelocity = 2_ss;
        d.acceleration = 0.2_ssf;
        d.brakeRate = 0.2_ssf;
        d.turnRate = 4000_ss;
        d.maxHitPoints = 100;
        d.buildTime = 0u;
        d.corpse = "HULK";
        d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        return d;
    }

    inline void definePuppetTestWorld(GameSimulation& sim)
    {
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        sim.unitDefinitions["TANK"] = makePuppetWalkerDef();

        UnitDefinition builder{};
        builder.objectName = "model";
        builder.isMobile = true;
        builder.canMove = true;
        builder.builder = true;
        builder.workerTimePerTick = 3u;
        builder.buildDistance = 200_ss;
        builder.maxHitPoints = 100;
        builder.buildTime = 0u;
        builder.maxVelocity = 3_ss;
        builder.acceleration = 1_ss;
        builder.brakeRate = 1_ss;
        builder.turnRate = 1000_ss;
        builder.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["KBOT"] = builder;

        UnitDefinition solar{};
        solar.objectName = "model";
        solar.isMobile = false;
        solar.canMove = false;
        solar.maxHitPoints = 100;
        solar.buildTime = 300u;
        solar.buildCostMetal = Metal(1.0f);
        solar.buildCostEnergy = Energy(1.0f);
        solar.corpse = "HULK";
        solar.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
        solar.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
        sim.unitDefinitions["SOLAR"] = solar;

        auto script = *makeEmptyCobScript({"base"});
        sim.unitScriptDefinitions["TANK"] = script;
        sim.unitScriptDefinitions["KBOT"] = script;
        sim.unitScriptDefinitions["SOLAR"] = script;

        FeatureDefinition hulk{};
        hulk.name = "HULK";
        hulk.footprintX = 2;
        hulk.footprintZ = 2;
        hulk.height = 20_ss;
        hulk.blocking = true;
        hulk.reclaimable = true;
        hulk.metal = 100;
        hulk.damage = 1000;
        auto hulkId = sim.featureDefinitions.insert(std::move(hulk));
        sim.featureNameIndex.insert_or_assign("HULK", hulkId);
    }

    inline std::optional<UnitId> findUnitOfType(const GameSimulation& sim, const std::string& type)
    {
        for (const auto& entry : sim.units)
        {
            if (entry.second.unitType == type)
            {
                return UnitId(entry.first);
            }
        }
        return std::nullopt;
    }
}
