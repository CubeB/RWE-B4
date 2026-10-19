#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rwe/game/save_util.h>
#include <rwe/LoadingScene_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        struct Lab
        {
            GameSimulation sim{makeFlatTerrain(128, 128), 0, 0, 0};
            PlayerId owner{0};
            UnitId factory;
            UnitId product;
            std::string productType;
            Lab(bool core = false, bool spawn = true)
            {
                // Relevant totala1.hpi ARMALAB/CORALAB and ARMZEUS/CORCAN FBI
                // fields. Inert COB/model and death media isolate teardown.
                const std::string lab = core ? "CORALAB" : "ARMALAB";
                productType = core ? "CORCAN" : "ARMZEUS";
                UnitDefinition d{};
                d.objectName = lab;
                d.builder = true;
                d.buildTime = core ? 14000 : 13520;
                d.maxHitPoints = core ? 3170 : 3005;
                d.workerTime = 200;
                d.workerTimePerTick = 6;
                d.buildCostMetal = Metal(core ? 1972 : 2007);
                d.buildCostEnergy = Energy(core ? 3625 : 3277);
                d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{core ? 7u : 5u,6,10,0,0,10};
                d.yardMap = parseYardMap(core ? 7u : 5u, 6,
                    core ? "ooooooo ooooooo occccco ccccccc ccccccc oocccoo"
                         : "occco occco occco occco occco occco");
                sim.unitDefinitions[lab] = d;
                d = UnitDefinition{};
                d.objectName = productType;
                d.isMobile = true;
                d.buildTime = core ? 7500 : 5478;
                d.maxHitPoints = core ? 2800 : 875;
                d.buildCostMetal = Metal(core ? 420 : 267);
                d.buildCostEnergy = Energy(core ? 3500 : 2228);
                d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2,2,15,0,12,15};
                sim.unitDefinitions[productType] = d;
                auto script = makeEmptyCobScript();
                for (auto name : {lab, productType})
                {
                    sim.unitScriptDefinitions[name] = *script;
                    sim.unitModelDefinitions[name] = createUnitModelDefinition(10_ss, {});
                }
                if (!spawn)
                {
                    return;
                }
                owner = addPlayer(sim);
                factory = addUnitOfType(sim, lab, owner, SimVector(0_ss,0_ss,0_ss), script);
                product = addUnitOfType(sim, productType, owner, SimVector(0_ss,0_ss,0_ss), script);
                sim.getUnitState(factory).hitPoints = sim.unitDefinitions.at(lab).maxHitPoints;
                sim.getUnitState(factory).yardOpen = true;
                auto& frame = sim.getUnitState(product);
                frame.buildTimeCompleted = sim.unitDefinitions.at(productType).buildTime / 2;
                frame.hitPoints = sim.unitDefinitions.at(productType).maxHitPoints / 2;
                hold();
            }
            void hold()
            {
                sim.getUnitState(factory).factoryState = FactoryBehaviorStateBuilding{std::make_pair(product, std::nullopt)};
                sim.getUnitState(factory).buildQueue.emplace_back(productType, 2);
            }
            void checkCancelled()
            {
                const auto& frame = sim.getUnitState(product);
                REQUIRE(frame.isDead());
                REQUIRE_FALSE(std::get<UnitState::LifeStateDead>(frame.lifeState).leaveCorpse);
                REQUIRE(sim.getPlayer(owner).unitsLost == 1);
                REQUIRE(std::holds_alternative<FactoryBehaviorStateIdle>(sim.getUnitState(factory).factoryState));
                REQUIRE(sim.getUnitState(factory).buildQueue.empty());
                REQUIRE(sim.getPlayer(owner).metal == Metal(1000));
                REQUIRE(sim.getPlayer(owner).energy == Energy(1000));
                sim.deleteDeadUnits();
                REQUIRE_FALSE(sim.tryGetUnitState(product));
                REQUIRE_FALSE(sim.tryGetUnitState(factory));
            }
        };
    }

    TEST_CASE("Destroyed advanced kbot labs silently cancel their retained product", "[factory-death]")
    {
        const bool core = GENERATE(false, true);
        {
            Lab lab(core);
            SECTION("weapon destruction") { lab.sim.killUnit(lab.factory); }
            SECTION("self destruction") { lab.sim.selfDestructUnit(lab.factory); }
            SECTION("quiet deletion") { lab.sim.quietlyKillUnit(lab.factory); }
            SECTION("reclaim") { REQUIRE(lab.sim.reclaimUnitStep(lab.factory, lab.owner, lab.sim.getUnitState(lab.factory).hitPoints)); }
            // Reclaim credits the structure's metal separately from cancellation.
            if (lab.sim.getPlayer(lab.owner).unitsLost == 0)
            {
                REQUIRE(lab.sim.getUnitState(lab.product).isDead());
                lab.sim.deleteDeadUnits();
                REQUIRE_FALSE(lab.sim.tryGetUnitState(lab.product));
            }
            else { lab.checkCancelled(); }
        }
    }

    TEST_CASE("Factory product ownership follows the reference rather than completion percentage", "[factory-death]")
    {
        Lab lab;
        SECTION("creation already returned a product")
        {
            lab.sim.getUnitState(lab.factory).factoryState = FactoryBehaviorStateCreatingUnit{
                lab.productType, lab.owner, SimVector{}, SimAngle{}, UnitCreationStatusDone{lab.product}};
        }
        SECTION("completed but still held")
        {
            lab.sim.getUnitState(lab.product).buildTimeCompleted = lab.sim.unitDefinitions.at(lab.productType).buildTime;
        }
        SECTION("already destroyed product keeps its external death")
        {
            lab.sim.killUnit(lab.product);
            lab.sim.killUnit(lab.factory);
            REQUIRE(lab.sim.getPlayer(lab.owner).unitsLost == 2);
            return;
        }
        SECTION("released product still on the pad survives")
        {
            lab.sim.getUnitState(lab.factory).factoryState = FactoryBehaviorStateIdle{};
            lab.sim.getUnitState(lab.product).buildTimeCompleted = lab.sim.unitDefinitions.at(lab.productType).buildTime;
            lab.sim.killUnit(lab.factory);
            REQUIRE(lab.sim.getUnitState(lab.product).isAlive());
            return;
        }
        lab.sim.killUnit(lab.factory);
        lab.checkCancelled();
    }

    TEST_CASE("Restored factory references are cancelled and mobile construction remains", "[factory-death][saveload]")
    {
        Lab lab;
        SECTION("restored held reference")
        {
            const auto save = saveSimulationToJson(lab.sim);
            Lab restored(false, false);
            loadSimulationFromJson(save, restored.sim);
            restored.factory = lab.factory;
            restored.product = lab.product;
            lab.sim.killUnit(lab.factory);
            restored.sim.killUnit(restored.factory);
            REQUIRE(computeHashOf(lab.sim) == computeHashOf(restored.sim));
            restored.checkCancelled();
        }
        SECTION("mobile builder frame is not a factory product")
        {
            lab.sim.getUnitState(lab.factory).factoryState = FactoryBehaviorStateIdle{};
            // Shipped ARMCK mobile constructor (totala1.hpi), inert media.
            auto builder = lab.sim.unitDefinitions.at(lab.productType);
            builder.objectName = "ARMCK";
            builder.builder = builder.isMobile = true;
            builder.buildTime = 5597;
            builder.maxHitPoints = 700;
            builder.workerTime = 80;
            builder.workerTimePerTick = 2;
            builder.buildCostMetal = Metal(120);
            builder.buildCostEnergy = Energy(2410);
            lab.sim.unitDefinitions["ARMCK"] = builder;
            lab.sim.unitModelDefinitions["ARMCK"] = createUnitModelDefinition(10_ss, {});
            lab.sim.unitScriptDefinitions["ARMCK"] = *makeEmptyCobScript();
            auto& mobile = lab.sim.getUnitState(lab.factory);
            mobile.unitType = "ARMCK";
            mobile.buildTimeCompleted = 5597;
            mobile.hitPoints = 700;
            mobile.buildOrderUnitId = lab.product;
            lab.sim.killUnit(lab.factory);
            REQUIRE(lab.sim.getUnitState(lab.product).isAlive());
            REQUIRE(lab.sim.getUnitState(lab.product).isBeingBuilt(lab.sim.unitDefinitions.at(lab.productType)));
        }
    }
}
