#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
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

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        void defineUnit(GameSimulation& sim, const std::string& type, unsigned int sight, unsigned int radar, bool onOffable)
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = sight;
            d.radarDistance = radar;
            d.onOffable = onOffable;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        UnitId addUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return unitId;
        }
    }

    TEST_CASE("line of sight", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineUnit(sim, "scout", /*sight*/ 100u, /*radar*/ 0u, false);

        // 64x64 tiles -> 32x32 vision cells of 32 world units. Map centre is world (0, 0).
        auto here = SimVector(0_ss, 0_ss, 0_ss);
        auto nearby = SimVector(64_ss, 0_ss, 0_ss);
        auto farAway = SimVector(400_ss, 0_ss, 0_ss);

        auto scoutId = addUnit(sim, "scout", us, here, script);
        REQUIRE_FALSE(sim.isExploredBy(us, here));

        sim.tick();

        SECTION("a unit reveals its surroundings to its owner only")
        {
            REQUIRE(sim.isVisibleTo(us, here));
            REQUIRE(sim.isVisibleTo(us, nearby));
            REQUIRE_FALSE(sim.isVisibleTo(us, farAway));
            REQUIRE_FALSE(sim.isVisibleTo(them, here));
            REQUIRE(sim.canSeeUnit(us, scoutId));
            REQUIRE_FALSE(sim.canSeeUnit(them, scoutId));
        }

        SECTION("explored ground stays explored after the unit is gone")
        {
            sim.getUnitState(scoutId).markAsDeadNoCorpse();
            sim.tick();
            REQUIRE_FALSE(sim.isVisibleTo(us, here));
            REQUIRE(sim.isExploredBy(us, here));
        }

        SECTION("positions off the map are neither explored nor visible")
        {
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(-5000_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isExploredBy(us, SimVector(0_ss, 0_ss, 5000_ss)));
        }
    }

    TEST_CASE("radar", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineUnit(sim, "radar", /*sight*/ 32u, /*radar*/ 300u, /*onOffable*/ true);
        defineUnit(sim, "tank", 32u, 0u, false);

        auto radarId = addUnit(sim, "radar", us, SimVector(0_ss, 0_ss, 0_ss), script);
        auto enemyId = addUnit(sim, "tank", them, SimVector(200_ss, 0_ss, 0_ss), script);

        SECTION("a switched-off radar covers nothing")
        {
            sim.getUnitState(radarId).activated = false;
            sim.tick();
            REQUIRE_FALSE(sim.canDetectUnit(us, enemyId));
        }

        SECTION("an active radar detects but does not reveal")
        {
            sim.getUnitState(radarId).activated = true;
            sim.tick();
            REQUIRE(sim.isOnRadarOf(us, SimVector(200_ss, 0_ss, 0_ss)));
            REQUIRE(sim.canDetectUnit(us, enemyId));
            REQUIRE_FALSE(sim.canSeeUnit(us, enemyId));
            // Radar contacts do not count as exploring the ground.
            REQUIRE_FALSE(sim.isExploredBy(us, SimVector(200_ss, 0_ss, 0_ss)));
        }

        SECTION("units always see and detect their own")
        {
            sim.getUnitState(radarId).activated = false;
            sim.tick();
            REQUIRE(sim.canSeeUnit(them, enemyId));
            REQUIRE(sim.canDetectUnit(them, enemyId));
        }
    }
}
