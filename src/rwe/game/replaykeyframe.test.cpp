#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/OpaqueId_io.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(128, 128, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeTankDef()
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
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            // A wreck, so a death writes to the feature table the way it
            // does in a real game.
            d.corpse = "TANK_DEAD";
            return d;
        }

        FeatureDefinition makeFeatureDef(const std::string& name)
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
            return d;
        }

        /**
         * A fresh simulation for "the map": definitions loaded, a couple of
         * features standing, nothing else. The same base the viewer's scene
         * would have, and the state a load is applied over.
         */
        GameSimulation makeBaseSim(const std::shared_ptr<CobScript>& script)
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 100, 3000);
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
            sim.unitDefinitions["TANK"] = makeTankDef();
            sim.unitScriptDefinitions["TANK"] = *script;
            // The name index is the loading pipeline's, not the table's, so
            // it is filled by hand here or the corpse lookup finds nothing.
            auto deadDef = sim.featureDefinitions.insert(makeFeatureDef("TANK_DEAD"));
            sim.featureNameIndex["TANK_DEAD"] = deadDef;
            auto rockDef = sim.featureDefinitions.insert(makeFeatureDef("rock"));
            sim.featureNameIndex["ROCK"] = rockDef;
            sim.addFeature(rockDef, 40, 40).value();
            sim.addFeature(rockDef, 90, 90).value();
            sim.addFeature(rockDef, 60, 100).value();
            return sim;
        }

        /**
         * What a replay would feed the game at each tick, applied straight to
         * the simulation since there is no scene here. Spawns, kills, and
         * feature changes are arranged so that the id tables have holes in
         * them at the keyframe and refill them afterwards: that is the case
         * a snapshot that hands out fresh ids gets wrong, because a slot
         * freed before the snapshot is where the next arrival after it goes.
         *
         * The first run records the ids it was handed; the second requires
         * the same ones back, which is the layout doing its job.
         */
        class CommandStream
        {
        public:
            CommandStream(GameSimulation& sim, const std::shared_ptr<CobScript>& script, PlayerId a, PlayerId b)
                : sim(sim), script(script), a(a), b(b)
            {
            }

            /** Where the spawn count stood when the keyframe was taken, so the second run checks from there. */
            void markKeyframe()
            {
                spawnCountAtKeyframe = spawnCount;
            }

            void rewind(GameSimulation& restored)
            {
                sim = std::ref(restored);
                recording = false;
                spawnCount = spawnCountAtKeyframe;
                rockDefinition.reset();
            }

            void apply(unsigned int tick)
            {
                switch (tick)
                {
                    case 0:
                        spawn(a, SimVector(-200_ss, 0_ss, -200_ss), SimVector(300_ss, 0_ss, 300_ss));
                        spawn(a, SimVector(-200_ss, 0_ss, -150_ss), SimVector(300_ss, 0_ss, 250_ss));
                        spawn(a, SimVector(-200_ss, 0_ss, -100_ss), SimVector(300_ss, 0_ss, 200_ss));
                        spawn(a, SimVector(-200_ss, 0_ss, -50_ss), SimVector(300_ss, 0_ss, 150_ss));
                        spawn(b, SimVector(200_ss, 0_ss, 200_ss), SimVector(-300_ss, 0_ss, -300_ss));
                        spawn(b, SimVector(200_ss, 0_ss, 150_ss), SimVector(-300_ss, 0_ss, -250_ss));
                        break;
                    case 15:
                        sim.get().killUnit(spawned[1]);
                        break;
                    case 30:
                        sim.get().killUnit(spawned[4]);
                        break;
                    case 45:
                        // Two holes at this point; this takes the later one.
                        spawn(a, SimVector(-100_ss, 0_ss, 0_ss), SimVector(300_ss, 0_ss, 0_ss));
                        break;
                    case 50:
                    {
                        // A hole in the feature table, made by hand since
                        // nothing here reclaims.
                        std::optional<FeatureId> first;
                        for (const auto& [id, f] : sim.get().features)
                        {
                            if (!first && sim.get().getFeatureDefinition(f.featureName).name == "rock")
                            {
                                first = FeatureId(id.value);
                            }
                        }
                        sim.get().deleteFeature(first.value());
                        break;
                    }
                    // --- the keyframe is taken before tick 100 runs ---
                    case 120:
                        // Into the hole the first death left. A fresh table
                        // would put this at the end instead.
                        spawn(a, SimVector(-100_ss, 0_ss, 50_ss), SimVector(300_ss, 0_ss, 50_ss));
                        break;
                    case 140:
                        // Its wreck goes into the feature hole from tick 50.
                        sim.get().killUnit(spawned[0]);
                        break;
                    case 160:
                        spawn(b, SimVector(150_ss, 0_ss, -150_ss), SimVector(-300_ss, 0_ss, 100_ss));
                        break;
                    case 170:
                    {
                        if (!rockDefinition)
                        {
                            rockDefinition = sim.get().tryGetFeatureDefinitionId("rock");
                        }
                        sim.get().addFeature(rockDefinition.value(), 20, 100).value();
                        break;
                    }
                    default:
                        break;
                }
            }

            std::vector<UnitId> spawned;

        private:
            void spawn(PlayerId owner, const SimVector& at, const SimVector& towards)
            {
                auto id = addUnitOfType(sim.get(), "TANK", owner, at, script);
                sim.get().getUnitState(id).orders.push_back(createMoveOrder(towards));
                if (recording)
                {
                    spawned.push_back(id);
                }
                else
                {
                    INFO("spawn number " << spawnCount);
                    REQUIRE(spawnCount < spawned.size());
                    REQUIRE(spawned[spawnCount] == id);
                }
                ++spawnCount;
            }

            std::reference_wrapper<GameSimulation> sim;
            std::shared_ptr<CobScript> script;
            PlayerId a;
            PlayerId b;
            bool recording{true};
            std::size_t spawnCount{0};
            std::size_t spawnCountAtKeyframe{0};
            std::optional<FeatureDefinitionId> rockDefinition;
        };

        std::vector<std::uint8_t> takeKeyframe(const GameSimulation& sim)
        {
            return nlohmann::json::to_cbor(saveSimulationToJson(sim));
        }
    }

    TEST_CASE("a replay keyframe restored into the same simulation replays tick for tick", "[replay]")
    {
        // Mirrors GameScene::tryTickGame: the keyframe at scene time T is
        // taken before tick T's commands are pushed, so restoring it and
        // feeding tick T is the same thing that happened the first time.
        const unsigned int keyframeTick = 100;
        const unsigned int endTick = 250;

        auto script = makeEmptyCobScript({"base"});
        auto sim = makeBaseSim(script);
        auto a = addPlayer(sim, "a");
        auto b = addPlayer(sim, "b");
        CommandStream commands(sim, script, a, b);

        std::vector<GameHash> hashes;
        std::vector<std::uint8_t> keyframe;
        for (unsigned int tick = 0; tick < endTick; ++tick)
        {
            if (tick == keyframeTick)
            {
                commands.markKeyframe();
                keyframe = takeKeyframe(sim);
            }
            commands.apply(tick);
            sim.tick();
            hashes.push_back(sim.computeHash());
        }
        REQUIRE(!keyframe.empty());
        REQUIRE(commands.spawned.size() == 9);

        // The keyframe carries the shape of the tables, and the tables did
        // have holes in them to carry.
        auto keyframeJson = nlohmann::json::from_cbor(keyframe);
        REQUIRE(keyframeJson.contains("layout"));
        REQUIRE(!keyframeJson.at("layout").at("units").at("firstFree").is_null());
        REQUIRE(!keyframeJson.at("layout").at("features").at("firstFree").is_null());

        // Back into the same simulation object, over the top of everything
        // that happened since -- which is what the viewer does.
        clearSimulationForLoad(sim);
        loadSimulationFromJson(keyframeJson, sim);
        REQUIRE(sim.computeHash() == hashes[keyframeTick - 1]);
        REQUIRE(nlohmann::json::to_cbor(saveSimulationToJson(sim)) == keyframe);

        commands.rewind(sim);
        for (unsigned int tick = keyframeTick; tick < endTick; ++tick)
        {
            commands.apply(tick);
            sim.tick();
            INFO("tick " << tick);
            REQUIRE(sim.computeHash() == hashes[tick]);
        }
    }

    TEST_CASE("a save without its layout hands out different ids from the original", "[replay]")
    {
        // The reason the layout is in the save at all: a table loaded densely
        // has no holes, so the next arrival lands at the end instead of in
        // the slot the original would have refilled.
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeBaseSim(script);
        auto a = addPlayer(sim, "a");
        auto b = addPlayer(sim, "b");
        CommandStream commands(sim, script, a, b);
        for (unsigned int tick = 0; tick < 100; ++tick)
        {
            commands.apply(tick);
            sim.tick();
        }
        auto saved = saveSimulationToJson(sim);
        auto nextInOriginal = addUnitOfType(sim, "TANK", a, SimVector(0_ss, 0_ss, 0_ss), script);

        {
            auto withLayout = makeBaseSim(script);
            loadSimulationFromJson(saved, withLayout);
            REQUIRE(addUnitOfType(withLayout, "TANK", a, SimVector(0_ss, 0_ss, 0_ss), script) == nextInOriginal);
        }
        {
            auto dense = makeBaseSim(script);
            auto withoutLayout = saved;
            withoutLayout.erase("layout");
            loadSimulationFromJson(withoutLayout, dense);
            REQUIRE(addUnitOfType(dense, "TANK", a, SimVector(0_ss, 0_ss, 0_ss), script) != nextInOriginal);
        }
    }
}
