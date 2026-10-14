#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/MetalMakerManager.h>
#include <rwe/ai/ai_test_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        struct MakerWorld
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            PlayerId owner{addPlayer(sim, "ai", GamePlayerType::Computer, "ARM")};
            std::shared_ptr<CobScript> script{makeEmptyCobScript()};
            AiTuningProfile profile;
            AiBlackboard bb;
            MetalMakerManager manager;

            MakerWorld()
            {
                profile.metalMakerUseSurplus = true;
                bb.currentEnergy = Energy(900.0f);
                bb.energyStorage = Energy(1000.0f);
            }

            UnitId maker(float energy = 60.0f, float metal = 1.0f, bool active = false)
            {
                auto type = "MAKER" + std::to_string(bb.metalMakers.size());
                auto def = makeDef(false, false, false, "", 0u);
                def.onOffable = true;
                def.energyUse = Energy(energy);
                def.makesMetal = Metal(metal);
                sim.unitDefinitions.emplace(type, def);
                auto id = addUnitOfType(sim, type, owner, SimVector(0_ss, 0_ss, 0_ss), script);
                sim.getUnitState(id).activated = active;
                bb.metalMakers.push_back(id);
                return id;
            }

            std::map<unsigned int, bool> update(unsigned int tick, float income, float demand)
            {
                sim.gameTime = GameTime(tick);
                bb.now = sim.gameTime;
                bb.energyIncome = Energy(income);
                bb.energyDemand = Energy(demand);
                std::vector<PlayerCommand> commands;
                manager.update(sim, profile, bb, commands);
                std::map<unsigned int, bool> switches;
                for (const auto& command : commands)
                {
                    const auto& unitCommand = std::get<PlayerUnitCommand>(command);
                    switches.emplace(unitCommand.unit.value, std::get<PlayerUnitCommand::SetOnOff>(unitCommand.command).on);
                }
                REQUIRE(switches.size() == commands.size());
                return switches;
            }
        };
    }

    TEST_CASE("surplus control runs only the converters generation can support", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto first = w.maker();
        w.maker();
        w.maker();
        auto switches = w.update(1, 170.0f, 80.0f);
        REQUIRE(switches.size() == 1);
        CHECK(switches.at(first.value));
    }

    TEST_CASE("converter demand is removed from the same resource sample", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto first = w.maker(60.0f, 1.0f, true);
        auto second = w.maker(60.0f, 1.0f, true);
        w.update(30, 170.0f, 200.0f);
        auto switches = w.update(31, 170.0f, 200.0f);
        REQUIRE(switches.size() == 1);
        CHECK_FALSE(switches.at(second.value));
        w.sim.getUnitState(second).activated = false;
        CHECK(w.update(45, 170.0f, 200.0f).empty());
        w.update(60, 170.0f, 200.0f);
        CHECK(w.update(61, 170.0f, 140.0f).empty());
        CHECK(w.sim.getUnitState(first).activated);
    }

    TEST_CASE("a switch after settlement does not change the sampled maker draw", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto id = w.maker(60.0f, 1.0f, true);
        w.update(30, 150.0f, 140.0f);
        w.sim.getUnitState(id).activated = false;
        auto switches = w.update(31, 150.0f, 140.0f);
        REQUIRE(switches.size() == 1);
        CHECK(switches.at(id.value));
    }

    TEST_CASE("surplus control favours efficient converters and fits smaller ones", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto basic = w.maker(60.0f, 1.0f);
        auto efficient = w.maker(120.0f, 4.0f);
        SECTION("the most metal per energy wins")
        {
            auto switches = w.update(1, 120.0f, 0.0f);
            REQUIRE(switches.size() == 1);
            CHECK(switches.at(efficient.value));
        }
        SECTION("an oversized converter does not block one that fits")
        {
            auto switches = w.update(1, 100.0f, 0.0f);
            REQUIRE(switches.size() == 1);
            CHECK(switches.at(basic.value));
        }
    }

    TEST_CASE("converter selection is independent of bucket order", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto first = w.maker();
        w.maker();
        std::reverse(w.bb.metalMakers.begin(), w.bb.metalMakers.end());
        auto switches = w.update(1, 60.0f, 0.0f);
        REQUIRE(switches.size() == 1);
        CHECK(switches.at(first.value));
    }

    TEST_CASE("surplus control preserves stored energy when generation falls short", "[ai][metalmakers]")
    {
        MakerWorld w;
        w.maker();
        CHECK(w.update(1, 80.0f, 90.0f).empty());
    }

    TEST_CASE("zero-energy converters can run without an energy surplus", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto id = w.maker(0.0f, 1.0f);
        auto switches = w.update(1, 0.0f, 100.0f);
        REQUIRE(switches.size() == 1);
        CHECK(switches.at(id.value));
    }

    TEST_CASE("a fractional surplus is converted without draining the reserve", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto id = w.maker();
        w.maker();
        w.bb.currentEnergy = Energy(1000.0f);
        REQUIRE(w.update(1, 40.0f, 0.0f).at(id.value));
        w.sim.getUnitState(id).activated = true;
        w.bb.currentEnergy = Energy(800.0f);
        w.update(30, 40.0f, 0.0f);
        CHECK(w.update(31, 40.0f, 60.0f).empty());
        w.bb.currentEnergy = Energy(700.0f);
        w.update(60, 40.0f, 60.0f);
        CHECK_FALSE(w.update(61, 40.0f, 60.0f).at(id.value));
        w.sim.getUnitState(id).activated = false;
        w.bb.currentEnergy = Energy(900.0f);
        w.update(90, 40.0f, 60.0f);
        CHECK(w.update(91, 40.0f, 0.0f).empty());
    }

    TEST_CASE("a full store does not fund converters during an underlying deficit", "[ai][metalmakers]")
    {
        MakerWorld w;
        w.maker();
        w.bb.currentEnergy = Energy(1000.0f);
        CHECK(w.update(1, 80.0f, 90.0f).empty());
    }

    TEST_CASE("emergencies cancel a pending activation without repeating commands", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto id = w.maker();
        REQUIRE(w.update(1, 120.0f, 0.0f).at(id.value));
        CHECK(w.update(2, 120.0f, 0.0f).empty());
        SECTION("low reserve") { w.bb.currentEnergy = Energy(200.0f); }
        SECTION("resource stall") { w.bb.energyStalled = true; }
        SECTION("base attacked") { w.bb.enemiesNearBase.push_back(UnitId(999)); }
        auto switches = w.update(3, 120.0f, 0.0f);
        REQUIRE(switches.size() == 1);
        CHECK_FALSE(switches.at(id.value));
        // The first switch can arrive before the cancellation. Do not keep
        // adding identical cancellations while that command is in flight.
        w.sim.getUnitState(id).activated = true;
        CHECK(w.update(15, 120.0f, 0.0f).empty());
        w.sim.getUnitState(id).activated = false;
        CHECK(w.update(30, 120.0f, 0.0f).empty());
    }

    TEST_CASE("storage hysteresis retains the permission between thresholds", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto id = w.maker();
        w.bb.currentEnergy = Energy(700.0f);
        CHECK(w.update(1, 120.0f, 0.0f).empty());
        w.bb.currentEnergy = Energy(900.0f);
        REQUIRE(w.update(31, 120.0f, 0.0f).at(id.value));
        w.sim.getUnitState(id).activated = true;
        w.bb.currentEnergy = Energy(500.0f);
        w.update(60, 120.0f, 60.0f);
        CHECK(w.update(61, 120.0f, 60.0f).empty());
        w.bb.currentEnergy = Energy(249.0f);
        CHECK_FALSE(w.update(62, 120.0f, 60.0f).at(id.value));
    }

    TEST_CASE("surplus control ignores missing dead unfinished and non-converter units", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto frame = w.maker();
        auto dead = w.maker();
        auto other = w.maker();
        w.sim.getUnitState(frame).buildTimeCompleted = 0;
        w.sim.quietlyKillUnit(dead);
        w.sim.unitDefinitions.at(w.sim.getUnitState(other).unitType).onOffable = false;
        w.bb.metalMakers.push_back(UnitId(999));
        CHECK(w.update(1, 1000.0f, 0.0f).empty());
    }

    TEST_CASE("an empty settlement clears the previous converter demand sample", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto old = w.maker(60.0f, 1.0f, true);
        w.update(30, 120.0f, 100.0f);
        w.sim.quietlyKillUnit(old);
        w.bb.metalMakers.clear();
        w.update(60, 120.0f, 100.0f);
        w.maker();
        CHECK(w.update(61, 120.0f, 100.0f).empty());
    }

    TEST_CASE("the comparison switch keeps the previous all-or-none maker policy", "[ai][metalmakers]")
    {
        MakerWorld w;
        w.profile.metalMakerUseSurplus = false;
        auto first = w.maker();
        auto second = w.maker();
        auto switches = w.update(1, 60.0f, 0.0f);
        REQUIRE(switches.size() == 2);
        CHECK(switches.at(first.value));
        CHECK(switches.at(second.value));
    }

    TEST_CASE("staged makers preserve energy through real settlements and delayed commands", "[ai][metalmakers]")
    {
        MakerWorld w;
        auto first = w.maker();
        w.maker();
        auto third = w.maker();
        auto power = makeDef(false, false, false, "", 0u);
        power.energyMake = Energy(170.0f);
        power.energyUse = Energy(80.0f);
        w.sim.unitDefinitions.emplace("POWER", power);
        auto generator = addUnitOfType(w.sim, "POWER", w.owner, SimVector(0_ss, 0_ss, 0_ss), w.script);
        w.sim.getUnitState(generator).activated = true;
        auto& player = w.sim.getPlayer(w.owner);
        player.hasBaseStorage = true;
        player.energy = Energy(900.0f);
        player.metal = Metal(0.0f);
        struct Pending
        {
            unsigned int at;
            unsigned int id;
            bool on;
        };
        std::vector<Pending> pending;
        unsigned int issued = 0;
        for (unsigned int tick = 1; tick <= 600; ++tick)
        {
            for (auto it = pending.begin(); it != pending.end();)
            {
                if (it->at == tick)
                {
                    w.sim.getUnitState(UnitId(it->id)).activated = it->on;
                    it = pending.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            w.bb.currentEnergy = player.energy;
            w.bb.energyStorage = player.maxEnergy;
            w.bb.energyStalled = player.energyStalled;
            auto switches = w.update(tick, player.previousEnergyProductionBuffer.value, player.previousDesiredEnergyConsumptionBuffer.value);
            for (const auto& [id, on] : switches)
            {
                pending.push_back({tick + 14, id, on});
                ++issued;
            }
            w.sim.updateResources();
            CHECK_FALSE(player.energyStalled);
            CHECK(player.energy.value >= 650.0f);
            CHECK_FALSE(w.sim.getUnitState(third).activated);
        }
        CHECK(w.sim.getUnitState(first).activated);
        CHECK(player.metalProduced.value >= 20.0f);
        CHECK(issued <= 8);
    }
}
