#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/game/Particle.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/cob.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeSmokeTerrain()
        {
            Grid<unsigned char> heights(2, 2, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        uint32_t op(OpCode code)
        {
            return static_cast<uint32_t>(code);
        }

        /**
         * SmokeUnit, transcribed word for word out of ARMSOLAR.COB in the
         * shipped data. Every unit that smokes carries a copy of this; 186 of
         * the 200 scripts in rev31 have it, and Create starts it as a thread.
         * The word addresses below are the file's own, so the jump targets are
         * the original's untouched.
         *
         *   SmokeUnit(healthpercent, sleeptime, smoketype)
         *   {
         *       while (get BUILD_PERCENT_LEFT) sleep 400;
         *       while (TRUE)
         *       {
         *           healthpercent = get HEALTH;
         *           if (healthpercent < 66)
         *           {
         *               smoketype = 256 | 2;
         *               if (Rand(1, 66) < healthpercent) smoketype = 256 | 1;
         *               emit-sfx smoketype from base;
         *           }
         *           sleeptime = healthpercent * 50;
         *           if (sleeptime < 200) sleeptime = 200;
         *           sleep sleeptime;
         *       }
         *   }
         */
        std::shared_ptr<CobScript> makeSmokeUnitScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            script->functions.push_back(CobFunctionInfo{"SmokeUnit", 0});
            script->instructions = std::vector<uint32_t>{
                /*  0 */ op(OpCode::CREATE_LOCAL_VAR),
                /*  1 */ op(OpCode::CREATE_LOCAL_VAR),
                /*  2 */ op(OpCode::CREATE_LOCAL_VAR),
                /*  3 */ op(OpCode::PUSH_CONSTANT), 17,
                /*  5 */ op(OpCode::GET_VALUE),
                /*  6 */ op(OpCode::JUMP_IF_ZERO), 13,
                /*  8 */ op(OpCode::PUSH_CONSTANT), 400,
                /* 10 */ op(OpCode::SLEEP),
                /* 11 */ op(OpCode::JUMP), 3,
                /* 13 */ op(OpCode::PUSH_CONSTANT), 1,
                /* 15 */ op(OpCode::JUMP_IF_ZERO), 80,
                /* 17 */ op(OpCode::PUSH_CONSTANT), 4,
                /* 19 */ op(OpCode::GET_VALUE),
                /* 20 */ op(OpCode::POP_LOCAL_VAR), 0,
                /* 22 */ op(OpCode::PUSH_LOCAL_VAR), 0,
                /* 24 */ op(OpCode::PUSH_CONSTANT), 66,
                /* 26 */ op(OpCode::SET_LESS),
                /* 27 */ op(OpCode::JUMP_IF_ZERO), 57,
                /* 29 */ op(OpCode::PUSH_CONSTANT), 256,
                /* 31 */ op(OpCode::PUSH_CONSTANT), 2,
                /* 33 */ op(OpCode::BITWISE_OR),
                /* 34 */ op(OpCode::POP_LOCAL_VAR), 2,
                /* 36 */ op(OpCode::PUSH_CONSTANT), 1,
                /* 38 */ op(OpCode::PUSH_CONSTANT), 66,
                /* 40 */ op(OpCode::RAND),
                /* 41 */ op(OpCode::PUSH_LOCAL_VAR), 0,
                /* 43 */ op(OpCode::SET_LESS),
                /* 44 */ op(OpCode::JUMP_IF_ZERO), 53,
                /* 46 */ op(OpCode::PUSH_CONSTANT), 256,
                /* 48 */ op(OpCode::PUSH_CONSTANT), 1,
                /* 50 */ op(OpCode::BITWISE_OR),
                /* 51 */ op(OpCode::POP_LOCAL_VAR), 2,
                /* 53 */ op(OpCode::PUSH_LOCAL_VAR), 2,
                /* 55 */ op(OpCode::EMIT_SFX), 0,
                /* 57 */ op(OpCode::PUSH_LOCAL_VAR), 0,
                /* 59 */ op(OpCode::PUSH_CONSTANT), 50,
                /* 61 */ op(OpCode::MUL),
                /* 62 */ op(OpCode::POP_LOCAL_VAR), 1,
                /* 64 */ op(OpCode::PUSH_LOCAL_VAR), 1,
                /* 66 */ op(OpCode::PUSH_CONSTANT), 200,
                /* 68 */ op(OpCode::SET_LESS),
                /* 69 */ op(OpCode::JUMP_IF_ZERO), 75,
                /* 71 */ op(OpCode::PUSH_CONSTANT), 200,
                /* 73 */ op(OpCode::POP_LOCAL_VAR), 1,
                /* 75 */ op(OpCode::PUSH_LOCAL_VAR), 1,
                /* 77 */ op(OpCode::SLEEP),
                /* 78 */ op(OpCode::JUMP), 13,
                /* 80 */ op(OpCode::PUSH_CONSTANT), 0,
                /* 82 */ op(OpCode::RETURN),
            };
            return script;
        }

        UnitDefinition makeSmokerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.maxHitPoints = 100;
            d.buildTime = 100u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addSmoker(GameSimulation& sim, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "smoker";
            unit.position = SimVector(0_ss, 0_ss, 0_ss);
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            unit.buildTimeCompleted = 100u;
            auto unitId = UnitId(sim.units.emplace(std::move(unit)));
            sim.getUnitState(unitId).cobEnvironment->createThread("SmokeUnit");
            return unitId;
        }

        struct Emission
        {
            unsigned int tick;
            EmitParticleFromPieceEvent::SfxType type;
        };

        /** Runs the unit's scripts for a stretch of ticks and reports every puff it asked for. */
        std::vector<Emission> collectSmoke(GameSimulation& sim, UnitId unitId, unsigned int ticks)
        {
            std::vector<Emission> emissions;
            for (unsigned int i = 0; i < ticks; ++i)
            {
                sim.events.clear();
                runUnitCobScripts(sim, unitId);
                for (const auto& event : sim.events)
                {
                    if (const auto* e = std::get_if<EmitParticleFromPieceEvent>(&event))
                    {
                        emissions.push_back(Emission{sim.gameTime.value, e->sfxType});
                    }
                }
                sim.gameTime = GameTime(sim.gameTime.value + 1);
            }
            return emissions;
        }

        /**
         * A stand-in for the original's rand(n) that hands out a fixed
         * sequence and then keeps repeating its last value, so a test only has
         * to spell out the rolls it cares about.
         */
        struct ScriptedRandom
        {
            std::vector<int> values;
            mutable size_t next{0};

            int operator()(int) const
            {
                auto index = std::min(next, values.size() - 1);
                ++next;
                return values[index];
            }
        };
    }

    TEST_CASE("a unit smokes only once it is below two thirds health", "[damagesmoke]")
    {
        // There is no health threshold anywhere in the original's per-tick unit
        // update: the whole trigger lives in the unit's own SmokeUnit script,
        // which asks for smoke while GET HEALTH is under 66. Running the real
        // bytecode is therefore the only honest way to test the threshold.
        auto script = makeSmokeUnitScript();

        SECTION("a unit at full health never asks for smoke")
        {
            GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
            sim.unitDefinitions["smoker"] = makeSmokerDef();
            auto unitId = addSmoker(sim, script);
            sim.getUnitState(unitId).hitPoints = 100;

            REQUIRE(collectSmoke(sim, unitId, 600).empty());
        }

        SECTION("66 of 100 hit points is still clear of the threshold")
        {
            GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
            sim.unitDefinitions["smoker"] = makeSmokerDef();
            auto unitId = addSmoker(sim, script);
            sim.getUnitState(unitId).hitPoints = 66;

            REQUIRE(collectSmoke(sim, unitId, 600).empty());
        }

        SECTION("one hit point lower and it smokes")
        {
            GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
            sim.unitDefinitions["smoker"] = makeSmokerDef();
            auto unitId = addSmoker(sim, script);
            sim.getUnitState(unitId).hitPoints = 65;

            REQUIRE(!collectSmoke(sim, unitId, 600).empty());
        }

        SECTION("a unit still under construction holds off until it is finished")
        {
            GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
            sim.unitDefinitions["smoker"] = makeSmokerDef();
            auto unitId = addSmoker(sim, script);
            sim.getUnitState(unitId).hitPoints = 10;
            sim.getUnitState(unitId).buildTimeCompleted = 50u;

            REQUIRE(collectSmoke(sim, unitId, 600).empty());
        }
    }

    TEST_CASE("smoke comes faster the more hurt the unit is", "[damagesmoke]")
    {
        // The script sleeps healthpercent * 50 milliseconds between puffs and
        // floors that at 200, so a unit on its last legs smokes at the cap of
        // one puff every 200ms while one just under the threshold takes over
        // three seconds between puffs.
        auto script = makeSmokeUnitScript();

        auto gapAtHealth = [&](int hitPoints) {
            GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
            sim.unitDefinitions["smoker"] = makeSmokerDef();
            auto unitId = addSmoker(sim, script);
            sim.getUnitState(unitId).hitPoints = hitPoints;

            auto emissions = collectSmoke(sim, unitId, 900);
            REQUIRE(emissions.size() >= 2);
            return emissions[1].tick - emissions[0].tick;
        };

        // 65 * 50 = 3250ms, which at thirty ticks a second is 97.5 ticks; the
        // thread wakes on the first tick at or past its wake time, so 98.
        REQUIRE(gapAtHealth(65) == 98);
        // 40 * 50 = 2000ms = 60 ticks exactly.
        REQUIRE(gapAtHealth(40) == 60);
        // 10 * 50 = 500ms = 15 ticks.
        REQUIRE(gapAtHealth(10) == 15);
        // 1 * 50 = 50ms, under the 200ms floor, so 6 ticks.
        REQUIRE(gapAtHealth(1) == 6);
        REQUIRE(gapAtHealth(3) == 6);
    }

    TEST_CASE("a nearly dead unit smokes black", "[damagesmoke]")
    {
        // The script asks for black smoke and then swaps in white when
        // Rand(1, 66) comes out below the health percentage. Rand never
        // returns less than one, so a unit on one percent health can only ever
        // smoke black -- which is the one end of the mix that is pinned down
        // without leaning on the generator.
        auto script = makeSmokeUnitScript();
        GameSimulation sim(makeSmokeTerrain(), 0u, 0, 0);
        sim.unitDefinitions["smoker"] = makeSmokerDef();
        auto unitId = addSmoker(sim, script);
        sim.getUnitState(unitId).hitPoints = 1;

        auto emissions = collectSmoke(sim, unitId, 600);
        REQUIRE(emissions.size() >= 50);
        for (const auto& emission : emissions)
        {
            REQUIRE(emission.type == EmitParticleFromPieceEvent::SfxType::BlackSmoke);
        }
    }

    TEST_CASE("a puff of smoke stops on a frame picked when it is born", "[damagesmoke]")
    {
        // smoke 1 out of anims/FX.GAF has twelve frames. The original picks the
        // frame to stop on as 2 + rand over (12 - 1) - 2, so between two and
        // ten of them get shown and the last frame of the sequence is never
        // reached.
        SECTION("the stopping frame is rolled over the sequence less three")
        {
            std::vector<int> bounds;
            std::function<int(int)> record = [&](int n) { bounds.push_back(n); return 0; };

            makeSmokePuffFrameSchedule(12, record);
            REQUIRE(bounds.front() == 9);

            bounds.clear();
            makeSmokePuffFrameSchedule(16, record);
            REQUIRE(bounds.front() == 13);
        }

        SECTION("the shortest puff shows two frames")
        {
            auto schedule = makeSmokePuffFrameSchedule(12, ScriptedRandom{{0}});
            REQUIRE(schedule.size() == 3);
        }

        SECTION("the longest puff shows ten of the twelve")
        {
            auto schedule = makeSmokePuffFrameSchedule(12, ScriptedRandom{{8}});
            REQUIRE(schedule.size() == 11);
        }

        SECTION("smoke 2's sixteen frames give two to fourteen")
        {
            REQUIRE(makeSmokePuffFrameSchedule(16, ScriptedRandom{{0}}).size() == 3);
            REQUIRE(makeSmokePuffFrameSchedule(16, ScriptedRandom{{12}}).size() == 15);
        }
    }

    TEST_CASE("a puff holds its first frame longest", "[damagesmoke]")
    {
        // Seven ticks for the first frame -- the emitter's own period -- and
        // then three to five for each one after it.
        SECTION("the first frame gets seven ticks and the rest three to five")
        {
            // Four frames shown, then holds of 3, 5 and 4.
            auto schedule = makeSmokePuffFrameSchedule(12, ScriptedRandom{{2, 0, 2, 1}});
            REQUIRE(schedule.size() == 5);
            REQUIRE(schedule[0].value == 0u);
            REQUIRE(schedule[1].value == 7u);
            REQUIRE(schedule[2].value == 10u);
            REQUIRE(schedule[3].value == 15u);
            REQUIRE(schedule[4].value == 19u);
        }

        SECTION("no hold is ever shorter than three ticks or longer than five")
        {
            for (int roll = 0; roll < 3; ++roll)
            {
                auto schedule = makeSmokePuffFrameSchedule(12, ScriptedRandom{{8, roll}});
                for (size_t i = 2; i < schedule.size(); ++i)
                {
                    auto hold = schedule[i].value - schedule[i - 1].value;
                    REQUIRE(hold >= smokePuffLaterFrameTicks);
                    REQUIRE(hold <= smokePuffLaterFrameTicks * 2);
                }
            }
        }
    }

    TEST_CASE("a puff walks its schedule one frame at a time and then goes away", "[damagesmoke]")
    {
        // Four frames, holds of 7, 3, 5, 4, so the puff is nineteen ticks old
        // when it disappears.
        Particle particle;
        particle.startTime = GameTime(100);
        ParticleRenderTypeSprite renderType{
            "FX",
            "smoke 1",
            ParticleFinishTimeEndOfFrames(),
            GameTime(2),
            true,
            false,
            makeSmokePuffFrameSchedule(12, ScriptedRandom{{2, 0, 2, 1}}),
        };
        particle.renderType = renderType;

        const unsigned int expectedFrames[] = {
            0, 0, 0, 0, 0, 0, 0, // ages 0..6
            1, 1, 1,             // ages 7..9
            2, 2, 2, 2, 2,       // ages 10..14
            3, 3, 3, 3,          // ages 15..18
        };

        for (unsigned int age = 0; age < 19; ++age)
        {
            auto now = GameTime(100 + age);
            CAPTURE(age);
            REQUIRE(!particle.isFinished(now, renderType, 12));
            REQUIRE(particle.getFrameIndex(now, renderType, 12) == expectedFrames[age]);
        }

        REQUIRE(particle.isFinished(GameTime(119), renderType, 12));

        // Without a schedule the old even-paced behaviour is untouched, which
        // is what the explosions and the weapon smoke still run on.
        ParticleRenderTypeSprite plain{"FX", "smoke 1", ParticleFinishTimeEndOfFrames(), GameTime(2), true, false, {}};
        REQUIRE(particle.getFrameIndex(GameTime(100), plain, 12) == 0u);
        REQUIRE(particle.getFrameIndex(GameTime(107), plain, 12) == 3u);
        REQUIRE(!particle.isFinished(GameTime(123), plain, 12));
        REQUIRE(particle.isFinished(GameTime(124), plain, 12));
    }
}
