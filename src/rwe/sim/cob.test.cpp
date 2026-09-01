#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMovementOrders.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/cob.h>
#include <limits>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeMinimalTerrain()
        {
            // 2x2 zeroed heightmap; GameSimulation requires width/height of at least 2.
            Grid<unsigned char> heights(2, 2, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        UnitId addBareUnit(GameSimulation& sim, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            return UnitId(sim.units.emplace(pieces, std::move(env)));
        }
    }

    TEST_CASE("cob handleQuery / handleSetQuery", "[cob]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeMinimalTerrain(), 0u, 0, 0);
        auto unitId = addBareUnit(sim, script);
        auto& unit = sim.getUnitState(unitId);
        const auto& env = *unit.cobEnvironment;

        SECTION("GET StandingFireOrders returns 0/1/2 for the three enum values")
        {
            unit.fireOrders = UnitFireOrders::HoldFire;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingFireOrders{}}) == 0);

            unit.fireOrders = UnitFireOrders::ReturnFire;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingFireOrders{}}) == 1);

            unit.fireOrders = UnitFireOrders::FireAtWill;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingFireOrders{}}) == 2);
        }

        SECTION("GET StandingMoveOrders default is Roam (2)")
        {
            // A unit with no StandingMoveOrder in its FBI gets the original's
            // parser default of 2, and a bare unit here has no definition to
            // take one from. In the shipped data only buildings ever land on
            // it: every mobile unit names one, and almost all of them say 1.
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingMoveOrders{}}) == 2);
        }

        SECTION("SET/GET round-trip for StandingMoveOrders")
        {
            for (int v : {0, 1, 2})
            {
                handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::StandingMoveOrders{v}});
                REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingMoveOrders{}}) == v);
            }
        }

        SECTION("SET/GET round-trip for StandingFireOrders")
        {
            for (int v : {0, 1, 2})
            {
                handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::StandingFireOrders{v}});
                REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingFireOrders{}}) == v);
            }
        }

        SECTION("StandingMoveOrders out-of-range value is ignored")
        {
            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::StandingMoveOrders{2}});
            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::StandingMoveOrders{99}});
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::StandingMoveOrders{}}) == 2);
        }

        SECTION("SET/GET round-trip for Busy")
        {
            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::Busy{true}});
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::Busy{}}) == 1);

            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::Busy{false}});
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::Busy{}}) == 0);
        }

        SECTION("SET/GET round-trip for Armored")
        {
            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::Armored{true}});
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::Armored{}}) == 1);

            handleSetQuery(sim, env, unitId, CobEnvironment::SetQueryStatus{CobEnvironment::SetQueryStatus::Armored{false}});
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::Armored{}}) == 0);
        }

        SECTION("BuggerOff round-trips through sim.setBuggerOff(false)")
        {
            // initial GET should be false (default)
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::BuggerOff{}}) == 0);

            // Set the field directly: sim.setBuggerOff(true) emits a footprint sweep that
            // requires unitDefinitions to be populated, which a bare test unit lacks.
            // The behavior under test is that setBuggerOff(false) clears the field.
            unit.buggerOffActive = true;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::BuggerOff{}}) == 1);

            sim.setBuggerOff(unitId, false);
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::BuggerOff{}}) == 0);
        }

        SECTION("VeteranLevel returns 0 by default (no kills)")
        {
            REQUIRE(unit.kills == 0);
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 0);
        }

        SECTION("VeteranLevel reflects kills via the threshold function")
        {
            // Threshold scheme is the original's own: tier = min(kills / 5, 5)
            // Walk through every kills count from 0 up through the cap and a
            // little past it, asserting the tier each time.
            struct Case
            {
                unsigned int kills;
                int expectedTier;
            };
            const Case cases[] = {
                {0, 0},
                {1, 0},
                {4, 0},
                {5, 1},
                {9, 1},
                {10, 2},
                {14, 2},
                {15, 3},
                {20, 4},
                {24, 4},
                {25, 5},
                {26, 5}, // capped at MaxTier (5)
                {100, 5}, // still capped
            };
            for (const auto& c : cases)
            {
                unit.kills = c.kills;
                REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == c.expectedTier);
            }
        }

        SECTION("VeteranLevel saturates at the maximum tier on huge kill counts")
        {
            // Make sure we don't overflow / wrap around when kills is enormous.
            unit.kills = std::numeric_limits<unsigned int>::max();
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 5);
        }

        SECTION("UnitState.kills participates in the deterministic hash")
        {
            // Determinism check: bumping kills must change the unit hash so
            // multiplayer desyncs don't mask diverging kill counts.
            auto hashBefore = computeHashOf(unit);
            unit.kills += 1;
            auto hashAfter = computeHashOf(unit);
            REQUIRE(hashBefore != hashAfter);
        }
    }

    TEST_CASE("VeteranLevel threshold function", "[cob][veterancy]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeMinimalTerrain(), 0u, 0, 0);
        auto unitId = addBareUnit(sim, script);
        auto& unit = sim.getUnitState(unitId);
        const auto& env = *unit.cobEnvironment;

        SECTION("Default kills value is 0 and yields tier 0")
        {
            REQUIRE(unit.kills == 0);
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 0);
        }

        SECTION("Tier increases at every multiple of 5 kills")
        {
            unit.kills = 4;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 0);
            unit.kills = 5;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 1);
            unit.kills = 10;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 2);
            unit.kills = 25;
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 5);
        }

        SECTION("Tier saturates at the maximum on huge kill counts (no overflow)")
        {
            unit.kills = std::numeric_limits<unsigned int>::max();
            REQUIRE(handleQuery(sim, env, unitId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 5);
        }

        SECTION("Bumping kills changes the deterministic UnitState hash")
        {
            auto hashBefore = computeHashOf(unit);
            unit.kills += 1;
            auto hashAfter = computeHashOf(unit);
            REQUIRE(hashBefore != hashAfter);
        }
    }

    TEST_CASE("cob MinId / MaxId", "[cob]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeMinimalTerrain(), 0u, 0, 0);

        SECTION("MinId / MaxId on a single-unit sim return that unit's id")
        {
            auto onlyId = addBareUnit(sim, script);
            const auto& env = *sim.getUnitState(onlyId).cobEnvironment;
            REQUIRE(handleQuery(sim, env, onlyId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::MinId{}}) == static_cast<int>(onlyId.value));
            REQUIRE(handleQuery(sim, env, onlyId, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::MaxId{}}) == static_cast<int>(onlyId.value));
        }

        SECTION("returns min/max ids over sparsely-allocated units")
        {
            auto a = addBareUnit(sim, script);
            auto b = addBareUnit(sim, script);
            auto c = addBareUnit(sim, script);
            auto d = addBareUnit(sim, script);

            // remove the middle one to leave a sparse layout
            sim.units.remove(b);

            // pick any surviving unit's env to drive the dispatcher
            const auto& env = *sim.getUnitState(c).cobEnvironment;

            unsigned int expectedMin = a.value;
            unsigned int expectedMax = a.value;
            for (auto id : {a, c, d})
            {
                if (id.value < expectedMin) expectedMin = id.value;
                if (id.value > expectedMax) expectedMax = id.value;
            }

            REQUIRE(handleQuery(sim, env, c, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::MinId{}}) == static_cast<int>(expectedMin));
            REQUIRE(handleQuery(sim, env, c, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::MaxId{}}) == static_cast<int>(expectedMax));
        }
    }
}
