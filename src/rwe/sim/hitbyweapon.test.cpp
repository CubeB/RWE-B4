#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>

namespace rwe
{
    using Catch::Approx;

    namespace
    {
        // Statics the stub script writes, so a test can read what it was told.
        constexpr std::size_t CosArg = 0;
        constexpr std::size_t SinArg = 1;
        constexpr std::size_t PercentArg = 2;
        constexpr std::size_t HitCount = 3;
        constexpr std::size_t CallOrder = 4;

        /**
         * A script shaped like armsolar's (BT-54): HitByWeapon(a, b) records
         * its arguments, signals and masks 2 so that a new hit replaces the
         * wait, sets ACTIVATION to 0, sleeps 8000 ms and sets it back to 1.
         * TakeDamage(percent) records its argument. Each call also appends
         * its number to CallOrder (HitByWeapon 1, TakeDamage 2) and counts.
         */
        std::shared_ptr<CobScript> makeHitScript(bool switchesOff)
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 5;
            script->pieces.push_back("base");

            auto& code = script->instructions;
            auto emit = [&](OpCode op) { code.push_back(static_cast<uint32_t>(op)); };
            auto operand = [&](uint32_t v) { code.push_back(v); };
            auto push = [&](uint32_t v) {
                emit(OpCode::PUSH_CONSTANT);
                operand(v);
            };
            auto logCall = [&](uint32_t n) {
                emit(OpCode::PUSH_STATIC);
                operand(CallOrder);
                push(10);
                emit(OpCode::MUL);
                push(n);
                emit(OpCode::ADD);
                emit(OpCode::POP_STATIC);
                operand(CallOrder);
            };

            // HitByWeapon
            auto hitAddress = static_cast<unsigned int>(code.size());
            emit(OpCode::PUSH_LOCAL_VAR);
            operand(0);
            emit(OpCode::POP_STATIC);
            operand(CosArg);
            emit(OpCode::PUSH_LOCAL_VAR);
            operand(1);
            emit(OpCode::POP_STATIC);
            operand(SinArg);
            emit(OpCode::PUSH_STATIC);
            operand(HitCount);
            push(1);
            emit(OpCode::ADD);
            emit(OpCode::POP_STATIC);
            operand(HitCount);
            logCall(1);
            if (switchesOff)
            {
                push(2);
                emit(OpCode::SIGNAL);
                push(2);
                emit(OpCode::SET_SIGNAL_MASK);
                push(1); // ACTIVATION
                push(0);
                emit(OpCode::SET_VALUE);
                push(8000);
                emit(OpCode::SLEEP);
                push(1);
                push(1);
                emit(OpCode::SET_VALUE);
            }
            push(0);
            emit(OpCode::RETURN);

            // TakeDamage
            auto takeAddress = static_cast<unsigned int>(code.size());
            emit(OpCode::PUSH_LOCAL_VAR);
            operand(0);
            emit(OpCode::POP_STATIC);
            operand(PercentArg);
            logCall(2);
            push(0);
            emit(OpCode::RETURN);

            script->functions.push_back(CobFunctionInfo{"HitByWeapon", hitAddress});
            script->functions.push_back(CobFunctionInfo{"TakeDamage", takeAddress});
            return script;
        }

