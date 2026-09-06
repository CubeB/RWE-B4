#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

/**
 * Automatic reclaim: what a construction unit picks up without being told to.
 *
 * The original has exactly one of these. `0x47EA40` is the area scan that
 * reads a feature's `autoreclaimable` bit, and its only two callers are the
 * ground `RepairPatrol` handler (`0x405B93`) and `VTOL_RepairPatrol`
 * (`0x41564F`). There is no area-reclaim command in the binary and no
 * idle-builder sweep, so a patrol is the whole of it. See TOTALA-EXE.md §97.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** `armsolar_dead` from `features/Corpses/arm_corpses.tdf`. */
        FeatureDefinition makeWreckDef()
        {
            FeatureDefinition d{};
            d.name = "armsolar_dead";
            d.footprintX = 5;
            d.footprintZ = 5;
            d.height = 40_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 116;
            d.energy = 0;
            d.damage = 261;
            d.blocking = true;
            return d;
        }

        /**
         * `Fortification` from `features/All Worlds/Armfort_fort.tdf` -- the
         * Arm fortification wall, one of only two things in the shipped data
         * that say `autoreclaimable=0`. It carries metal and may be reclaimed
         * on command; it must simply never be picked up by itself, or a
         * patrolling builder would eat its owner's own walls.
         */
        FeatureDefinition makeWallDef()
        {
            FeatureDefinition d{};
            d.name = "fortification";
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 55_ss;
            d.reclaimable = true;
            d.autoreclaimable = false;
            d.metal = 23;
            d.energy = 0;
            d.damage = 1600;
            d.blocking = true;
            return d;
        }

        /** An acid plant from `features/Acid/AcidPlants.tdf`: energy, no metal. */
        FeatureDefinition makePlantDef()
        {
            FeatureDefinition d{};
            d.name = "acidplant";
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 180_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 0;
            d.energy = 525;
            d.damage = 5;
            d.blocking = false;
            return d;
        }

        /** ARMCK.FBI, the Arm construction kbot. */
        UnitDefinition makeConstructionKbotDef()
        {
            UnitDefinition d{};
            d.objectName = "armck";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.canReclamate = true;
            d.workerTimePerTick = 80u / 30u;
            d.maxHitPoints = 700;
            d.sightDistance = 235u;
            d.buildDistance = 40_ss;
            d.buildTime = 0u;
            d.maxVelocity = 0.8_ssf;
            d.acceleration = 0.12_ssf;
            d.brakeRate = 0.24_ssf;
            d.turnRate = 1020_ss;
            d.energyStorage = Energy(50.0f);
            d.metalStorage = Metal(50.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeStorageDef(float metalStorage, float energyStorage)
        {
            UnitDefinition d{};
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.metalStorage = Metal(metalStorage);
            d.energyStorage = Energy(energyStorage);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * A player's storage capacity is not a fixture's to declare: the
         * economy rebuilds it every second out of what the player's units
         * carry. So these tests own the two shipped storage buildings --
         * ARMMSTOR (`MetalStorage=1000`) and ARMESTOR (`EnergyStorage=3000`)
         * -- parked out of the way, which with the construction kbot's own
         * fifty of each puts the totals at 1050 and 3050.
         */
        void addStorageBuildings(GameSimulation& sim, PlayerId owner, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["armmstor"] = makeStorageDef(1000.0f, 0.0f);
            sim.unitDefinitions["armestor"] = makeStorageDef(0.0f, 3000.0f);
            addUnitOfType(sim, "armmstor", owner, SimVector(900_ss, 0_ss, 900_ss), script);
            addUnitOfType(sim, "armestor", owner, SimVector(950_ss, 0_ss, 900_ss), script);
        }

        constexpr float MetalCapacity = 1050.0f;
        constexpr float EnergyCapacity = 3050.0f;

        PlayerId addPlayerHolding(GameSimulation& sim, float metal, float energy)
        {
            GamePlayerInfo p{
                std::optional<std::string>("builder"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(MetalCapacity),
                Energy(EnergyCapacity),
                Metal(MetalCapacity),
                Energy(EnergyCapacity),
            };
            return sim.addPlayer(p);
        }

        /**
         * A script with nothing in it but a `StopBuilding` that counts how
         * many times it has been run. Stowing the nanolathe is the whole of
         * what the engine asks a construction unit's script to do when a job
         * ends -- ARMCK's is `start-script RequestState( INACTIVE )` and
         * nothing else -- so the count is the reported symptom, measured.
         */
        std::shared_ptr<CobScript> makeStowCountingScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 1;
            script->functions.push_back(CobFunctionInfo{"StopBuilding", 0u});
            script->instructions = {
                static_cast<uint32_t>(OpCode::PUSH_STATIC),
                0u,
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                1u,
                static_cast<uint32_t>(OpCode::ADD),
                static_cast<uint32_t>(OpCode::POP_STATIC),
                0u,
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                0u,
                static_cast<uint32_t>(OpCode::RETURN)};
            return script;
        }

        int stowCount(GameSimulation& sim, UnitId id)
        {
            return sim.getUnitState(id).cobEnvironment->_statics.at(0);
        }

        UnitId addPatrollingKbot(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["armck"] = makeConstructionKbotDef();
            auto unitId = addUnitOfType(sim, "armck", owner, pos, script);
            // Normally set by the COB script's StartBuilding thread; the
            // empty test script has none, so pretend the arm is deployed.
            sim.getUnitState(unitId).inBuildStance = true;
            // A route that is already at its own destination, so the only
            // thing that can make the kbot move is a wreck it decides to go
            // and clear.
            sim.getUnitState(unitId).orders.push_back(PatrolOrder(pos));
            return unitId;
        }
    }

    TEST_CASE("a construction unit on patrol clears wreckage it can reach", "[reclaim][autoreclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerHolding(sim, 0.0f, 0.0f);
        addStorageBuildings(sim, player, script);

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        auto kbotId = addPatrollingKbot(sim, player, wreckPosition + SimVector(30_ss, 0_ss, 0_ss), script);

        for (int i = 0; i < 300 && sim.tryGetFeature(wreckId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetFeature(wreckId).has_value());
        REQUIRE(sim.getPlayer(player).metal.value > 0.0f);
        // The route is still there underneath: patrol never finishes.
        REQUIRE_FALSE(sim.getUnitState(kbotId).orders.empty());
    }

    TEST_CASE("autoreclaimable=0 keeps a feature out of the automatic sweep", "[reclaim][autoreclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerHolding(sim, 0.0f, 0.0f);
        addStorageBuildings(sim, player, script);

        auto wallDef = sim.featureDefinitions.insert(makeWallDef());
        auto wallId = sim.addFeature(wallDef, 20, 20).value();
        auto wallPosition = sim.getFeature(wallId).position;

        auto kbotId = addPatrollingKbot(sim, player, wallPosition + SimVector(30_ss, 0_ss, 0_ss), script);

        SECTION("a patrolling builder walks past its own fortification wall")
        {
            for (int i = 0; i < 300; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.tryGetFeature(wallId).has_value());
            REQUIRE(sim.getPlayer(player).metal.value == 0.0f);
        }

        SECTION("but will still take it down when told to")
        {
            // The bit gates the scan (0x47EB1B) and nothing else: `reclaimable`
            // alone is what the Reclaim mission itself tests (0x404B2A).
            auto& kbot = sim.getUnitState(kbotId);
            kbot.orders.clear();
            kbot.orders.push_back(ReclaimOrder(wallId));

            for (int i = 0; i < 600 && sim.tryGetFeature(wallId); ++i)
            {
                sim.tick();
            }

            REQUIRE_FALSE(sim.tryGetFeature(wallId).has_value());
        }
    }

    TEST_CASE("the automatic sweep reaches no further than the unit's own sight", "[reclaim][autoreclaim]")
    {
        // 0x405B74 reads def+0x202 -- SightDistance -- and hands it to the
        // scan as the radius.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerHolding(sim, 0.0f, 0.0f);
        addStorageBuildings(sim, player, script);

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        SECTION("something inside 235 units is picked up")
        {
            addPatrollingKbot(sim, player, wreckPosition + SimVector(200_ss, 0_ss, 0_ss), script);
            for (int i = 0; i < 600 && sim.tryGetFeature(wreckId); ++i)
            {
                sim.tick();
            }
            REQUIRE_FALSE(sim.tryGetFeature(wreckId).has_value());
        }

        SECTION("something beyond it is not")
        {
            addPatrollingKbot(sim, player, wreckPosition + SimVector(400_ss, 0_ss, 0_ss), script);
            for (int i = 0; i < 600; ++i)
            {
                sim.tick();
            }
            REQUIRE(sim.tryGetFeature(wreckId).has_value());
        }
    }

    TEST_CASE("a patrolling builder only reclaims into a store with room in it", "[reclaim][autoreclaim]")
    {
        // 0x405B18-0x405B54: the scan is not run at all unless one of the two
        // stores is under a fifth of its capacity, and the same comparison
        // picks which of the two candidates gets taken (0x405BA0 onwards).
        auto script = makeEmptyCobScript();

        SECTION("full stores mean the wreck field is left alone")
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
            auto player = addPlayerHolding(sim, MetalCapacity, EnergyCapacity);
            addStorageBuildings(sim, player, script);
            auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
            auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
            auto wreckPosition = sim.getFeature(wreckId).position;
            addPatrollingKbot(sim, player, wreckPosition + SimVector(30_ss, 0_ss, 0_ss), script);

            for (int i = 0; i < 300; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.tryGetFeature(wreckId).has_value());
        }

        SECTION("a builder short of energy goes for the energy, not the metal")
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
            auto player = addPlayerHolding(sim, MetalCapacity, 0.0f);
            addStorageBuildings(sim, player, script);

            auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
            auto plantDef = sim.featureDefinitions.insert(makePlantDef());
            auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
            auto plantId = sim.addFeature(plantDef, 20, 25).value();
            auto wreckPosition = sim.getFeature(wreckId).position;
            addPatrollingKbot(sim, player, wreckPosition + SimVector(30_ss, 0_ss, 0_ss), script);

            for (int i = 0; i < 800 && sim.tryGetFeature(plantId); ++i)
            {
                sim.tick();
            }

            // The plant goes; the wreck stands, because there is nowhere to
            // put its metal.
            REQUIRE_FALSE(sim.tryGetFeature(plantId).has_value());
            REQUIRE(sim.tryGetFeature(wreckId).has_value());
        }
    }

    TEST_CASE("a job a patrolling builder has begun is not given up half way", "[reclaim][autoreclaim]")
    {
        // The original does not leave the choice to a scan that runs again
        // every tick. `RepairPatrol` picks a candidate once and then builds a
        // RECLAIM mission for it -- 0x405CA2 allocates the 0x56 bytes,
        // 0x405D0A names it from `0x5016C4` ("RECLAIM"), 0x405D11 initialises
        // it and 0x405D18 pushes it in front of the patrol -- before resetting
        // its own state at 0x405D1D and standing down. That mission owns the
        // unit until the feature is gone, so neither the scan nor the two
        // store comparisons that gate it are consulted again while the job
        // runs.
        //
        // RWE re-decided every tick, and a target that changes under a job in
        // progress takes the builder through UnitBehaviorStateIdle, which
        // runs StopBuilding: the nanolathe goes away and comes back out for
        // the new target. Reclaiming metal is enough on its own to trigger it,
        // because the payout is credited as the work is done and the fifth of
        // capacity that opened the scan (S:97) is crossed part-way through
        // the very wreck being reclaimed -- at which point the metal
        // candidate gives way to the energy one.
        auto script = makeStowCountingScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

        // A fifth of 1050 is 210. Starting at 150 with the wreck worth 116,
        // the store crosses the line about half way through the job.
        auto player = addPlayerHolding(sim, 150.0f, 0.0f);
        addStorageBuildings(sim, player, script);

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto plantDef = sim.featureDefinitions.insert(makePlantDef());
        auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
        auto plantId = sim.addFeature(plantDef, 20, 25).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        auto kbotId = addPatrollingKbot(sim, player, wreckPosition + SimVector(30_ss, 0_ss, 0_ss), script);

        // Sampled at the top of each pass, so the stow that ends the job
        // itself is never counted -- only one that happens while there is
        // still wreckage in front of the builder.
        int stowsMidJob = 0;
        for (int i = 0; i < 600; ++i)
        {
            if (!sim.tryGetFeature(wreckId))
            {
                break;
            }
            stowsMidJob = stowCount(sim, kbotId);
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetFeature(wreckId).has_value());
        // It never put the arm away in the middle of the job...
        REQUIRE(stowsMidJob == 0);
        // ...and it finished what it started before going on to the plant.
        REQUIRE(sim.tryGetFeature(plantId).has_value());
    }
}
