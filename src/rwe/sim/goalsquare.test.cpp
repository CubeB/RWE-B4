#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <optional>
#include <vector>

/**
 * BT-96, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md on branch spec-room-findings
 * (static byte-tactics reading at 97d7ac59, not executed against the
 * original): a ground Move ends when the unit stands on the goal's map square,
 * not when it is within eight world units of the point. The Move mission asks
 * for a point goal of radius tolerance + 4; the goal turns it into a square
 * with the unit's footprint correction and divides the radius by sixteen, so a
 * plain Move wants zero squares of slack.
 */
namespace rwe
{
    namespace
    {
        UnitDefinition makeWalkerDef(bool canFly)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = canFly;
            d.maxVelocity = 1.8_ssf;
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.19_ssf;
            d.turnRate = 1120_ss;
            d.maxHitPoints = 250;
            d.buildTime = 0u;
            d.sightDistance = 280u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 17u, 255u, 0u, 12u};
            return d;
        }

        GameSimulation makeSquareSim()
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
            sim.unitDefinitions["walker"] = makeWalkerDef(false);
            sim.unitDefinitions["flier"] = makeWalkerDef(true);
            CobScript empty;
            empty.staticVariableCount = 0;
            empty.pieces.push_back("base");
            sim.unitScriptDefinitions["walker"] = empty;
            sim.unitScriptDefinitions["flier"] = empty;
            return sim;
        }

        UnitId spawnAt(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        bool reached(GameSimulation& sim, UnitId id, const SimVector& goal)
        {
            const auto& unit = sim.getUnitState(id);
            return hasReachedGoal(sim, sim.terrain, unit, sim.unitDefinitions.at(unit.unitType), goal);
        }
    }

    // A footprint-2 unit's square is the top-left cell of its footprint, rounded
    // to nearest: (position - 16) / 16 rounds to the cell, so the squares run
    // [16k + 8, 16k + 24) on each axis. x 40..55.99 is one square.
    TEST_CASE("a ground unit has reached a point goal on the goal's map square, not within eight units", "[goalsquare]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSquareSim();
        auto player = addPlayer(sim);
        auto id = spawnAt(sim, "walker", player, SimVector(41_ss, 0_ss, 200_ss), script);

        // Two units from the goal and across a square boundary: arrived under
        // the old eight-unit disc, not arrived on the squares.
        REQUIRE_FALSE(reached(sim, id, SimVector(39_ss, 0_ss, 200_ss)));

        // Fourteen units from the goal and on the same square: not arrived
        // under the old disc, arrived on the squares.
        REQUIRE(reached(sim, id, SimVector(55_ss, 0_ss, 200_ss)));

        // The same square on the other axis, and the square next door on it.
        REQUIRE(reached(sim, id, SimVector(41_ss, 0_ss, 207_ss)));
        REQUIRE_FALSE(reached(sim, id, SimVector(41_ss, 0_ss, 224_ss)));
    }

    TEST_CASE("an aircraft keeps its own goal handling rather than the map square", "[goalsquare]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSquareSim();
        auto player = addPlayer(sim);
        auto id = spawnAt(sim, "flier", player, SimVector(41_ss, 0_ss, 200_ss), script);

        // Two units off across a square boundary is still close enough for a flier.
        REQUIRE(reached(sim, id, SimVector(39_ss, 0_ss, 200_ss)));
        // Fourteen units off on the same square is not (it wants max(8, 4 x speed)).
        REQUIRE_FALSE(reached(sim, id, SimVector(55_ss, 0_ss, 200_ss)));
    }

    TEST_CASE("a Move order ends as the unit enters the goal's square", "[goalsquare]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSquareSim();
        sim.pathFindingService.expansionBudgetPerTick = 0;
        auto player = addPlayer(sim);

        // x 311 sits at the far end of the square [296, 312): a unit walking
        // in from the west gets there some fifteen units before the point.
        auto goal = SimVector(311_ss, 0_ss, 200_ss);
        auto id = spawnAt(sim, "walker", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(id).addOrder(MoveOrder(goal));

        std::optional<int> doneTick;
        for (int t = 0; t < 600; ++t)
        {
            sim.tick();
            if (sim.getUnitState(id).orders.empty())
            {
                doneTick = t;
                break;
            }
        }

        REQUIRE(doneTick.has_value());
        const auto& unit = sim.getUnitState(id);
        const auto& def = sim.unitDefinitions.at("walker");
        REQUIRE(isOnGoalSquare(sim, unit, def, goal));
        // The order ended on entering the square, well short of the eight-unit disc.
        auto remaining = SimVector(goal.x - unit.position.x, 0_ss, goal.z - unit.position.z).length();
        INFO("remaining " << remaining.value << " after " << *doneTick << " ticks");
        REQUIRE(remaining > 8_ss);
    }

    TEST_CASE("a Move to a point beside a square boundary does not end on the wrong side of it", "[goalsquare]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSquareSim();
        sim.pathFindingService.expansionBudgetPerTick = 0;
        auto player = addPlayer(sim);

        // x 296.5 is the first half unit of its square, so a unit walking up
        // from the east is within eight units of the point for many ticks
        // while still on the neighbouring square [312, 328). The order must
        // not end there, and the unit must not stop short of the square.
        auto goal = SimVector(296.5_ssf, 0_ss, 200_ss);
        auto id = spawnAt(sim, "walker", player, SimVector(400_ss, 0_ss, 200_ss), script);
        sim.getUnitState(id).addOrder(MoveOrder(goal));

        bool done = false;
        for (int t = 0; t < 600 && !done; ++t)
        {
            sim.tick();
            done = sim.getUnitState(id).orders.empty();
            if (done)
            {
                const auto& unit = sim.getUnitState(id);
                REQUIRE(isOnGoalSquare(sim, unit, sim.unitDefinitions.at("walker"), goal));
            }
        }
        REQUIRE(done);
    }
}
