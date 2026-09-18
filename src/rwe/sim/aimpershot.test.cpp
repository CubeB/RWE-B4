#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <vector>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeAimTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** The commander's laser exactly as rev31's WEAPONS.TDF ships it. */
        constexpr const char* ArmComLaserTdf = R"TDF(
[ARMCOMLASER]
	{
	ID=20;
	name=J7 Laser;
	rendertype=0;
	lineofsight=1;
	turret=1;

	range=200;
	reloadtime=.85;
	weaponvelocity=400;
	areaofeffect=16;
	duration=.03;
	soundtrigger=1;

	soundstart=lasrfir1;
	soundhit=lasrhit2;

	firestarter=70;
	beamweapon=1;
	color=232;	/* GREEN */
	color2=234;

	explosiongaf=fx;
	explosionart=explode5;

	waterexplosiongaf=fx;
	waterexplosionart=h2oboom1;

	lavaexplosiongaf=fx;
	lavaexplosionart=lavasplashsm;

	[DAMAGE]
		{
		default=60;
		}
	}
)TDF";

        WeaponDefinition shippedCommanderLaser()
        {
            auto tdf = parseTdfFromString(ArmComLaserTdf);
            auto block = tdf.findBlock("ARMCOMLASER");
            REQUIRE(block.has_value());
            return parseWeaponDefinition(parseWeaponBlock(block->get()));
        }

        constexpr unsigned int StaticAims = 0;
        constexpr unsigned int StaticShots = 1;

        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        void pushIncrementStatic(CobScript& script, unsigned int index)
        {
            push(script, OpCode::PUSH_STATIC);
            push(script, index);
            push(script, OpCode::PUSH_CONSTANT);
            push(script, 1u);
            push(script, OpCode::ADD);
            push(script, OpCode::POP_STATIC);
            push(script, index);
        }

        /**
         * A tally-keeper: `AimPrimary` adds one to static 0 and says yes,
         * `FirePrimary` adds one to static 1. The shipped ARMCOM script turns
         * its torso and arm and waits for both before answering; this one
         * answers at once, so that what is counted is how often the engine
         * asks and nothing about how long a turn takes.
         */
        std::shared_ptr<CobScript> makeCountingScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 2;
            script->pieces.push_back("base");

            script->functions.push_back(CobFunctionInfo{"AimPrimary", static_cast<unsigned int>(script->instructions.size())});
            pushIncrementStatic(*script, StaticAims);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 1u);
            push(*script, OpCode::RETURN);

            script->functions.push_back(CobFunctionInfo{"FirePrimary", static_cast<unsigned int>(script->instructions.size())});
            pushIncrementStatic(*script, StaticShots);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u);
            push(*script, OpCode::RETURN);

            return script;
        }

        void registerAimModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineAimUnit(GameSimulation& sim, const std::string& type, unsigned int maxHitPoints, bool canAttack)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = canAttack;
            d.sightDistance = 1000u;
            d.maxHitPoints = maxHitPoints;
            d.buildTime = 0u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        UnitId spawnAimUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        int readStatic(GameSimulation& sim, UnitId id, unsigned int index)
        {
            return sim.getUnitState(id).cobEnvironment->_statics.at(index);
        }

        /** Ticks until the shot counter reaches `shots`, or gives up after `limit` ticks. */
        bool tickUntilShots(GameSimulation& sim, UnitId id, int shots, int limit)
        {
            for (int i = 0; i < limit; ++i)
            {
                if (readStatic(sim, id, StaticShots) >= shots)
                {
                    return true;
                }
                sim.tick();
            }
            return readStatic(sim, id, StaticShots) >= shots;
        }

        struct AimFixture
        {
            std::shared_ptr<CobScript> script = makeCountingScript();
            GameSimulation sim{makeAimTerrain(), 0u, 0, 0};
            PlayerId us;
            PlayerId them;
            UnitId commander;

            AimFixture()
            {
                us = addPlayer(sim, "us");
                them = addPlayer(sim, "them");
                registerAimModel(sim);

                // ARMCOM.FBI: MaxDamage=3000, Weapon1=ARMCOMLASER. The hit
                // points matter because the reload is scaled by damage taken.
                defineAimUnit(sim, "ARMCOM", 3000u, true);
                defineAimUnit(sim, "VICTIM", 1000000u, false);
                sim.weaponDefinitions["ARMCOMLASER"] = shippedCommanderLaser();

                commander = spawnAimUnit(sim, "ARMCOM", us, SimVector(0_ss, 0_ss, 0_ss), script);
                UnitWeapon weapon;
                weapon.weaponType = "ARMCOMLASER";
                sim.getUnitState(commander).weapons[0] = weapon;
            }
        };
    }

    TEST_CASE("an aim script runs once per shot, not once per tick of the reload", "[weapon][cob][aim]")
    {
        // Upstream #42: "RWE seems to run the aiming script twice even when
        // attacking ground, where running the aiming script once should be
        // sufficient. TA seems to manage to run it once." Observed on ARMCOM.
        //
        // The original starts the aim, sets bit 0 of the slot's flag byte and
        // does not start another while that bit is up (0x49E211). While the
        // reload counter is running it does not even look (0x49E3AE), and the
        // bit comes down only when the shot is spawned (0x49D78B), when the
        // fire-time recheck finds the target has moved past tolerance
        // (0x49D68A), or when the target is lost. RWE used to drop back to
        // idle whenever a successful aim found the reload unfinished, and so
        // ran the script again, and again, for the whole of every reload.
        AimFixture f;

        SECTION("attacking the ground")
        {
            f.sim.getUnitState(f.commander).orders.push_back(AttackOrder(SimVector(0_ss, 0_ss, 150_ss)));

            REQUIRE(tickUntilShots(f.sim, f.commander, 1, 60));
            REQUIRE(readStatic(f.sim, f.commander, StaticAims) == 1);

            // Ten seconds of the 0.85 s reload.
            tick(f.sim, 300);

            auto shots = readStatic(f.sim, f.commander, StaticShots);
            auto aims = readStatic(f.sim, f.commander, StaticAims);
            REQUIRE(shots >= 10);

            // One aim before the first shot and one after each shot since:
            // the one after the latest may or may not have started yet.
            REQUIRE(aims >= shots);
            REQUIRE(aims <= shots + 1);
        }

        SECTION("a target that moves during the reload is aimed at again before the shot")
        {
            // The recheck is what stops a stale aim going off: the angles
            // the script was given are compared against fresh ones when the
            // reload runs out, and a miss by more than tolerance starts
            // another aim rather than a shot.
            auto victim = spawnAimUnit(f.sim, "VICTIM", f.them, SimVector(0_ss, 0_ss, 150_ss), f.script);
            f.sim.getUnitState(f.commander).orders.push_back(AttackOrder(victim));

            REQUIRE(tickUntilShots(f.sim, f.commander, 1, 60));

            // The tick after a shot is when the next aim goes out, at the
            // target where it stands then.
            f.sim.tick();
            REQUIRE(readStatic(f.sim, f.commander, StaticAims) == 2);

            SECTION("standing still, that aim is the one that fires")
            {
                REQUIRE(tickUntilShots(f.sim, f.commander, 2, 60));
                REQUIRE(readStatic(f.sim, f.commander, StaticAims) == 2);
            }

            SECTION("moved a hundred units sideways, it is aimed at once more")
            {
                // About 34 degrees of bearing, far past any tolerance.
                auto& v = f.sim.getUnitState(victim);
                v.position = SimVector(100_ss, 0_ss, 150_ss);
                v.previousPosition = v.position;

                REQUIRE(tickUntilShots(f.sim, f.commander, 2, 60));
                REQUIRE(readStatic(f.sim, f.commander, StaticAims) == 3);
            }
        }
    }
}