        UnitDefinition makeTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            return d;
        }

        struct Fixture
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            PlayerId owner;
            UnitId unit;
            std::shared_ptr<CobScript> script;

            explicit Fixture(bool switchesOff, float energyOut = 0.0f)
            {
                owner = addPlayer(sim, "us");
                std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
                sim.unitModelDefinitions["model"] = createUnitModelDefinition(20_ss, std::move(pieces));
                auto def = makeTargetDef();
                def.energyUse = Energy(-energyOut);
                sim.unitDefinitions["target"] = def;
                script = makeHitScript(switchesOff);
                unit = addUnitOfType(sim, "target", owner, SimVector(100_ss, 0_ss, 100_ss), script);
                sim.getUnitState(unit).hitPoints = 1000;
                sim.getUnitState(unit).activated = true;
            }

            /** A kind-1 hit whose round burst `offset` away from the unit. */
            void hit(unsigned int damage, const SimVector& offset)
            {
                sim.applyDamage(unit, damage, std::nullopt, false, std::nullopt, "weapon", false, sim.getUnitState(unit).position + offset);
            }

            int staticValue(std::size_t i)
            {
                return sim.getUnitState(unit).cobEnvironment->_statics.at(i);
            }
        };
    }

    TEST_CASE("the arguments HitByWeapon is given are the burst direction's cosine and sine times 400", "[hitbyweapon]")
    {
        // 0x499CD0 keeps the high byte of the angle and 0x509F00 is a sine
        // table of amplitude 8192; the products are rounded to nearest.
        SECTION("straight ahead")
        {
            auto [c, s] = computeHitByWeaponArguments(SimAngle(0));
            REQUIRE(c == 400);
            REQUIRE(s == 0);
        }
        SECTION("a quarter turn")
        {
            auto [c, s] = computeHitByWeaponArguments(QuarterTurn);
            REQUIRE(c == 0);
            REQUIRE(s == 400);
        }
        SECTION("behind")
        {
            auto [c, s] = computeHitByWeaponArguments(HalfTurn);
            REQUIRE(c == -400);
            REQUIRE(s == 0);
        }
        SECTION("an eighth of a turn")
        {
            // 8192 * sqrt(1/2) = 5793, times 400 over 8192 = 282.9.
            auto [c, s] = computeHitByWeaponArguments(EighthTurn);
            REQUIRE(c == 283);
            REQUIRE(s == 283);
        }
        SECTION("only the high byte of the angle counts")
        {
            REQUIRE(computeHitByWeaponArguments(SimAngle(0x40FF)) == computeHitByWeaponArguments(SimAngle(0x4000)));
            REQUIRE(computeHitByWeaponArguments(SimAngle(0x4100)) != computeHitByWeaponArguments(SimAngle(0x4000)));
        }
    }

    TEST_CASE("TakeDamage is told the health percentage left, clamped", "[hitbyweapon]")
    {
        REQUIRE(computeTakeDamagePercent(900, 1000) == 90);
        REQUIRE(computeTakeDamagePercent(1, 1000) == 0);
        REQUIRE(computeTakeDamagePercent(1500, 1000) == 100);
        REQUIRE(computeTakeDamagePercent(5, 0) == 0);
    }

    TEST_CASE("a weapon hit that leaves the unit standing starts HitByWeapon and then TakeDamage", "[hitbyweapon]")
    {
        Fixture f(false);

        SECTION("a burst dead ahead, +z of an unturned unit")
        {
            f.hit(100, SimVector(0_ss, 0_ss, 10_ss));
            tick(f.sim, 1);
            REQUIRE(f.staticValue(HitCount) == 1);
            REQUIRE(f.staticValue(CosArg) == 400);
            REQUIRE(f.staticValue(SinArg) == 0);
            REQUIRE(f.staticValue(PercentArg) == 90);
            // HitByWeapon first, then TakeDamage.
            REQUIRE(f.staticValue(CallOrder) == 12);
        }

        SECTION("a burst to the +x side is a quarter turn")
        {
            f.hit(250, SimVector(10_ss, 0_ss, 0_ss));
            tick(f.sim, 1);
            REQUIRE(f.staticValue(CosArg) == 0);
            REQUIRE(f.staticValue(SinArg) == 400);
            REQUIRE(f.staticValue(PercentArg) == 75);
        }

        SECTION("the unit's own heading is taken off")
        {
            f.sim.getUnitState(f.unit).rotation = QuarterTurn;
            f.hit(100, SimVector(10_ss, 0_ss, 0_ss));
            tick(f.sim, 1);
            REQUIRE(f.staticValue(CosArg) == 400);
            REQUIRE(f.staticValue(SinArg) == 0);
        }

        SECTION("a burst behind it")
        {
            f.hit(100, SimVector(0_ss, 0_ss, -10_ss));
            tick(f.sim, 1);
            REQUIRE(f.staticValue(CosArg) == -400);
        }
    }

    TEST_CASE("only an ordinary weapon hit that does not kill runs the scripts", "[hitbyweapon]")
    {
        Fixture f(false);

        SECTION("a killing blow returns before the calls")
        {
            // No tick: the kill runs the unit's scripts itself, so a
            // HitByWeapon started by the hit would already have run.
            f.hit(5000, SimVector(0_ss, 0_ss, 10_ss));
            REQUIRE(f.sim.getUnitState(f.unit).isDead());
            REQUIRE(f.staticValue(HitCount) == 0);
            REQUIRE(f.staticValue(CallOrder) == 0);
        }

        SECTION("damage of another kind (no burst: water, a wipe) never does")
        {
            f.sim.applyDamage(f.unit, 100u, std::nullopt, false, std::nullopt, "acid_water");
            tick(f.sim, 1);
            REQUIRE(f.staticValue(HitCount) == 0);
        }

        SECTION("paralysis takes its own path")
        {
            f.sim.applyDamage(f.unit, 100u, std::nullopt, true, std::nullopt, "weapon", false, f.sim.getUnitState(f.unit).position + SimVector(0_ss, 0_ss, 10_ss));
            tick(f.sim, 1);
            REQUIRE(f.staticValue(HitCount) == 0);
        }

        SECTION("a script without the functions costs nothing")
        {
            auto bare = makeEmptyCobScript({"base"});
            auto other = addUnitOfType(f.sim, "target", f.owner, SimVector(200_ss, 0_ss, 200_ss), bare);
            f.sim.getUnitState(other).hitPoints = 1000;
            f.sim.applyDamage(other, 100u, std::nullopt, false, std::nullopt, "weapon", false, SimVector(200_ss, 0_ss, 210_ss));
            tick(f.sim, 1);
            REQUIRE(f.sim.getUnitState(other).hitPoints == 900);
        }
    }

    TEST_CASE("a shell bursting beside the unit gives the script the direction of the burst", "[hitbyweapon]")
    {
        // The call site in applyDamageInRadius passes the projectile's own
        // position, which is what the direction is measured to. The blast
        // finds its unit through the occupancy grid, so this one is placed
        // the way the game places a unit.
        Fixture f(false);
        auto def = makeTargetDef();
        def.isMobile = true;
        def.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        f.sim.unitDefinitions["mobile"] = def;

        auto env = std::make_unique<CobEnvironment>(f.script.get());
        UnitMesh base;
        base.name = "base";
        std::vector<UnitMesh> pieces{base};
        UnitState unit(pieces, std::move(env));
        unit.unitType = "mobile";
        unit.owner = f.owner;
        unit.position = SimVector(200_ss, 0_ss, 200_ss);
        unit.previousPosition = unit.position;
        unit.hitPoints = 1000;
        auto id = f.sim.tryAddUnit(std::move(unit)).value();

        Projectile p{};
        p.owner = f.owner;
        p.position = SimVector(205_ss, 0_ss, 200_ss);
        p.previousPosition = p.position;
        p.origin = p.position;
        p.damage["DEFAULT"] = 100;
        p.damageRadius = 16_ss;

        f.sim.applyDamageInRadius(p.position, p.damageRadius, p);
        tick(f.sim, 1);

        const auto& hit = f.sim.getUnitState(id);
        REQUIRE(hit.hitPoints == 900);
        const auto& statics = hit.cobEnvironment->_statics;
        REQUIRE(statics.at(HitCount) == 1);
        REQUIRE(statics.at(CosArg) == 0);
        REQUIRE(statics.at(SinArg) == 400);
        REQUIRE(statics.at(PercentArg) == 90);
    }

    TEST_CASE("a hit solar collector switches off for eight seconds, and a second hit restarts the wait", "[hitbyweapon][economy]")
    {
        // armsolar, corsolar and armarad: HitByWeapon sets the unit's
        // activation to 0, sleeps 8000 ms and sets it back, with a signal so
        // that a new hit restarts the wait. The engine's part is only to run
        // the script; production stopping is the activation gate already in
        // updateResources.
        Fixture f(true, 20.0f);

        auto produced = [&] { return f.sim.getPlayer(f.owner).energyProduced.value; };

        SECTION("an unhit collector makes its twenty a second")
        {
            tick(f.sim, 90);
            REQUIRE(produced() == Approx(60.0f).margin(20.0f));
            REQUIRE(f.sim.getUnitState(f.unit).activated);
        }

        SECTION("a hit one makes nothing for eight seconds and then makes it again")
        {
            f.hit(10, SimVector(0_ss, 0_ss, 10_ss));
            tick(f.sim, 1);
            REQUIRE_FALSE(f.sim.getUnitState(f.unit).activated);

            auto before = produced();
            tick(f.sim, 200);
            REQUIRE(produced() == Approx(before));
            REQUIRE_FALSE(f.sim.getUnitState(f.unit).activated);

            tick(f.sim, 100);
            REQUIRE(f.sim.getUnitState(f.unit).activated);
            REQUIRE(produced() > before);
        }

        SECTION("a second hit restarts the eight seconds")
        {
            f.hit(10, SimVector(0_ss, 0_ss, 10_ss));
            tick(f.sim, 150);
            REQUIRE_FALSE(f.sim.getUnitState(f.unit).activated);

            f.hit(10, SimVector(0_ss, 0_ss, 10_ss));
            // 150 + 150 ticks is past the first wait's 240 but not the second's.
            tick(f.sim, 150);
            REQUIRE_FALSE(f.sim.getUnitState(f.unit).activated);

            tick(f.sim, 150);
            REQUIRE(f.sim.getUnitState(f.unit).activated);
        }
    }
}
