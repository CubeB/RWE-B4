#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <string>

/**
 * Core Contingency's mines, and the DefaultMissionType key that makes them go
 * off (issue #108). A mine has no weapon, cannot be ordered to attack, and is
 * kamikaze; what sets it off is Standby_Mine (0x406090), the mission it is
 * given whenever it is idle, which looks round with the sight-range search
 * every rand(30)+30 ticks and pushes SELFDESTRUCT when it sees something
 * standing on the ground. TOTALA-EXE-WEAPONS.md §9 has the handler.
 *
 * The mine is ccdata.ccx's ARMMINE1, as far as these rules read it; ARMMINE6
 * differs where it matters, starting on Hold Fire with a two-second count.
 */
namespace rwe
{
    namespace
    {
        UnitFbi fbiFrom(const std::string& unitName, const std::string& keys)
        {
            return parseUnitFbi(parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n"));
        }

        UnitDefinition definitionFrom(const std::string& unitName, const std::string& keys)
        {
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbiFrom(unitName, keys), movementClasses);
        }

        // ccdata.ccx units/ARMMINE1.FBI, the keys the rules here read.
        const char* ArmMine1Keys =
            "FootprintX=1;\nFootprintZ=1;\nBuildCostEnergy=1017;\nBuildCostMetal=32;\n"
            "MaxDamage=100;\nBuildTime=1322;\nBMcode=0;\nSightDistance=55;\n"
            "ExplodeAs=ARMMINE1;\nSelfDestructAs=ARMMINE1;\nfirestandorders=0;\n"
            "StandingFireOrder=2;\ncanmove=0;\ncanattack=0;\nYardMap=o;\n"
            "DefaultMissionType=Standby_Mine;\nkamikaze=1;\nselfdestructcountdown=1;\n"
            "CloakCost=7;\ninit_cloaked=1;\nmincloakdistance=10;";

        // rev31 units/ARMPW.FBI, what walks onto it.
        const char* ArmPwKeys =
            "FootprintX=2;\nFootprintZ=2;\nBuildCostEnergy=697;\nBuildCostMetal=53;\n"
            "MaxDamage=250;\nBuildTime=1452;\nBMcode=1;\nShootMe=1;\nSightDistance=280;\n"
            "DefaultMissionType=Standby;";

        // rev31 units/ARMPEEP.FBI, the scout plane, as far as the rule reads it.
        const char* ArmPeepKeys =
            "FootprintX=2;\nFootprintZ=2;\nBMcode=1;\ncanfly=1;\nShootMe=1;\nSightDistance=500;\n"
            "DefaultMissionType=VTOL_Standby;";

        struct Minefield
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};
            std::shared_ptr<CobScript> script = makeEmptyCobScript();
            PlayerId layer;
            PlayerId walker;
            UnitId mine;

            explicit Minefield(const std::string& mineKeys = ArmMine1Keys)
            {
                layer = addPlayer(sim, "layer");
                walker = addPlayer(sim, "walker");
                sim.unitDefinitions["ARMMINE1"] = definitionFrom("ARMMINE1", mineKeys);
                sim.unitDefinitions["ARMPW"] = definitionFrom("ARMPW", ArmPwKeys);
                sim.unitDefinitions["ARMPEEP"] = definitionFrom("ARMPEEP", ArmPeepKeys);
                // The blast the mine goes off as. Its numbers are beside the
                // point here; that there is one to look up is not.
                WeaponDefinition blast{};
                blast.damage["DEFAULT"] = 100u;
                sim.weaponDefinitions["ARMMINE1"] = blast;
                mine = addUnitOfType(sim, "ARMMINE1", layer, SimVector(0_ss, 0_ss, 0_ss), script);
                sim.getUnitState(mine).fireOrders = sim.unitDefinitions.at("ARMMINE1").standingFireOrder;
            }

