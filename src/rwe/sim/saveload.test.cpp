#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/util/OpaqueId_io.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 128, int height = 128)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
            };
            return sim.addPlayer(p);
        }

        /**
         * A script with one function, "Loop", that keeps a couple of values
         * parked on its stack, sleeps 200ms at a time and counts wakeups into
         * static 0 forever. Exercises the parts of the COB VM a save has to
         * carry: statics, a live stack, a call stack and the sleeping queue.
         */
        CobScript makeLoopScript()
        {
            CobScript script;
            script.staticVariableCount = 1;
            script.pieces.push_back("base");
            script.functions.push_back(CobFunctionInfo{"Loop", 0});
            auto op = [](OpCode c) { return static_cast<uint32_t>(c); };
            script.instructions = {
                op(OpCode::PUSH_CONSTANT), 42u,
                // loop: (instruction index 2)
                op(OpCode::PUSH_CONSTANT), 200u,
                op(OpCode::SLEEP),
                op(OpCode::PUSH_STATIC), 0u,
                op(OpCode::PUSH_CONSTANT), 1u,
                op(OpCode::ADD),
                op(OpCode::POP_STATIC), 0u,
                op(OpCode::JUMP), 2u};
            return script;
        }

        void registerModel(GameSimulation& sim)
        {
            // The one piece doubles as the firing point, so it sits up in the
            // air the way the turret fixtures put it: a muzzle at ground level
            // buries the round on the tick it spawns.
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makeMobileDef(unsigned int footprint)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.energyStorage = Energy(500.0f);
            d.metalStorage = Metal(500.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeLauncherDef()
        {
            auto d = makeMobileDef(1u);
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 1000u;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            return d;
        }

        UnitDefinition makeVictimDef()
        {
            auto d = makeMobileDef(2u);
            d.canMove = false;
            d.maxHitPoints = 1000000;
            d.shootMe = true;
            return d;
        }

        UnitDefinition makeTransportDef()
        {
            auto d = makeMobileDef(2u);
            d.canLoad = true;
            d.transportCapacity = 1;
            d.transportSize = 2;
            return d;
        }

        /**
         * ARMTRUCK_ROCKET's shape: no launch speed, a long vertical climb on
         * weapontimer, then guided flight. Slow enough that a round fired in
         * the first few ticks is still in the air when the test saves at tick
         * 100, and lands during the replayed second hundred.
         */
        void defineMissileWeapon(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 800_ss;
            w.reloadTime = 1000_ss;
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 350_ss / 30_ss;
            w.damageRadius = 48_ss;
            w.damage["DEFAULT"] = 250;
            w.weaponTimer = GameTime(150);
            w.randomDecay = GameTime(0);
            w.turret = true;
            w.tolerance = SimAngle(32767u);
            w.pitchTolerance = SimAngle(32767u);

            ProjectilePhysicsTypeSelfPropelled p;
            p.startVelocity = 0_ss;
            p.acceleration = 40_ss / 900_ss;
            p.maxVelocity = 350_ss / 30_ss;
            p.turnRate = SimAngle(24384u / 30u);
            p.guidance = true;
            p.tracks = false;
            p.twoPhase = true;
            p.vLaunch = true;
            p.flightTime = GameTime(300);
            p.burnBlow = false;
            p.cruise = false;
            p.autoRange = false;
            w.physicsType = p;

            sim.weaponDefinitions["MISSILE"] = w;
        }

        FeatureDefinition makeTreeDef(const std::string& name, bool flammable)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 20_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.energy = 250;
            d.damage = 100;
            d.flamable = flammable;
            // Burns much longer than the test runs, sparking every second, so
            // the fire is alive on both sides of the save and keeps drawing
            // from the (saved and restored) RNG all through the comparison.
            d.burnMin = 60;
            d.burnMax = 90;
            d.sparkTime = 1;
            d.spreadChance = 100;
            return d;
        }

        /**
         * A fresh simulation for "the map": definitions loaded, scripts
         * registered, the map's initial features standing, and nothing else.
         * Both the played sim and the load target start from exactly this.
         */
        GameSimulation makeBaseSim()
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 100, 3000);

            registerModel(sim);
            sim.unitDefinitions["LAUNCHER"] = makeLauncherDef();
            sim.unitDefinitions["VICTIM"] = makeVictimDef();
            sim.unitDefinitions["TANK"] = makeMobileDef(2u);
            sim.unitDefinitions["KBOT"] = makeMobileDef(2u);
            sim.unitDefinitions["TRANSPORT"] = makeTransportDef();
            defineMissileWeapon(sim);

            auto loopScript = makeLoopScript();
            sim.unitScriptDefinitions["LAUNCHER"] = loopScript;
            sim.unitScriptDefinitions["VICTIM"] = loopScript;
            sim.unitScriptDefinitions["TANK"] = loopScript;
            sim.unitScriptDefinitions["KBOT"] = loopScript;
            sim.unitScriptDefinitions["TRANSPORT"] = loopScript;

            auto crispDef = sim.featureDefinitions.insert(makeTreeDef("crisp", false));
            auto treeDef = sim.featureDefinitions.insert(makeTreeDef("tree", true));
            sim.featureDefinitions.get(treeDef).featureBurnt = crispDef;
            auto rockDef = sim.featureDefinitions.insert(makeTreeDef("rock", false));

            // The map's initial features.
            sim.addFeature(treeDef, 40, 40).value();
            sim.addFeature(treeDef, 42, 40).value();
            sim.addFeature(rockDef, 90, 90).value();

            return sim;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos)
        {
            auto env = std::make_unique<CobEnvironment>(&sim.unitScriptDefinitions.at(unitType));
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /**
         * A little battlefield with most kinds of live state in play: a
         * missile launcher engaging a victim (a projectile spends the whole
         * test in the air), a tank driving a long move order with a COB
         * thread ticking away, a kbot on a patrol loop, a transport carrying
         * cargo across the map, and a feature on fire.
         */
        void buildScenario(GameSimulation& sim)
        {
            auto us = addPlayer(sim, "us");
            auto them = addPlayer(sim, "them");

            auto launcherId = spawnUnit(sim, "LAUNCHER", us, SimVector(0_ss, 0_ss, 0_ss));
            UnitWeapon missile;
            missile.weaponType = "MISSILE";
            sim.getUnitState(launcherId).weapons[0] = missile;
            sim.getUnitState(launcherId).fireOrders = UnitFireOrders::FireAtWill;

            // Close enough to be seen: line of sight is capped at eight vision
            // cells (256 world units) whatever SightDistance says.
            spawnUnit(sim, "VICTIM", them, SimVector(0_ss, 0_ss, 200_ss));

            auto tankId = spawnUnit(sim, "TANK", us, SimVector(-200_ss, 0_ss, -200_ss));
            sim.getUnitState(tankId).orders.push_back(createMoveOrder(SimVector(400_ss, 0_ss, 400_ss)));
            sim.getUnitState(tankId).cobEnvironment->createThread("Loop", std::vector<int>());

            auto kbotId = spawnUnit(sim, "KBOT", us, SimVector(-300_ss, 0_ss, -100_ss));
            sim.getUnitState(kbotId).orders.push_back(PatrolOrder(SimVector(100_ss, 0_ss, -100_ss)));
            sim.getUnitState(kbotId).orders.push_back(PatrolOrder(SimVector(-300_ss, 0_ss, -100_ss)));

            auto transportId = spawnUnit(sim, "TRANSPORT", us, SimVector(200_ss, 0_ss, -200_ss));
            auto cargoId = spawnUnit(sim, "KBOT", us, SimVector(240_ss, 0_ss, -200_ss));
            REQUIRE(sim.loadUnitIntoTransport(transportId, cargoId, ""));
            sim.getUnitState(transportId).orders.push_back(createMoveOrder(SimVector(-200_ss, 0_ss, 200_ss)));

            // Set the first tree alight.
            for (const auto& [featureId, feature] : sim.features)
            {
                if (sim.getFeatureDefinition(feature.featureName).flamable)
                {
                    sim.igniteFeature(FeatureId(featureId.value));
                    break;
                }
            }
        }
    }

    TEST_CASE("a saved simulation replays tick for tick from its save", "[saveload]")
    {
        auto simA = makeBaseSim();
        buildScenario(simA);

        for (int i = 0; i < 100; ++i)
        {
            simA.tick();
        }

        auto saved = saveSimulationToJson(simA);

        auto simB = makeBaseSim();
        loadSimulationFromJson(saved, simB);

        REQUIRE(computeHashOf(simA) == computeHashOf(simB));

        // Saving the loaded sim reproduces the save byte for byte: nothing
        // was lost in the round trip, and nothing depended on the ids the
        // original happened to hold.
        REQUIRE(saveSimulationToJson(simB) == saved);

        // The projectile really is mid-flight over the save.
        {
            bool anyProjectile = false;
            for ([[maybe_unused]] const auto& p : simB.projectiles)
            {
                anyProjectile = true;
            }
            REQUIRE(anyProjectile);
        }

        // The two simulations now diverge if and only if the save missed
        // something, and the per-tick hash pins down the tick where it did.
        for (int i = 0; i < 100; ++i)
        {
            simA.tick();
            simB.tick();
            INFO("tick " << i << " after the save");
            REQUIRE(computeHashOf(simA) == computeHashOf(simB));
        }
    }

    TEST_CASE("the save carries state nothing hashes", "[saveload]")
    {
        // The round trip above compares the whole save byte for byte, which
        // catches a field nobody serialised -- but only if the scenario gives
        // that field a value to lose. None of the state added in September is
        // hashed, so the replay check cannot see it either: a missing
        // serialiser would simply reload as the default and the two
        // simulations would agree.
        //
        // So this sets each of them deliberately and looks for it on the
        // other side. CLAUDE.md calls this the discipline unhashed state
        // needs, and it is the whole of it.
        auto simA = makeBaseSim();
        buildScenario(simA);
        simA.tick();

        // buildScenario has already made two players; this joins theirs.
        auto us = PlayerId(0);
        auto markedId = spawnUnit(simA, "TANK", us, SimVector(300_ss, 0_ss, 300_ss));
        {
            auto& marked = simA.getUnitState(markedId);

            // A commandfire weapon that has just fired: what ends a D-gun order.
            marked.commandFireShotFired = true;

            // A chase the unit started for itself, which gives up at the leash.
            marked.orders.push_back(AttackOrder(markedId, AttackLeash(SimVector(11_ss, 0_ss, 22_ss), 640_ss)));
        }

        auto deadId = spawnUnit(simA, "KBOT", us, SimVector(360_ss, 0_ss, 300_ss));
        simA.getUnitState(deadId).lifeState = UnitState::LifeStateDead{true, 3};

        auto saved = saveSimulationToJson(simA);

        auto simB = makeBaseSim();
        loadSimulationFromJson(saved, simB);

        // Byte for byte, as above -- with something in each field to lose.
        REQUIRE(saveSimulationToJson(simB) == saved);

        // And named individually, so a failure says which one went missing
        // rather than pointing at a diff of the whole save.
        const auto& markedB = simB.getUnitState(markedId);
        REQUIRE(markedB.commandFireShotFired);

        REQUIRE(!markedB.orders.empty());
        const auto* attack = std::get_if<AttackOrder>(&markedB.orders.front());
        REQUIRE(attack != nullptr);
        REQUIRE(attack->leash.has_value());
        REQUIRE(attack->leash->distance == 640_ss);
        REQUIRE(attack->leash->anchor == SimVector(11_ss, 0_ss, 22_ss));

        const auto* deadB = std::get_if<UnitState::LifeStateDead>(&simB.getUnitState(deadId).lifeState);
        REQUIRE(deadB != nullptr);
        REQUIRE(deadB->corpseLevel == 3);
    }
}
