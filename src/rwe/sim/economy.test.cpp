#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/tad_economy_episodes.h>
#include <string>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** The economy tests set their own starting stores; that is what they measure. */
        PlayerId addPlayerWithResources(GameSimulation& sim, float metal = 0.0f, float energy = 0.0f, float storage = 10000.0f)
        {
            GamePlayerInfo p{
                std::optional<std::string>("player"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(storage),
                Energy(storage),
                Metal(storage),
                Energy(storage),
            };
            return sim.addPlayer(p);
        }

        // The settle recomputes each player's storage caps from its units, so a
        // test unit has to carry the storage the test wants to be able to hold.
        UnitDefinition makeInertDef(float storage = 10000.0f)
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.energyStorage = Energy(storage);
            d.metalStorage = Metal(storage);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** Runs a whole second so that GameSimulation::updateResources settles. */
        void tickOneSecond(GameSimulation& sim)
        {
            for (unsigned int i = 0; i < SimTicksPerSecond; ++i)
            {
                sim.tick();
            }
        }
    }

    TEST_CASE("settleResourcePool", "[economy]")
    {
        SECTION("everything is paid when supply covers it")
        {
            auto r = settleResourcePool(100.0f, 20.0f, 30.0f);
            REQUIRE(r.debtFraction == Catch::Approx(1.0f));
            REQUIRE(r.requestFraction == Catch::Approx(1.0f));
            REQUIRE(r.remaining == Catch::Approx(50.0f));
            REQUIRE_FALSE(r.stalled);
        }

        SECTION("a shortfall is shared out as one fraction, not first come first served")
        {
            auto r = settleResourcePool(30.0f, 0.0f, 90.0f);
            REQUIRE(r.requestFraction == Catch::Approx(1.0f / 3.0f));
            REQUIRE(r.remaining == Catch::Approx(0.0f));
            REQUIRE(r.stalled);
        }

        SECTION("debt is paid before anything new")
        {
            // 40 of supply against 30 of debt leaves 10 for the 50 asked for.
            auto r = settleResourcePool(40.0f, 30.0f, 50.0f);
            REQUIRE(r.debtFraction == Catch::Approx(1.0f));
            REQUIRE(r.requestFraction == Catch::Approx(0.2f));
            REQUIRE(r.stalled);
        }

        SECTION("when the debt alone is too big, nothing new gets anything at all")
        {
            auto r = settleResourcePool(25.0f, 100.0f, 50.0f);
            REQUIRE(r.debtFraction == Catch::Approx(0.25f));
            REQUIRE(r.requestFraction == Catch::Approx(0.0f));
            REQUIRE(r.remaining == Catch::Approx(0.0f));
            REQUIRE(r.stalled);
        }

        SECTION("an idle player is not stalled and keeps its stockpile")
        {
            auto r = settleResourcePool(500.0f, 0.0f, 0.0f);
            REQUIRE(r.remaining == Catch::Approx(500.0f));
            REQUIRE_FALSE(r.stalled);
        }
    }

    TEST_CASE("a consumer is refused while it owes for earlier work", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerWithResources(sim, 10.0f, 10.0f);
        sim.unitDefinitions["inert"] = makeInertDef();
        auto unitId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        // Asking for more than the whole player has is still granted: nothing
        // decides affordability until the second is settled.
        REQUIRE(sim.addResourceDelta(unitId, Energy(-40.0f), Metal(-40.0f)));
        REQUIRE(sim.getUnitState(unitId).energyRequestBuffer.value == Catch::Approx(40.0f));

        tickOneSecond(sim);

        // A quarter of it could be paid, so three quarters is carried as debt.
        const auto& unit = sim.getUnitState(unitId);
        REQUIRE(unit.energyDebt.value == Catch::Approx(30.0f));
        REQUIRE(unit.metalDebt.value == Catch::Approx(30.0f));
        REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(0.0f));
        REQUIRE(sim.getPlayer(player).energyStalled);

        // And until that debt is cleared the unit is turned away outright.
        REQUIRE_FALSE(sim.addResourceDelta(unitId, Energy(-1.0f), Metal(-1.0f)));
        REQUIRE(sim.getUnitState(unitId).energyRequestBuffer.value == Catch::Approx(0.0f));
        // The refusal is still counted as demand for the resource display.
        REQUIRE(sim.getUnitState(unitId).energyConsumptionBuffer.value == Catch::Approx(1.0f));
    }

    TEST_CASE("a unit that owes for earlier work still earns", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerWithResources(sim);
        auto def = makeInertDef();
        def.energyMake = Energy(100.0f);
        sim.unitDefinitions["gen"] = def;
        auto unitId = addUnitOfType(sim, "gen", player, SimVector(100_ss, 0_ss, 100_ss), script);

        // Put the unit into metal debt with a bill nothing can pay.
        REQUIRE(sim.addResourceDelta(unitId, Energy(0), Metal(-50.0f)));
        tickOneSecond(sim);
        REQUIRE(sim.getUnitState(unitId).metalDebt.value == Catch::Approx(50.0f));

        // It is turned away from any further work, but what it makes is not
        // asked for and so cannot be refused: a stall must not switch off the
        // generators that would end it.
        auto energyBefore = sim.getPlayer(player).energy.value;
        tickOneSecond(sim);
        REQUIRE_FALSE(sim.addResourceDelta(unitId, Energy(0), Metal(-1.0f)));
        REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(energyBefore + 100.0f));
    }

    TEST_CASE("a shortfall slows every consumer by the same fraction", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerWithResources(sim, 30.0f, 30.0f);
        sim.unitDefinitions["inert"] = makeInertDef();

        auto heavyId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);
        auto lightId = addUnitOfType(sim, "inert", player, SimVector(200_ss, 0_ss, 100_ss), script);

        // 60 asked for against 30 in the bank: half of each consumer's bill.
        REQUIRE(sim.addResourceDelta(heavyId, Energy(-50.0f), Metal(-50.0f)));
        REQUIRE(sim.addResourceDelta(lightId, Energy(-10.0f), Metal(-10.0f)));

        tickOneSecond(sim);

        // The big consumer is not served in full at the small one's expense,
        // and the small one is not starved for being later in the list: both
        // carry half of what they asked for.
        REQUIRE(sim.getUnitState(heavyId).energyDebt.value == Catch::Approx(25.0f));
        REQUIRE(sim.getUnitState(lightId).energyDebt.value == Catch::Approx(5.0f));
    }

    TEST_CASE("income above the storage cap is thrown away", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayerWithResources(sim);
        sim.unitDefinitions["inert"] = makeInertDef(100.0f);
        auto unitId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        sim.addResourceDelta(unitId, Energy(500.0f), Metal(500.0f));
        tickOneSecond(sim);

        REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(100.0f));
        REQUIRE(sim.getPlayer(player).metal.value == Catch::Approx(100.0f));
    }

    TEST_CASE("generators and consumers over a second", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        sim.tidalStrength = 20;
        auto player = addPlayerWithResources(sim);

        SECTION("a tidal generator makes its rating times the map's tidal strength")
        {
            auto def = makeInertDef();
            def.tidalGenerator = Energy(1.0f);
            sim.unitDefinitions["tide"] = def;
            auto unitId = addUnitOfType(sim, "tide", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(unitId).activated = true;

            tickOneSecond(sim);

            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(20.0f));
        }

        SECTION("a tidal generator that is switched off makes nothing")
        {
            auto def = makeInertDef();
            def.tidalGenerator = Energy(1.0f);
            sim.unitDefinitions["tide"] = def;
            auto unitId = addUnitOfType(sim, "tide", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(unitId).activated = false;

            tickOneSecond(sim);

            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(0.0f));
        }

        SECTION("a solar collector's negative EnergyUse is its output")
        {
            auto def = makeInertDef();
            def.energyUse = Energy(-20.0f);
            sim.unitDefinitions["solar"] = def;
            auto unitId = addUnitOfType(sim, "solar", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(unitId).activated = true;

            tickOneSecond(sim);

            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(20.0f));
        }

        SECTION("a metal maker makes nothing on the second its energy is refused")
        {
            auto def = makeInertDef();
            def.energyUse = Energy(60.0f);
            def.makesMetal = Metal(1.0f);
            sim.unitDefinitions["maker"] = def;
            auto unitId = addUnitOfType(sim, "maker", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(unitId).activated = true;

            // Nothing in the bank, so the first second's draw goes unpaid and
            // leaves a debt; the second's is refused outright and no metal is
            // made for it.
            tickOneSecond(sim);
            REQUIRE(sim.getUnitState(unitId).energyDebt.value == Catch::Approx(60.0f));
            auto metalAfterFirst = sim.getPlayer(player).metal.value;

            tickOneSecond(sim);
            REQUIRE_FALSE(sim.getUnitState(unitId).isSufficientlyPowered);
            REQUIRE(sim.getPlayer(player).metal.value == Catch::Approx(metalAfterFirst));
        }
    }

    // ------------------------------------------------------------------
    // Everything above is hand-written: a case is whatever it took to pin one
    // rule. Everything below comes out of real games -- the episodes in
    // tad_economy_episodes.h, mined from the demo corpus by tad_episodes
    // --emit-cpp. The two kinds sit together deliberately, because a reader
    // needs to be able to tell at a glance which numbers somebody chose and
    // which ones a game produced.
    // ------------------------------------------------------------------

    namespace
    {
        /**
         * Rebuilds an episode's player: the lobby's storage setting, then one
         * unit per unit the player owned at the sample, each carrying the
         * storage figures the episode transcribed out of its FBI.
         *
         * Units under construction are given a build time they have not
         * finished paying, which is all `isBeingBuilt` looks at.
         */
        PlayerId rebuildEpisode(GameSimulation& sim, const TadStorageEpisode& episode)
        {
            GamePlayerInfo p{
                std::optional<std::string>("player"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(0.0f),
                Energy(0.0f),
                Metal(0.0f),
                Energy(0.0f),
                Metal(episode.startingMetal),
                Energy(episode.startingEnergy),
            };
            auto player = sim.addPlayer(p);

            // The commander is not in the composition -- see the header -- but
            // the capacity it carries is credited per commander, so one has to
            // stand in the world for the settle to count it.
            UnitDefinition commander{};
            commander.maxHitPoints = 100;
            commander.buildTime = 0u;
            commander.commander = true;
            commander.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            sim.unitDefinitions["commander"] = commander;

            auto script = makeEmptyCobScript();
            auto next = 0;
            auto place = [&]() {
                auto x = SimScalar(static_cast<float>(64 + 16 * (next % 8)));
                auto z = SimScalar(static_cast<float>(64 + 16 * (next / 8)));
                ++next;
                return SimVector(x, 0_ss, z);
            };

            addUnitOfType(sim, "commander", player, place(), script);

            auto add = [&](const TadEpisodeComposition* composition, std::size_t count, bool finished) {
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto& entry = composition[i];
                    auto type = std::string(entry.unitName) + (finished ? "" : "_nanoframe");

                    UnitDefinition def{};
                    def.maxHitPoints = 100;
                    def.buildTime = finished ? 0u : 1000u;
                    def.metalStorage = Metal(entry.metalStorage);
                    def.energyStorage = Energy(entry.energyStorage);
                    def.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
                    sim.unitDefinitions[type] = def;

                    for (unsigned int n = 0; n < entry.count; ++n)
                    {
                        auto unitId = addUnitOfType(sim, type, player, place(), script);
                        if (!finished)
                        {
                            sim.getUnitState(unitId).buildTimeCompleted = 0u;
                        }
                    }
                }
            };

            add(episode.finished, episode.finishedCount, true);
            add(episode.building, episode.buildingCount, false);

            return player;
        }

        std::string episodeName(const TadStorageEpisode& episode)
        {
            return std::string(episode.demo) + " block " + std::to_string(episode.ownerBlock)
                + " tick " + std::to_string(episode.sampleTick);
        }
    }

    TEST_CASE("storage capacity is what real games report", "[economy][corpus]")
    {
        // The capacity is a plain sum over what the player has FINISHED, and
        // the corpus says so on its own terms: base plus the transcribed FBI
        // figures tracks the reported capacity from the opening sample of all
        // 86 players in the corpus, for hundreds of samples each, and two
        // players match every sample to the end of their recording.
        for (const auto& episode : tadStorageEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
                auto player = rebuildEpisode(sim, episode);

                tickOneSecond(sim);

                REQUIRE(sim.getPlayer(player).maxMetal.value
                    == Catch::Approx(episode.metalStorage + episode.expectedMetalStorageDelta));
                REQUIRE(sim.getPlayer(player).maxEnergy.value
                    == Catch::Approx(episode.energyStorage + episode.expectedEnergyStorageDelta));
            }
        }
    }

    TEST_CASE("a nanoframe holds nothing until it is finished", "[economy][corpus]")
    {
        // The falsifiable half of the same episodes. Nine of the thirteen had
        // a nanoframe standing when the sample was taken, and two of those
        // nanoframes were of a type that holds something -- so crediting them
        // would move the capacity off what the game reported, and finishing
        // them has to move it by exactly what they carry.
        for (const auto& episode : tadStorageEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
                auto player = rebuildEpisode(sim, episode);

                tickOneSecond(sim);
                auto withNanoframes = sim.getPlayer(player).maxMetal.value;

                // Finish them, and the capacity moves by exactly what they carry.
                float pending = 0.0f;
                for (std::size_t i = 0; i < episode.buildingCount; ++i)
                {
                    pending += episode.building[i].metalStorage * static_cast<float>(episode.building[i].count);
                }
                for (auto& entry : sim.units)
                {
                    entry.second.buildTimeCompleted = sim.unitDefinitions.at(entry.second.unitType).buildTime;
                }

                tickOneSecond(sim);

                REQUIRE(sim.getPlayer(player).maxMetal.value == Catch::Approx(withNanoframes + pending));
            }
        }
    }

    TEST_CASE("income above a real game's storage cap is thrown away", "[economy][corpus]")
    {
        // Across the corpus, stored never once exceeded capacity in 61,709
        // samples, and it sat exactly ON the capacity in 27,688 of them. Where
        // an episode caught that, the observed stockpile IS the cap, so the
        // clamp has a number out of a real game to land on.
        for (const auto& episode : tadStorageEpisodes)
        {
            if (episode.energyStored != episode.energyStorage)
            {
                continue;
            }

            DYNAMIC_SECTION(episodeName(episode))
            {
                GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
                auto player = rebuildEpisode(sim, episode);

                auto anyUnit = sim.units.begin()->first;
                sim.addResourceDelta(UnitId(anyUnit), Energy(1000000.0f), Metal(1000000.0f));

                tickOneSecond(sim);

                REQUIRE(sim.getPlayer(player).energy.value
                    == Catch::Approx(episode.energyStored + episode.expectedEnergyStorageDelta));
            }
        }
    }

    TEST_CASE("the wind factor never exceeds a full gale", "[economy]")
    {
        // A map whose wind range sits above what a generator can use still
        // gives exactly the generator's rating, not more.
        GameSimulation sim(makeFlatTerrain(), 0u, MaxUtilizableWindSpeed * 3, MaxUtilizableWindSpeed * 4);
        sim.tick();
        REQUIRE(sim.currentWindGenerationFactor.value == Catch::Approx(1.0f));
    }
}
