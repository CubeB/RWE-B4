#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <cmath>
#include <memory>

namespace rwe
{
    namespace
    {
        void defineBlockedCellMover(GameSimulation& sim, const std::string& type, float maxVelocity, float acceleration)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = SimScalar(maxVelocity);
            d.acceleration = SimScalar(acceleration);
            d.brakeRate = SimScalar(acceleration);
            d.turnRate = 0_ss;
            d.moveRate1 = SimScalar(maxVelocity) * 2_ss;
            d.moveRate2 = SimScalar(maxVelocity) * 2_ss;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        UnitId spawnBlockedCellUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos)
        {
            static const auto script = makeEmptyCobScript({"base"});
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "mover";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 1000;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        const UnitPhysicsInfoGround& blockedCellGround(const GameSimulation& sim, UnitId id)
        {
            return std::get<UnitPhysicsInfoGround>(sim.getUnitState(id).physics);
        }


        struct BlockedCellScene
        {
            GameSimulation sim;
            PlayerId player;
            SimVector centre;
            SimVector heading;
            UnitId mover;
            UnitId blocker;

            /**
             * A unit sitting `edgeOffset` units along its heading from the
             * middle of its footprint block, ordered to walk straight ahead,
             * with a neighbour in the cell it would enter (or none).
             */
            BlockedCellScene(float maxVelocity, float acceleration, float edgeOffset, bool blocked)
                : sim(makeFlatTerrain(64, 64), 0u, 0, 0), player(addPlayer(sim))
            {
                defineBlockedCellMover(sim, "mover", maxVelocity, acceleration);

                // The middle of the footprint block of cell (20, 20): the centre
                // of the window the clamp holds a blocked unit in.
                auto corner = sim.terrain.heightmapIndexToWorldCorner(20, 20);
                centre = SimVector(corner.x + 8_ss, 0_ss, corner.z + 8_ss);
                centre.y = sim.terrain.getHeightAt(centre.x, centre.z);

                mover = spawnBlockedCellUnit(sim, player, centre);
                heading = UnitState::toDirection(sim.getUnitState(mover).rotation);
                int dx = 0;
                int dz = 0;
                if (rweAbs(heading.x) > rweAbs(heading.z))
                {
                    dx = heading.x > 0_ss ? 1 : -1;
                }
                else
                {
                    dz = heading.z > 0_ss ? 1 : -1;
                }

                // Move the unit off the middle of its block, along the heading.
                auto& state = sim.getUnitState(mover);
                const auto& mc = sim.unitDefinitions.at("mover").movementCollisionInfo;
                auto before = sim.computeFootprintRegion(state.position, mc);
                state.position = centre + (heading * SimScalar(edgeOffset));
                state.previousPosition = state.position;
                auto after = sim.computeFootprintRegion(state.position, mc);
                REQUIRE(before.x == after.x);
                REQUIRE(before.y == after.y);

                auto neighbourCorner = blocked
                    ? sim.terrain.heightmapIndexToWorldCorner(20 + dx, 20 + dz)
                    : sim.terrain.heightmapIndexToWorldCorner(45, 45);
                blocker = spawnBlockedCellUnit(sim, player, SimVector(neighbourCorner.x + 8_ss, centre.y, neighbourCorner.z + 8_ss));

                // Straight ahead, far beyond the neighbour.
                sim.getUnitState(mover).addOrder(MoveOrder(centre + (heading * 400_ss)));
            }

            bool inStartCell() const
            {
                const auto& mc = sim.unitDefinitions.at("mover").movementCollisionInfo;
                auto now = sim.computeFootprintRegion(sim.getUnitState(mover).position, mc);
                auto then = sim.computeFootprintRegion(centre, mc);
                return now.x == then.x && now.y == then.y;
            }

            /** How far the mover is from the middle of its block, along its heading. */
            float alongHeading() const
            {
                auto d = sim.getUnitState(mover).position - centre;
                return simScalarToFloat(d.x * heading.x + d.z * heading.z);
            }

            float speed() const
            {
                return simScalarToFloat(blockedCellGround(sim, mover).currentSpeed);
            }
        };
    }

    // BT-12 (0x43D6D0): a step into a cell the unit cannot enter holds it in
    // the window of the cell it is in, and a speed above half the maximum is
    // stored back as half.
    TEST_CASE("a blocked ground unit is held in its cell at no more than half speed", "[blockedcell]")
    {
        SECTION("a fast unit pressing on a neighbour ends in its own cell, at half speed")
        {
            BlockedCellScene scene(4.0f, 4.0f, 0.0f, true);

            bool everBlocked = false;
            for (int i = 0; i < 60; ++i)
            {
                scene.sim.tick();
                REQUIRE(scene.inStartCell());
                everBlocked = everBlocked || scene.sim.getUnitState(scene.mover).inCollision;
            }

            REQUIRE(everBlocked);
            REQUIRE(scene.sim.getUnitState(scene.mover).inCollision);
            // Stored speed, not only that tick's velocity: it is what the next
            // tick accelerates from, and it was cut to exactly half the maximum.
            REQUIRE(scene.speed() == 2.0f);
            // Pinned at the edge of the window: just under eight units out.
            REQUIRE(scene.alongHeading() > 7.98f);
            REQUIRE(scene.alongHeading() < 8.0f);
        }

        SECTION("the clamp does not slide the unit along the obstacle")
        {
            BlockedCellScene scene(4.0f, 4.0f, 0.0f, true);
            tick(scene.sim, 60);

            // Only the heading axis moved.
            const auto& unit = scene.sim.getUnitState(scene.mover);
            auto sideways = rweAbs(unit.position.x - scene.centre.x) * rweAbs(scene.heading.z)
                + rweAbs(unit.position.z - scene.centre.z) * rweAbs(scene.heading.x);
            REQUIRE(sideways.value < 0.001f);
        }

        SECTION("a unit at or below half speed keeps its speed and is still held in the cell")
        {
            // Already at the window's edge and picking up half a unit a tick:
            // the speeds a blocked tick leaves behind are the unclamped ones
            // until the stored speed passes half the maximum, two units.
            BlockedCellScene scene(4.0f, 0.5f, 7.9f, true);

            std::vector<float> blockedSpeeds;
            for (int i = 0; i < 60; ++i)
            {
                scene.sim.tick();
                REQUIRE(scene.inStartCell());
                if (scene.sim.getUnitState(scene.mover).inCollision)
                {
                    blockedSpeeds.push_back(scene.speed());
                }
            }

            REQUIRE(blockedSpeeds.size() >= 6);
            REQUIRE(blockedSpeeds[0] == 0.5f);
            REQUIRE(blockedSpeeds[1] == 1.0f);
            REQUIRE(blockedSpeeds[2] == 1.5f);
            REQUIRE(blockedSpeeds[3] == 2.0f);
            // Past half it is cut back, and stays there.
            REQUIRE(blockedSpeeds[4] == 2.0f);
            REQUIRE(blockedSpeeds.back() == 2.0f);
        }

        SECTION("with nothing in the way the same unit reaches full speed and leaves its cell")
        {
            BlockedCellScene scene(4.0f, 4.0f, 0.0f, false);

            tick(scene.sim, 12);

            REQUIRE_FALSE(scene.inStartCell());
            REQUIRE(scene.speed() == 4.0f);
            REQUIRE_FALSE(scene.sim.getUnitState(scene.mover).inCollision);
        }
    }
}
