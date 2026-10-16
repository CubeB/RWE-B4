#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>

namespace rwe
{
    namespace
    {
        const SimScalar SeaHoldSeaLevel = 20_ss;

        /** Ground at `ground` everywhere, with the sea at twenty. */
        MapTerrain makeSeaHoldTerrain(unsigned char ground)
        {
            Grid<unsigned char> heights(64, 64, ground);
            return MapTerrain(std::move(heights), SeaHoldSeaLevel);
        }

        /** A ramp from 0 to 124 going down the map, sea at zero: a slope the footprint can tilt to. */
        MapTerrain makeSeaHoldRamp()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(y * 2));
                }
            }
            return MapTerrain(std::move(heights), 0_ss);
        }

        enum class Kind
        {
            Plain,
            Hover,
            HoverUpright,
            Floater,
            FloaterWithWaterLine,
            Submarine,
        };

        void defineSeaHoldUnit(GameSimulation& sim, const std::string& type, Kind kind)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 4_ss;
            d.acceleration = 4_ss;
            d.brakeRate = 4_ss;
            d.turnRate = 3000_ss;
            d.moveRate1 = 8_ss;
            d.moveRate2 = 8_ss;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};

            // The raw FBI bits: canhover is bit 12, floater bit 19, upright bit 20.
            d.canHover = kind == Kind::Hover || kind == Kind::HoverUpright;
            d.upright = kind == Kind::HoverUpright || kind == Kind::Submarine;
            d.floater = kind == Kind::Floater || kind == Kind::FloaterWithWaterLine;
            d.waterLine = (kind == Kind::FloaterWithWaterLine || kind == Kind::HoverUpright || kind == Kind::Hover) ? 6u : 0u;
            sim.unitDefinitions[type] = d;
        }

        UnitId spawnSeaHoldUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos)
        {
            static const auto script = makeEmptyCobScript({"base"});
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 1000;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /** Walks a unit of the kind across the map for a while and returns it. */
        UnitId walk(GameSimulation& sim, PlayerId player, Kind kind, int ticks)
        {
            defineSeaHoldUnit(sim, "walker", kind);
            auto id = spawnSeaHoldUnit(sim, "walker", player, SimVector(200_ss, 0_ss, 200_ss));
            sim.getUnitState(id).addOrder(MoveOrder(SimVector(700_ss, 0_ss, 200_ss)));
            tick(sim, ticks);
            return id;
        }

        const UnitPhysicsInfoGround& seaHoldGround(const GameSimulation& sim, UnitId id)
        {
            return std::get<UnitPhysicsInfoGround>(sim.getUnitState(id).physics);
        }
    }

    // BT-42 (0x48A870, 0x48A490): the unit-height rule keys the sea hold on
    // canhover, not floater. A floater is not aligned at all and sits at sea
    // level minus its WaterLine.
    TEST_CASE("computeGroundUnitHeight follows the flags the original tests", "[seahold]")
    {
        auto flooded = makeSeaHoldTerrain(0);
        auto land = makeSeaHoldTerrain(60);

        UnitDefinition d{};
        d.waterLine = 6u;

        SECTION("an ordinary unit takes the ground, wet or dry")
        {
            REQUIRE(computeGroundUnitHeight(flooded, d, 100_ss, 100_ss) == 0_ss);
            REQUIRE(computeGroundUnitHeight(land, d, 100_ss, 100_ss) == 60_ss);
        }

        SECTION("canhover holds the ground at sea level, with no waterline taken off")
        {
            d.canHover = true;
            REQUIRE(computeGroundUnitHeight(flooded, d, 100_ss, 100_ss) == 20_ss);
            REQUIRE(computeGroundUnitHeight(land, d, 100_ss, 100_ss) == 60_ss);
        }

        SECTION("a floater is not aligned: sea level minus waterline over water and over land")
        {
            d.floater = true;
            REQUIRE(computeGroundUnitHeight(flooded, d, 100_ss, 100_ss) == 14_ss);
            REQUIRE(computeGroundUnitHeight(land, d, 100_ss, 100_ss) == 14_ss);
        }

        SECTION("an upright hover craft is floored at sea level minus waterline")
        {
            d.canHover = true;
            d.upright = true;
            REQUIRE(computeGroundUnitHeight(flooded, d, 100_ss, 100_ss) == 14_ss);
            REQUIRE(computeGroundUnitHeight(land, d, 100_ss, 100_ss) == 60_ss);
        }

        SECTION("an upright unit that cannot hover takes the sea floor")
        {
            d.upright = true;
            REQUIRE(computeGroundUnitHeight(flooded, d, 100_ss, 100_ss) == 0_ss);
        }

        SECTION("floater beats the hover floor when both are set and the unit is not upright")
        {
            d.floater = true;
            d.canHover = true;
            REQUIRE(computeGroundUnitHeight(land, d, 100_ss, 100_ss) == 14_ss);
        }
    }

    TEST_CASE("a moving unit's height is held by canhover, and a floater's by the waterline", "[seahold]")
    {
        SECTION("a hover craft over water is held at sea level")
        {
            GameSimulation sim(makeSeaHoldTerrain(0), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto id = walk(sim, player, Kind::Hover, 30);
            REQUIRE(sim.getUnitState(id).position.x > 210_ss);
            REQUIRE(sim.getUnitState(id).position.y == SeaHoldSeaLevel);
        }

        SECTION("a hover craft over land follows the ground")
        {
            GameSimulation sim(makeSeaHoldTerrain(60), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto id = walk(sim, player, Kind::Hover, 30);
            REQUIRE(sim.getUnitState(id).position.y == 60_ss);
        }

        SECTION("a floater with no waterline sits at sea level over water")
        {
            GameSimulation sim(makeSeaHoldTerrain(0), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto id = walk(sim, player, Kind::Floater, 30);
            REQUIRE(sim.getUnitState(id).position.x > 210_ss);
            REQUIRE(sim.getUnitState(id).position.y == SeaHoldSeaLevel);
        }

        SECTION("a floater with a waterline sits that far below the surface")
        {
            GameSimulation sim(makeSeaHoldTerrain(0), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto id = walk(sim, player, Kind::FloaterWithWaterLine, 30);
            REQUIRE(sim.getUnitState(id).position.x > 210_ss);
            REQUIRE(sim.getUnitState(id).position.y == 14_ss);
        }

        SECTION("a floater is not lifted by ground that is higher than the sea")
        {
            GameSimulation sim(makeSeaHoldTerrain(60), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto id = walk(sim, player, Kind::Floater, 30);
            REQUIRE(sim.getUnitState(id).position.x > 210_ss);
            REQUIRE(sim.getUnitState(id).position.y == SeaHoldSeaLevel);
        }

        SECTION("a floater below the surface is not slowed as a submerged unit is")
        {
            GameSimulation floatSim(makeSeaHoldTerrain(0), 0u, 0, 0);
            auto floaterId = walk(floatSim, addPlayer(floatSim), Kind::FloaterWithWaterLine, 30);
            REQUIRE(floatSim.getUnitState(floaterId).position.y < SeaHoldSeaLevel);
            REQUIRE(seaHoldGround(floatSim, floaterId).currentSpeed == 4_ss);

            // A submarine on the sea floor has its origin under the sea and is
            // halved.
            GameSimulation subSim(makeSeaHoldTerrain(0), 0u, 0, 0);
            auto subId = walk(subSim, addPlayer(subSim), Kind::Submarine, 30);
            REQUIRE(subSim.getUnitState(subId).position.y == 0_ss);
            REQUIRE(seaHoldGround(subSim, subId).currentSpeed == 2_ss);
        }
    }

    TEST_CASE("a floater is never tilted, a hover craft over a slope is", "[seahold]")
    {
        GameSimulation sim(makeSeaHoldRamp(), 0u, 0, 0);
        auto player = addPlayer(sim);
        defineSeaHoldUnit(sim, "plain", Kind::Plain);
        defineSeaHoldUnit(sim, "floater", Kind::Floater);
        defineSeaHoldUnit(sim, "hover", Kind::Hover);
        UnitDefinition wide = sim.unitDefinitions.at("plain");
        wide.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        for (const auto* name : {"plain", "floater", "hover"})
        {
            auto copy = sim.unitDefinitions.at(name);
            copy.movementCollisionInfo = wide.movementCollisionInfo;
            sim.unitDefinitions[name] = copy;
        }

        auto plainId = spawnSeaHoldUnit(sim, "plain", player, SimVector(200_ss, 0_ss, 300_ss));
        auto floaterId = spawnSeaHoldUnit(sim, "floater", player, SimVector(300_ss, 0_ss, 300_ss));
        auto hoverId = spawnSeaHoldUnit(sim, "hover", player, SimVector(400_ss, 0_ss, 300_ss));

        tick(sim, 3);

        REQUIRE(seaHoldGround(sim, plainId).pitch != 0_ss);
        REQUIRE(seaHoldGround(sim, floaterId).pitch == 0_ss);
        REQUIRE(seaHoldGround(sim, floaterId).roll == 0_ss);
        // The ramp is above the sea here, so the hover craft's corner samples
        // are the ground and it tilts like the plain one.
        REQUIRE(seaHoldGround(sim, hoverId).pitch != 0_ss);
    }

    TEST_CASE("a new floater is placed at sea level minus its waterline", "[seahold]")
    {
        GameSimulation sim(makeSeaHoldTerrain(0), 0u, 0, 0);
        auto player = addPlayer(sim);
        defineSeaHoldUnit(sim, "SHIP", Kind::FloaterWithWaterLine);
        auto script = makeEmptyCobScript({"base"});
        sim.unitScriptDefinitions["SHIP"] = *script;

        auto id = sim.trySpawnUnit("SHIP", player, SimVector(200_ss, 60_ss, 200_ss), std::nullopt);

        REQUIRE(id.has_value());
        REQUIRE(sim.getUnitState(*id).position.y == 14_ss);
    }
}
