#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
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
            script->pieces.push_back("base");
            return script;
        }

        void registerModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makeMobileDef(unsigned int footprint)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeTransportDef()
        {
            auto d = makeMobileDef(2u);
            d.transportCapacity = 1;
            d.transportSize = 2;
            return d;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        bool cellHolds(const GameSimulation& sim, const SimVector& position, UnitId unitId)
        {
            auto cell = sim.terrain.worldToHeightmapCoordinate(position);
            return sim.occupiedGrid.get(cell.x, cell.y).mobileUnitId == unitId;
        }

        template <typename Pred>
        bool tickUntil(GameSimulation& sim, unsigned int maxTicks, Pred pred)
        {
            for (unsigned int i = 0; i < maxTicks; ++i)
            {
                if (pred())
                {
                    return true;
                }
                sim.tick();
            }
            return pred();
        }
    }

    TEST_CASE("transports load, carry and unload units", "[transport]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "hauler");
        sim.unitDefinitions["transport"] = makeTransportDef();
        sim.unitDefinitions["kbot"] = makeMobileDef(2u);
        sim.unitDefinitions["tank"] = makeMobileDef(3u);
        registerModel(sim, "model");

        auto transportId = spawnUnit(sim, "transport", player, SimVector(-200_ss, 0_ss, 0_ss), script);
        auto kbotId = spawnUnit(sim, "kbot", player, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotStart = sim.getUnitState(kbotId).position;

        SECTION("a load order drives the transport to the unit and lifts it")
        {
            sim.getUnitState(transportId).orders.push_back(LoadOrder(kbotId));

            REQUIRE(tickUntil(sim, 600, [&] { return sim.getUnitState(kbotId).carriedBy.has_value(); }));

            const auto& kbot = sim.getUnitState(kbotId);
            const auto& transport = sim.getUnitState(transportId);
            REQUIRE(*kbot.carriedBy == transportId);
            REQUIRE(transport.carriedUnits == std::vector<UnitId>{kbotId});
            REQUIRE_FALSE(cellHolds(sim, kbotStart, kbotId));
            REQUIRE(kbot.orders.empty());

            SECTION("the carried unit follows the transport")
            {
                sim.getUnitState(transportId).orders.push_back(createMoveOrder(SimVector(200_ss, 0_ss, 200_ss)));
                for (int i = 0; i < 60; ++i)
                {
                    sim.tick();
                }
                REQUIRE((sim.getUnitState(kbotId).position == sim.getUnitState(transportId).position));
            }

            SECTION("an unload order sets it down near the destination, back on the ground")
            {
                auto destination = SimVector(200_ss, 0_ss, 200_ss);
                sim.getUnitState(transportId).orders.push_back(UnloadOrder(destination));

                REQUIRE(tickUntil(sim, 900, [&] { return !sim.getUnitState(kbotId).carriedBy.has_value(); }));

                const auto& kbot = sim.getUnitState(kbotId);
                auto dx = kbot.position.x - destination.x;
                auto dz = kbot.position.z - destination.z;
                REQUIRE((dx * dx) + (dz * dz) <= 96_ss * 96_ss);
                REQUIRE(cellHolds(sim, kbot.position, kbotId));
                REQUIRE(sim.getUnitState(transportId).carriedUnits.empty());
            }

            SECTION("a carried unit dies with its transport")
            {
                sim.killUnit(transportId);
                REQUIRE(sim.getUnitState(kbotId).isDead());
            }
        }

        SECTION("a unit too big for the transport is refused")
        {
            auto tankId = spawnUnit(sim, "tank", player, SimVector(-150_ss, 0_ss, 0_ss), script);
            sim.getUnitState(transportId).orders.push_back(LoadOrder(tankId));
            sim.tick();
            REQUIRE(sim.getUnitState(transportId).orders.empty());
            REQUIRE_FALSE(sim.getUnitState(tankId).carriedBy.has_value());
        }

        SECTION("only transports respond to load orders")
        {
            sim.getUnitState(kbotId).orders.push_back(LoadOrder(transportId));
            sim.tick();
            REQUIRE(sim.getUnitState(kbotId).orders.empty());
            REQUIRE_FALSE(sim.getUnitState(transportId).carriedBy.has_value());
        }
    }
}
