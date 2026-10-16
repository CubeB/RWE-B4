#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

/**
 * BT-100, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md on branch spec-room-findings
 * (static byte-tactics reading at 97d7ac59, not executed against the
 * original): when no route reaches a Move's goal the mission is woken with a
 * bit that is not arrival and returns 9. The order executor deletes the node
 * if another follows; otherwise it resets the mission and waits 30 plus a
 * random 0 to 29 ticks before asking for the same goal again, so a lone
 * unreachable Move never ends.
 */
namespace rwe
{
    namespace
    {
        UnitDefinition makeWalkerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
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

        /**
         * A 64 by 64 map of land at 60 with sea level 30 and a channel four
         * cells wide (columns 30 to 33) cut down to 0, which is depth 30
         * against the walker's limit of 0: the far bank is a different region
         * and no route reaches it. Terrain only reaches the search through a
         * named movement class's registered walkable grid, so the walker has
         * one.
         */
        GameSimulation makeChannelSim()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 30; x < 34; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(0));
                }
            }
            GameSimulation sim(MapTerrain(std::move(heights), 30_ss), 0u, 0, 0);

            MovementClassDefinition walkerClass{"BENCHWALK", 1u, 1u, 0u, 0u, 255u, 255u};
            auto classId = sim.movementClassDatabase.registerMovementClass(walkerClass);
            sim.movementClassCollisionService.registerMovementClass(classId, computeWalkableGrid(sim.terrain, walkerClass));

            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
            auto walker = makeWalkerDef();
            walker.movementCollisionInfo = UnitDefinition::NamedMovementClass{classId};
            sim.unitDefinitions["walker"] = walker;
            CobScript empty;
            empty.staticVariableCount = 0;
            empty.pieces.push_back("base");
            sim.unitScriptDefinitions["walker"] = empty;
            return sim;
        }

        UnitId spawnWalker(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "walker";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at("walker").maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        // World space is centred on the map, so the centre of cell c is
        // 16c - 504. The unit stands on the west bank (cell 20), the goal is
        // on the east (cell 40) and another goal is further along the west.
        SimVector startPoint()
        {
            return SimVector(-184_ss, 0_ss, 8_ss);
        }

        SimVector farBankPoint()
        {
            return SimVector(136_ss, 0_ss, 8_ss);
        }

        SimVector westPoint()
        {
            return SimVector(-344_ss, 0_ss, 8_ss);
        }
    }

    TEST_CASE("a lone Move to a goal no route reaches is not completed and retries after 30 to 59 ticks", "[unreachablemove]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeChannelSim();
        auto player = addPlayer(sim);
        auto id = spawnWalker(sim, player, startPoint(), script);
        sim.getUnitState(id).addOrder(MoveOrder(farBankPoint()));

        std::vector<unsigned int> waits;
        unsigned int previous = 0;
        for (int t = 0; t < 1200; ++t)
        {
            sim.tick();
            const auto& unit = sim.getUnitState(id);

            // The order never ends, and the unit never gets onto the far bank.
            REQUIRE(unit.orders.size() == 1);
            REQUIRE_FALSE(isOnGoalSquare(sim, unit, sim.unitDefinitions.at("walker"), farBankPoint()));

            const auto* move = std::get_if<MoveOrder>(&unit.orders.front());
            REQUIRE(move != nullptr);
            if (previous == 0 && move->unreachableRetryTicks > 0)
            {
                waits.push_back(move->unreachableRetryTicks);
            }
            previous = move->unreachableRetryTicks;
        }

        // It asked again more than once over twenty seconds, and every wait
        // was 30 plus a draw below 30.
        REQUIRE(waits.size() >= 2);
        for (auto w : waits)
        {
            REQUIRE(w >= 30u);
            REQUIRE(w <= 59u);
        }
    }

    TEST_CASE("an unreachable Move with another order after it is deleted and the next order runs", "[unreachablemove]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeChannelSim();
        auto player = addPlayer(sim);
        auto id = spawnWalker(sim, player, startPoint(), script);
        auto second = westPoint();
        sim.getUnitState(id).addOrder(MoveOrder(farBankPoint()));
        sim.getUnitState(id).addOrder(MoveOrder(second));

        bool done = false;
        for (int t = 0; t < 1200 && !done; ++t)
        {
            sim.tick();
            done = sim.getUnitState(id).orders.empty();
        }

        REQUIRE(done);
        REQUIRE(isOnGoalSquare(sim, sim.getUnitState(id), sim.unitDefinitions.at("walker"), second));
    }

    TEST_CASE("the retry wait is part of a Move order's hash", "[unreachablemove]")
    {
        MoveOrder a(SimVector(10_ss, 0_ss, 10_ss));
        MoveOrder b(SimVector(10_ss, 0_ss, 10_ss));
        REQUIRE(computeHashOf(a) == computeHashOf(b));
        b.unreachableRetryTicks = 41u;
        REQUIRE_FALSE(computeHashOf(a) == computeHashOf(b));
    }
}