            /** Ticks until the mine starts counting down, or nothing within the budget. */
            std::optional<int> ticksUntilArmed(int budget)
            {
                for (int i = 1; i <= budget; ++i)
                {
                    sim.tick();
                    if (sim.getUnitState(mine).selfDestructTime)
                    {
                        return i;
                    }
                }
                return std::nullopt;
            }
        };
    }

    TEST_CASE("DefaultMissionType names one of the four missions the data uses", "[fbi][mine]")
    {
        CHECK(parseDefaultMission("Standby") == DefaultMission::Standby);
        CHECK(parseDefaultMission("GUARD_NOMOVE") == DefaultMission::GuardNoMove);
        CHECK(parseDefaultMission("VTOL_standby") == DefaultMission::VtolStandby);
        CHECK(parseDefaultMission("Standby_Mine") == DefaultMission::StandbyMine);
        CHECK(parseDefaultMission("") == DefaultMission::None);
        CHECK(parseDefaultMission("Standby_Everything") == DefaultMission::None);
        // A mission the original knows, and RWE does not give an idle unit.
        CHECK(parseDefaultMission("VTOL_Patrol") == DefaultMission::Unported);
        CHECK(parseDefaultMission("attack_kamikaze") == DefaultMission::Unported);

        SECTION("and says so at load only for one it knows and does not route")
        {
            CHECK_FALSE(defaultMissionWarning(fbiFrom("ARMMINE1", ArmMine1Keys)).has_value());
            CHECK_FALSE(defaultMissionWarning(fbiFrom("ARMSOLAR", "BMcode=0;")).has_value());
            auto warning = defaultMissionWarning(fbiFrom("MODUNIT", "DefaultMissionType=VTOL_Patrol;"));
            REQUIRE(warning.has_value());
            CHECK(warning->find("MODUNIT") != std::string::npos);
            CHECK(warning->find("VTOL_Patrol") != std::string::npos);
        }
    }

    TEST_CASE("selfdestructcountdown is five seconds unless the FBI says otherwise", "[fbi][mine]")
    {
        CHECK(definitionFrom("ARMPW", ArmPwKeys).selfDestructCountdown == 5u);
        CHECK(definitionFrom("ARMMINE1", ArmMine1Keys).selfDestructCountdown == 1u);
        CHECK(definitionFrom("ARMMINE6", "selfdestructcountdown=2;").selfDestructCountdown == 2u);
        CHECK(definitionFrom("NOW", "selfdestructcountdown=0;").selfDestructCountdown == 0u);
    }

    TEST_CASE("a mine goes off when something walks up to it", "[mine]")
    {
        Minefield field;
        field.sim.tick();

        SECTION("nothing about: it waits, and keeps looking")
        {
            REQUIRE_FALSE(field.ticksUntilArmed(300).has_value());
            REQUIRE(field.sim.getUnitState(field.mine).minePollAt.has_value());
        }

        SECTION("a Peewee inside its sight: armed within a look, with its own one-second count")
        {
            addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(40_ss, 0_ss, 0_ss), field.script);
            // The first look is a tick after it goes idle and the longest
            // sleep between looks is 59 ticks, so a walker standing there is
            // found inside sixty.
            auto armedAfter = field.ticksUntilArmed(61);
            REQUIRE(armedAfter.has_value());
            const auto& mine = field.sim.getUnitState(field.mine);
            REQUIRE(*mine.selfDestructTime == field.sim.gameTime + GameTime(30));
            REQUIRE_FALSE(mine.minePollAt.has_value());

            // And it goes off.
            for (int i = 0; i < 31 && field.sim.tryGetUnitState(field.mine) && field.sim.getUnitState(field.mine).isAlive(); ++i)
            {
                field.sim.tick();
            }
            auto after = field.sim.tryGetUnitState(field.mine);
            REQUIRE((!after || after->get().isDead()));
        }

        SECTION("outside its sight: nothing")
        {
            addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(70_ss, 0_ss, 0_ss), field.script);
            REQUIRE_FALSE(field.ticksUntilArmed(300).has_value());
        }

        SECTION("an aircraft overhead does not set it off")
        {
            // Held in the air over the mine: left to itself an idle plane
            // lands, and a landed one is on the ground and does set it off.
            auto plane = addUnitOfType(field.sim, "ARMPEEP", field.walker, SimVector(20_ss, 0_ss, 0_ss), field.script);
            for (int i = 0; i < 300; ++i)
            {
                auto& p = field.sim.getUnitState(plane);
                p.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                p.position = SimVector(20_ss, 0_ss, 0_ss);
                field.sim.tick();
                REQUIRE_FALSE(field.sim.getUnitState(field.mine).selfDestructTime.has_value());
            }
        }

        SECTION("its own side's units do not set it off")
        {
            addUnitOfType(field.sim, "ARMPW", field.layer, SimVector(20_ss, 0_ss, 0_ss), field.script);
            REQUIRE_FALSE(field.ticksUntilArmed(300).has_value());
        }

        SECTION("on Hold Fire it lets them walk past")
        {
            field.sim.getUnitState(field.mine).fireOrders = UnitFireOrders::HoldFire;
            addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(40_ss, 0_ss, 0_ss), field.script);
            REQUIRE_FALSE(field.ticksUntilArmed(300).has_value());
        }
    }

    TEST_CASE("the heavy mine starts on Hold Fire and counts two seconds once armed", "[mine]")
    {
        // ARMMINE6: StandingFireOrder=0 with the fire-orders button, and
        // selfdestructcountdown=2. Its other keys are ARMMINE1's here.
        std::string keys = ArmMine1Keys;
        keys.replace(keys.find("StandingFireOrder=2"), 19, "StandingFireOrder=0");
        keys.replace(keys.find("selfdestructcountdown=1"), 23, "selfdestructcountdown=2");
        Minefield field(keys);
        REQUIRE(field.sim.getUnitState(field.mine).fireOrders == UnitFireOrders::HoldFire);
        addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(40_ss, 0_ss, 0_ss), field.script);
        field.sim.tick();
        REQUIRE_FALSE(field.ticksUntilArmed(120).has_value());

        field.sim.getUnitState(field.mine).fireOrders = UnitFireOrders::FireAtWill;
        REQUIRE(field.ticksUntilArmed(61).has_value());
        REQUIRE(*field.sim.getUnitState(field.mine).selfDestructTime == field.sim.gameTime + GameTime(60));
    }

    TEST_CASE("a mine does nothing while it is still being built", "[mine]")
    {
        Minefield field;
        field.sim.getUnitState(field.mine).buildTimeCompleted = 0;
        addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(40_ss, 0_ss, 0_ss), field.script);
        field.sim.tick();
        REQUIRE_FALSE(field.ticksUntilArmed(120).has_value());
        REQUIRE_FALSE(field.sim.getUnitState(field.mine).minePollAt.has_value());
    }

    TEST_CASE("a self-destruct ordered by hand counts the definition's own seconds", "[mine]")
    {
        Minefield field;
        auto peewee = addUnitOfType(field.sim, "ARMPW", field.walker, SimVector(400_ss, 0_ss, 0_ss), field.script);
        field.sim.toggleSelfDestruct(peewee);
        CHECK(*field.sim.getUnitState(peewee).selfDestructTime == field.sim.gameTime + GameTime(5 * 30));
        field.sim.toggleSelfDestruct(field.mine);
        CHECK(*field.sim.getUnitState(field.mine).selfDestructTime == field.sim.gameTime + GameTime(30));
    }
}
