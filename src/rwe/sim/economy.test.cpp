#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitMesh.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/tad_economy_episodes.h>
#include <rwe/sim/tad_stall_episodes.h>
#include <set>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
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

    TEST_CASE("a player with a base storage keeps it with no commander, and a commander does not double it", "[economy]")
    {
        // Issue #293. The original adds a per-player base to the storage its
        // units hold when bit 0 of player+0x149 is set (0x401988); a mission
        // sets it, so a mission player with no commander keeps what the
        // schema gave it instead of losing it all at the first settle.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayerWithResources(sim, 1000.0f, 1000.0f, 1000.0f);
        sim.unitDefinitions["inert"] = makeInertDef(50.0f);
        auto commanderDef = makeInertDef(0.0f);
        commanderDef.commander = true;
        sim.unitDefinitions["commander"] = commanderDef;
        addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        SECTION("without the flag and without a commander, only the units' storage is left")
        {
            tickOneSecond(sim);
            REQUIRE(sim.getPlayer(player).maxMetal == Metal(50.0f));
            REQUIRE(sim.getPlayer(player).metal == Metal(50.0f));
        }

        SECTION("with it, the base is kept")
        {
            sim.getPlayer(player).hasBaseStorage = true;
            tickOneSecond(sim);
            REQUIRE(sim.getPlayer(player).maxMetal == Metal(1050.0f));
            REQUIRE(sim.getPlayer(player).maxEnergy == Energy(1050.0f));
            REQUIRE(sim.getPlayer(player).metal == Metal(1000.0f));
        }

        SECTION("with it and a commander, the base is counted once")
        {
            sim.getPlayer(player).hasBaseStorage = true;
            addUnitOfType(sim, "commander", player, SimVector(200_ss, 0_ss, 100_ss), script);
            tickOneSecond(sim);
            REQUIRE(sim.getPlayer(player).maxMetal == Metal(1050.0f));
        }
    }

    TEST_CASE("a consumer is refused while it owes for earlier work", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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

    TEST_CASE("a shot's price comes off the stockpile on the spot", "[economy]")
    {
        // 0x401220, 0x401260 and 0x4012A0 subtract energypershot, metalpershot
        // and the cloak cost from the player's stockpile there and then, or
        // refuse when it will not cover them. Nothing about them goes through
        // the once-a-second settle.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithResources(sim, 1000.0f, 1000.0f);
        sim.unitDefinitions["inert"] = makeInertDef();
        auto unitId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        SECTION("what is covered is taken at once, and never becomes a request or a debt")
        {
            REQUIRE(sim.chargeStockpile(unitId, Energy(400.0f), Metal(10.0f)));
            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(600.0f));
            REQUIRE(sim.getPlayer(player).metal.value == Catch::Approx(990.0f));
            REQUIRE(sim.getUnitState(unitId).energyRequestBuffer.value == Catch::Approx(0.0f));
            REQUIRE(sim.getUnitState(unitId).metalRequestBuffer.value == Catch::Approx(0.0f));

            // Booked as demand for the display, on the unit and the player.
            REQUIRE(sim.getUnitState(unitId).energyConsumptionBuffer.value == Catch::Approx(400.0f));
            REQUIRE(sim.getPlayer(player).desiredEnergyConsumptionBuffer.value == Catch::Approx(400.0f));

            tickOneSecond(sim);
            REQUIRE(sim.getUnitState(unitId).energyDebt.value == Catch::Approx(0.0f));
            REQUIRE_FALSE(sim.getPlayer(player).energyStalled);
        }

        SECTION("several shots in one second each see the stock the last one left")
        {
            // Used to be: three checks against an untouched 1000, three
            // requests of 400, and every builder throttled at the settle.
            REQUIRE(sim.chargeStockpile(unitId, Energy(400.0f), Metal(0)));
            REQUIRE(sim.chargeStockpile(unitId, Energy(400.0f), Metal(0)));
            REQUIRE_FALSE(sim.chargeStockpile(unitId, Energy(400.0f), Metal(0)));
            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(200.0f));
        }

        SECTION("a price the stock will not cover is refused whole, in either resource")
        {
            REQUIRE_FALSE(sim.chargeStockpile(unitId, Energy(1000.5f), Metal(0)));
            REQUIRE_FALSE(sim.chargeStockpile(unitId, Energy(0), Metal(1001.0f)));
            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(1000.0f));
            REQUIRE(sim.getPlayer(player).metal.value == Catch::Approx(1000.0f));
            REQUIRE(sim.getUnitState(unitId).energyConsumptionBuffer.value == Catch::Approx(0.0f));
        }

        SECTION("a unit that owes for earlier work still pays for its shot")
        {
            // The request path turns such a unit away, and the fire path used
            // to ignore that refusal and fire for free. The original's shot
            // routine never looks at the unit's debt at all.
            sim.getUnitState(unitId).energyDebt = Energy(50.0f);
            REQUIRE_FALSE(sim.addResourceDelta(unitId, Energy(-1.0f), Metal(0)));
            REQUIRE(sim.chargeStockpile(unitId, Energy(400.0f), Metal(0)));
            REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(600.0f));
        }
    }

    TEST_CASE("a unit that owes for earlier work still earns", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithResources(sim);
        sim.unitDefinitions["inert"] = makeInertDef(100.0f);
        auto unitId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        sim.addResourceDelta(unitId, Energy(500.0f), Metal(500.0f));
        tickOneSecond(sim);

        REQUIRE(sim.getPlayer(player).energy.value == Catch::Approx(100.0f));
        REQUIRE(sim.getPlayer(player).metal.value == Catch::Approx(100.0f));

        // ...and counted on the way out. The end-of-game chart's Excess columns
        // are exactly this: what the cap took off the top, summed over the
        // game.
        REQUIRE(sim.getPlayer(player).energyExcess.value == Catch::Approx(400.0f));
        REQUIRE(sim.getPlayer(player).metalExcess.value == Catch::Approx(400.0f));
    }

    TEST_CASE("what a player earned is totted up for the end of the game", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithResources(sim);
        sim.unitDefinitions["inert"] = makeInertDef();
        auto unitId = addUnitOfType(sim, "inert", player, SimVector(100_ss, 0_ss, 100_ss), script);

        sim.addResourceDelta(unitId, Energy(60.0f), Metal(30.0f));
        tickOneSecond(sim);
        sim.addResourceDelta(unitId, Energy(40.0f), Metal(20.0f));
        tickOneSecond(sim);

        const auto& p = sim.getPlayer(player);
        REQUIRE(p.energyProduced.value == Catch::Approx(100.0f));
        REQUIRE(p.metalProduced.value == Catch::Approx(50.0f));

        // Nothing was wasted: the storage was big enough for all of it.
        REQUIRE(p.energyExcess.value == Catch::Approx(0.0f));
        REQUIRE(p.metalExcess.value == Catch::Approx(0.0f));
    }

    TEST_CASE("generators and consumers over a second", "[economy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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

        SECTION("a unit that owes metal but has the energy stays powered")
        {
            // The settle sweep's EnergyUse gate reads the unit's energy owed
            // and never its metal (0x4013F9, 0x40164F); only the request
            // routine a builder goes through tests both. TOTALA-EXE.md
            // section 111.
            auto def = makeInertDef();
            def.energyUse = Energy(10.0f);
            sim.unitDefinitions["drawer"] = def;
            auto unitId = addUnitOfType(sim, "drawer", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(unitId).activated = true;

            // Plenty of energy to cover the draw, and a metal bill nothing can
            // pay, so the unit carries metal debt and no energy debt.
            sim.getPlayer(player).energy = Energy(1000.0f);
            REQUIRE(sim.addResourceDelta(unitId, Energy(0), Metal(-50.0f)));
            tickOneSecond(sim);
            REQUIRE(sim.getUnitState(unitId).metalDebt.value == Catch::Approx(50.0f));
            REQUIRE(sim.getUnitState(unitId).energyDebt.value == Catch::Approx(0.0f));

            tickOneSecond(sim);
            REQUIRE(sim.getUnitState(unitId).isSufficientlyPowered);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, MaxUtilizableWindSpeed * 3, MaxUtilizableWindSpeed * 4);
        sim.tick();
        REQUIRE(sim.currentWindGenerationFactor.value == Catch::Approx(1.0f));
    }

    // ------------------------------------------------------------------
    // The stall episodes: tad_stall_episodes.h, mined by tad_episodes
    // --emit-stall-cpp and scored by tools/tad-stalltime.py.
    //
    // Each is a factory that finished a job in a second whose settle a 0x28
    // sample caught stalled, and started its next job before the following
    // settle. What the corpus says happened is that the new job was refused
    // from its first tick until a settle paid the factory's debt, and not a
    // tick sooner or later, so it finished late by 30 - start % 30 plus whole
    // seconds. The replay below does not model that: it empties the store the
    // sample saw empty for exactly the settles the episode stalled on, refills
    // it after, and lets GameSimulation::tick and UnitState's debt gate decide
    // when the factory works.
    //
    // WHAT IS NOT DRIVEN THROUGH THE PIPELINE, and why. The lathe itself is
    // called by the test rather than by UnitBehaviorService, for the reason
    // buildtime.test.cpp gives: RWE's factory path does not credit progress on
    // the tick the nanoframe appears, and the corpus number is chosen to
    // exclude a builder's deploy. So the test calls the same two things the
    // factory path calls, in the same order -- GameSimulation::addResourceDelta
    // with the job's per-tick cost, then UnitState::addBuildProgress if it was
    // accepted -- once per tick, AFTER sim.tick() returns, which is where the
    // behaviour pass sits relative to updateResources inside tick(). The
    // settle's cadence, its phase and the debt rule are all the engine's own.
    //
    // THE CLOCK. The replay's gameTime is the demo's tick. The 0x09's tick is
    // where TA's first increment lands (buildtime.test.cpp), and the corpus
    // puts every settle of every player on a multiple of 30 of that clock
    // (docs/TOTALA-EXE.md section 111), which is where RWE settles.
    // ------------------------------------------------------------------

    namespace
    {
        UnitDefinition stallProduct(unsigned int buildTime, unsigned int metal, unsigned int energy)
        {
            UnitDefinition def{};
            def.maxHitPoints = 100u;
            def.buildTime = buildTime;
            def.buildCostMetal = Metal(static_cast<float>(metal));
            def.buildCostEnergy = Energy(static_cast<float>(energy));
            return def;
        }

        UnitState stallNanoframe(const std::shared_ptr<CobScript>& script)
        {
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::make_unique<CobEnvironment>(script.get()));
            unit.buildTimeCompleted = 0u;
            unit.hitPoints = 0u;
            return unit;
        }

        struct StallReplay
        {
            /** Whether the settle the sample saw really did stall in the replay. */
            bool settleStalled = false;
            std::optional<unsigned int> firstAcceptedTick;
            std::optional<unsigned int> finishTick;
        };

        StallReplay replayStall(const TadStallEpisode& episode)
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);

            // One player per owner block up to the episode's, and the factory
            // belongs to the last: TA settles every player on the same tick,
            // and an engine that did not would move the episodes of every
            // player but the first.
            PlayerId owner;
            for (unsigned int i = 0; i <= episode.ownerBlock; ++i)
            {
                owner = addPlayerWithResources(sim, 1000000.0f, 1000000.0f);
            }

            auto p = episode.workerTime / 30u;
            auto factoryDef = makeInertDef(1.0e9f);
            factoryDef.workerTimePerTick = p;
            sim.unitDefinitions["factory"] = factoryDef;
            auto script = makeEmptyCobScript();
            auto factory = addUnitOfType(sim, "factory", owner, SimVector(64_ss, 0_ss, 64_ss), script);

            // The job it was finishing: one tick of it is left, on
            // previousFinishTick, which is all the stall needs of it.
            auto previousDef = stallProduct(episode.previousBuildTime, episode.previousBuildCostMetal, episode.previousBuildCostEnergy);
            auto previous = stallNanoframe(script);
            previous.buildTimeCompleted = previousDef.buildTime - std::min(p, previousDef.buildTime);

            auto productDef = stallProduct(episode.buildTime, episode.buildCostMetal, episode.buildCostEnergy);
            auto product = stallNanoframe(script);

            // What UnitBehaviorService's factory path does with a tick: ask for
            // the step's cost, and build only if the answer is yes.
            auto lathe = [&](UnitState& target, const UnitDefinition& def, bool& finished) {
                auto costs = target.getBuildCostInfo(def, p);
                auto accepted = sim.addResourceDelta(
                    factory,
                    -Energy(def.buildCostEnergy.value * static_cast<float>(p) / static_cast<float>(def.buildTime)),
                    -Metal(def.buildCostMetal.value * static_cast<float>(p) / static_cast<float>(def.buildTime)),
                    -costs.energyCost,
                    -costs.metalCost);
                finished = accepted && target.addBuildProgress(def, p);
                return accepted;
            };

            auto lastStalledSettle = episode.stalledSettleTick + 30u * episode.furtherStalledSettles;

            StallReplay replay;
            sim.gameTime = GameTime(episode.previousFinishTick - 1u);
            for (auto t = episode.previousFinishTick; t < episode.startTick + 400000u; ++t)
            {
                auto& player = sim.getPlayer(owner);
                if (t >= episode.stalledSettleTick && t <= lastStalledSettle)
                {
                    if (episode.metalEmpty)
                    {
                        player.metal = Metal(0.0f);
                    }
                    else
                    {
                        player.energy = Energy(0.0f);
                    }
                }
                else if (t == lastStalledSettle + 1u)
                {
                    player.metal = Metal(1000000.0f);
                    player.energy = Energy(1000000.0f);
                }

                sim.tick();
                if (sim.gameTime != GameTime(t))
                {
                    return replay;
                }

                if (t == episode.stalledSettleTick)
                {
                    replay.settleStalled = episode.metalEmpty ? player.metalStalled : player.energyStalled;
                }

                bool finished = false;
                if (t == episode.previousFinishTick)
                {
                    lathe(previous, previousDef, finished);
                }

                if (t >= episode.startTick && lathe(product, productDef, finished))
                {
                    if (!replay.firstAcceptedTick)
                    {
                        replay.firstAcceptedTick = t;
                    }
                    if (finished)
                    {
                        replay.finishTick = t;
                        return replay;
                    }
                }
            }

            return replay;
        }

        std::string stallEpisodeName(const TadStallEpisode& episode)
        {
            return std::string(episode.builderName) + " " + episode.previousProductName + " -> "
                + episode.productName + " (" + episode.demo + " block " + std::to_string(episode.ownerBlock)
                + " tick " + std::to_string(episode.startTick) + ")";
        }
    }

    TEST_CASE("a factory stalled at the end of one job is refused into the next until a settle", "[economy][corpus]")
    {
        // The whole observation: finishTick - startTick, which is the build
        // model's duration plus 30 - start % 30 plus whole stalled seconds.
        // expectedDurationDelta is RWE's integer accumulator finishing a tick
        // early on the two episodes whose BuildTime divides exactly by the rate
        // (docs/TOTALA-EXE.md section 88), and nothing about the settle.
        for (const auto& episode : tadStallEpisodes)
        {
            DYNAMIC_SECTION(stallEpisodeName(episode))
            {
                auto replay = replayStall(episode);
                CHECK(replay.settleStalled);
                REQUIRE(replay.finishTick.has_value());

                auto duration = static_cast<int>(*replay.finishTick) - static_cast<int>(episode.startTick);
                REQUIRE(duration
                    == static_cast<int>(episode.finishTick - episode.startTick) + episode.expectedDurationDelta);
            }
        }
    }

    TEST_CASE("the refusal ends on a settle, and on the first one that pays the debt", "[economy][corpus]")
    {
        // The half of the same replay that does not go through the accumulator:
        // the factory's first accepted tick is a settle tick, and it is exactly
        // the residue plus the stalled seconds after the start, which is the
        // number the corpus says and which the section 88 accumulator cannot touch.
        for (const auto& episode : tadStallEpisodes)
        {
            DYNAMIC_SECTION(stallEpisodeName(episode))
            {
                auto replay = replayStall(episode);
                REQUIRE(replay.firstAcceptedTick.has_value());
                REQUIRE(*replay.firstAcceptedTick % 30u == 0u);
                REQUIRE(*replay.firstAcceptedTick - episode.startTick
                    == episode.residueTicks + 30u * episode.furtherStalledSettles);
            }
        }
    }

    TEST_CASE("the stall episodes cover the second and more than one player", "[economy][corpus]")
    {
        // What makes the replays above worth anything: residues spread across
        // the second, so a settle off by a tick or on a different cadence moves
        // them; owners other than the first player, so a per-player phase does;
        // and deltas only where the accumulator licenses one.
        std::set<unsigned int> residues;
        std::set<unsigned int> blocks;
        bool metal = false;
        bool energy = false;
        for (const auto& episode : tadStallEpisodes)
        {
            residues.insert(episode.residueTicks);
            blocks.insert(episode.ownerBlock);
            (episode.metalEmpty ? metal : energy) = true;

            // The episode is internally what its header says it is.
            REQUIRE(episode.residueTicks == 30u - episode.startTick % 30u);
            REQUIRE(episode.stalledSettleTick == episode.startTick - episode.startTick % 30u);
            REQUIRE(episode.previousFinishTick + 30u >= episode.stalledSettleTick);
            REQUIRE(episode.previousFinishTick < episode.stalledSettleTick);
            REQUIRE(episode.lateTicks == episode.residueTicks + 30u * episode.furtherStalledSettles);
            REQUIRE(episode.finishTick - episode.startTick == episode.modelDurationTicks + episode.lateTicks);

            auto divisible = episode.buildTime % (episode.workerTime / 30u) == 0u;
            if (episode.expectedDurationDelta != 0)
            {
                REQUIRE(episode.expectedDurationDelta == -1);
                REQUIRE(divisible);
                REQUIRE(episode.expectedDifference != nullptr);
            }
            else
            {
                REQUIRE(episode.expectedDifference == nullptr);
            }
        }

        REQUIRE(residues.size() >= 20u);
        REQUIRE(blocks.size() >= 3u);
        REQUIRE(*blocks.rbegin() > 0u);
        REQUIRE(metal);
        REQUIRE(energy);
    }
}
