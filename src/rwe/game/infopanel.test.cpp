#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    namespace
    {
        UnitState makeUnit()
        {
            // The footer only ever reads orders, kills and the three states
            // that displace an order, so an empty shell is enough here. A
            // mesh list and a script are what a real unit would carry.
            return UnitState({}, nullptr);
        }
    }

    TEST_CASE("missionDisplayName: the strings are the original's own", "[infopanel]")
    {
        // Transcribed from the mission tables at 0x4FC490 (ground) and
        // 0x4FCA18 (air): each record's +0x00 is the display name and
        // 0x439DF0 hands the footer the one belonging to the current
        // mission. If any of these look wrong, they are still what the
        // shipped executable draws.
        REQUIRE(std::string(missionDisplayName(UnitActivity::Standby)) == "Standby");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Moving)) == "Moving");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Attacking)) == "Attacking");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Annihilating)) == "Annihilating");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Nanolathing)) == "Nanolathing");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Guarding)) == "Guarding");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Reclaiming)) == "Reclaiming");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Repairing)) == "Repairing");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Patrolling)) == "Patrolling");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Capturing)) == "Capturing");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Loading)) == "Loading");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Unloading)) == "Unloading");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Landing)) == "Landing");
        REQUIRE(std::string(missionDisplayName(UnitActivity::UnderRepair)) == "Under repair");
        REQUIRE(std::string(missionDisplayName(UnitActivity::UnderConstruction)) == "Under construction");
        REQUIRE(std::string(missionDisplayName(UnitActivity::BeingTransported)) == "Being transported");
        REQUIRE(std::string(missionDisplayName(UnitActivity::Paralyzed)) == "Paralyzed");
        REQUIRE(std::string(missionDisplayName(UnitActivity::SelfDestructing)) == "SELF DESTRUCT ENGAGED");
    }

    TEST_CASE("unitActivity: an idle unit is on standby", "[infopanel]")
    {
        auto unit = makeUnit();
        REQUIRE(unitActivity(unit, false, false) == UnitActivity::Standby);
    }

    TEST_CASE("unitActivity: the order at the front is what the footer reports", "[infopanel]")
    {
        auto unit = makeUnit();
        unit.orders.push_back(MoveOrder(SimVector(0_ss, 0_ss, 0_ss)));
        unit.orders.push_back(PatrolOrder(SimVector(0_ss, 0_ss, 0_ss)));
        REQUIRE(unitActivity(unit, false, false) == UnitActivity::Moving);
    }

    TEST_CASE("unitActivity: a launcher with a round on order is nanolathing", "[infopanel]")
    {
        // BuildWeapon is ground mission 9 and shares its display name with
        // the two build missions. RWE keeps the queue on the weapon rather
        // than in the order list, so it is passed in separately.
        auto unit = makeUnit();
        REQUIRE(unitActivity(unit, false, true) == UnitActivity::Nanolathing);
    }

    TEST_CASE("unitActivity: the displacing states win over the order list", "[infopanel]")
    {
        // GetBuilt, BeCarried, Paralyze and SelfDestruct are missions in
        // their own right in the original and are pushed in front of
        // whatever the unit was doing.
        auto unit = makeUnit();
        unit.orders.push_back(MoveOrder(SimVector(0_ss, 0_ss, 0_ss)));

        REQUIRE(unitActivity(unit, true, false) == UnitActivity::UnderConstruction);

        unit.carriedBy = UnitId(3);
        REQUIRE(unitActivity(unit, false, false) == UnitActivity::BeingTransported);
        unit.carriedBy = std::nullopt;

        unit.paralyzedUntil = GameTime(100);
        REQUIRE(unitActivity(unit, false, false) == UnitActivity::Paralyzed);
        unit.paralyzedUntil = std::nullopt;

        unit.selfDestructTime = GameTime(100);
        REQUIRE(unitActivity(unit, true, false) == UnitActivity::SelfDestructing);
    }

    TEST_CASE("killsCaption: nothing at all until the first kill", "[infopanel]")
    {
        // 0x46B2C8 compares the kill count with zero and jumps over the whole
        // block, so a unit that has killed nothing shows no line rather than
        // a zero.
        REQUIRE(killsCaption(0).empty());
    }

    TEST_CASE("killsCaption: singular at one, plural after", "[infopanel]")
    {
        REQUIRE(killsCaption(1) == "1 kill");
        REQUIRE(killsCaption(2) == "2 kills");
        REQUIRE(killsCaption(4) == "4 kills");
    }

    TEST_CASE("killsCaption: Veteran from the fifth kill", "[infopanel]")
    {
        // The comparison is jbe against 4 at 0x46B30D: four kills take the
        // plain "%d %s", five take "%d %s - %s" with Veteran in the third
        // slot. There is no other veterancy display in the binary.
        REQUIRE(killsCaption(4) == "4 kills");
        REQUIRE(killsCaption(5) == "5 kills - Veteran");
        REQUIRE(killsCaption(30) == "30 kills - Veteran");
    }

    TEST_CASE("unitOrderTargetUnit: an idle unit points at nothing", "[infopanel]")
    {
        auto unit = makeUnit();
        REQUIRE(!unitOrderTargetUnit(unit).has_value());
    }

    TEST_CASE("unitOrderTargetUnit: it is the mission's target, not only a build target", "[infopanel]")
    {
        // 0x439DD0 returns mission+0x16 whatever the mission is, which is why
        // the footer's second slot shows what a guard is guarding as readily
        // as what a builder is building.
        auto unit = makeUnit();
        unit.orders.push_back(GuardOrder(UnitId(7)));
        REQUIRE(unitOrderTargetUnit(unit) == UnitId(7));

        unit.orders.clear();
        unit.orders.push_back(RepairOrder(UnitId(9)));
        REQUIRE(unitOrderTargetUnit(unit) == UnitId(9));

        unit.orders.clear();
        unit.orders.push_back(AttackOrder(UnitId(11)));
        REQUIRE(unitOrderTargetUnit(unit) == UnitId(11));
    }

    TEST_CASE("unitOrderTargetUnit: an attack on a place has no target unit", "[infopanel]")
    {
        auto unit = makeUnit();
        unit.orders.push_back(AttackOrder(SimVector(1_ss, 2_ss, 3_ss)));
        REQUIRE(!unitOrderTargetUnit(unit).has_value());
    }

    TEST_CASE("unitOrderTargetUnit: a build order points at the nanoframe once there is one", "[infopanel]")
    {
        auto unit = makeUnit();
        unit.orders.push_back(BuildOrder("ARMSOLAR", SimVector(0_ss, 0_ss, 0_ss)));
        REQUIRE(!unitOrderTargetUnit(unit).has_value());

        unit.buildOrderUnitId = UnitId(4);
        REQUIRE(unitOrderTargetUnit(unit) == UnitId(4));
    }
}
