#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /** Room enough to stand a couple of hundred buildings side by side. */
        MapTerrain makeBuildAngleTerrain()
        {
            Grid<unsigned char> heights(256, 256, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addBuildAnglePlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("engineer"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makeBuildingDef(unsigned int buildAngle, bool factory)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = factory;
            d.maxHitPoints = 100;
            d.buildTime = 300u;
            d.buildCostMetal = Metal(1.0f);
            d.buildCostEnergy = Energy(1.0f);
            d.buildAngle = SimAngle(static_cast<uint16_t>(buildAngle));
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
            return d;
        }

        UnitDefinition makeBuilderDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.buildDistance = 200_ss;
            d.workerTimePerTick = 3;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnBuilder(GameSimulation& sim, PlayerId owner, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "builder";
            unit.owner = owner;
            unit.position = SimVector(-1900_ss, 0_ss, -1900_ss);
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /**
         * Stands a run of one unit type up through the ordinary construction
         * path and reports how far off square each one came out, signed, in
         * sixteen-bit angle units. Every building goes somewhere different so
         * none of them trips over another's footprint.
         */
        std::vector<int> standUpBuildings(GameSimulation& sim, UnitId builderId, const std::string& unitType, int count, int firstSlot = 0)
        {
            std::vector<int> offsets;
            for (int i = firstSlot; i < firstSlot + count; ++i)
            {
                auto x = intToSimScalar(-1600 + (i % 20) * 160);
                auto z = intToSimScalar(-1600 + (i / 20) * 160);

                sim.getUnitState(builderId).behaviourState = UnitBehaviorStateCreatingUnit{unitType, sim.getUnitState(builderId).owner, SimVector(x, 0_ss, z)};
                sim.unitCreationRequests.push_back(builderId);
                sim.spawnNewUnits();

                // A building that could not be placed leaves no offset behind,
                // so a caller counting the run catches it.
                auto s = std::get_if<UnitBehaviorStateCreatingUnit>(&sim.getUnitState(builderId).behaviourState);
                if (s == nullptr)
                {
                    continue;
                }
                if (auto done = std::get_if<UnitCreationStatusDone>(&s->status); done != nullptr)
                {
                    offsets.push_back(static_cast<int16_t>(sim.getUnitState(done->unitId).rotation.value));
                }
            }
            return offsets;
        }

        int lowestOf(const std::vector<int>& offsets)
        {
            return *std::min_element(offsets.begin(), offsets.end());
        }

        int highestOf(const std::vector<int>& offsets)
        {
            return *std::max_element(offsets.begin(), offsets.end());
        }

        int widthOf(const std::vector<int>& offsets)
        {
            return highestOf(offsets) - lowestOf(offsets);
        }

        int meanOf(const std::vector<int>& offsets)
        {
            int total = 0;
            for (auto o : offsets)
            {
                total += o;
            }
            return total / static_cast<int>(offsets.size());
        }
    }

    TEST_CASE("a building settles inside the arc its own FBI names", "[buildangle]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeBuildAngleTerrain(), 0u, 0, 0);
        auto player = addBuildAnglePlayer(sim);
        sim.unitDefinitions["builder"] = makeBuilderDef();
        // A light laser tower asks for the widest arc in the original data, a
        // vehicle plant for the narrowest, and a fortification wall for none.
        sim.unitDefinitions["TOWER"] = makeBuildingDef(32768, false);
        sim.unitDefinitions["PLANT"] = makeBuildingDef(1024, true);
        sim.unitDefinitions["WALL"] = makeBuildingDef(0, false);
        sim.unitScriptDefinitions["TOWER"] = *script;
        sim.unitScriptDefinitions["PLANT"] = *script;
        sim.unitScriptDefinitions["WALL"] = *script;
        registerModel(sim);

        auto builderId = spawnBuilder(sim, player, script);

        SECTION("a wide arc scatters the buildings over most of it")
        {
            auto offsets = standUpBuildings(sim, builderId, "TOWER", 200);
            REQUIRE(offsets.size() == 200);

            // Every one lands inside the half-turn the tower asks for.
            REQUIRE(lowestOf(offsets) >= -16384);
            REQUIRE(highestOf(offsets) < 16384);

            // Two hundred draws over that arc leave next to no chance of the
            // run huddling in the middle.
            REQUIRE(widthOf(offsets) > 30000);
        }

        SECTION("a narrow arc keeps them nearly square")
        {
            auto offsets = standUpBuildings(sim, builderId, "PLANT", 200);
            REQUIRE(offsets.size() == 200);

            REQUIRE(lowestOf(offsets) >= -512);
            REQUIRE(highestOf(offsets) < 512);

            // A plant is a builder, and the original twists it all the same:
            // it is the yards and aircraft plants naming no arc that keeps
            // their roll-off square, not their being factories.
            REQUIRE(widthOf(offsets) > 800);
        }

        SECTION("naming no arc leaves the building dead square")
        {
            auto offsets = standUpBuildings(sim, builderId, "WALL", 40);
            REQUIRE(offsets.size() == 40);

            REQUIRE(lowestOf(offsets) == 0);
            REQUIRE(highestOf(offsets) == 0);
        }

        SECTION("the spread is centred on square, not offset from it")
        {
            auto offsets = standUpBuildings(sim, builderId, "TOWER", 200);
            REQUIRE(offsets.size() == 200);

            REQUIRE(std::abs(meanOf(offsets)) < 3000);

            auto anticlockwise = std::count_if(offsets.begin(), offsets.end(), [](int o) { return o < 0; });
            REQUIRE(anticlockwise > 70);
            REQUIRE(anticlockwise < 130);
        }

        SECTION("the wider the arc, the wider the spread")
        {
            auto wide = standUpBuildings(sim, builderId, "TOWER", 100);
            auto narrow = standUpBuildings(sim, builderId, "PLANT", 100, 100);

            REQUIRE(widthOf(wide) > widthOf(narrow) * 20);
        }
    }

    TEST_CASE("a mobile unit is not twisted on the way out", "[buildangle]")
    {
        // The original overrides a mobile unit's heading with its build angle
        // verbatim rather than randomising it, but RWE takes a unit's facing
        // from the pad it rolls off instead, so the arc must not reach it.
        // Only the ten capital ships name one at all, every one at 16384.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeBuildAngleTerrain(), 0u, 0, 0);
        auto player = addBuildAnglePlayer(sim);
        sim.unitDefinitions["builder"] = makeBuilderDef();
        auto ship = makeBuilderDef();
        ship.builder = false;
        ship.buildAngle = SimAngle(16384);
        sim.unitDefinitions["SHIP"] = ship;
        sim.unitScriptDefinitions["SHIP"] = *script;
        registerModel(sim);

        auto builderId = spawnBuilder(sim, player, script);
        auto offsets = standUpBuildings(sim, builderId, "SHIP", 20);
        REQUIRE(offsets.size() == 20);

        // Half a turn is what createUnit gives a mobile unit with no facing
        // of its own, and nothing may have moved it off that.
        REQUIRE(lowestOf(offsets) == static_cast<int16_t>(HalfTurn.value));
        REQUIRE(highestOf(offsets) == static_cast<int16_t>(HalfTurn.value));
    }
}
